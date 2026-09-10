#include "editor.h"
#include "controller.h"
#include "pluginids.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace SaturatorMixFX {
namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

static void setParameter(EditController* c, ParamID id, ParamValue v)
{
    if (!c) return;
    v = std::clamp<ParamValue>(v, 0.0, 1.0);
    c->beginEdit(id);
    c->setParamNormalized(id, v);
    c->performEdit(id, v);
    c->endEdit(id);
}

static int characterIndex(EditController* c)
{
    if (!c) return 0;
    return std::clamp(static_cast<int>(std::lround(c->getParamNormalized(kParamCharacter) * 2.0)), 0, 2);
}

static bool isBypassed(EditController* c)
{
    return !c || c->getParamNormalized(kParamOnOff) >= 0.5;
}

class BitmapView final : public VSTGUI::CView {
public:
    BitmapView(const VSTGUI::CRect& r, const char* resource) : CView(r)
    {
        bitmap_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(resource));
        setMouseEnabled(false);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (ctx && bitmap_) bitmap_->draw(ctx, getViewSize(), VSTGUI::CPoint(0,0), 1.f);
        setDirty(false);
    }
private:
    VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap_;
};

class TubeGlowView final : public VSTGUI::CView {
public:
    explicit TubeGlowView(const VSTGUI::CRect& r) : CView(r)
    {
        const std::array<const char*,3> names{{"TubeGlow_1.png","TubeGlow_2.png","TubeGlow_3.png"}};
        for (int i=0;i<3;++i) glow_[i] = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(names[i]));
        setMouseEnabled(false);
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) { phase_ += 0.055; invalid(); }, 50);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx) { setDirty(false); return; }
        const std::array<double,3> phaseOffset{{0.0, 2.1, 4.4}};
        const std::array<double,3> speed{{1.0, 0.83, 1.17}};
        const VSTGUI::CRect full(0,0,1536,1024);
        for (int i=0;i<3;++i) {
            if (!glow_[i]) continue;
            double a = 0.22 + 0.055 * std::sin(phase_ * speed[i] + phaseOffset[i]);
            a += 0.018 * std::sin(phase_ * 0.37 + phaseOffset[i] * 1.7);
            a = std::clamp(a, 0.14, 0.31);
            glow_[i]->draw(ctx, full, VSTGUI::CPoint(0,0), static_cast<float>(a));
        }
        setDirty(false);
    }
private:
    std::array<VSTGUI::SharedPointer<VSTGUI::CBitmap>,3> glow_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    double phase_ = 0.0;
};

class DriveView final : public VSTGUI::CView {
public:
    DriveView(const VSTGUI::CRect& r, EditController* c) : CView(r), controller_(c)
    {
        bitmap_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("Drive_Runtime.png"));
        setMouseEnabled(true);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (ctx && bitmap_) bitmap_->draw(ctx, getViewSize(), VSTGUI::CPoint(0,0), 1.f);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& p, const VSTGUI::CButtonState&) override
    {
        if (!controller_) return VSTGUI::kMouseEventNotHandled;
        dragging_ = true;
        startY_ = p.y;
        startValue_ = controller_->getParamNormalized(kParamDrive);
        controller_->beginEdit(kParamDrive);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& p, const VSTGUI::CButtonState& b) override
    {
        if (!dragging_ || !b.isLeftButton() || !controller_) return VSTGUI::kMouseEventNotHandled;
        const auto v = std::clamp<ParamValue>(startValue_ + (startY_ - p.y) / 300.0, 0.0, 1.0);
        controller_->setParamNormalized(kParamDrive, v);
        controller_->performEdit(kParamDrive, v);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override
    {
        if (dragging_ && controller_) controller_->endEdit(kParamDrive);
        dragging_ = false;
        return VSTGUI::kMouseEventHandled;
    }
private:
    EditController* controller_ = nullptr;
    bool dragging_ = false;
    double startY_ = 0.0;
    ParamValue startValue_ = 0.30;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap_;
};

class ModeView final : public VSTGUI::CView {
public:
    ModeView(const VSTGUI::CRect& r, EditController* c) : CView(r), controller_(c)
    {
        const std::array<const char*,6> buttons{{
            "Triode_OUT_Runtime.png","Triode_IN_Runtime.png",
            "Pentode_OUT_Runtime.png","Pentode_IN_Runtime.png",
            "Iron_OUT_Runtime.png","Iron_IN_Runtime.png"
        }};
        for (size_t i=0;i<buttons.size();++i)
            button_[i] = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(buttons[i]));
        ledOff_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("LED_Amber_OFF_Runtime.png"));
        ledOn_  = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("LED_Amber_ON_Runtime.png"));
        setMouseEnabled(true);
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx || !controller_) { setDirty(false); return; }
        const bool bypass = isBypassed(controller_);
        const int active = characterIndex(controller_);

        // VSTGUI drawing uses the parent coordinate system for this custom view.
        // These are the measured final-layout positions on the 1536x1024 faceplate.
        constexpr std::array<double,3> bx{{1027,1167,1307}};
        constexpr double by = 704;
        constexpr std::array<double,3> lx{{1075,1215,1355}};
        constexpr double ly = 654;
        constexpr double bw = 126;
        constexpr double bh = 132;
        constexpr double led = 30;

        for (int i=0;i<3;++i) {
            const bool pressed = !bypass && active == i;
            auto& b = button_[static_cast<size_t>(i*2 + (pressed ? 1 : 0))];
            if (b) {
                VSTGUI::CRect dst(bx[i], by, bx[i]+bw, by+bh);
                b->draw(ctx, dst, VSTGUI::CPoint(0,0), 1.f);
            }
            auto& lamp = pressed ? ledOn_ : ledOff_;
            if (lamp) {
                VSTGUI::CRect dst(lx[i], ly, lx[i]+led, ly+led);
                lamp->draw(ctx, dst, VSTGUI::CPoint(0,0), 1.f);
            }
        }
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& p, const VSTGUI::CButtonState&) override
    {
        if (!controller_) return VSTGUI::kMouseEventNotHandled;
        constexpr std::array<double,3> bx{{1027,1167,1307}};
        constexpr double by = 704;
        constexpr double bw = 126;
        constexpr double bh = 132;

        for (int i=0;i<3;++i) {
            if (p.x >= bx[i] && p.x < bx[i]+bw && p.y >= by && p.y < by+bh) {
                const bool bypass = isBypassed(controller_);
                const int active = characterIndex(controller_);
                if (!bypass && active == i) {
                    setParameter(controller_, kParamOnOff, 1.0);
                } else {
                    setParameter(controller_, kParamCharacter, i / 2.0);
                    setParameter(controller_, kParamOnOff, 0.0);
                }
                invalid();
                return VSTGUI::kMouseEventHandled;
            }
        }
        return VSTGUI::kMouseEventHandled;
    }
private:
    EditController* controller_ = nullptr;
    std::array<VSTGUI::SharedPointer<VSTGUI::CBitmap>,6> button_;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> ledOff_;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> ledOn_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

class ZoomView final : public VSTGUI::CView {
public:
    ZoomView(const VSTGUI::CRect& r, SMX3Editor* e) : CView(r), editor_(e) { setMouseEnabled(true); }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx || !editor_) { setDirty(false); return; }
        auto r=getViewSize();
        ctx->setDrawMode(VSTGUI::kAntiAliasing);
        ctx->setFillColor(VSTGUI::CColor(15,13,11,205));
        ctx->setFrameColor(VSTGUI::CColor(151,116,68,190));
        ctx->setLineWidth(1);
        ctx->drawRect(r,VSTGUI::kDrawFilledAndStroked);
        ctx->setFont(VSTGUI::kNormalFontSmall);
        ctx->setFontColor(VSTGUI::CColor(214,193,157,255));
        ctx->drawString(editor_->getZoomFactor()>.59 ? "UI 100%" : "UI 75%", r, VSTGUI::kCenterText);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override
    {
        if (!editor_) return VSTGUI::kMouseEventNotHandled;
        editor_->setUserZoom(editor_->getZoomFactor()>.59 ? .50 : .68);
        invalid();
        return VSTGUI::kMouseEventHandled;
    }
private:
    SMX3Editor* editor_ = nullptr;
};

} // anonymous namespace

SMX3Editor::SMX3Editor(Steinberg::Vst::EditController* c)
: VSTGUI::VST3Editor(c, "view", "SMX3.uidesc"), controller_(c)
{
    setZoomFactor(.68);
    setAllowedZoomFactors({.50,.68});
}

void SMX3Editor::setUserZoom(double f)
{
    if (f == .50 || f == .68) setZoomFactor(f);
}

VSTGUI::CView* SMX3Editor::createView(const VSTGUI::UIAttributes& a,
                                      const VSTGUI::IUIDescription* d)
{
    if (const auto n = a.getAttributeValue(VSTGUI::IUIDescription::kCustomViewName)) {
        if (*n == "TubeGlow") return new TubeGlowView({0,0,1536,450});
        if (*n == "Nameplate") return new BitmapView({152,457,602,612}, "SMX3_Nameplate_Runtime.png");
        if (*n == "CompanyBadge") return new BitmapView({175,795,425,920}, "125A_Badge_Runtime.png");
        if (*n == "Drive") return new DriveView({563,496,993,926}, controller_);
        if (*n == "Mode") return new ModeView({1010,640,1455,890}, controller_);
        if (*n == "UiZoom") return new ZoomView({1370,70,1450,102}, this);
    }
    return VSTGUI::VST3Editor::createView(a,d);
}

} // namespace SaturatorMixFX
