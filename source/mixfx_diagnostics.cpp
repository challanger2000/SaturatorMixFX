#include "processor.h"

#include <algorithm>
#include <cstring>

namespace SaturatorMixFX {

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
    for (auto& state : mixFxStates_)
    {
        state = {};
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
    if (index < 0 || index >= kMaxMixFxChannels)
        return Steinberg::kInvalidArgument;

    // Reuse the already validated SMX-3 DSP verbatim, but give every Studio One
    // mixer channel its own complete nonlinear/filter state and its own parameter
    // smoothing history. This prevents crosstalk through our internal state.
    const auto savedDsp = channelState_;
    const double savedDrive = smoothDrive_;
    const double savedCharacter = smoothCharacter_;
    const double savedMix = smoothMix_;
    const double savedOutput = smoothOutput_;

    auto& mixState = mixFxStates_[static_cast<size_t>(index)];
    channelState_ = mixState.dsp;
    smoothDrive_ = mixState.smoothDrive;
    smoothCharacter_ = mixState.smoothCharacter;
    smoothMix_ = mixState.smoothMix;
    smoothOutput_ = mixState.smoothOutput;

    // The regular process() path contains the finished Channel DSP. Temporarily
    // enter that path for this one private Mix-FX channel callback only.
    const bool wasMixFxEngaged = mixFxEngaged_;
    mixFxEngaged_ = false;
    const Steinberg::tresult result = process(data);
    mixFxEngaged_ = wasMixFxEngaged;

    mixState.dsp = channelState_;
    mixState.smoothDrive = smoothDrive_;
    mixState.smoothCharacter = smoothCharacter_;
    mixState.smoothMix = smoothMix_;
    mixState.smoothOutput = smoothOutput_;

    channelState_ = savedDsp;
    smoothDrive_ = savedDrive;
    smoothCharacter_ = savedCharacter;
    smoothMix_ = savedMix;
    smoothOutput_ = savedOutput;

    return result;
}

Steinberg::tresult PLUGIN_API Processor::mixMethodA(
    Steinberg::Vst::SpeakerArrangement* /*arrangements*/,
    Steinberg::int32 count)
{
    // Studio One announces the participating mixer channels here. From this
    // point on the private per-channel path owns the saturation processing.
    mixFxEngaged_ = true;
    mixFxChannelCount_ = std::max<Steinberg::int32>(0,
        std::min<Steinberg::int32>(count, kMaxMixFxChannels));
    resetMixFxStates();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::mixMethodB(Steinberg::Vst::ProcessData* data)
{
    // Global Mix-FX callback: keep the shared parameter targets synchronized,
    // but do not saturate the summed signal here. The actual sound processing
    // happens once, on each contributing mixer channel, in channelMethod().
    if (data)
        readParameterChanges(data->inputParameterChanges);
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
