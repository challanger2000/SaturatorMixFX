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

double smootherStep(double d)
{
    d = clamp01(d);
    return d * d * d * (10.0 - 15.0 * d + 6.0 * d * d);
}

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
    else
        processing_ = false;
    return AudioEffect::setActive(s);
}

tresult PLUGIN_API Processor::setProcessing(TBool state)
{
    const bool shouldProcess = state != 0;
    if (shouldProcess && !processing_)
        resetDsp();

    processing_ = shouldProcess;
    return AudioEffect::setProcessing(state);
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
    y += .0115 * even * std::copysign(1.0, xs + 1.0e-30);
    return y;
}

double Processor::shapePentode(double x, ChannelState& s)
{
    const double a = std::abs(x);
    s.pentodeCharge = pentodeChargeCoeff_ * s.pentodeCharge + (1.0 - pentodeChargeCoeff_) * a;
    const double charge = s.pentodeCharge / (.28 + s.pentodeCharge);
    const double sag = 1.0 - .035 * charge;
    const double bias = .11 + .023 * charge;
    const double xs = x * sag;
    double y = std::tanh(1.68 * (xs + bias)) - std::tanh(1.68 * bias);
    y += .017 * xs * xs * std::copysign(1.0, xs + 1.0e-30) / (1.0 + 1.25 * a);
    return y;
}

double Processor::shapeIron(double x, ChannelState& s)
{
    const double a = std::abs(x);
    s.ironFlux = ironFluxCoeff_ * s.ironFlux + (1.0 - ironFluxCoeff_) * a;
    const double flux = s.ironFlux / (.22 + s.ironFlux);
    s.ironMemory = ironMemoryCoeff_ * s.ironMemory + (1.0 - ironMemoryCoeff_) * x;
    const double hysteresis = .16 * s.ironMemory * (1.0 - .55 * flux);
    const double xs = x + hysteresis;
    double y = std::tanh(1.22 * xs);
    const double cubic = xs * xs * xs;
    y += .021 * cubic / (1.0 + 1.75 * std::abs(cubic));
    return y;
}

double Processor::processNonlinear(double x, int mode, ChannelState& state)
{
    switch (mode)
    {
        case 0: return shapeTriode(x, state);
        case 1: return shapePentode(x, state);
        default: return shapeIron(x, state);
    }
}

double Processor::dcBlock(double x, ChannelState& s)
{
    const double y = x - s.dcX1 + dcCoeff_ * s.dcY1;
    s.dcX1 = x;
    s.dcY1 = y;
    return y;
}

double Processor::processCoreSample(double x, ChannelState& state, const CoreParams& params)
{
    const double drive = shapeDrive(params.drive);
    const double inGain = 1.0 + 6.8 * drive;
    const double driven = x * inGain;

    const int mode = std::clamp(static_cast<int>(std::lround(params.character * 2.0)), 0, 2);
    const double wT = mode == 0 ? 1.0 : 0.0;
    const double wP = mode == 1 ? 1.0 : 0.0;
    const double wI = mode == 2 ? 1.0 : 0.0;

    double nonlinear = processNonlinear(driven, mode, state);
    nonlinear = dcBlock(nonlinear, state);

    const double trimDb = characterTrimDb(drive, wT, wP, wI);
    nonlinear *= dbToGain(trimDb);

    const double wet = nonlinear;
    const double mixed = x * (1.0 - params.mix) + wet * params.mix;
    const double outDb = (params.output - .75) * 24.0;
    return peakProtect(mixed * dbToGain(outDb));
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    readParameterChanges(data.inputParameterChanges);

    if (data.numOutputs <= 0 || !data.outputs)
        return kResultOk;

    if (onOff_ >= .5)
    {
        for (int32 bus = 0; bus < data.numOutputs; ++bus)
        {
            auto& out = data.outputs[bus];
            if (data.numInputs > bus && data.inputs)
            {
                auto& in = data.inputs[bus];
                const int32 channels = std::min(in.numChannels, out.numChannels);
                if (data.symbolicSampleSize == kSample32)
                {
                    for (int32 ch = 0; ch < channels; ++ch)
                        if (in.channelBuffers32 && out.channelBuffers32 && in.channelBuffers32[ch] && out.channelBuffers32[ch])
                            std::copy_n(in.channelBuffers32[ch], data.numSamples, out.channelBuffers32[ch]);
                }
                else if (data.symbolicSampleSize == kSample64)
                {
                    for (int32 ch = 0; ch < channels; ++ch)
                        if (in.channelBuffers64 && out.channelBuffers64 && in.channelBuffers64[ch] && out.channelBuffers64[ch])
                            std::copy_n(in.channelBuffers64[ch], data.numSamples, out.channelBuffers64[ch]);
                }
                out.silenceFlags = in.silenceFlags;
            }
        }
        return kResultOk;
    }

    updateSmoothers();
    CoreParams params{smoothDrive_, smoothCharacter_, smoothMix_, smoothOutput_};

    for (int32 bus = 0; bus < data.numOutputs; ++bus)
    {
        auto& out = data.outputs[bus];
        if (data.numInputs <= bus || !data.inputs)
            continue;
        auto& in = data.inputs[bus];
        const int32 channels = std::min({in.numChannels, out.numChannels, kMaxChannels});

        if (data.symbolicSampleSize == kSample32)
        {
            for (int32 ch = 0; ch < channels; ++ch)
            {
                auto* src = in.channelBuffers32 ? in.channelBuffers32[ch] : nullptr;
                auto* dst = out.channelBuffers32 ? out.channelBuffers32[ch] : nullptr;
                if (!src || !dst)
                    continue;
                auto& state = channelState_[static_cast<size_t>(ch)];
                for (int32 i = 0; i < data.numSamples; ++i)
                    dst[i] = static_cast<float>(processCoreSample(src[i], state, params));
            }
        }
        else if (data.symbolicSampleSize == kSample64)
        {
            for (int32 ch = 0; ch < channels; ++ch)
            {
                auto* src = in.channelBuffers64 ? in.channelBuffers64[ch] : nullptr;
                auto* dst = out.channelBuffers64 ? out.channelBuffers64[ch] : nullptr;
                if (!src || !dst)
                    continue;
                auto& state = channelState_[static_cast<size_t>(ch)];
                for (int32 i = 0; i < data.numSamples; ++i)
                    dst[i] = processCoreSample(src[i], state, params);
            }
        }
        out.silenceFlags = in.silenceFlags;
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kInvalidArgument;

    IBStreamer s(state, kLittleEndian);
    double values[5]{};
    for (double& v : values)
        if (!s.readDouble(v))
            return kResultFalse;

    onOff_ = clamp01(values[0]);
    drive_ = clamp01(values[1]);
    character_ = clamp01(values[2]);
    mix_ = clamp01(values[3]);
    output_ = clamp01(values[4]);
    smoothDrive_ = drive_;
    smoothCharacter_ = character_;
    smoothMix_ = mix_;
    smoothOutput_ = output_;
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kInvalidArgument;

    IBStreamer s(state, kLittleEndian);
    const double values[5] = {onOff_, drive_, character_, mix_, output_};
    for (double v : values)
        if (!s.writeDouble(v))
            return kResultFalse;
    return kResultOk;
}

} // namespace SaturatorMixFX
