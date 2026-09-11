#include "processor.h"
#include "pluginids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

namespace SaturatorMixFX {
namespace {
double mixFxClamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
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

    // Snapshot the host-global targets at the beginning of the block. Per-channel
    // automation points below are still applied at their exact sample offsets.
    mixState.targetBypass = mixFxTargetBypass_.load(std::memory_order_relaxed);
    mixState.targetDrive = mixFxTargetDrive_.load(std::memory_order_relaxed);
    mixState.targetCharacter = mixFxTargetCharacter_.load(std::memory_order_relaxed);
    mixState.targetMix = mixFxTargetMix_.load(std::memory_order_relaxed);
    mixState.targetOutput = mixFxTargetOutput_.load(std::memory_order_relaxed);

    struct QueueCursor
    {
        IParamValueQueue* q = nullptr;
        int32 next = 0;
        int32 count = 0;
        ParamID id = 0;
    };

    std::array<QueueCursor, 5> cursors{};
    int cursorCount = 0;
    if (data.inputParameterChanges)
    {
        for (int32 i = 0; i < data.inputParameterChanges->getParameterCount() && cursorCount < static_cast<int>(cursors.size()); ++i)
        {
            auto* q = data.inputParameterChanges->getParameterData(i);
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
                int32 offset = 0;
                ParamValue value = 0.0;
                if (c.q->getPoint(c.next, offset, value) != kResultTrue)
                {
                    ++c.next;
                    continue;
                }
                if (offset > sample)
                    break;

                value = mixFxClamp01(value);
                switch (c.id)
                {
                    case kParamOnOff:
                        mixState.targetBypass = value;
                        mixFxTargetBypass_.store(value, std::memory_order_relaxed);
                        break;
                    case kParamDrive:
                        mixState.targetDrive = value;
                        mixFxTargetDrive_.store(value, std::memory_order_relaxed);
                        break;
                    case kParamCharacter:
                        mixState.targetCharacter = value;
                        mixFxTargetCharacter_.store(value, std::memory_order_relaxed);
                        break;
                    case kParamMix:
                        mixState.targetMix = value;
                        mixFxTargetMix_.store(value, std::memory_order_relaxed);
                        break;
                    case kParamOutput:
                        mixState.targetOutput = value;
                        mixFxTargetOutput_.store(value, std::memory_order_relaxed);
                        break;
                    default:
                        break;
                }
                ++c.next;
            }
        }
    };

    if (data.numInputs <= 0 || data.numOutputs <= 0 || data.numSamples <= 0)
    {
        applyAutomation(0);
        return kResultOk;
    }

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
            applyAutomation(n);

            const double aSmooth = 1.0 - smoothCoeff_;
            mixState.smoothDrive = smoothCoeff_ * mixState.smoothDrive + aSmooth * mixState.targetDrive;
            mixState.smoothCharacter = smoothCoeff_ * mixState.smoothCharacter + aSmooth * mixState.targetCharacter;
            mixState.smoothMix = smoothCoeff_ * mixState.smoothMix + aSmooth * mixState.targetMix;
            mixState.smoothOutput = smoothCoeff_ * mixState.smoothOutput + aSmooth * mixState.targetOutput;

            const bool bypass = mixState.targetBypass >= .5;
            allBypassed = allBypassed && bypass;
            const CoreParams params{
                mixState.smoothDrive,
                mixState.smoothCharacter,
                mixState.smoothMix,
                mixState.smoothOutput
            };

            for (int32 ch = 0; ch < chans; ++ch)
            {
                auto* src = srcs ? srcs[ch] : nullptr;
                auto* dst = dsts ? dsts[ch] : nullptr;
                if (!src || !dst)
                    continue;

                const double x = static_cast<double>(src[n]);
                auto& state = mixState.dsp[static_cast<size_t>(ch)];
                const double y = processCoreSample(x, state, params);
                dst[n] = static_cast<Sample>(bypass ? x : y);
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
