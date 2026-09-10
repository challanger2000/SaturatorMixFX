#include "processor.h"
#include "pluginids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

namespace SaturatorMixFX {

using namespace Steinberg;
using namespace Steinberg::Vst;

static double clamp01(double v) {
    return std::max(0.0, std::min(1.0, v));
}

// Match the known-good LightOrgan lifecycle: associate the controller as soon
// as the processor object is constructed, before the host calls initialize().
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
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

void Processor::readParameterChanges(IParameterChanges* changes) {
    if (!changes)
        return;

    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        auto* queue = changes->getParameterData(i);
        if (!queue || queue->getPointCount() <= 0)
            continue;

        int32 sampleOffset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != kResultTrue)
            continue;

        switch (queue->getParameterId()) {
            case kParamOnOff:      onOff_ = clamp01(value); break;
            case kParamDrive:      drive_ = clamp01(value); break;
            case kParamCharacter:  character_ = clamp01(value); break;
            case kParamMix:        mix_ = clamp01(value); break;
            case kParamOutput:     output_ = clamp01(value); break;
            default: break;
        }
    }
}

float Processor::saturate(float x) const {
    const float gain = std::pow(16.0f, static_cast<float>(drive_));
    const float driven = x * gain;
    const int mode = std::clamp(static_cast<int>(std::lround(character_ * 2.0)), 0, 2);

    float y = driven;
    switch (mode) {
        case kTriode: {
            const float bias = 0.16f;
            const float positive = std::tanh(driven * 1.05f + bias);
            const float negative = std::tanh(driven * 0.98f + bias);
            y = 0.5f * (positive + negative) - std::tanh(bias);
            break;
        }
        case kPentode: {
            const float shaped = std::tanh(driven * 1.42f);
            const float second = std::tanh(driven * 2.15f);
            y = shaped * 0.86f + second * 0.14f;
            break;
        }
        case kIron:
        default: {
            const float a = std::abs(driven);
            const float compressed = driven / (1.0f + 0.58f * a);
            y = std::tanh(compressed * 1.22f);
            break;
        }
    }

    const float compensation = 1.0f / std::sqrt(gain);
    return y * compensation;
}

float Processor::magic(float x, float saturated) const {
    const float ax = std::abs(x);
    const float intensity = 0.025f + 0.055f * std::clamp(ax * 1.8f, 0.0f, 1.0f);
    const float asym = x + 0.035f * x * x;
    const float harmonic = std::tanh(asym * 2.25f) - std::tanh(asym * 0.82f);
    const float polished = saturated + intensity * harmonic;
    return polished - 0.006f * polished * polished * polished;
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    readParameterChanges(data.inputParameterChanges);

    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;
    if (data.symbolicSampleSize != kSample32)
        return kResultFalse;

    auto& in = data.inputs[0];
    auto& out = data.outputs[0];
    const int32 channels = std::min(in.numChannels, out.numChannels);

    const bool bypassed = onOff_ >= 0.5;
    const float wet = static_cast<float>(mix_);
    const float dry = 1.0f - wet;
    const float outputDb = -18.0f + static_cast<float>(output_) * 24.0f;
    const float outputGain = std::pow(10.0f, outputDb / 20.0f);

    for (int32 ch = 0; ch < channels; ++ch) {
        const float* src = in.channelBuffers32[ch];
        float* dst = out.channelBuffers32[ch];
        if (!src || !dst)
            continue;

        if (bypassed) {
            std::copy(src, src + data.numSamples, dst);
            continue;
        }

        for (int32 s = 0; s < data.numSamples; ++s) {
            const float x = src[s];
            const float tube = saturate(x);
            const float processed = magic(x, tube);
            dst[s] = (dry * x + wet * processed) * outputGain;
        }
    }

    out.silenceFlags = in.silenceFlags;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    double bypass = 0.0, drive = 0.30, character = 0.0, mix = 1.0, output = 0.75;
    if (!streamer.readDouble(bypass) ||
        !streamer.readDouble(drive) ||
        !streamer.readDouble(character) ||
        !streamer.readDouble(mix) ||
        !streamer.readDouble(output))
        return kResultFalse;

    onOff_ = clamp01(bypass);
    drive_ = clamp01(drive);
    character_ = clamp01(character);
    mix_ = clamp01(mix);
    output_ = clamp01(output);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    streamer.writeDouble(onOff_);
    streamer.writeDouble(drive_);
    streamer.writeDouble(character_);
    streamer.writeDouble(mix_);
    streamer.writeDouble(output_);
    return kResultOk;
}

} // namespace SaturatorMixFX
