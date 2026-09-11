#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include <array>

namespace SaturatorMixFX {

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() SMTG_OVERRIDE = default;
    static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor()); }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns, Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
private:
    static constexpr int kMaxChannels = 2;
    static constexpr int kOversample = 4;
    static constexpr int kOversampleSections = 8;

    struct BiquadState { double z1=0.0, z2=0.0; };
    struct BiquadCoeffs { double b0=1.0, b1=0.0, b2=0.0, a1=0.0, a2=0.0; };
    struct ChannelState {
        double previousInput=0.0, ironMemory=0.0, dcX1=0.0, dcY1=0.0;
        double lowBand=0.0, highSmooth=0.0;
        double envFast=0.0, envSlow=0.0;
        double triodeCharge=0.0, pentodeCharge=0.0, ironFlux=0.0;
        std::array<BiquadState,kOversampleSections> osUp{};
        std::array<BiquadState,kOversampleSections> osDown{};
        std::array<BiquadState,kOversampleSections> cleanUp{};
        std::array<BiquadState,kOversampleSections> cleanDown{};
    };
    struct CoreParams {
        double drive=0.30;
        double character=0.0;
        double mix=1.0;
        double output=0.75;
    };

    void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void resetDsp();
    void updateSmoothers();
    void designOversamplingFilters();
    double runOversamplingFilter(double x, std::array<BiquadState,kOversampleSections>& state) const;
    double shapeTriode(double x, ChannelState& state);
    double shapePentode(double x, ChannelState& state);
    double shapeIron(double x, ChannelState& state);
    double processNonlinear(double x,int mode,ChannelState& state);
    double dcBlock(double x,ChannelState& state);
    double processCoreSample(double x, ChannelState& state, const CoreParams& params);

    double onOff_=0.0, drive_=0.30, character_=0.0, mix_=1.0, output_=0.75;
    double sampleRate_=44100.0, smoothDrive_=0.30, smoothCharacter_=0.0, smoothMix_=1.0, smoothOutput_=0.75;
    double smoothCoeff_=0.0, ironMemoryCoeff_=0.0, dcCoeff_=0.995;
    double lowCoeff_=0.0, highCoeff_=0.0, envFastCoeff_=0.0, envSlowCoeff_=0.0;
    double triodeChargeCoeff_=0.0, pentodeChargeCoeff_=0.0, ironFluxCoeff_=0.0;
    std::array<BiquadCoeffs,kOversampleSections> osCoeffs_{};
    std::array<ChannelState,kMaxChannels> channelState_{};
};

} // namespace SaturatorMixFX
