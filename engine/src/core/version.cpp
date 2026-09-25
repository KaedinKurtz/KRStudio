#include <krsg/version.h>

namespace krsg
{

Version GetVersion()
{
    return Version{KRSG_VERSION_MAJOR, KRSG_VERSION_MINOR, KRSG_VERSION_PATCH};
}

} // namespace krsg
