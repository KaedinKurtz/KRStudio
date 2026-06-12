#include "HardwareCaps.hpp"

namespace krs {

HardwareCaps& hardwareCaps()
{
    static HardwareCaps caps;
    return caps;
}

} // namespace krs
