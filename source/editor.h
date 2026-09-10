#pragma once

#include "vstgui/plugin-bindings/vst3editor.h"

namespace SaturatorMixFX {

class SMX3Editor final : public VSTGUI::VST3Editor {
public:
    explicit SMX3Editor(Steinberg::Vst::EditController* controller);

    VSTGUI::CView* createView(const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override;

    void setUserZoom(double factor);

private:
    Steinberg::Vst::EditController* controller_ = nullptr;
};

} // namespace SaturatorMixFX
