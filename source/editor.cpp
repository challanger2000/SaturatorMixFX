#include "editor.h"
#include "controller.h"

namespace SaturatorMixFX {

SMX3Editor::SMX3Editor(Steinberg::Vst::EditController* controller)
: VSTGUI::VST3Editor(controller, "view", "SMX3.uidesc"), controller_(controller)
{
    setZoomFactor(.68);
    setAllowedZoomFactors({.68, 1.0});
}

void SMX3Editor::setUserZoom(double factor)
{
    if (factor == .68 || factor == 1.0)
        setZoomFactor(factor);
}

VSTGUI::CView* SMX3Editor::createView(const VSTGUI::UIAttributes& attributes,
                                      const VSTGUI::IUIDescription* description)
{
    // Isolation build: use the stock VSTGUI view creation path only.
    // If Studio One opens this editor, the VST3/UIDescription foundation is sound.
    return VSTGUI::VST3Editor::createView(attributes, description);
}

} // namespace SaturatorMixFX
