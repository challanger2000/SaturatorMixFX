#include "processor.h"
#include "pluginids.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace SaturatorMixFX {
using namespace Steinberg; using namespace Steinberg::Vst;
namespace { constexpr double kPi=3.14159265358979323846; double clamp01(double v){return std::max(0.0,std::min(1.0,v));} double dbToGain(double d){return std::pow(10.0,d/20.0);} double softClip(double x){return std::tanh(x);} }

Processor::Processor(){setControllerClass(kControllerUID);}
tresult PLUGIN_API Processor::initialize(FUnknown* c){auto r=AudioEffect::initialize(c);if(r!=kResultOk)return r;addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo,kMain,BusInfo::kDefaultActive);addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo,kMain,BusInfo::kDefaultActive);return kResultOk;}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* i,int32 ni,SpeakerArrangement* o,int32 no){if(ni==1&&no==1&&i[0]==SpeakerArr::kStereo&&o[0]==SpeakerArr::kStereo)return AudioEffect::setBusArrangements(i,ni,o,no);return kResultFalse;}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 s){return(s==kSample32||s==kSample64)?kResultTrue:kResultFalse;}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& s){auto r=AudioEffect::setupProcessing(s);if(r!=kResultOk)return r;sampleRate_=s.sampleRate>1.0?s.sampleRate:44100.0;smoothCoeff_=std::exp(-1.0/(0.018*sampleRate_));double ir=sampleRate_*kOversample;ironMemoryCoeff_=std::exp(-2.0*kPi*95.0/ir);dcCoeff_=std::exp(-2.0*kPi*18.0/sampleRate_);lowCoeff_=std::exp(-2.0*kPi*145.0/sampleRate_);highCoeff_=std::exp(-2.0*kPi*6200.0/sampleRate_);envFastCoeff_=std::exp(-1.0/(0.0015*sampleRate_));envSlowCoeff_=std::exp(-1.0/(0.035*sampleRate_));resetDsp();return kResultOk;}
tresult PLUGIN_API Processor::setActive(TBool s){if(s)resetDsp();return AudioEffect::setActive(s);}
void Processor::resetDsp(){for(auto& s:channelState_)s={};smoothDrive_=drive_;smoothMix_=mix_;smoothOutput_=output_;}
void Processor::updateSmoothers(){double a=1.0-smoothCoeff_;smoothDrive_=smoothCoeff_*smoothDrive_+a*drive_;smoothMix_=smoothCoeff_*smoothMix_+a*mix_;smoothOutput_=smoothCoeff_*smoothOutput_+a*output_;}
void Processor::readParameterChanges(IParameterChanges* c){if(!c)return;for(int32 i=0;i<c->getParameterCount();++i){auto*q=c->getParameterData(i);if(!q||q->getPointCount()<=0)continue;int32 off=0;ParamValue v=0;if(q->getPoint(q->getPointCount()-1,off,v)!=kResultTrue)continue;switch(q->getParameterId()){case kParamOnOff:onOff_=clamp01(v);break;case kParamDrive:drive_=clamp01(v);break;case kParamCharacter:character_=clamp01(v);break;case kParamMix:mix_=clamp01(v);break;case kParamOutput:output_=clamp01(v);break;default:break;}}}

double Processor::shapeTriode(double x)const{constexpr double b=.22;double p=std::tanh(1.18*x+b)-std::tanh(b),n=std::tanh(.92*x-.55*b)+std::tanh(.55*b);double y=.64*p+.36*n;y+=.075*x*x/(1.0+1.8*std::abs(x));return y;}
double Processor::shapePentode(double x)const{return .58*std::tanh(1.55*x)+.27*std::tanh(2.85*x)+.15*std::atan(2.2*x)*(2.0/kPi);}
double Processor::shapeIron(double x,ChannelState&s){s.ironMemory=ironMemoryCoeff_*s.ironMemory+(1.0-ironMemoryCoeff_)*x;double m=s.ironMemory,f=x+.22*m;return .72*std::tanh(1.22*f)+.28*f/(1.0+.42*std::abs(f))+.035*m*std::abs(m);}
double Processor::processNonlinear(double x,int m,ChannelState&s){if(m==kTriode)return shapeTriode(x);if(m==kPentode)return shapePentode(x);return shapeIron(x,s);}
double Processor::dcBlock(double x,ChannelState&s){double y=x-s.dcX1+dcCoeff_*s.dcY1;s.dcX1=x;s.dcY1=y;return y;}

tresult PLUGIN_API Processor::process(ProcessData& d){readParameterChanges(d.inputParameterChanges);if(d.numInputs==0||d.numOutputs==0||d.numSamples<=0)return kResultOk;auto&in=d.inputs[0];auto&out=d.outputs[0];int32 chans=std::min<int32>(std::min(in.numChannels,out.numChannels),kMaxChannels);bool bypass=onOff_>=.5;int mode=std::clamp((int)std::lround(character_*2.0),0,2);
 auto run=[&](auto**srcs,auto**dsts){using Sample=std::remove_pointer_t<std::remove_pointer_t<decltype(srcs)>>;for(int32 n=0;n<d.numSamples;++n){updateSmoothers();double driveDb=24.0*smoothDrive_,inputGain=dbToGain(driveDb),wet=smoothMix_,dry=1.0-wet,outGain=dbToGain(-18.0+24.0*smoothOutput_);double trim=(mode==kTriode?-9.50:(mode==kPentode?-19.00:-14.47))*smoothDrive_;double baseComp=(mode==kTriode?-.46:(mode==kPentode?-.52:-.40))*driveDb;double comp=dbToGain(trim+baseComp);
  for(int32 ch=0;ch<chans;++ch){auto*src=srcs[ch];auto*dst=dsts[ch];if(!src||!dst)continue;double x=(double)src[n];auto&s=channelState_[(size_t)ch];if(bypass){dst[n]=(Sample)x;s.previousInput=x;continue;}
   // Three-band pre-emphasis without crossover phase mismatch in the output: only the nonlinear drive sees it.
   s.lowBand=lowCoeff_*s.lowBand+(1.0-lowCoeff_)*x;
   s.highSmooth=highCoeff_*s.highSmooth+(1.0-highCoeff_)*x;
   double low=s.lowBand,high=x-s.highSmooth,mid=x-low-high;
   double coloured=x;
   if(mode==kTriode) coloured=.92*low+1.08*mid+.88*high;       // warm vocal/mid density, protected sub/top
   else if(mode==kPentode) coloured=.84*low+1.12*mid+1.06*high;// bite without wasting distortion on sub
   else coloured=1.10*low+1.02*mid+.82*high;                  // transformer body and gentle HF rounding

   // Fast/slow envelope contrast detects attacks. Strong attacks back the nonlinear drive off slightly,
   // preserving punch instead of flattening every drum transient. No lookahead and no pumping gain stage.
   double a=std::abs(x);s.envFast=envFastCoeff_*s.envFast+(1.0-envFastCoeff_)*a;s.envSlow=envSlowCoeff_*s.envSlow+(1.0-envSlowCoeff_)*a;
   double transient=std::max(0.0,s.envFast-s.envSlow);double normTransient=transient/(.06+s.envSlow);normTransient=clamp01(normTransient);
   double protect=(mode==kPentode?.34:(mode==kIron?.24:.18));double dynamicGain=inputGain*(1.0-protect*normTransient);

   double acc=0.0,prev=s.previousInput;for(int os=0;os<kOversample;++os){double t=(double)(os+1)/kOversample;double interp=prev+(coloured-prev)*t;acc+=processNonlinear(interp*dynamicGain,mode,s);}s.previousInput=coloured;
   double processed=(acc/kOversample)*comp;
   // Blend a small, attack-dependent clean component back into the wet path. This restores leading edges
   // while leaving sustained material fully saturated; Pentode gets the most help because it is the firmest mode.
   double attackBlend=normTransient*(mode==kPentode?.16:(mode==kIron?.11:.08));processed=processed*(1.0-attackBlend)+x*attackBlend;
   processed=softClip(processed*1.03)/1.03;processed=dcBlock(processed,s);dst[n]=(Sample)((dry*x+wet*processed)*outGain);
  }} };
 if(d.symbolicSampleSize==kSample64)run(in.channelBuffers64,out.channelBuffers64);else if(d.symbolicSampleSize==kSample32)run(in.channelBuffers32,out.channelBuffers32);else return kResultFalse;out.silenceFlags=in.silenceFlags;return kResultOk;}

tresult PLUGIN_API Processor::setState(IBStream* s){if(!s)return kResultFalse;IBStreamer f(s,kLittleEndian);double b=0,dr=.30,c=0,m=1,o=.75;if(!f.readDouble(b)||!f.readDouble(dr)||!f.readDouble(c)||!f.readDouble(m)||!f.readDouble(o))return kResultFalse;onOff_=clamp01(b);drive_=clamp01(dr);character_=clamp01(c);mix_=clamp01(m);output_=clamp01(o);smoothDrive_=drive_;smoothMix_=mix_;smoothOutput_=output_;return kResultOk;}
tresult PLUGIN_API Processor::getState(IBStream* s){if(!s)return kResultFalse;IBStreamer f(s,kLittleEndian);f.writeDouble(onOff_);f.writeDouble(drive_);f.writeDouble(character_);f.writeDouble(mix_);f.writeDouble(output_);return kResultOk;}
} // namespace SaturatorMixFX
