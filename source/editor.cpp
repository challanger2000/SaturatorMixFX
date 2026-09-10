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

namespace SaturatorMixFX {
namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

constexpr double kPi = 3.14159265358979323846;

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
        const std::array<const char*,3> tubes{{
            "SMX3_Tube_1.png", "SMX3_Tube_2.png", "SMX3_Tube_3.png"
        }};
        const std::array<const char*,3> glows{{
            "SMX3_Tube_Glow_1.png", "SMX3_Tube_Glow_2.png", "SMX3_Tube_Glow_3.png"
        }};
        for (size_t i=0;i<3;++i) {
            tube_[i] = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(tubes[i]));
            glow_[i] = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(glows[i]));
        }
        setMouseEnabled(false);
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                phase_ += 0.070;
                invalid();
            }, 50);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx) { setDirty(false); return; }

        constexpr std::array<double,3> cx{{421.0, 768.0, 1115.0}};
        constexpr double tubeCy = 322.0;
        constexpr double tubeW = 240.0;
        constexpr double tubeH = 390.0;
        constexpr double glowW = 260.0;
        constexpr double glowH = 260.0;
        const std::array<double,3> phaseOffset{{0.0, 2.15, 4.55}};
        const std::array<double,3> speed{{1.00, 0.79, 1.21}};

        // Build a visibly larger light field from several native-size halo passes.
        // This avoids relying on bitmap scaling and makes the breathing obvious in the black bay.
        const std::array<VSTGUI::CPoint,5> haloOffset{{
            VSTGUI::CPoint(-18.0,  0.0),
            VSTGUI::CPoint( 18.0,  0.0),
            VSTGUI::CPoint(  0.0,-18.0),
            VSTGUI::CPoint(  0.0, 18.0),
            VSTGUI::CPoint(  0.0,  0.0)
        }};

        for (size_t i=0;i<3;++i) {
            if (!glow_[i]) continue;

            double breathe = 0.5 + 0.5 * std::sin(phase_ * speed[i] + phaseOffset[i]);
            breathe = 0.15 + 0.85 * breathe;

            for (size_t p=0; p<haloOffset.size(); ++p) {
                const double passGain = (p == haloOffset.size()-1) ? 0.95 : 0.42;
                const float alpha = static_cast<float>(std::clamp(breathe * passGain, 0.0, 1.0));
                const double x = cx[i] + haloOffset[p].x;
                const double y = tubeCy + haloOffset[p].y;
                VSTGUI::CRect dst(x-glowW/2.0, y-glowH/2.0,
                                  x+glowW/2.0, y+glowH/2.0);
                glow_[i]->draw(ctx, dst, VSTGUI::CPoint(0,0), alpha);
            }
        }

        // Tube stays crisp while the surrounding light field breathes behind it.
        for (size_t i=0;i<3;++i) {
            if (!tube_[i]) continue;
            VSTGUI::CRect dst(cx[i]-tubeW/2.0, tubeCy-tubeH/2.0,
                              cx[i]+tubeW/2.0, tubeCy+tubeH/2.0);
            tube_[i]->draw(ctx, dst, VSTGUI::CPoint(0,0), 1.f);
        }

        // Small centre pulse over the glass gives a visible internal change as well.
        for (size_t i=0;i<3;++i) {
            if (!glow_[i]) continue;
            const double pulse = 0.08 + 0.34 * (0.5 + 0.5 * std::sin(phase_ * speed[i] + phaseOffset[i]));
            VSTGUI::CRect dst(cx[i]-glowW/2.0, tubeCy-glowH/2.0,
                              cx[i]+glowW/2.0, tubeCy+glowH/2.0);
            glow_[i]->draw(ctx, dst, VSTGUI::CPoint(0,0), static_cast<float>(pulse));
        }
        setDirty(false);
    }

private:
    std::array<VSTGUI::SharedPointer<VSTGUI::CBitmap>,3> tube_;
    std::array<VSTGUI::SharedPointer<VSTGUI::CBitmap>,3> glow_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    double phase_ = 0.0;
};

class DriveView final : public VSTGUI::CView {
public:
    DriveView(const VSTGUI::CRect& r, EditController* c) : CView(r), controller_(c)
    {
        ring_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Drive_Ring_Blue.png"));
        knob_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Drive.png"));
        setMouseEnabled(true);
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) { invalid(); }, 33);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx || !controller_) { setDirty(false); return; }
        const auto r = getViewSize();
        if (ring_) ring_->draw(ctx, r, VSTGUI::CPoint(0,0), 0.92f);
        if (knob_) knob_->draw(ctx, r, VSTGUI::CPoint(0,0), 1.f);

        const auto center = r.getCenter();
        const double v = std::clamp(controller_->getParamNormalized(kParamDrive), 0.0, 1.0);
        const double a = (-135.0 + 270.0 * v) * kPi / 180.0;
        const VSTGUI::CPoint p0(center.x + std::sin(a) * 84.0,
                                center.y - std::cos(a) * 84.0);
        const VSTGUI::CPoint p1(center.x + std::sin(a) * 126.0,
                                center.y - std::cos(a) * 126.0);
        ctx->setDrawMode(VSTGUI::kAntiAliasing);
        ctx->setLineWidth(9.0);
        ctx->setFrameColor(VSTGUI::CColor(0, 0, 0, 120));
        ctx->drawLine(VSTGUI::CPoint(p0.x+2,p0.y+2), VSTGUI::CPoint(p1.x+2,p1.y+2));
        ctx->setLineWidth(5.5);
        ctx->setFrameColor(VSTGUI::CColor(245, 248, 252, 255));
        ctx->drawLine(p0, p1);
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
    VSTGUI::SharedPointer<VSTGUI::CBitmap> ring_;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> knob_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

class ModeView final : public VSTGUI::CView {
public:
    ModeView(const VSTGUI::CRect& r, EditController* c) : CView(r), controller_(c)
    {
        off_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_OFF.png"));
        pressed_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_PRESSED.png"));
        ring_ = VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_Ring_Blue.png"));
        setMouseEnabled(true);
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50);
    }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx || !controller_) { setDirty(false); return; }
        const bool bypass = isBypassed(controller_);
        const int active = characterIndex(controller_);
        constexpr std::array<double,3> cx{{1065.0, 1231.0, 1398.0}};
        constexpr double cy = 742.0;
        constexpr double size = 130.0;

        for (int i=0;i<3;++i) {
            const bool on = !bypass && active == i;
            VSTGUI::CRect dst(cx[i]-size/2.0, cy-size/2.0,
                              cx[i]+size/2.0, cy+size/2.0);
            auto& body = on ? pressed_ : off_;
            if (body) body->draw(ctx, dst, VSTGUI::CPoint(0,0), 1.f);
            if (on && ring_) ring_->draw(ctx, dst, VSTGUI::CPoint(0,0), 1.f);
        }
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& p, const VSTGUI::CButtonState&) override
    {
        if (!controller_) return VSTGUI::kMouseEventNotHandled;
        constexpr std::array<double,3> cx{{1065.0, 1231.0, 1398.0}};
        constexpr double cy = 742.0;
        constexpr double size = 130.0;

        for (int i=0;i<3;++i) {
            const double dx = p.x - cx[i];
            const double dy = p.y - cy;
            if (dx*dx + dy*dy <= (size*0.5)*(size*0.5)) {
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
    VSTGUI::SharedPointer<VSTGUI::CBitmap> off_;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> pressed_;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> ring_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

class ZoomView final : public VSTGUI::CView {
public:
    ZoomView(const VSTGUI::CRect& r, SMX3Editor* e) : CView(r), editor_(e) { setMouseEnabled(true); }

    void draw(VSTGUI::CDrawContext* ctx) override
    {
        if (!ctx || !editor_) { setDirty(false); return; }
        auto r = getViewSize();
        ctx->setDrawMode(VSTGUI::kAntiAliasing);
        ctx->setFillColor(VSTGUI::CColor(245,247,250,230));
        ctx->setFrameColor(VSTGUI::CColor(0,145,255,210));
        ctx->setLineWidth(1.5);
        ctx->drawRect(r,VSTGUI::kDrawFilledAndStroked);
        ctx->setFont(VSTGUI::kNormalFontSmall);
        ctx->setFontColor(VSTGUI::CColor(18,24,32,255));
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
        if (*n == "TubeGlow") return new TubeGlowView({0,0,1536,520});
        if (*n == "Underlight") return new BitmapView({243,895,1293,965}, "SMX3_Base_Underlight.png");
        if (*n == "Drive") return new DriveView({618,553,918,853}, controller_);
        if (*n == "Mode") return new ModeView({990,660,1468,825}, controller_);
        if (*n == "UiZoom") return new ZoomView({1370,70,1450,102}, this);
    }
    return VSTGUI::VST3Editor::createView(a,d);
}

} // namespace SaturatorMixFX