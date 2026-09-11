#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SaturatorMixFX {

// Dedicated IDs for the Studio One Mix FX build. Keep separate from the normal Channel VST3.
static const Steinberg::FUID kProcessorUID (0xA54E2D71, 0x6C8B4F29, 0x9E134A62, 0xC7D5B801);
static const Steinberg::FUID kControllerUID (0x3F91C6A4, 0xD2E7485B, 0xB06A1F93, 0x74CE520D);

constexpr Steinberg::Vst::ParamID kParamOnOff      = 99;
constexpr Steinberg::Vst::ParamID kParamDrive      = 100;
constexpr Steinberg::Vst::ParamID kParamCharacter  = 101;
constexpr Steinberg::Vst::ParamID kParamMix        = 102;
constexpr Steinberg::Vst::ParamID kParamOutput     = 103;

enum Character : int32_t {
    kTriode = 0,
    kPentode = 1,
    kIron = 2
};

} // namespace SaturatorMixFX
