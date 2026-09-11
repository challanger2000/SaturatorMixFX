#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>

namespace SaturatorMixFX {

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() SMTG_OVERRIDE = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    static constexpr int kMaxChannels = 2;
    static constexpr int kOversample = 4;

    struct ChannelState {
        double previousInput = 0.0;
        double ironMemory = 0.0;
        double dcX1 = 0.0;
        double dcY1 = 0.0;
    };

    void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void resetDsp();
    void updateSmoothers();

    double shapeTriode(double x) const;
    double shapePentode(double x) const;
    double shapeIron(double x, ChannelState& state);
    double processNonlinear(double x, int mode, ChannelState& state);
    double dcBlock(double x, ChannelState& state);

    double onOff_ = 0.0;
    double drive_ = 0.30;
    double character_ = 0.0;
    double mix_ = 1.0;
    double output_ = 0.75;

    double sampleRate_ = 44100.0;
    double smoothDrive_ = 0.30;
    double smoothMix_ = 1.0;
    double smoothOutput_ = 0.75;
    double smoothCoeff_ = 0.0;
    double ironMemoryCoeff_ = 0.0;
    double dcCoeff_ = 0.995;

    std::array<ChannelState, kMaxChannels> channelState_{};
};

} // namespace SaturatorMixFX
