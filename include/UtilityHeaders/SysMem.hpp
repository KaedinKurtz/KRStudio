#pragma once
#include <cstddef>
// Process memory introspection, isolated from windows.h so PhysX/Eigen/Qt TUs
// never see the min/max/near/far macros. Used by GATE D (no-resource-growth).
namespace krs {
size_t processWorkingSetBytes();   // current working set (RSS), 0 on failure
}
