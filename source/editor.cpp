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
        for(size_t i=0;i<bolts_.size();++i) bolts_[i].wait=6+static_cast<int>(i)*5;
        timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*){
            bool changed=false;
            for(size_t i=0;i<bolts_.size();++i){
                auto& b=bolts_[i];
                if(b.stage>0){
                    if(b.stage<=kLeaderStages){
                        ++b.stage;
                        if(b.stage>kLeaderStages) b.flashTicks=2;
                    } else if(b.flashTicks>0){
                        --b.flashTicks;
                        if(b.flashTicks==0){ b.stage=0; b.wait=8+static_cast<int>(random01()*20.); }
                    }
                    changed=true;
                } else if(--b.wait<=0){ makeBolt(i); changed=true; }
            }
            if(changed) invalid();
        },33);
    }

    void draw(VSTGUI::CDrawContext*ctx) override {
        if(!ctx||!tube_){setDirty(false);return;}
        constexpr std::array<double,3> cx{{421.,768.,1115.}};
        constexpr double cy=322.,tubeW=280.,tubeH=340.;
        const double srcW=tube_->getWidth();
        const double srcH=tube_->getHeight();
        if(srcW<=0.||srcH<=0.){setDirty(false);return;}
        for(size_t i=0;i<3;++i){
            const double left=cx[i]-tubeW/2., top=cy-tubeH/2.;
            VSTGUI::CGraphicsTransform transform;
            transform.scale(tubeW/srcW,tubeH/srcH).translate(left,top);
            VSTGUI::CDrawContext::Transform guard(*ctx,transform);
            tube_->draw(ctx,VSTGUI::CRect(0.,0.,srcW,srcH),VSTGUI::CPoint(0.,0.),1.f);
        }

        ctx->setDrawMode(VSTGUI::kAntiAliasing);
        for(const auto& b:bolts_){
            if(b.stage<=0) continue;
            const bool fullFlash=b.stage>kLeaderStages;
            const size_t visible=fullFlash?b.points.size():std::min<size_t>(b.points.size(),static_cast<size_t>(b.stage+1));
            if(visible<2) continue;

            // Broad transparent halo: makes the discharge illuminate the glass instead of reading as a drawn line.
            ctx->setLineWidth(fullFlash?18.0:13.0);
            ctx->setFrameColor({0,92,255,static_cast<uint8_t>(fullFlash?42:26)});
            for(size_t p=1;p<visible;++p) ctx->drawLine(b.points[p-1],b.points[p]);
            ctx->setLineWidth(fullFlash?10.0:7.5);
            ctx->setFrameColor({0,130,255,static_cast<uint8_t>(fullFlash?80:52)});
            for(size_t p=1;p<visible;++p) ctx->drawLine(b.points[p-1],b.points[p]);
            ctx->setLineWidth(fullFlash?4.0:3.0);
            ctx->setFrameColor({75,195,255,static_cast<uint8_t>(fullFlash?245:215)});
            for(size_t p=1;p<visible;++p) ctx->drawLine(b.points[p-1],b.points[p]);
            ctx->setLineWidth(fullFlash?1.5:1.1);
            ctx->setFrameColor({248,254,255,255});
            for(size_t p=1;p<visible;++p) ctx->drawLine(b.points[p-1],b.points[p]);

            for(const auto& br:b.branches){
                if(br.root>=visible) continue;
                const size_t bv=fullFlash?br.points.size():std::min<size_t>(br.points.size(),1u+(visible-br.root));
                if(bv<2) continue;
                auto drawBranch=[&](double width,VSTGUI::CColor color){
                    ctx->setLineWidth(width); ctx->setFrameColor(color);
                    ctx->drawLine(b.points[br.root],br.points[0]);
                    for(size_t p=1;p<bv;++p) ctx->drawLine(br.points[p-1],br.points[p]);
                };
                drawBranch(fullFlash?10.0:7.0,{0,95,255,static_cast<uint8_t>(fullFlash?32:20)});
                drawBranch(fullFlash?5.5:4.0,{0,140,255,static_cast<uint8_t>(fullFlash?68:42)});
                drawBranch(fullFlash?2.2:1.7,{105,210,255,static_cast<uint8_t>(fullFlash?220:180)});
                drawBranch(.75,{235,251,255,static_cast<uint8_t>(fullFlash?250:215)});
            }
        }
        setDirty(false);
    }

private:
    static constexpr int kLeaderStages=6;
    struct Branch { size_t root=0; std::array<VSTGUI::CPoint,3> points{}; };
    struct Bolt { std::array<VSTGUI::CPoint,7> points{}; std::array<Branch,3> branches{}; int stage=0; int flashTicks=0; int wait=0; };
    double random01(){ seed_=1664525u*seed_+1013904223u; return static_cast<double>((seed_>>8)&0x00FFFFFFu)/16777215.0; }
    double randomSigned(double amount){ return (random01()*2.0-1.0)*amount; }
    void makeBolt(size_t index){
        static constexpr std::array<double,3> cx{{421.,768.,1115.}};
        auto& b=bolts_[index];
        const double startY=238.+randomSigned(5.), endY=368.+randomSigned(5.);
        for(size_t p=0;p<b.points.size();++p){
            const double t=static_cast<double>(p)/static_cast<double>(b.points.size()-1);
            const double y=startY+(endY-startY)*t;
            const double spread=(p==0||p+1==b.points.size())?4.:24.;
            b.points[p]=VSTGUI::CPoint(cx[index]+randomSigned(spread),y+randomSigned(4.));
        }
        const std::array<size_t,3> roots{{2,3,4}};
        for(size_t j=0;j<b.branches.size();++j){
            auto& br=b.branches[j]; br.root=roots[j];
            const double dir=random01()<.5?-1.0:1.0, reach=22.+random01()*24., drop=14.+random01()*20.;
            const auto root=b.points[br.root];
            br.points[0]=VSTGUI::CPoint(root.x+dir*(8.+random01()*7.),root.y+6.+random01()*6.);
            br.points[1]=VSTGUI::CPoint(root.x+dir*(14.+reach*.45)+randomSigned(4.),root.y+drop*.55+randomSigned(3.));
            br.points[2]=VSTGUI::CPoint(root.x+dir*reach+randomSigned(3.),root.y+drop+randomSigned(3.));
            const double minX=cx[index]-58.,maxX=cx[index]+58.;
            for(auto& p:br.points){ p.x=std::clamp(p.x,minX,maxX); p.y=std::clamp(p.y,246.,374.); }
        }
        b.stage=1; b.flashTicks=0; b.wait=0;
    }
    VSTGUI::SharedPointer<VSTGUI::CBitmap> tube_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    std::array<Bolt,3> bolts_{};
    unsigned int seed_=0x125A3u;
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
    ModeView(const VSTGUI::CRect&r,EditController*c):CView(r),controller_(c){setMouseEnabled(true);timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*){invalid();},50);}
    void draw(VSTGUI::CDrawContext*ctx) override {
        if(!ctx||!controller_){setDirty(false);return;}
        const bool bypass=isBypassed(controller_); const int active=characterIndex(controller_); ctx->setDrawMode(VSTGUI::kAntiAliasing);
        for(int i=0;i<3;++i){
            const bool on=!bypass&&active==i; const double x=kModeCx[i],y=kModeCy;
            VSTGUI::CRect shadow(x-53.,y-50.,x+55.,y+58.); ctx->setFillColor({0,0,0,78}); ctx->setFrameColor({0,0,0,0}); ctx->drawEllipse(shadow,VSTGUI::kDrawFilled);
            if(on){ VSTGUI::CRect glow(x-57.,y-57.,x+57.,y+57.); ctx->setFillColor({0,118,255,34}); ctx->setFrameColor({0,151,255,255}); ctx->setLineWidth(4.); ctx->drawEllipse(glow,VSTGUI::kDrawFilledAndStroked); }
            VSTGUI::CRect metal(x-53.,y-53.,x+53.,y+53.); ctx->setFillColor({72,74,78,255}); ctx->setFrameColor({24,25,28,255}); ctx->setLineWidth(2.); ctx->drawEllipse(metal,VSTGUI::kDrawFilledAndStroked);
            VSTGUI::CRect face(x-45.,y-45.,x+45.,y+45.); ctx->setFillColor(on?VSTGUI::CColor{38,40,44,255}:VSTGUI::CColor{28,29,32,255}); ctx->setFrameColor({12,13,15,255}); ctx->setLineWidth(2.); ctx->drawEllipse(face,VSTGUI::kDrawFilledAndStroked);
        }
        setDirty(false);
    }
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&p,const VSTGUI::CButtonState&) override { if(!controller_)return VSTGUI::kMouseEventNotHandled; for(int i=0;i<3;++i){const double dx=p.x-kModeCx[i],dy=p.y-kModeCy;if(dx*dx+dy*dy<=kModeHitRadius*kModeHitRadius){const bool bypass=isBypassed(controller_);const int active=characterIndex(controller_);if(!bypass&&active==i)setParameter(controller_,kParamOnOff,1.);else{setParameter(controller_,kParamCharacter,i/2.);setParameter(controller_,kParamOnOff,0.);}invalid();return VSTGUI::kMouseEventHandled;}}return VSTGUI::kMouseEventHandled; }
private:
    static constexpr std::array<double,3> kModeCx{{1065.,1231.,1398.}}; static constexpr double kModeCy=742.; static constexpr double kModeHitRadius=56.; EditController*controller_=nullptr; VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer>timer_;
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
