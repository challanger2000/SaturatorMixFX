#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace SaturatorMixFX {

class Controller final : public Steinberg::Vst::EditControllerEx1 {
public:
    Controller() = default;
    ~Controller() SMTG_OVERRIDE = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
};

} // namespace SaturatorMixFX
