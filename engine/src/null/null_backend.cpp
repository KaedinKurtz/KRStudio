// Null backend: the reference implementation of API semantics for unit tests and
// logic-only runs. Today its behavior lives in the shared facade (handle tables in
// core/device.cpp); this TU exists as the backend's home — WP3's render-graph tests
// grow it into a recorder that asserts exact barrier/layout sequences.

namespace krsg::null_backend
{

// Intentionally empty in WP0.

} // namespace krsg::null_backend
