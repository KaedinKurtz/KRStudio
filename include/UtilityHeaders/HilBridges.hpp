#pragma once

#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief OS hardware-in-the-loop bridges. The simulated camera pixel buffer is
 * exposed to host perception stacks as a video device, and simulated joint
 * telemetry is exposed on a CAN bus — both behind abstract interfaces so the
 * physics engine dispatches identically regardless of backend.
 *
 * Backends:
 *  - Linux (deployment): v4l2loopback (/dev/videoX) and SocketCAN (vcan0),
 *    compiled only under __linux__.
 *  - Windows (this dev box): a named shared-memory frame ring (cross-process,
 *    zero-copy, what an external reader opens) for the camera, and a UDP
 *    localhost transport carrying the real SocketCAN can_frame byte layout for
 *    CAN. These prove the engine dispatches correctly-formatted payloads and
 *    are what LOOPBACK_FRAME_INTEGRITY verifies here.
 */
namespace krs::hil {

// --- Virtual camera ---------------------------------------------------------
class IVirtualCamera {
public:
    virtual ~IVirtualCamera() = default;
    virtual bool open(int width, int height) = 0;
    virtual bool writeFrame(const uint8_t* rgba, size_t bytes, uint64_t frameId) = 0; // zero-copy publish
    virtual void close() = 0;
    virtual const char* backendName() const = 0;
};

// Factory: returns the v4l2loopback backend on Linux, the shared-memory ring
// backend on Windows. `deviceOrName` is /dev/videoN (Linux) or a shm name (Win).
std::unique_ptr<IVirtualCamera> makeVirtualCamera(const std::string& deviceOrName);

// --- Virtual CAN bus --------------------------------------------------------
// Byte-compatible with the Linux SocketCAN `struct can_frame` (16 bytes): so the
// same wire bytes go to vcan0 on Linux or the UDP transport on Windows.
#pragma pack(push, 1)
struct CanFrame {
    uint32_t can_id = 0;     // CAN identifier (+ EFF/RTR/ERR flags, as SocketCAN)
    uint8_t  len = 0;        // data length code (0..8)  [can_dlc]
    uint8_t  pad = 0;
    uint8_t  res0 = 0;
    uint8_t  res1 = 0;
    uint8_t  data[8] = { 0 };
};
#pragma pack(pop)
static_assert(sizeof(CanFrame) == 16, "CanFrame must match SocketCAN can_frame layout");

// --- CANopen-style telemetry codec ----------------------------------------
// Maps actuator efforts/state onto can_frame payloads (int16, fixed scale).
// Command IDs are 0x200+axis (host->sim); state IDs are 0x180/0x1C0/0x140+axis
// (sim->host) for position / velocity / applied-effort. Mirrors a PDO mapping.
namespace cancodec {
constexpr uint32_t kCmdBase = 0x200; // effort command  (N, scale 100)
constexpr uint32_t kPosBase = 0x180; // encoder position (m, scale 1000 -> mm)
constexpr uint32_t kVelBase = 0x1C0; // velocity         (m/s, scale 1000)
constexpr uint32_t kTrqBase = 0x140; // applied effort   (N, scale 100)

CanFrame encodeEffort(int axis, const float f[3]);   // host -> sim
bool     decodeEffort(const CanFrame& fr, int& axis, float f[3]);
CanFrame encodePose(int axis, const float p[3]);     // sim -> host
bool     decodePose(const CanFrame& fr, int& axis, float p[3]);
CanFrame encodeVel(int axis, const float v[3]);
CanFrame encodeTorque(int axis, const float t[3]);
} // namespace cancodec

class IVirtualCAN {
public:
    virtual ~IVirtualCAN() = default;
    virtual bool open(const std::string& iface) = 0;
    virtual bool send(const CanFrame& f) = 0;          // physics -> bus (torques/encoders)
    virtual bool recv(CanFrame& out) = 0;              // bus -> physics (commands); non-blocking
    virtual void close() = 0;
    virtual const char* backendName() const = 0;
};

// Linux: SocketCAN on `iface` (e.g. "vcan0"). Windows: UDP localhost transport;
// `iface` encodes "txPort:rxPort" so two endpoints form a bidirectional loop.
std::unique_ptr<IVirtualCAN> makeVirtualCAN();

// --- Verification module (Task 3) ------------------------------------------
// LOOPBACK_FRAME_INTEGRITY: stream a synthetic 1080p calibration pattern through
// the camera bridge and read it back on a detached thread; require zero frame
// drops and bit-exact color preservation. Window defaults to 5 s (set
// KRS_HIL_LOOPBACK_SECS, e.g. 60, for the full spec window). Also runs a
// bidirectional CAN round-trip check. Logs and returns pass/fail.
bool runBridgeSelfTest();

// CAN_PLANT module: command -> effort -> integrate a mock 1-DOF plant ->
// encoder -> command round-trip through the bridge, verifying the codec and the
// "apply as force / read back state" data path the SimulationController uses.
bool runCanPlantSelfTest();

} // namespace krs::hil
