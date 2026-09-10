#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace SaturatorMixFX {

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor() = default;
    ~Processor() SMTG_OVERRIDE = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    double onOff_ = 1.0;
    double drive_ = 0.30;
    double character_ = 0.0;
    double mix_ = 1.0;
    double output_ = 0.50;

    void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    float saturate(float x) const;
};

} // namespace SaturatorMixFX
