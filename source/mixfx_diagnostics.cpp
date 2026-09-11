#include "processor.h"

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

Steinberg::tresult PLUGIN_API Processor::mixMethodA(
    Steinberg::Vst::SpeakerArrangement* /*arrangements*/,
    Steinberg::int32 /*count*/)
{
    // Studio One announces the participating mixer-channel layouts here.
    // The established v10.3 probe returned success without altering the
    // regular VST3 bus topology, so keep the proven Channel DSP untouched.
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::mixMethodB(Steinberg::Vst::ProcessData* /*data*/)
{
    // Global Mix-FX callback. The first integration step deliberately mirrors
    // the previously successful clean-room v10.3 probe: advertise the ABI and
    // let Studio One enable its Mix-FX routing before adding per-channel DSP.
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::channelMethod(
    Steinberg::int32 /*index*/,
    Steinberg::Vst::ProcessData* /*data*/)
{
    // Per-mixer-channel callback. Returning success is enough to reproduce the
    // proven ABI handshake; channel DSP will be attached only after the host
    // confirms that the contributing channels are now exposed/marked.
    return Steinberg::kResultOk;
}

} // namespace SaturatorMixFX
