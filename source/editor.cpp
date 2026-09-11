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

static void setParameter(EditController* c, ParamID id, ParamValue v) { if(!c)return; v=std::clamp<ParamValue>(v,0.,1.); c->beginEdit(id); c->setParamNormalized(id,v); c->performEdit(id,v); c->endEdit(id); }
static int characterIndex(EditController* c) { return c ? std::clamp(static_cast<int>(std::lround(c->getParamNormalized(kParamCharacter)*2.)),0,2) : 0; }
static bool isBypassed(EditController* c) { return !c || c->getParamNormalized(kParamOnOff)>=.5; }

class BitmapView final : public VSTGUI::CView {
public:
    BitmapView(const VSTGUI::CRect&r,const char*res):CView(r){ bitmap_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription(res)); setMouseEnabled(false); }
    void draw(VSTGUI::CDrawContext*c) override { if(c&&bitmap_) bitmap_->draw(c,getViewSize(),{0,0},1.f); setDirty(false); }
private: VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap_;
};

class TubeGlowView final : public VSTGUI::CView {
public:
    explicit TubeGlowView(const VSTGUI::CRect&r):CView(r) {
        tube_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_MITTEL.png"));
        setMouseEnabled(false);
        timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*){
            phase_ += 0.075;
            if(phase_ > 2.*kPi) phase_ -= 2.*kPi;
            invalid();
        },33);
    }
    void draw(VSTGUI::CDrawContext*ctx) override {
        if(!ctx||!tube_){setDirty(false);return;}
        constexpr std::array<double,3> cx{{421.,768.,1115.}};
        constexpr double cy=322.,tubeW=280.,tubeH=340.;
        const double srcW=tube_->getWidth();
        const double srcH=tube_->getHeight();
        if(srcW<=0.||srcH<=0.){setDirty(false);return;}

        // One immutable tube bitmap. Only its central reactor area is redrawn
        // on top of itself. This keeps glass, frame, screws and lettering fixed.
        const double pulse=0.5+0.5*std::sin(phase_);
        const float glowAlpha=static_cast<float>(0.12+0.58*pulse);
        const VSTGUI::CRect core(srcW*0.31,srcH*0.18,srcW*0.69,srcH*0.69);

        for(size_t i=0;i<3;++i){
            const double left=cx[i]-tubeW/2.;
            const double top=cy-tubeH/2.;
            VSTGUI::CGraphicsTransform transform;
            transform.scale(tubeW/srcW,tubeH/srcH).translate(left,top);
            VSTGUI::CDrawContext::Transform guard(*ctx,transform);

            // Static master tube.
            tube_->draw(ctx,VSTGUI::CRect(0.,0.,srcW,srcH),VSTGUI::CPoint(0.,0.),1.f);

            // Visible internal glow only: same exact pixels, same exact position.
            tube_->draw(ctx,core,VSTGUI::CPoint(core.left,core.top),glowAlpha);
            if(pulse>0.72)
                tube_->draw(ctx,core,VSTGUI::CPoint(core.left,core.top),static_cast<float>((pulse-0.72)*0.85));
        }
        setDirty(false);
    }
private:
    VSTGUI::SharedPointer<VSTGUI::CBitmap> tube_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    double phase_=0.;
};

class DriveView final : public VSTGUI::CView {
public:
    DriveView(const VSTGUI::CRect&r,EditController*c):CView(r),controller_(c){ ring_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Drive_Ring_Blue.png")); knob_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Drive.png")); setMouseEnabled(true); timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*){invalid();},33); }
    void draw(VSTGUI::CDrawContext*ctx) override { if(!ctx||!controller_){setDirty(false);return;} auto r=getViewSize(); if(knob_)knob_->draw(ctx,r,{0,0},1.f); if(ring_)ring_->draw(ctx,r,{0,0},1.f); auto center=r.getCenter(); double v=std::clamp(controller_->getParamNormalized(kParamDrive),0.,1.); double a=(-135.+270.*v)*kPi/180.; VSTGUI::CPoint p0(center.x+std::sin(a)*84.,center.y-std::cos(a)*84.),p1(center.x+std::sin(a)*126.,center.y-std::cos(a)*126.); ctx->setDrawMode(VSTGUI::kAntiAliasing); ctx->setLineWidth(9.); ctx->setFrameColor({0,0,0,120}); ctx->drawLine({p0.x+2,p0.y+2},{p1.x+2,p1.y+2}); ctx->setLineWidth(5.5); ctx->setFrameColor({245,248,252,255}); ctx->drawLine(p0,p1); setDirty(false); }
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&p,const VSTGUI::CButtonState&) override { if(!controller_)return VSTGUI::kMouseEventNotHandled; dragging_=true; startY_=p.y; startValue_=controller_->getParamNormalized(kParamDrive); controller_->beginEdit(kParamDrive); return VSTGUI::kMouseEventHandled; }
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint&p,const VSTGUI::CButtonState&b) override { if(!dragging_||!b.isLeftButton()||!controller_)return VSTGUI::kMouseEventNotHandled; auto v=std::clamp<ParamValue>(startValue_+(startY_-p.y)/300.,0.,1.); controller_->setParamNormalized(kParamDrive,v); controller_->performEdit(kParamDrive,v); invalid(); return VSTGUI::kMouseEventHandled; }
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint&,const VSTGUI::CButtonState&) override { if(dragging_&&controller_)controller_->endEdit(kParamDrive); dragging_=false; return VSTGUI::kMouseEventHandled; }
private: EditController*controller_=nullptr; bool dragging_=false; double startY_=0.; ParamValue startValue_=.30; VSTGUI::SharedPointer<VSTGUI::CBitmap>ring_,knob_; VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer>timer_;
};

class ModeView final : public VSTGUI::CView {
public:
    ModeView(const VSTGUI::CRect&r,EditController*c):CView(r),controller_(c){ off_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_OFF.png")); pressed_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_PRESSED.png")); ring_=VSTGUI::makeOwned<VSTGUI::CBitmap>(VSTGUI::CResourceDescription("SMX3_Button_Ring_Blue.png")); setMouseEnabled(true); timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*){invalid();},50); }
    void draw(VSTGUI::CDrawContext*ctx) override { if(!ctx||!controller_){setDirty(false);return;} bool bypass=isBypassed(controller_); int active=characterIndex(controller_); constexpr std::array<double,3>cx{{1065.,1231.,1398.}}; constexpr double cy=742.,size=130.; for(int i=0;i<3;++i){bool on=!bypass&&active==i; VSTGUI::CRect dst(cx[i]-size/2.,cy-size/2.,cx[i]+size/2.,cy+size/2.); auto&body=on?pressed_:off_; if(body)body->draw(ctx,dst,{0,0},1.f); if(on&&ring_)ring_->draw(ctx,dst,{0,0},1.f);} setDirty(false); }
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&p,const VSTGUI::CButtonState&) override { if(!controller_)return VSTGUI::kMouseEventNotHandled; constexpr std::array<double,3>cx{{1065.,1231.,1398.}}; constexpr double cy=742.,size=130.; for(int i=0;i<3;++i){double dx=p.x-cx[i],dy=p.y-cy; if(dx*dx+dy*dy<=(size*.5)*(size*.5)){bool bypass=isBypassed(controller_);int active=characterIndex(controller_);if(!bypass&&active==i)setParameter(controller_,kParamOnOff,1.);else{setParameter(controller_,kParamCharacter,i/2.);setParameter(controller_,kParamOnOff,0.);}invalid();return VSTGUI::kMouseEventHandled;}} return VSTGUI::kMouseEventHandled; }
private: EditController*controller_=nullptr; VSTGUI::SharedPointer<VSTGUI::CBitmap>off_,pressed_,ring_; VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer>timer_;
};

class ZoomView final : public VSTGUI::CView {
public:
    ZoomView(const VSTGUI::CRect&r,SMX3Editor*e):CView(r),editor_(e){setMouseEnabled(true);}
    void draw(VSTGUI::CDrawContext*ctx) override { if(!ctx||!editor_){setDirty(false);return;} auto r=getViewSize(); ctx->setDrawMode(VSTGUI::kAntiAliasing); ctx->setFillColor({245,247,250,230}); ctx->setFrameColor({0,145,255,210}); ctx->setLineWidth(1.5); ctx->drawRect(r,VSTGUI::kDrawFilledAndStroked); ctx->setFont(VSTGUI::kNormalFontSmall); ctx->setFontColor({18,24,32,255}); ctx->drawString(editor_->getZoomFactor()>.59?"UI 100%":"UI 75%",r,VSTGUI::kCenterText); setDirty(false); }
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&,const VSTGUI::CButtonState&) override { if(!editor_)return VSTGUI::kMouseEventNotHandled; editor_->setUserZoom(editor_->getZoomFactor()>.59?.50:.68); invalid(); return VSTGUI::kMouseEventHandled; }
private: SMX3Editor*editor_=nullptr;
};

} // anonymous namespace

SMX3Editor::SMX3Editor(Steinberg::Vst::EditController*c):VSTGUI::VST3Editor(c,"view","SMX3.uidesc"),controller_(c){setZoomFactor(.68);setAllowedZoomFactors({.50,.68});}
void SMX3Editor::setUserZoom(double f){if(f==.50||f==.68)setZoomFactor(f);}
VSTGUI::CView* SMX3Editor::createView(const VSTGUI::UIAttributes&a,const VSTGUI::IUIDescription*d){if(const auto n=a.getAttributeValue(VSTGUI::IUIDescription::kCustomViewName)){if(*n=="TubeGlow")return new TubeGlowView({0,0,1536,520});if(*n=="Underlight")return new BitmapView({243,895,1293,965},"SMX3_Base_Underlight.png");if(*n=="Drive")return new DriveView({618,553,918,853},controller_);if(*n=="Mode")return new ModeView({990,660,1468,825},controller_);if(*n=="UiZoom")return new ZoomView({1370,70,1450,102},this);}return VSTGUI::VST3Editor::createView(a,d);}

} // namespace SaturatorMixFX