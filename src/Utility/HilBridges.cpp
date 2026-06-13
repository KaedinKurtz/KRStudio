#include "HilBridges.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  ifdef __linux__
#    include <linux/can.h>
#    include <linux/can/raw.h>
#    include <linux/videodev2.h>
#    include <net/if.h>
#    include <sys/ioctl.h>
#  endif
#endif

namespace krs::hil {

static int envInt(const char* k, int def) {
#ifdef _WIN32
    char* v = nullptr; size_t n = 0;
    if (_dupenv_s(&v, &n, k) == 0 && v) { int r = std::atoi(v); free(v); return r; }
    return def;
#else
    const char* v = std::getenv(k); return v ? std::atoi(v) : def;
#endif
}

// ===========================================================================
// Cross-process shared-memory frame ring. A named OS mapping holds a header
// plus N frame slots; the producer (camera) overwrites the next slot and bumps
// a sequence counter, an external reader maps the same name and reads frames.
// This is the zero-copy host hand-off an external perception stack opens.
// ===========================================================================
struct ShmHeader {
    uint32_t magic, width, height, bpp, slotCount, slotBytes, pad0, pad1;
    std::atomic<uint64_t> seq;          // total frames published (8-byte aligned)
};
static constexpr uint32_t kShmMagic = 0x4B525343; // 'KRSC'
static constexpr size_t kDataOffset = 64;

class SharedFrameRing {
public:
    bool create(const std::string& name, int w, int h, int slots) {
        m_slotBytes = size_t(w) * h * 4;
        m_size = kDataOffset + m_slotBytes * slots;
        if (!mapMem(name, true)) return false;
        m_hdr->magic = kShmMagic; m_hdr->width = w; m_hdr->height = h; m_hdr->bpp = 4;
        m_hdr->slotCount = slots; m_hdr->slotBytes = uint32_t(m_slotBytes);
        m_hdr->seq.store(0, std::memory_order_release);
        return true;
    }
    bool openExisting(const std::string& name) {
        // map header first to learn the geometry, then size the full region
        m_size = kDataOffset;
        if (!mapMem(name, false)) return false;
        int slots = m_hdr->slotCount; m_slotBytes = m_hdr->slotBytes;
        m_size = kDataOffset + m_slotBytes * slots;
        unmap();
        return mapMem(name, false);
    }
    void publish(const uint8_t* rgba) {                     // producer
        uint64_t s = m_hdr->seq.load(std::memory_order_relaxed);
        std::memcpy(slot(s % m_hdr->slotCount), rgba, m_slotBytes);
        m_hdr->seq.store(s + 1, std::memory_order_release);  // publish
    }
    uint64_t seq() const { return m_hdr->seq.load(std::memory_order_acquire); }
    int slotCount() const { return m_hdr->slotCount; }
    size_t slotBytes() const { return m_slotBytes; }
    // Copy frame `id` if still resident; returns false if it was overwritten (drop).
    bool read(uint64_t id, uint8_t* out) const {
        std::memcpy(out, slot(id % m_hdr->slotCount), m_slotBytes);
        uint64_t after = m_hdr->seq.load(std::memory_order_acquire);
        return (after - id) <= uint64_t(m_hdr->slotCount); // slot not lapped during copy
    }
    void destroy() { unmap();
#ifdef _WIN32
        if (m_h) { CloseHandle(m_h); m_h = nullptr; }
#else
        if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
        if (m_creator && !m_name.empty()) shm_unlink(m_name.c_str());
#endif
    }
    ~SharedFrameRing() { destroy(); }
private:
    uint8_t* slot(uint64_t i) const { return (uint8_t*)m_base + kDataOffset + i * m_slotBytes; }
    bool mapMem(const std::string& name, bool create) {
        m_name = name; m_creator = create;
#ifdef _WIN32
        std::string n = "Local\\" + name;
        if (create) m_h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                             (DWORD)(m_size >> 32), (DWORD)(m_size & 0xffffffff), n.c_str());
        else        m_h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, n.c_str());
        if (!m_h) return false;
        m_base = MapViewOfFile(m_h, FILE_MAP_ALL_ACCESS, 0, 0, m_size);
        if (!m_base) return false;
#else
        m_fd = shm_open(name.c_str(), create ? (O_CREAT | O_RDWR) : O_RDWR, 0666);
        if (m_fd < 0) return false;
        if (create && ftruncate(m_fd, (off_t)m_size) != 0) return false;
        m_base = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (m_base == MAP_FAILED) { m_base = nullptr; return false; }
#endif
        m_hdr = reinterpret_cast<ShmHeader*>(m_base);
        return true;
    }
    void unmap() {
        if (!m_base) return;
#ifdef _WIN32
        UnmapViewOfFile(m_base);
#else
        munmap(m_base, m_size);
#endif
        m_base = nullptr; m_hdr = nullptr;
    }
    std::string m_name; bool m_creator = false;
    void* m_base = nullptr; ShmHeader* m_hdr = nullptr; size_t m_size = 0, m_slotBytes = 0;
#ifdef _WIN32
    HANDLE m_h = nullptr;
#else
    int m_fd = -1;
#endif
};

// --- Camera: shared-memory backend (Windows + portable) ---------------------
class SharedMemCamera : public IVirtualCamera {
public:
    explicit SharedMemCamera(std::string name) : m_name(std::move(name)) {}
    bool open(int w, int h) override { return m_ring.create(m_name, w, h, 8); } // 8-slot ring
    bool writeFrame(const uint8_t* rgba, size_t bytes, uint64_t) override {
        if (bytes != m_ring.slotBytes()) return false;
        m_ring.publish(rgba); return true;
    }
    void close() override { m_ring.destroy(); }
    const char* backendName() const override { return "shared-memory ring"; }
    SharedFrameRing& ring() { return m_ring; }
    const std::string& name() const { return m_name; }
private:
    std::string m_name; SharedFrameRing m_ring;
};

#ifdef __linux__
// --- Camera: v4l2loopback backend (Linux deployment) ------------------------
class V4l2Camera : public IVirtualCamera {
public:
    explicit V4l2Camera(std::string dev) : m_dev(std::move(dev)) {}
    bool open(int w, int h) override {
        m_fd = ::open(m_dev.c_str(), O_RDWR);
        if (m_fd < 0) return false;
        v4l2_format fmt{}; fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        fmt.fmt.pix.width = w; fmt.fmt.pix.height = h;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGBA32; fmt.fmt.pix.field = V4L2_FIELD_NONE;
        fmt.fmt.pix.bytesperline = w * 4; fmt.fmt.pix.sizeimage = w * h * 4;
        if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) return false;
        m_bytes = size_t(w) * h * 4; return true;
    }
    bool writeFrame(const uint8_t* rgba, size_t bytes, uint64_t) override {
        return ::write(m_fd, rgba, bytes) == (ssize_t)bytes; // stream into /dev/videoX
    }
    void close() override { if (m_fd >= 0) { ::close(m_fd); m_fd = -1; } }
    const char* backendName() const override { return "v4l2loopback"; }
private:
    std::string m_dev; int m_fd = -1; size_t m_bytes = 0;
};
#endif

std::unique_ptr<IVirtualCamera> makeVirtualCamera(const std::string& deviceOrName) {
#ifdef __linux__
    if (deviceOrName.rfind("/dev/", 0) == 0) return std::make_unique<V4l2Camera>(deviceOrName);
#endif
    return std::make_unique<SharedMemCamera>(deviceOrName);
}

// --- CAN: UDP-localhost backend carrying the SocketCAN can_frame bytes -------
class UdpCanBus : public IVirtualCAN {
public:
    bool open(const std::string& iface) override {
        int tx = 0, rx = 0; auto c = iface.find(':');
        if (c == std::string::npos) return false;
        tx = std::atoi(iface.substr(0, c).c_str()); rx = std::atoi(iface.substr(c + 1).c_str());
#ifdef _WIN32
        WSADATA wsa; static bool inited = false; if (!inited) { WSAStartup(MAKEWORD(2, 2), &wsa); inited = true; }
#endif
        m_sock = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock < 0) return false;
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons((u_short)rx);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
        m_peer.sin_family = AF_INET; m_peer.sin_port = htons((u_short)tx);
        m_peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
        u_long nb = 1; ioctlsocket(m_sock, FIONBIO, &nb);
#else
        fcntl(m_sock, F_SETFL, O_NONBLOCK);
#endif
        return true;
    }
    bool send(const CanFrame& f) override {                 // physics -> bus
        return sendto(m_sock, (const char*)&f, sizeof(f), 0, (sockaddr*)&m_peer, sizeof(m_peer)) == sizeof(f);
    }
    bool recv(CanFrame& out) override {                     // bus -> physics (non-blocking)
        int n = ::recvfrom(m_sock, (char*)&out, sizeof(out), 0, nullptr, nullptr);
        return n == (int)sizeof(out);
    }
    void close() override {
        if (m_sock >= 0) {
#ifdef _WIN32
            closesocket(m_sock);
#else
            ::close(m_sock);
#endif
            m_sock = -1;
        }
    }
    const char* backendName() const override { return "UDP localhost (can_frame)"; }
private:
    int m_sock = -1; sockaddr_in m_peer{};
};

#ifdef __linux__
// --- CAN: SocketCAN backend (Linux deployment, vcan0) -----------------------
class SocketCanBus : public IVirtualCAN {
public:
    bool open(const std::string& iface) override {
        m_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (m_sock < 0) return false;
        ifreq ifr{}; std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
        if (ioctl(m_sock, SIOCGIFINDEX, &ifr) < 0) return false;
        sockaddr_can addr{}; addr.can_family = AF_CAN; addr.can_ifindex = ifr.ifr_ifindex;
        if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        fcntl(m_sock, F_SETFL, O_NONBLOCK);
        return true;
    }
    bool send(const CanFrame& f) override {
        can_frame cf{}; cf.can_id = f.can_id; cf.can_dlc = f.len; std::memcpy(cf.data, f.data, 8);
        return ::write(m_sock, &cf, sizeof(cf)) == (ssize_t)sizeof(cf);
    }
    bool recv(CanFrame& out) override {
        can_frame cf{}; if (::read(m_sock, &cf, sizeof(cf)) != (ssize_t)sizeof(cf)) return false;
        out.can_id = cf.can_id; out.len = cf.can_dlc; std::memcpy(out.data, cf.data, 8); return true;
    }
    void close() override { if (m_sock >= 0) { ::close(m_sock); m_sock = -1; } }
    const char* backendName() const override { return "SocketCAN vcan"; }
private:
    int m_sock = -1;
};
#endif

std::unique_ptr<IVirtualCAN> makeVirtualCAN() {
#ifdef __linux__
    return std::make_unique<SocketCanBus>();
#else
    return std::make_unique<UdpCanBus>();
#endif
}

// ===========================================================================
// CANopen-style telemetry codec — pack/unpack int16 channels into can_frame.
// ===========================================================================
namespace cancodec {
static inline void put16(uint8_t* d, int i, float v, float scale) {
    int32_t q = (int32_t)std::lround(v * scale);
    q = q < -32768 ? -32768 : (q > 32767 ? 32767 : q);  // saturate to int16
    d[i] = uint8_t(q & 0xff); d[i + 1] = uint8_t((q >> 8) & 0xff);
}
static inline float get16(const uint8_t* d, int i, float scale) {
    int16_t q = int16_t(uint16_t(d[i]) | (uint16_t(d[i + 1]) << 8));
    return float(q) / scale;
}
static CanFrame pack3(uint32_t id, const float v[3], float scale) {
    CanFrame f; f.can_id = id; f.len = 6;
    put16(f.data, 0, v[0], scale); put16(f.data, 2, v[1], scale); put16(f.data, 4, v[2], scale);
    return f;
}
CanFrame encodeEffort(int axis, const float f[3]) { return pack3(kCmdBase + axis, f, 100.0f); }
CanFrame encodePose(int axis, const float p[3])   { return pack3(kPosBase + axis, p, 1000.0f); }
CanFrame encodeVel(int axis, const float v[3])    { return pack3(kVelBase + axis, v, 1000.0f); }
CanFrame encodeTorque(int axis, const float t[3]) { return pack3(kTrqBase + axis, t, 100.0f); }
bool decodeEffort(const CanFrame& fr, int& axis, float f[3]) {
    if (fr.can_id < kCmdBase || fr.can_id >= kCmdBase + 0x40) return false;
    axis = int(fr.can_id - kCmdBase);
    f[0] = get16(fr.data, 0, 100.0f); f[1] = get16(fr.data, 2, 100.0f); f[2] = get16(fr.data, 4, 100.0f);
    return true;
}
bool decodePose(const CanFrame& fr, int& axis, float p[3]) {
    if (fr.can_id < kPosBase || fr.can_id >= kPosBase + 0x40) return false;
    axis = int(fr.can_id - kPosBase);
    p[0] = get16(fr.data, 0, 1000.0f); p[1] = get16(fr.data, 2, 1000.0f); p[2] = get16(fr.data, 4, 1000.0f);
    return true;
}
} // namespace cancodec

// ===========================================================================
// LOOPBACK_FRAME_INTEGRITY + bidirectional CAN round-trip (Task 3 module).
// ===========================================================================
// Deterministic 1080p calibration pattern -> exercises every byte lane.
static inline void fillPattern(std::vector<uint8_t>& buf, int w, int h, uint64_t f) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* px = &buf[(size_t(y) * w + x) * 4];
            px[0] = uint8_t(x ^ (f * 7)); px[1] = uint8_t(y ^ (f * 13));
            px[2] = uint8_t((x + y) ^ (f * 29)); px[3] = 255;
        }
}

static bool loopbackFrameIntegrity() {
    const int W = 1920, H = 1080;
    const int secs = std::max(1, envInt("KRS_HIL_LOOPBACK_SECS", 5)); // full spec window = 60
    const int fps = 30;
    const int frames = secs * fps;
    const std::string name = "krs_hil_cam";
    SharedMemCamera cam(name);
    if (!cam.open(W, H)) { std::fprintf(stderr, "[HIL] camera bridge open FAILED\n"); return false; }

    std::atomic<bool> run{ true };
    std::atomic<uint64_t> drops{ 0 }, mismatches{ 0 }, verified{ 0 };
    // Detached verification thread: opens the SAME named region and reads frames.
    std::thread reader([&]() {
        SharedFrameRing rr; if (!rr.openExisting(name)) { mismatches.store(1); return; }
        std::vector<uint8_t> got(rr.slotBytes()), expect(rr.slotBytes());
        uint64_t last = 0;
        while (run.load(std::memory_order_acquire) || last < cam.ring().seq()) {
            uint64_t cur = rr.seq();
            while (last < cur) {
                if (cur - last > uint64_t(rr.slotCount())) { drops.fetch_add(1); last++; continue; } // lapped
                if (!rr.read(last, got.data())) { drops.fetch_add(1); last++; continue; }
                fillPattern(expect, W, H, last);
                if (std::memcmp(got.data(), expect.data(), got.size()) != 0) mismatches.fetch_add(1);
                else verified.fetch_add(1);
                last++;
                cur = rr.seq();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::vector<uint8_t> frame(size_t(W) * H * 4);
    auto period = std::chrono::microseconds(1000000 / fps);
    auto next = std::chrono::steady_clock::now();
    for (uint64_t f = 0; f < uint64_t(frames); ++f) {       // produce at the sensor frame rate
        fillPattern(frame, W, H, f);
        cam.writeFrame(frame.data(), frame.size(), f);
        next += period;
        std::this_thread::sleep_until(next);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the reader drain
    run.store(false, std::memory_order_release);
    reader.join();
    cam.close();

    bool pass = (drops.load() == 0 && mismatches.load() == 0 && verified.load() == uint64_t(frames));
    std::fprintf(stderr,
        "[HIL] LOOPBACK_FRAME_INTEGRITY %s  1080p %ds @%dfps  produced=%d verified=%llu drops=%llu mismatch=%llu\n",
        pass ? "PASS" : "FAIL", secs, fps, frames, (unsigned long long)verified.load(),
        (unsigned long long)drops.load(), (unsigned long long)mismatches.load());
    std::fprintf(stderr, "[HIL]   (camera backend: %s; set KRS_HIL_LOOPBACK_SECS=60 for the full window)\n",
                 cam.backendName());
    return pass;
}

static bool canBidirectional() {
    auto A = makeVirtualCAN(), B = makeVirtualCAN();
#ifdef __linux__
    bool ok = A->open("vcan0") && B->open("vcan0");
#else
    bool ok = A->open("57101:57100") && B->open("57100:57101"); // crossed UDP ports
#endif
    if (!ok) { std::fprintf(stderr, "[HIL] CAN bridge open FAILED\n"); return false; }
    const int N = 64; int got = 0, bad = 0;
    for (int i = 0; i < N; ++i) {
        CanFrame cmd{}; cmd.can_id = 0x200 + i; cmd.len = 8;            // simulated torque command A->B
        for (int b = 0; b < 8; ++b) cmd.data[b] = uint8_t(i * 8 + b);
        A->send(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CanFrame rx{};
        if (B->recv(rx)) {                                              // physics-side reads next tick
            ++got;
            if (rx.can_id != cmd.can_id || rx.len != 8 || std::memcmp(rx.data, cmd.data, 8) != 0) ++bad;
            CanFrame enc{}; enc.can_id = 0x180 + i; enc.len = 8;        // echo encoder reading B->A
            std::memcpy(enc.data, rx.data, 8); B->send(enc);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            CanFrame back{}; if (A->recv(back) && (back.can_id != enc.can_id || std::memcmp(back.data, cmd.data, 8) != 0)) ++bad;
        }
    }
    A->close(); B->close();
    bool pass = (got == N && bad == 0);
    std::fprintf(stderr, "[HIL] CAN_LOOPBACK %s  backend=%s  sent=%d received=%d corrupt=%d\n",
                 pass ? "PASS" : "FAIL", A->backendName(), N, got, bad);
    return pass;
}

// Command -> effort -> integrate a mock 1-DOF plant -> encoder -> command,
// proving the codec + "apply as force / read back state" path the
// SimulationController uses, with no PhysX dependency.
bool runCanPlantSelfTest() {
    std::fprintf(stderr, "[HIL] === CAN_PLANT (command -> force -> encoder round-trip) ===\n");
    auto host = makeVirtualCAN(), plant = makeVirtualCAN();
#ifdef __linux__
    bool ok = host->open("vcan0") && plant->open("vcan0");
#else
    bool ok = host->open("57111:57110") && plant->open("57110:57111");
#endif
    if (!ok) { std::fprintf(stderr, "[HIL] CAN_PLANT open FAILED\n"); return false; }

    const int axis = 0; const float m = 1.0f, dt = 0.001f; const float fx = 8.0f;
    float cmd[3] = { fx, 0, 0 };
    host->send(cancodec::encodeEffort(axis, cmd));                 // host commands effort
    std::this_thread::sleep_for(std::chrono::milliseconds(3));

    float force[3] = { 0, 0, 0 }; int rxAxis = -1; bool got = false; CanFrame rx{};
    while (plant->recv(rx)) { int a; if (cancodec::decodeEffort(rx, a, force)) { rxAxis = a; got = true; } }
    float v[3] = { 0, 0, 0 }, x[3] = { 0, 0, 0 };                   // mock free-mass plant
    if (got) for (int s = 0; s < 100; ++s) for (int k = 0; k < 3; ++k) { v[k] += force[k] / m * dt; x[k] += v[k] * dt; }
    plant->send(cancodec::encodePose(axis, x));                    // plant reports encoder
    std::this_thread::sleep_for(std::chrono::milliseconds(3));

    float pose[3] = { 0, 0, 0 }; int pAxis = -1; bool gotPose = false; CanFrame pr{};
    while (host->recv(pr)) { int a; if (cancodec::decodePose(pr, a, pose)) { pAxis = a; gotPose = true; } }
    host->close(); plant->close();

    float vv = 0, expected = 0;                                    // analytic discrete integral
    for (int s = 0; s < 100; ++s) { vv += fx / m * dt; expected += vv * dt; }
    bool pass = got && gotPose && rxAxis == axis && pAxis == axis
              && force[0] == fx && pose[0] > 0.0f && std::fabs(pose[0] - expected) < 0.002f; // 1mm quantization
    std::fprintf(stderr, "[HIL] CAN_PLANT %s  cmdF=%.2fN decodedF=%.2fN encoderX=%.4fm expected=%.4fm\n",
                 pass ? "PASS" : "FAIL", fx, force[0], pose[0], expected);
    return pass;
}

bool runBridgeSelfTest() {
    std::fprintf(stderr, "[HIL] === LOOPBACK_FRAME_INTEGRITY + CAN ===\n");
    bool ok = true;
    ok &= loopbackFrameIntegrity();
    ok &= canBidirectional();
    ok &= runCanPlantSelfTest();
    std::fprintf(stderr, "[HIL] bridges overall: %s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok;
}

} // namespace krs::hil
