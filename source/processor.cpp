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

double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}

double softClip(double x) {
    return std::tanh(x);
}
}

Processor::Processor() {
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 1 && numOuts == 1 &&
        inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    const auto result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk)
        return result;

    sampleRate_ = setup.sampleRate > 1.0 ? setup.sampleRate : 44100.0;

    // About 18 ms parameter smoothing: fast enough to feel immediate, slow enough
    // to keep automation and mouse moves free of zipper noise.
    smoothCoeff_ = std::exp(-1.0 / (0.018 * sampleRate_));

    // Transformer memory is evaluated at the internal 4x rate. Around 95 Hz gives
    // a slow, low-frequency magnetic memory without turning the model into a bass filter.
    const double internalRate = sampleRate_ * static_cast<double>(kOversample);
    ironMemoryCoeff_ = std::exp(-2.0 * kPi * 95.0 / internalRate);

    // 18 Hz DC blocker, needed because the triode model is intentionally asymmetric.
    dcCoeff_ = std::exp(-2.0 * kPi * 18.0 / sampleRate_);

    resetDsp();
    return kResultOk;
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state)
        resetDsp();
    return AudioEffect::setActive(state);
}

void Processor::resetDsp() {
    for (auto& s : channelState_)
        s = {};
    smoothDrive_ = drive_;
    smoothMix_ = mix_;
    smoothOutput_ = output_;
}

void Processor::updateSmoothers() {
    const double oneMinus = 1.0 - smoothCoeff_;
    smoothDrive_ = smoothCoeff_ * smoothDrive_ + oneMinus * drive_;
    smoothMix_ = smoothCoeff_ * smoothMix_ + oneMinus * mix_;
    smoothOutput_ = smoothCoeff_ * smoothOutput_ + oneMinus * output_;
}

void Processor::readParameterChanges(IParameterChanges* changes) {
    if (!changes) return;
    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        auto* queue = changes->getParameterData(i);
        if (!queue || queue->getPointCount() <= 0) continue;
        int32 sampleOffset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != kResultTrue) continue;
        switch (queue->getParameterId()) {
            case kParamOnOff: onOff_ = clamp01(value); break;
            case kParamDrive: drive_ = clamp01(value); break;
            case kParamCharacter: character_ = clamp01(value); break;
            case kParamMix: mix_ = clamp01(value); break;
            case kParamOutput: output_ = clamp01(value); break;
            default: break;
        }
    }
}

double Processor::shapeTriode(double x) const {
    // A biased transfer characteristic produces the even-order content associated
    // with a single-ended triode stage. The second term keeps the negative half
    // slightly softer than the positive half instead of simply offsetting tanh.
    constexpr double bias = 0.22;
    const double positive = std::tanh(1.18 * x + bias) - std::tanh(bias);
    const double negative = std::tanh(0.92 * x - 0.55 * bias) + std::tanh(0.55 * bias);
    double y = 0.64 * positive + 0.36 * negative;

    // Gentle curvature adds a controlled H2 component without a hard kink.
    const double quadratic = x * x / (1.0 + 1.8 * std::abs(x));
    y += 0.075 * quadratic;
    return y;
}

double Processor::shapePentode(double x) const {
    // Pentode mode stays more symmetric and therefore emphasizes odd harmonics.
    // A two-slope blend gives a firmer knee and more upper-harmonic density than Triode.
    const double a = std::tanh(1.55 * x);
    const double b = std::tanh(2.85 * x);
    const double c = std::atan(2.2 * x) * (2.0 / kPi);
    return 0.58 * a + 0.27 * b + 0.15 * c;
}

double Processor::shapeIron(double x, ChannelState& state) {
    // Low-frequency magnetic memory. This is deliberately modest: it gives Iron
    // a stateful, transformer-like feel without unstable or exaggerated hysteresis.
    state.ironMemory = ironMemoryCoeff_ * state.ironMemory +
                       (1.0 - ironMemoryCoeff_) * x;

    const double memory = state.ironMemory;
    const double flux = x + 0.22 * memory;

    // Core saturation: rounded around zero, increasingly compressed at higher flux.
    const double core = std::tanh(1.22 * flux);
    const double soft = flux / (1.0 + 0.42 * std::abs(flux));

    // Small memory-dependent asymmetry makes the path stateful rather than merely
    // another static waveshaper, while staying subtle enough for full mixes.
    const double hysteretic = 0.035 * memory * std::abs(memory);
    return 0.72 * core + 0.28 * soft + hysteretic;
}

double Processor::processNonlinear(double x, int mode, ChannelState& state) {
    switch (mode) {
        case kTriode:  return shapeTriode(x);
        case kPentode: return shapePentode(x);
        case kIron:
        default:       return shapeIron(x, state);
    }
}

double Processor::dcBlock(double x, ChannelState& state) {
    const double y = x - state.dcX1 + dcCoeff_ * state.dcY1;
    state.dcX1 = x;
    state.dcY1 = y;
    return y;
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    readParameterChanges(data.inputParameterChanges);
    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;

    auto& in = data.inputs[0];
    auto& out = data.outputs[0];
    const int32 channels = std::min<int32>(std::min(in.numChannels, out.numChannels), kMaxChannels);
    const bool bypassed = onOff_ >= 0.5;
    const int mode = std::clamp(static_cast<int>(std::lround(character_ * 2.0)), 0, 2);

    auto processBuffers = [&](auto** srcBuffers, auto** dstBuffers) {
        using Sample = std::remove_pointer_t<std::remove_pointer_t<decltype(srcBuffers)>>;

        for (int32 s = 0; s < data.numSamples; ++s) {
            updateSmoothers();

            // Drive spans 0..+24 dB. The curve is exponential in level, which makes
            // the lower half useful for subtle saturation and leaves the top for obvious color.
            const double driveDb = 24.0 * smoothDrive_;
            const double inputGain = dbToGain(driveDb);
            const double wet = smoothMix_;
            const double dry = 1.0 - wet;
            const double outputDb = -18.0 + 24.0 * smoothOutput_;
            const double outputGain = dbToGain(outputDb);

            // Mode-dependent compensation keeps level changes sensible while preserving
            // some natural loudness increase as Drive rises.
            double compensationDb = 0.0;
            if (mode == kTriode)       compensationDb = -0.46 * driveDb;
            else if (mode == kPentode) compensationDb = -0.52 * driveDb;
            else                       compensationDb = -0.40 * driveDb;
            const double compensation = dbToGain(compensationDb);

            for (int32 ch = 0; ch < channels; ++ch) {
                const Sample* src = srcBuffers[ch];
                Sample* dst = dstBuffers[ch];
                if (!src || !dst) continue;

                const double x = static_cast<double>(src[s]);
                auto& state = channelState_[static_cast<size_t>(ch)];

                if (bypassed) {
                    dst[s] = static_cast<Sample>(x);
                    state.previousInput = x;
                    continue;
                }

                // 4x nonlinear evaluation with linear inter-sample interpolation and
                // averaging on decimation. This is intentionally lightweight but materially
                // reduces the harshest alias products versus one base-rate waveshaper call.
                double acc = 0.0;
                const double previous = state.previousInput;
                for (int os = 0; os < kOversample; ++os) {
                    const double t = static_cast<double>(os + 1) / static_cast<double>(kOversample);
                    const double interp = previous + (x - previous) * t;
                    const double driven = interp * inputGain;
                    acc += processNonlinear(driven, mode, state);
                }
                state.previousInput = x;

                double processed = (acc / static_cast<double>(kOversample)) * compensation;

                // A tiny final soft ceiling catches pathological peaks while remaining
                // essentially linear at normal operating level.
                processed = softClip(processed * 1.03) / 1.03;
                processed = dcBlock(processed, state);

                const double y = (dry * x + wet * processed) * outputGain;
                dst[s] = static_cast<Sample>(y);
            }
        }
    };

    if (data.symbolicSampleSize == kSample64)
        processBuffers(in.channelBuffers64, out.channelBuffers64);
    else if (data.symbolicSampleSize == kSample32)
        processBuffers(in.channelBuffers32, out.channelBuffers32);
    else
        return kResultFalse;

    out.silenceFlags = in.silenceFlags;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);
    double bypass = 0.0, drive = 0.30, character = 0.0, mix = 1.0, output = 0.75;
    if (!streamer.readDouble(bypass) || !streamer.readDouble(drive) ||
        !streamer.readDouble(character) || !streamer.readDouble(mix) ||
        !streamer.readDouble(output)) return kResultFalse;
    onOff_ = clamp01(bypass);
    drive_ = clamp01(drive);
    character_ = clamp01(character);
    mix_ = clamp01(mix);
    output_ = clamp01(output);
    smoothDrive_ = drive_;
    smoothMix_ = mix_;
    smoothOutput_ = output_;
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);
    streamer.writeDouble(onOff_);
    streamer.writeDouble(drive_);
    streamer.writeDouble(character_);
    streamer.writeDouble(mix_);
    streamer.writeDouble(output_);
    return kResultOk;
}

} // namespace SaturatorMixFX
