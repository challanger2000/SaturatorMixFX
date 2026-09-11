#include "processor.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace SaturatorMixFX {

Steinberg::tresult PLUGIN_API Processor::queryInterface(const Steinberg::TUID iid, void** obj)
{
    const auto result = Steinberg::Vst::AudioEffect::queryInterface(iid, obj);

    // Temporary Mix FX reverse-engineering diagnostic. Studio One asks the
    // Audio Mix Processor for private interfaces before the channel callbacks
    // are enabled. Logging the raw 16-byte IID lets us identify those interfaces
    // without touching the proven audio/DSP path.
    if (const char* temp = std::getenv("TEMP"))
    {
        std::ofstream log(std::string(temp) + "\\SMX3_MixFX_QI.log", std::ios::app);
        if (log)
        {
            log << "QI ";
            const auto* bytes = reinterpret_cast<const unsigned char*>(iid);
            log << std::hex << std::uppercase << std::setfill('0');
            for (int i = 0; i < 16; ++i)
                log << std::setw(2) << static_cast<unsigned int>(bytes[i]);
            log << " result=" << std::dec << static_cast<int>(result)
                << " obj=" << (obj && *obj ? "YES" : "NO") << '\n';
        }
    }

    return result;
}

} // namespace SaturatorMixFX
