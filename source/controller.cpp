#include "controller.h"
#include "pluginids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace SaturatorMixFX {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(STR16("Drive"), nullptr, 0, 0.30,
        ParameterInfo::kCanAutomate, kParamDrive);

    auto* character = new StringListParameter(STR16("Character"), kParamCharacter);
    character->appendString(STR16("Triode"));
    character->appendString(STR16("Pentode"));
    character->appendString(STR16("Iron"));
    parameters.addParameter(character);

    parameters.addParameter(STR16("Mix"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kParamMix);

    parameters.addParameter(STR16("Output"), STR16("dB"), 0, 0.50,
        ParameterInfo::kCanAutomate, kParamOutput);

    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    double drive = 0.0, character = 0.0, mix = 0.0, output = 0.0;
    if (!streamer.readDouble(drive) ||
        !streamer.readDouble(character) ||
        !streamer.readDouble(mix) ||
        !streamer.readDouble(output))
        return kResultFalse;

    setParamNormalized(kParamDrive, drive);
    setParamNormalized(kParamCharacter, character);
    setParamNormalized(kParamMix, mix);
    setParamNormalized(kParamOutput, output);
    return kResultOk;
}

} // namespace SaturatorMixFX
