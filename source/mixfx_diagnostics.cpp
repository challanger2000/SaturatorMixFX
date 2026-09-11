#include "processor.h"
#include "pluginids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace SaturatorMixFX {
namespace {
constexpr double kMixFxPi = 3.14159265358979323846;

double mixFxClamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

double mixFxDbToGain(double d)
{
    return std::pow(10.0, d / 20.0);
}

double mixFxPeakProtect(double x)
{
    constexpr double threshold = .94;
    constexpr double headroom = 1.0 - threshold;
    const double a = std::abs(x);
    if (a <= threshold)
        return x;
    const double y = threshold + headroom * std::tanh((a - threshold) / headroom);
    return std::copysign(y, x);
}

double mixFxShapeDrive(double d)
{
    d = mixFxClamp01(d);
    constexpr double pivot = .30;
    if (d <= pivot)
    {
        const double u = d / pivot;
        return pivot * std::pow(u, 1.35);
    }
    const double u = (d - pivot) / (1.0 - pivot);
    return pivot + (1.0 - pivot) * std::pow(u, .78);
}

double mixFxHighDriveCharacterTrimDb(double effectiveDrive, double wT, double wP, double wI)
{
    (void)wI;
    if (effectiveDrive <= .30)
        return 0.0;

    const double t = mixFxClamp01((effectiveDrive - .30) / .70);
    const double t2 = t * t;
    const double tri = (-6.692542930684427 * t) + (3.327880038412518 * t2);
    const double pent = (-3.5778484322423374 * t) + (7.70367037382395 * t2);
    return wT * tri + wP * pent;
}
} // namespace

const Steinberg::TUID PresonusProbe::IAudioMixProcessor::iid = {
    char(0x4C),char(0x05),char(0xC9),char(0x5A),char(0xE1),char(0xFC),char(0xF0),char(0x4F),
    char(0xAF),char(0x37),char(0x1A),char(0xD9),char(0xA6),char(0x88),char(0x73),char(0x21)};

const Steinberg::TUID PresonusProbe::IAudioMixChannelProcessor::iid = {
    char(0xB1),char(0x17),char(0x05),char(0x30),char(0x61),char(0x81),char(0xC2),char(0x47),
    char(0x8D),char(0x58),char(0x73),char(0x2B),char(0x8E),char(0xEE),char(0x72),char(0xC9)};

Steinberg::tresult PLUGIN_API Processor::queryInterface(const Steinberg::TUID iid, void** obj)
{
    if (!obj)
        return Steinberg::kInvalidArgument;

    if (std::memcmp(iid, PresonusProbe::IAudioMixProcessor::iid, 16) == 0)
    {
        *obj = static_cast<PresonusProbe::IAudioMixProcessor*>(this);
        Steinberg::Vst::AudioEffect::addRef();
        return Steinberg::kResultOk;
    }

    if (std::memcmp(iid, PresonusProbe::IAudioMixChannelProcessor::iid, 16) == 0)
    {
        *obj = static_cast<PresonusProbe::IAudioMixChannelProcessor*>(this);
        Steinberg::Vst::AudioEffect::addRef();
        return Steinberg::kResultOk;
    }

    return Steinberg::Vst::AudioEffect::queryInterface(iid, obj);
}

void Processor::resetMixFxStates()
{
    mixFxTargetBypass_.store(onOff_, std::memory_order_relaxed);
    mixFxTargetDrive_.store(drive_, std::memory_order_relaxed);
    mixFxTargetCharacter_.store(character_, std::memory_order_relaxed);
    mixFxTargetMix_.store(mix_, std::memory_order_relaxed);
    mixFxTargetOutput_.store(output_, std::memory_order_relaxed);

    for (auto& state : mixFxStates_)
    {
        state = {};
        state.targetBypass = onOff_;
        state.targetDrive = drive_;
        state.targetCharacter = character_;
        state.targetMix = mix_;
        state.targetOutput = output_;
        state.smoothDrive = drive_;
        state.smoothCharacter = character_;
        state.smoothMix = mix_;
        state.smoothOutput = output_;
    }
}

Steinberg::tresult Processor::processMixFxChannel(
    Steinberg::int32 index,
    Steinberg::Vst::ProcessData& data)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    if (index < 0 || index >= kMaxMixFxChannels)
        return kInvalidArgument;

    auto& mixState = mixFxStates_[static_cast<size_t>(index)];

    mixState.targetBypass = mixFxTargetBypass_.load(std::memory_order_relaxed);
    mixState.targetDrive = mixFxTargetDrive_.load(std::memory_order_relaxed);
    mixState.targetCharacter = mixFxTargetCharacter_.load(std::memory_order_relaxed);
    mixState.targetMix = mixFxTargetMix_.load(std::memory_order_relaxed);
    mixState.targetOutput = mixFxTargetOutput_.load(std::memory_order_relaxed);

    bool receivedParameterChange = false;
    if (data.inputParameterChanges)
    {
        for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i)
        {
            auto* q = data.inputParameterChanges->getParameterData(i);
            if (!q || q->getPointCount() <= 0)
                continue;

            int32 offset = 0;
            ParamValue value = 0.0;
            if (q->getPoint(q->getPointCount() - 1, offset, value) != kResultTrue)
                continue;

            value = mixFxClamp01(value);
            switch (q->getParameterId())
            {
                case kParamOnOff:     mixState.targetBypass = value; receivedParameterChange = true; break;
                case kParamDrive:     mixState.targetDrive = value; receivedParameterChange = true; break;
                case kParamCharacter: mixState.targetCharacter = value; receivedParameterChange = true; break;
                case kParamMix:       mixState.targetMix = value; receivedParameterChange = true; break;
                case kParamOutput:    mixState.targetOutput = value; receivedParameterChange = true; break;
                default: break;
            }
        }
    }

    // Studio One may deliver a parameter queue to one private channel callback
    // before the other callbacks see it. Publish the resolved targets atomically
    // so every participating mixer channel converges to the same parameter state
    // on the following block instead of being pulled back to stale global values.
    if (receivedParameterChange)
    {
        mixFxTargetBypass_.store(mixState.targetBypass, std::memory_order_relaxed);
        mixFxTargetDrive_.store(mixState.targetDrive, std::memory_order_relaxed);
        mixFxTargetCharacter_.store(mixState.targetCharacter, std::memory_order_relaxed);
        mixFxTargetMix_.store(mixState.targetMix, std::memory_order_relaxed);
        mixFxTargetOutput_.store(mixState.targetOutput, std::memory_order_relaxed);
    }

    if (data.numInputs <= 0 || data.numOutputs <= 0 || data.numSamples <= 0)
        return kResultOk;

    auto& in = data.inputs[0];
    auto& out = data.outputs[0];
    const int32 chans = std::min<int32>(std::min(in.numChannels, out.numChannels), kMaxChannels);
    if (chans <= 0)
        return kResultOk;

    bool allBypassed = true;

    auto run = [&](auto** srcs, auto** dsts)
    {
        using Sample = std::remove_pointer_t<std::remove_pointer_t<decltype(srcs)>>;
        for (int32 n = 0; n < data.numSamples; ++n)
        {
            const double aSmooth = 1.0 - smoothCoeff_;
            mixState.smoothDrive = smoothCoeff_ * mixState.smoothDrive + aSmooth * mixState.targetDrive;
            mixState.smoothCharacter = smoothCoeff_ * mixState.smoothCharacter + aSmooth * mixState.targetCharacter;
            mixState.smoothMix = smoothCoeff_ * mixState.smoothMix + aSmooth * mixState.targetMix;
            mixState.smoothOutput = smoothCoeff_ * mixState.smoothOutput + aSmooth * mixState.targetOutput;

            const bool bypass = mixState.targetBypass >= .5;
            allBypassed = allBypassed && bypass;

            const double pos = mixFxClamp01(mixState.smoothCharacter) * 2.0;
            const double wT = std::max(0.0, 1.0 - pos);
            const double wI = std::max(0.0, pos - 1.0);
            const double wP = 1.0 - wT - wI;
            const double effectiveDrive = mixFxShapeDrive(mixState.smoothDrive);
            const double driveDb = 24.0 * effectiveDrive;
            const double inputGain = mixFxDbToGain(driveDb);
            const double wet = mixState.smoothMix;
            const double dry = 1.0 - wet;
            const double outGain = mixFxDbToGain(-18.0 + 24.0 * mixState.smoothOutput);
            const double trim = (-9.50 * wT - 19.00 * wP - 14.47 * wI) * effectiveDrive;
            const double baseComp = (-.46 * wT - .52 * wP - .40 * wI) * driveDb;
            const double referenceAmount = std::min(1.0, effectiveDrive / .30);
            const double polishTrimDb = (.73 * wT + 2.67 * wP - .06 * wI) * referenceAmount;
            const double highDriveTrimDb = mixFxHighDriveCharacterTrimDb(effectiveDrive, wT, wP, wI);
            const double comp = mixFxDbToGain(trim + baseComp + polishTrimDb + highDriveTrimDb);
            const double protect = .18 * wT + .42 * wP + .30 * wI;
            const double attackAmount = .08 * wT + .22 * wP + .15 * wI;

            for (int32 ch = 0; ch < chans; ++ch)
            {
                auto* src = srcs ? srcs[ch] : nullptr;
                auto* dst = dsts ? dsts[ch] : nullptr;
                if (!src || !dst)
                    continue;

                const double x = static_cast<double>(src[n]);
                auto& s = mixState.dsp[static_cast<size_t>(ch)];

                s.lowBand = lowCoeff_ * s.lowBand + (1.0 - lowCoeff_) * x;
                s.highSmooth = highCoeff_ * s.highSmooth + (1.0 - highCoeff_) * x;
                const double low = s.lowBand;
                const double high = x - s.highSmooth;
                const double mid = x - low - high;
                const double triCol = .94 * low + 1.09 * mid + .84 * high;
                const double penCol = .84 * low + 1.10 * mid + 1.07 * high;
                const double ironCol = 1.13 * low + 1.025 * mid + .80 * high;
                const double coloured = wT * triCol + wP * penCol + wI * ironCol;

                const double ax = std::abs(x);
                s.envFast = envFastCoeff_ * s.envFast + (1.0 - envFastCoeff_) * ax;
                s.envSlow = envSlowCoeff_ * s.envSlow + (1.0 - envSlowCoeff_) * ax;
                const double transient = std::max(0.0, s.envFast - s.envSlow);
                const double normTransient = mixFxClamp01(transient / (.06 + s.envSlow));
                const double dynamicGain = inputGain * (1.0 - protect * normTransient);

                double processedOs = 0.0;
                double cleanOs = 0.0;
                for (int os = 0; os < kOversample; ++os)
                {
                    const double stuffed = (os == 0) ? (coloured * static_cast<double>(kOversample)) : 0.0;
                    const double cleanStuffed = (os == 0) ? (x * static_cast<double>(kOversample)) : 0.0;
                    const double up = runOversamplingFilter(stuffed, s.osUp);
                    const double cleanUp = runOversamplingFilter(cleanStuffed, s.cleanUp);
                    const double nlT = shapeTriode(up * dynamicGain, s);
                    const double nlP = shapePentode(up * dynamicGain, s);
                    const double nlI = shapeIron(up * dynamicGain, s);
                    const double nl = wT * nlT + wP * nlP + wI * nlI;
                    const double filtered = runOversamplingFilter(nl, s.osDown);
                    const double cleanFiltered = runOversamplingFilter(cleanUp, s.cleanDown);
                    if (os == kOversample - 1)
                    {
                        processedOs = filtered;
                        cleanOs = cleanFiltered;
                    }
                }

                double processed = processedOs * comp;
                const double attackBlend = normTransient * attackAmount;
                processed = processed * (1.0 - attackBlend) + cleanOs * attackBlend;
                processed = mixFxPeakProtect(processed);
                processed = dcBlock(processed, s);
                const double mixed = dry * cleanOs + wet * processed;
                const double active = (wet <= 1.0e-6 || effectiveDrive <= 1.0e-12)
                    ? x
                    : (cleanOs + referenceAmount * (mixed - cleanOs));
                dst[n] = static_cast<Sample>(bypass ? x : (active * outGain));
            }
        }
    };

    if (data.symbolicSampleSize == kSample64)
        run(in.channelBuffers64, out.channelBuffers64);
    else if (data.symbolicSampleSize == kSample32)
        run(in.channelBuffers32, out.channelBuffers32);
    else
        return kResultFalse;

    out.silenceFlags = allBypassed ? in.silenceFlags : 0;
    return kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::mixMethodA(
    Steinberg::Vst::SpeakerArrangement* /*arrangements*/,
    Steinberg::int32 count)
{
    mixFxEngaged_ = true;
    mixFxChannelCount_ = std::max<Steinberg::int32>(0,
        std::min<Steinberg::int32>(count, kMaxMixFxChannels));
    resetMixFxStates();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::mixMethodB(Steinberg::Vst::ProcessData* data)
{
    if (data)
        readParameterChanges(data->inputParameterChanges);

    mixFxTargetBypass_.store(onOff_, std::memory_order_relaxed);
    mixFxTargetDrive_.store(drive_, std::memory_order_relaxed);
    mixFxTargetCharacter_.store(character_, std::memory_order_relaxed);
    mixFxTargetMix_.store(mix_, std::memory_order_relaxed);
    mixFxTargetOutput_.store(output_, std::memory_order_relaxed);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::channelMethod(
    Steinberg::int32 index,
    Steinberg::Vst::ProcessData* data)
{
    if (!data)
        return Steinberg::kInvalidArgument;
    if (mixFxChannelCount_ > 0 && index >= mixFxChannelCount_)
        return Steinberg::kInvalidArgument;

    return processMixFxChannel(index, *data);
}

} // namespace SaturatorMixFX
