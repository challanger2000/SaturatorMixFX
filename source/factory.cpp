#include "processor.h"
#include "controller.h"
#include "pluginids.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#define stringPluginName "SaturatorMixFX"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("challanger2000",
                  "https://github.com/challanger2000/SaturatorMixFX",
                  "")

DEF_CLASS2(INLINE_UID_FROM_FUID(SaturatorMixFX::kProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Vst::kDistributable,
           Vst::PlugType::kFx,
           SATURATORMIXFX_VERSION,
           kVstVersionString,
           SaturatorMixFX::Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(SaturatorMixFX::kControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           stringPluginName " Controller",
           0,
           "",
           SATURATORMIXFX_VERSION,
           kVstVersionString,
           SaturatorMixFX::Controller::createInstance)

END_FACTORY
