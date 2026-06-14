#include "SysMem.hpp"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>   // K32GetProcessMemoryInfo is exported from kernel32 (no psapi.lib link)
namespace krs {
size_t processWorkingSetBytes()
{
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.WorkingSetSize);
    return 0;
}
} // namespace krs
#else
namespace krs { size_t processWorkingSetBytes() { return 0; } }
#endif
