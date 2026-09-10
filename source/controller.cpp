#include "controller.h"
#include "editor.h"
#include "pluginids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstring>

namespace SaturatorMixFX {

using namespace Steinberg;
using namespace Steinberg::Vst;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    // VST3 bypass convention: 0 = processing active, 1 = bypass.
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kParamOnOff);

    parameters.addParameter(STR16("Drive"), nullptr, 0, 0.30,
        ParameterInfo::kCanAutomate, kParamDrive);

    auto* character = new StringListParameter(STR16("Character"), kParamCharacter);
    character->appendString(STR16("Triode"));
    character->appendString(STR16("Pentode"));
    character->appendString(STR16("Iron"));
    parameters.addParameter(character);

    parameters.addParameter(STR16("Mix"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kParamMix);

    // 0.75 maps to 0 dB with the current -18..+6 dB processor range.
    parameters.addParameter(STR16("Output"), STR16("dB"), 0, 0.75,
        ParameterInfo::kCanAutomate, kParamOutput);

    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
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

    setParamNormalized(kParamOnOff, bypass);
    setParamNormalized(kParamDrive, drive);
    setParamNormalized(kParamCharacter, character);
    setParamNormalized(kParamMix, mix);
    setParamNormalized(kParamOutput, output);
    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (name && std::strcmp(name, ViewType::kEditor) == 0)
        return new SMX3Editor(this);
    return nullptr;
}

} // namespace SaturatorMixFX
