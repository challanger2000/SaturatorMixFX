#include "processor.h"
#include "controller.h"
#include "pluginids.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#define stringPluginName "SMX-3 Mix FX"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("challanger2000",
                  "https://github.com/challanger2000/SaturatorMixFX",
                  "")

// Studio One/Fender Studio recognizes dedicated Mix FX processors through
// the host-specific factory category "Audio Mix Processor". Keep this build
// separate from the normal Channel VST3 (main branch).
DEF_CLASS2(INLINE_UID_FROM_FUID(SaturatorMixFX::kProcessorUID),
           PClassInfo::kManyInstances,
           "Audio Mix Processor",
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
