#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SaturatorMixFX {

static const Steinberg::FUID kProcessorUID (0x74D9F51A, 0x2E754BC8, 0xA9439C72, 0x1DB0A1F4);
static const Steinberg::FUID kControllerUID (0x1D27F0E2, 0xA98442CE, 0xB8B0B6D1, 0x90E83753);

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
