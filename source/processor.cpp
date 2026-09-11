#include "processor.h"
#include "pluginids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace SaturatorMixFX {
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
double dbToGain(double d) { return std::pow(10.0, d / 20.0); }

double peakProtect(double x)
{
    constexpr double threshold = .94;
    constexpr double headroom = 1.0 - threshold;
    const double a = std::abs(x);
    if (a <= threshold)
        return x;
    const double y = threshold + headroom * std::tanh((a - threshold) / headroom);
    return std::copysign(y, x);
}

// Single global Drive law. Monotonic, continuous and without internal knees.
double shapeDrive(double d)
{
    d = clamp01(d);
    if (d <= 0.0)
        return 0.0;
    if (d >= 1.0)
        return 1.0;

    constexpr double exponent = 1.3012419807039308;
    constexpr double balance = 0.7747292343480475;
    const double a = std::pow(d, exponent);
    const double b = balance * std::pow(1.0 - d, exponent);
    return a / (a + b);
}

// C2-continuous 0..1 easing used only for level compensation.
double smootherStep(double d)
{
    d = clamp01(d);
    return d * d * d * (10.0 - 15.0 * d + 6.0 * d * d);
}

// Global Character calibration. No thresholds or piecewise Drive regions.
double characterTrimDb(double effectiveDrive, double wT, double wP, double wI)
{
    (void)wI;
    const double s = smootherStep(effectiveDrive);
    const double s2 = s * s;
    const double s3 = s2 * s;
    const double s4 = s3 * s;

    const double tri =
        4.522567512112715 * s
        - 34.02706594845086 * s2
        + 41.02777776056358 * s3
        - 14.887942224225434 * s4;

    const double pent =
        5.165025880904459 * s
        - 45.37422422361783 * s2
        + 91.75665840452228 * s3
        - 47.421638161808914 * s4;

    return wT * tri + wP * pent;
}
} // namespace

Processor::Processor() { setControllerClass(kControllerUID); }

tresult PLUGIN_API Processor::initialize(FUnknown* c)
{
    auto r = AudioEffect::initialize(c);
    if (r != kResultOk)
        return r;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* i, int32 ni, SpeakerArrangement* o, int32 no)
{
    if (ni == 1 && no == 1 && i[0] == SpeakerArr::kStereo && o[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(i, ni, o, no);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 s)
{
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

void Processor::designOversamplingFilters()
{
    const double internalRate = sampleRate_ * kOversample;
    const double cutoff = 0.485 * sampleRate_;
    const double w0 = 2.0 * kPi * cutoff / internalRate;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    constexpr int order = 2 * kOversampleSections;

    for (int section = 0; section < kOversampleSections; ++section)
    {
        const double angle = (2.0 * (section + 1) - 1.0) * kPi / (2.0 * order);
        const double q = 1.0 / (2.0 * std::cos(angle));
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        auto& c = osCoeffs_[static_cast<size_t>(section)];
        c.b0 = ((1.0 - cw) * .5) / a0;
        c.b1 = (1.0 - cw) / a0;
        c.b2 = c.b0;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha) / a0;
    }
}

double Processor::runOversamplingFilter(double x, std::array<BiquadState, kOversampleSections>& state) const
{
    double y = x;
    for (int i = 0; i < kOversampleSections; ++i)
    {
        const auto& c = osCoeffs_[static_cast<size_t>(i)];
        auto& s = state[static_cast<size_t>(i)];
        const double out = c.b0 * y + s.z1;
        s.z1 = c.b1 * y - c.a1 * out + s.z2;
        s.z2 = c.b2 * y - c.a2 * out;
        y = out;
    }
    return y;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& s)
{
    auto r = AudioEffect::setupProcessing(s);
    if (r != kResultOk)
        return r;

    sampleRate_ = s.sampleRate > 1.0 ? s.sampleRate : 44100.0;
    smoothCoeff_ = std::exp(-1.0 / (0.018 * sampleRate_));
    const double ir = sampleRate_ * kOversample;
    ironMemoryCoeff_ = std::exp(-2.0 * kPi * 95.0 / ir);
    triodeChargeCoeff_ = std::exp(-1.0 / (0.030 * ir));
    pentodeChargeCoeff_ = std::exp(-1.0 / (0.055 * ir));
    ironFluxCoeff_ = std::exp(-1.0 / (0.085 * ir));
    dcCoeff_ = std::exp(-2.0 * kPi * 18.0 / sampleRate_);
    lowCoeff_ = std::exp(-2.0 * kPi * 145.0 / sampleRate_);
    highCoeff_ = std::exp(-2.0 * kPi * 6200.0 / sampleRate_);
    envFastCoeff_ = std::exp(-1.0 / (0.0015 * sampleRate_));
    envSlowCoeff_ = std::exp(-1.0 / (0.035 * sampleRate_));
    designOversamplingFilters();
    resetDsp();
    return kResultOk;
}

tresult PLUGIN_API Processor::setActive(TBool s)
{
    if (s)
        resetDsp();
    return AudioEffect::setActive(s);
}

void Processor::resetDsp()
{
    for (auto& s : channelState_)
        s = {};
    smoothDrive_ = drive_;
    smoothCharacter_ = character_;
    smoothMix_ = mix_;
    smoothOutput_ = output_;
}

void Processor::updateSmoothers()
{
    const double a = 1.0 - smoothCoeff_;
    smoothDrive_ = smoothCoeff_ * smoothDrive_ + a * drive_;
    smoothCharacter_ = smoothCoeff_ * smoothCharacter_ + a * character_;
    smoothMix_ = smoothCoeff_ * smoothMix_ + a * mix_;
    smoothOutput_ = smoothCoeff_ * smoothOutput_ + a * output_;
}

void Processor::readParameterChanges(IParameterChanges* c)
{
    if (!c)
        return;

    for (int32 i = 0; i < c->getParameterCount(); ++i)
    {
        auto* q = c->getParameterData(i);
        if (!q || q->getPointCount() <= 0)
            continue;

        int32 off = 0;
        ParamValue v = 0;
        if (q->getPoint(q->getPointCount() - 1, off, v) != kResultTrue)
            continue;

        v = clamp01(v);
        switch (q->getParameterId())
        {
            case kParamOnOff: onOff_ = v; break;
            case kParamDrive: drive_ = v; break;
            case kParamCharacter: character_ = v; break;
            case kParamMix: mix_ = v; break;
            case kParamOutput: output_ = v; break;
            default: break;
        }
    }
}

double Processor::shapeTriode(double x, ChannelState& s)
{
    const double a = std::abs(x);
    s.triodeCharge = triodeChargeCoeff_ * s.triodeCharge + (1.0 - triodeChargeCoeff_) * a;
    const double charge = s.triodeCharge / (.35 + s.triodeCharge);
    const double sag = 1.0 - .055 * charge;
    const double bias = .225 + .034 * charge;
    const double xs = x * sag;
    const double p = std::tanh(1.16 * xs + bias) - std::tanh(bias);
    const double n = std::tanh(.89 * xs - .52 * bias) + std::tanh(.52 * bias);
    double y = .655 * p + .345 * n;
    const double even = xs * xs / (1.0 + 1.65 * a);
    y += .086 * even;
    return y;
}

double Processor::shapePentode(double x, ChannelState& s)
{
    const double a = std::abs(x);
    s.pentodeCharge = pentodeChargeCoeff_ * s.pentodeCharge + (1.0 - pentodeChargeCoeff_) * a;
    const double charge = s.pentodeCharge / (.30 + s.pentodeCharge);
    const double screenSag = 1.0 - .070 * charge;
    const double xs = x * screenSag;
    const double oddCore = .44 * std::tanh((1.34 + .11 * charge) * xs);
    const double oddEdge = .205 * std::tanh((2.28 + .23 * charge) * xs);
    const double open = .145 * std::atan(1.82 * xs) * (2.0 / kPi);
    const double quasiLinear = .21 * xs / (1.0 + .17 * std::abs(xs));
    return oddCore + oddEdge + open + quasiLinear;
}

double Processor::shapeIron(double x, ChannelState& s)
{
    s.ironMemory = ironMemoryCoeff_ * s.ironMemory + (1.0 - ironMemoryCoeff_) * x;
    s.ironFlux = ironFluxCoeff_ * s.ironFlux + (1.0 - ironFluxCoeff_) * std::abs(x);
    const double fluxAmount = s.ironFlux / (.28 + s.ironFlux);
    const double m = s.ironMemory;
    const double f = x + (.255 + .045 * fluxAmount) * m;
    const double core = (.555 - .020 * fluxAmount) * std::tanh((1.08 + .10 * fluxAmount) * f);
    const double soft = .305 * f / (1.0 + (.285 + .055 * fluxAmount) * std::abs(f));
    const double linear = (.125 + .018 * (1.0 - fluxAmount)) * f;
    const double hysteretic = (.052 + .014 * fluxAmount) * m * std::abs(m);
    return core + soft + linear + hysteretic;
}

double Processor::processNonlinear(double x, int m, ChannelState& s)
{
    if (m == kTriode)
        return shapeTriode(x, s);
    if (m == kPentode)
        return shapePentode(x, s);
    return shapeIron(x, s);
}

double Processor::dcBlock(double x, ChannelState& s)
{
    const double y = x - s.dcX1 + dcCoeff_ * s.dcY1;
    s.dcX1 = x;
    s.dcY1 = y;
    return y;
}

double Processor::processCoreSample(double x, ChannelState& s, const CoreParams& params)
{
    const double pos = clamp01(params.character) * 2.0;
    const double wT = std::max(0.0, 1.0 - pos);
    const double wI = std::max(0.0, pos - 1.0);
    const double wP = 1.0 - wT - wI;

    const double effectiveDrive = shapeDrive(params.drive);
    const double driveDb = 24.0 * effectiveDrive;
    const double inputGain = dbToGain(driveDb);
    const double wet = clamp01(params.mix);
    const double dry = 1.0 - wet;
    const double outGain = dbToGain(-18.0 + 24.0 * clamp01(params.output));

    const double trim = (-9.50 * wT - 19.00 * wP - 14.47 * wI) * effectiveDrive;
    const double baseComp = (-.46 * wT - .52 * wP - .40 * wI) * driveDb;
    const double polishTrimDb = (.73 * wT + 2.67 * wP - .06 * wI) * effectiveDrive;
    const double smoothTrimDb = characterTrimDb(effectiveDrive, wT, wP, wI);
    const double comp = dbToGain(trim + baseComp + polishTrimDb + smoothTrimDb);

    const double protect = .18 * wT + .42 * wP + .30 * wI;
    const double attackAmount = .08 * wT + .22 * wP + .15 * wI;

    s.lowBand = lowCoeff_ * s.lowBand + (1.0 - lowCoeff_) * x;
    s.highSmooth = highCoeff_ * s.highSmooth + (1.0 - highCoeff_) * x;
    const double low = s.lowBand;
    const double high = x - s.highSmooth;
    const double mid = x - low - high;

    const double triCol = .94 * low + 1.09 * mid + .84 * high;
    const double penCol = .84 * low + 1.10 * mid + 1.07 * high;
    const double ironCol = 1.13 * low + 1.025 * mid + .80 * high;
    const double coloured = wT * triCol + wP * penCol + wI * ironCol;

    const double a = std::abs(x);
    s.envFast = envFastCoeff_ * s.envFast + (1.0 - envFastCoeff_) * a;
    s.envSlow = envSlowCoeff_ * s.envSlow + (1.0 - envSlowCoeff_) * a;
    const double transient = std::max(0.0, s.envFast - s.envSlow);
    const double normTransient = clamp01(transient / (.06 + s.envSlow));
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
    processed = peakProtect(processed);
    processed = dcBlock(processed, s);

    // All parallel paths are phase matched: both dry and wet come from the
    // same oversampled clean reference. Drive controls one continuous blend.
    const double wetSignal = cleanOs + effectiveDrive * (processed - cleanOs);
    const double mixed = dry * cleanOs + wet * wetSignal;
    return mixed * outGain;
}

tresult PLUGIN_API Processor::process(ProcessData& d)
{
    if (d.numInputs == 0 || d.numOutputs == 0 || d.numSamples <= 0)
    {
        readParameterChanges(d.inputParameterChanges);
        return kResultOk;
    }

    auto& in = d.inputs[0];
    auto& out = d.outputs[0];
    const int32 chans = std::min<int32>(std::min(in.numChannels, out.numChannels), kMaxChannels);

    struct QueueCursor
    {
        IParamValueQueue* q = nullptr;
        int32 next = 0;
        int32 count = 0;
        ParamID id = 0;
    };

    std::array<QueueCursor, 5> cursors{};
    int cursorCount = 0;
    if (d.inputParameterChanges)
    {
        for (int32 i = 0; i < d.inputParameterChanges->getParameterCount() && cursorCount < static_cast<int>(cursors.size()); ++i)
        {
            auto* q = d.inputParameterChanges->getParameterData(i);
            if (!q || q->getPointCount() <= 0)
                continue;
            const ParamID id = q->getParameterId();
            if (id != kParamOnOff && id != kParamDrive && id != kParamCharacter && id != kParamMix && id != kParamOutput)
                continue;
            cursors[static_cast<size_t>(cursorCount++)] = {q, 0, q->getPointCount(), id};
        }
    }

    auto applyAutomation = [&](int32 sample)
    {
        for (int i = 0; i < cursorCount; ++i)
        {
            auto& c = cursors[static_cast<size_t>(i)];
            while (c.next < c.count)
            {
                int32 off = 0;
                ParamValue v = 0;
                if (c.q->getPoint(c.next, off, v) != kResultTrue)
                {
                    ++c.next;
                    continue;
                }
                if (off > sample)
                    break;

                v = clamp01(v);
                switch (c.id)
                {
                    case kParamOnOff: onOff_ = v; break;
                    case kParamDrive: drive_ = v; break;
                    case kParamCharacter: character_ = v; break;
                    case kParamMix: mix_ = v; break;
                    case kParamOutput: output_ = v; break;
                    default: break;
                }
                ++c.next;
            }
        }
    };

    bool allBypassed = true;
    auto run = [&](auto** srcs, auto** dsts)
    {
        using Sample = std::remove_pointer_t<std::remove_pointer_t<decltype(srcs)>>;
        for (int32 n = 0; n < d.numSamples; ++n)
        {
            applyAutomation(n);
            updateSmoothers();
            const bool bypass = onOff_ >= .5;
            allBypassed = allBypassed && bypass;
            const CoreParams params{smoothDrive_, smoothCharacter_, smoothMix_, smoothOutput_};

            for (int32 ch = 0; ch < chans; ++ch)
            {
                auto* src = srcs[ch];
                auto* dst = dsts[ch];
                if (!src || !dst)
                    continue;
                const double x = static_cast<double>(src[n]);
                auto& state = channelState_[static_cast<size_t>(ch)];
                const double y = processCoreSample(x, state, params);
                dst[n] = static_cast<Sample>(bypass ? x : y);
            }
        }
    };

    if (d.symbolicSampleSize == kSample64)
        run(in.channelBuffers64, out.channelBuffers64);
    else if (d.symbolicSampleSize == kSample32)
        run(in.channelBuffers32, out.channelBuffers32);
    else
        return kResultFalse;

    out.silenceFlags = allBypassed ? in.silenceFlags : 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* s)
{
    if (!s)
        return kResultFalse;

    IBStreamer f(s, kLittleEndian);
    double b = 0, dr = .30, c = 0, m = 1, o = .75;
    if (!f.readDouble(b) || !f.readDouble(dr) || !f.readDouble(c) || !f.readDouble(m) || !f.readDouble(o))
        return kResultFalse;

    onOff_ = clamp01(b);
    drive_ = clamp01(dr);
    character_ = clamp01(c);
    mix_ = clamp01(m);
    output_ = clamp01(o);
    smoothDrive_ = drive_;
    smoothCharacter_ = character_;
    smoothMix_ = mix_;
    smoothOutput_ = output_;
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* s)
{
    if (!s)
        return kResultFalse;

    IBStreamer f(s, kLittleEndian);
    f.writeDouble(onOff_);
    f.writeDouble(drive_);
    f.writeDouble(character_);
    f.writeDouble(mix_);
    f.writeDouble(output_);
    return kResultOk;
}

} // namespace SaturatorMixFX
