#include "TelemetryProtocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace krs::ktp {

// ============================================================================
// COBS framing
// ============================================================================
// Standard COBS: emit a "code" byte = 1 + the count of following non-zero bytes,
// then those bytes. A zero in the input terminates a group (the code equals the
// distance to it). A full group of 254 data bytes with no zero uses code 0xFF
// and does NOT consume a zero, so a >254 non-zero run splits into multiple groups.
std::vector<uint8_t> cobsEncode(const std::vector<uint8_t>& in)
{
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 254 + 2);
    size_t codeIdx = out.size();
    out.push_back(0);            // placeholder for the current group's code byte
    uint8_t code = 1;            // 1 + number of non-zero bytes seen in this group

    for (uint8_t b : in) {
        if (b != 0) {
            out.push_back(b);
            ++code;
            if (code == 0xFF) {  // group full (254 data bytes) -> close it, open next
                out[codeIdx] = code;
                codeIdx = out.size();
                out.push_back(0);
                code = 1;
            }
        } else {                 // zero terminates the group; code = distance to it
            out[codeIdx] = code;
            codeIdx = out.size();
            out.push_back(0);
            code = 1;
        }
    }
    out[codeIdx] = code;         // finalize the trailing group
    return out;                  // NOTE: no 0x00 delimiter appended (framer owns it)
}

bool cobsDecode(const std::vector<uint8_t>& frame, std::vector<uint8_t>& out)
{
    out.clear();
    if (frame.empty()) return false;                 // a COBS frame is >= 1 byte
    size_t i = 0;
    while (i < frame.size()) {
        const uint8_t code = frame[i++];
        if (code == 0) return false;                 // 0x00 cannot appear inside a frame
        const size_t n = size_t(code) - 1;           // non-zero data bytes to copy
        if (i + n > frame.size()) return false;      // code overruns the buffer -> corrupt
        for (size_t k = 0; k < n; ++k) {
            const uint8_t b = frame[i++];
            if (b == 0) return false;                // a stuffed frame has NO interior zero data bytes either
            out.push_back(b);
        }
        // A non-full group (code < 0xFF) implies a zero separator UNLESS it was the
        // final group. Emit the implied zero only when more data follows.
        if (code != 0xFF && i < frame.size()) out.push_back(0);
    }
    return true;
}

// ============================================================================
// FrameReader (streaming resync)
// ============================================================================
void FrameReader::push(uint8_t b)
{
    if (b == 0) {
        // Delimiter: try to decode whatever accumulated. Garbage (from a dropped
        // byte) fails cobsDecode and is dropped -> we resync from here. An empty
        // accumulator (back-to-back delimiters) is a no-op, not an empty frame.
        if (!m_acc.empty()) {
            std::vector<uint8_t> decoded;
            if (cobsDecode(m_acc, decoded)) m_ready.push_back(std::move(decoded));
            m_acc.clear();
        }
    } else {
        m_acc.push_back(b);
    }
}

bool FrameReader::next(std::vector<uint8_t>& frame)
{
    if (m_readIdx >= m_ready.size()) {
        // All buffered frames consumed: reset the queue so it does not grow forever.
        if (!m_ready.empty()) { m_ready.clear(); m_readIdx = 0; }
        return false;
    }
    frame = std::move(m_ready[m_readIdx++]);
    return true;
}

std::vector<uint8_t> frameEncode(const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> stream = cobsEncode(payload);
    stream.push_back(0);   // zero delimiter marks end-of-frame on the wire
    return stream;
}

// ============================================================================
// Binary telemetry packet (little-endian, self-describing)
// ============================================================================
namespace {
void putU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x & 0xFF));
    v.push_back(uint8_t((x >> 8) & 0xFF));
}
void putU64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(uint8_t((x >> (8 * i)) & 0xFF));
}
void putF32(std::vector<uint8_t>& v, float f) {
    uint32_t u; std::memcpy(&u, &f, 4);              // bit-cast, IEEE-754 on the wire
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t((u >> (8 * i)) & 0xFF));
}
bool getU16(const std::vector<uint8_t>& v, size_t& p, uint16_t& x) {
    if (p + 2 > v.size()) return false;
    x = uint16_t(v[p]) | (uint16_t(v[p + 1]) << 8); p += 2; return true;
}
bool getU64(const std::vector<uint8_t>& v, size_t& p, uint64_t& x) {
    if (p + 8 > v.size()) return false;
    x = 0; for (int i = 0; i < 8; ++i) x |= uint64_t(v[p + i]) << (8 * i); p += 8; return true;
}
bool getF32(const std::vector<uint8_t>& v, size_t& p, float& f) {
    if (p + 4 > v.size()) return false;
    uint32_t u = 0; for (int i = 0; i < 4; ++i) u |= uint32_t(v[p + i]) << (8 * i);
    std::memcpy(&f, &u, 4); p += 4; return true;
}
} // namespace

std::vector<uint8_t> encodeSample(const Sample& s)
{
    std::vector<uint8_t> v;
    v.reserve(2 + 8 + 2 + s.values.size() * 4);
    putU16(v, s.channelId);
    putU64(v, s.deviceMicros);
    putU16(v, uint16_t(s.values.size()));            // self-describing float count
    for (float f : s.values) putF32(v, f);
    return v;
}

bool decodeSample(const std::vector<uint8_t>& buf, Sample& out)
{
    size_t p = 0;
    uint16_t count = 0;
    if (!getU16(buf, p, out.channelId)) return false;
    if (!getU64(buf, p, out.deviceMicros)) return false;
    if (!getU16(buf, p, count)) return false;
    if (p + size_t(count) * 4 != buf.size()) return false;  // exact fit -> no trailing junk
    out.values.clear();
    out.values.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        float f;
        if (!getF32(buf, p, f)) return false;
        out.values.push_back(f);
    }
    return true;
}

// ============================================================================
// Capability descriptor (JSON, "ktp/1")
// ============================================================================
std::string computeCapabilityHash(const std::vector<Channel>& channels)
{
    // FNV-1a 64-bit over a canonical (order-independent) serialization: sort each
    // channel's field string, then XOR-fold per-channel digests so reordering the
    // list does not change the hash. Any field edit / add / remove flips it.
    auto fnv1a = [](const std::string& s) -> uint64_t {
        uint64_t h = 1469598103934665603ULL;         // FNV offset basis
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }  // FNV prime
        return h;
    };
    uint64_t acc = 0;
    for (const auto& c : channels) {
        std::string rec = std::to_string(c.id) + '\x1f' + c.name + '\x1f'
                        + c.unit + '\x1f' + c.type;
        acc ^= fnv1a(rec);                            // XOR-fold: commutative in channel order
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(acc));
    return std::string(buf);
}

QJsonObject Descriptor::toJson() const
{
    QJsonArray arr;
    for (const auto& c : channels) {
        QJsonObject co;
        co["id"]   = int(c.id);
        co["name"] = QString::fromStdString(c.name);
        co["unit"] = QString::fromStdString(c.unit);
        co["type"] = QString::fromStdString(c.type);
        arr.append(co);
    }
    QJsonObject o;
    o["format"]         = QStringLiteral("ktp/1");
    o["boardId"]        = QString::fromStdString(boardId);
    // Recompute so the emitted hash always matches the emitted channel set.
    o["capabilityHash"] = QString::fromStdString(computeCapabilityHash(channels));
    o["channels"]       = arr;
    return o;
}

bool Descriptor::fromJson(const QJsonObject& o, Descriptor& out)
{
    if (o.value("format").toString() != QStringLiteral("ktp/1")) return false;  // versioned
    if (!o.contains("boardId") || !o.value("channels").isArray()) return false;
    out.boardId = o.value("boardId").toString().toStdString();
    out.channels.clear();
    for (const QJsonValue& v : o.value("channels").toArray()) {
        if (!v.isObject()) return false;
        const QJsonObject co = v.toObject();
        Channel c;
        c.id   = uint16_t(co.value("id").toInt());
        c.name = co.value("name").toString().toStdString();
        c.unit = co.value("unit").toString().toStdString();
        c.type = co.value("type").toString().toStdString();
        out.channels.push_back(c);
    }
    // Prefer the transmitted hash, but fall back to recomputation if absent.
    out.capabilityHash = o.contains("capabilityHash")
                       ? o.value("capabilityHash").toString().toStdString()
                       : computeCapabilityHash(out.channels);
    return true;
}

// ============================================================================
// Host clock-sync estimator (online least-squares)
// ============================================================================
void ClockSync::observe(uint64_t deviceMicros, double hostSeconds)
{
    const double x = double(deviceMicros);
    if (m_n == 0) m_x0 = x;                           // center on first sample
    const double xc = x - m_x0;                       // mean-centered -> well-conditioned
    m_sx  += xc;
    m_sy  += hostSeconds;
    m_sxx += xc * xc;
    m_sxy += xc * hostSeconds;
    ++m_n;
}

double ClockSync::skew() const
{
    if (m_n < 2) return 0.0;
    const double n = double(m_n);
    const double denom = n * m_sxx - m_sx * m_sx;     // >0 once device times differ
    if (std::abs(denom) < 1e-30) return 0.0;
    return (n * m_sxy - m_sx * m_sy) / denom;         // slope a (host s / device us)
}

double ClockSync::offset() const
{
    if (m_n < 1) return 0.0;
    const double n = double(m_n);
    const double a = skew();
    // Intercept in centered coords, then shift back by a*m_x0 to un-center.
    const double bc = (m_sy - a * m_sx) / n;          // b at x=m_x0
    return bc - a * m_x0;                             // b at x=0 (host s)
}

double ClockSync::toHost(uint64_t deviceMicros) const
{
    if (m_n == 0) return 0.0;
    if (m_n == 1) return m_sy;                        // single point: best guess is its host time
    return skew() * double(deviceMicros) + offset();
}

// ============================================================================
// GATE TELEMETRY
// ============================================================================
bool runTelemetryGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[ktp] GATE TELEMETRY -- host-side wire codec: COBS framing, resync, sample codec, JSON handshake, clock-sync\n");
    bool pass = true;

    // (1) COBS round-trip including zero bytes and a >254 non-zero run.
    {
        std::vector<uint8_t> data;
        data.push_back(0x00);                         // leading zero
        for (int i = 0; i < 300; ++i) data.push_back(uint8_t(1 + (i % 200)));  // >254 non-zero run
        data.push_back(0x00);                         // interior zero
        data.push_back(0x00);                         // adjacent zeros
        data.push_back(0x42);
        std::vector<uint8_t> enc = cobsEncode(data);
        const bool noZeros = std::find(enc.begin(), enc.end(), uint8_t(0)) == enc.end();
        std::vector<uint8_t> dec;
        const bool rt = cobsDecode(enc, dec) && dec == data;
        const bool ok = noZeros && rt;
        printf("[ktp]   COBS round-trip (zeros + >254 run): stuffed-has-no-zero=%s roundtrip=%s  %s\n",
               noZeros ? "yes" : "NO", rt ? "yes" : "NO", ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // (2) FrameReader resynchronizes across a garbage byte between frames and still
    // recovers BOTH real frames. A dropped/injected byte only survives until the
    // next 0x00 delimiter, where the corrupt segment fails cobsDecode and is dropped;
    // the next delimited frame decodes cleanly (this is COBS's whole point).
    {
        std::vector<uint8_t> pa = { 10, 0, 20, 0, 0, 30 };     // payload A (has zeros)
        std::vector<uint8_t> pb = { 99, 98, 0, 1 };            // payload B
        FrameReader fr;
        for (uint8_t b : frameEncode(pa)) fr.push(b);          // [frameA + delim]
        fr.push(0x7E); fr.push(0x00);                          // [garbage byte] isolated by its delimiter -> dropped
        for (uint8_t b : frameEncode(pb)) fr.push(b);          // [frameB + delim]

        std::vector<uint8_t> f1, f2, f3;
        const bool got1 = fr.next(f1);                         // frameA recovered
        const bool got2 = fr.next(f2);                         // frameB recovered (post-resync)
        const bool got3 = fr.next(f3);                         // garbage produced NO frame
        const bool ok = got1 && f1 == pa && got2 && f2 == pb && !got3;
        printf("[ktp]   FrameReader resync [A][garbage][B]: gotA=%s A-ok=%s gotB=%s B-ok=%s garbage-dropped=%s  %s\n",
               got1 ? "y" : "n", (got1 && f1 == pa) ? "y" : "n",
               got2 ? "y" : "n", (got2 && f2 == pb) ? "y" : "n", !got3 ? "y" : "n",
               ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // (3) Sample encode/decode round-trip (channelId / timestamp / floats exact).
    {
        Sample s; s.channelId = 0xBEEF; s.deviceMicros = 0x0123456789ABCDEFull;
        s.values = { 0.0f, -1.5f, 3.14159f, 1e-9f, -2.5e7f };
        std::vector<uint8_t> enc = encodeSample(s);
        Sample d;
        const bool rt = decodeSample(enc, d);
        bool exact = rt && d.channelId == s.channelId && d.deviceMicros == s.deviceMicros
                        && d.values.size() == s.values.size();
        for (size_t i = 0; exact && i < s.values.size(); ++i)
            exact = std::memcmp(&d.values[i], &s.values[i], 4) == 0;  // bit-exact floats
        printf("[ktp]   Sample codec: id=0x%04X ts-match=%s vals=%zu bit-exact=%s  %s\n",
               unsigned(d.channelId), d.deviceMicros == s.deviceMicros ? "y" : "n",
               d.values.size(), exact ? "yes" : "NO", exact ? "PASS" : "FAIL");
        pass &= exact;
    }

    // (4) Descriptor toJson/fromJson round-trip + capabilityHash changes with the set.
    {
        Descriptor desc;
        desc.boardId = "board-uuid-0001";
        desc.channels = {
            { 1, "motor_current", "A",   "f32" },
            { 2, "winding_temp",  "degC", "f32" },
        };
        desc.capabilityHash = computeCapabilityHash(desc.channels);

        QJsonObject o = desc.toJson();
        // Serialize through QJsonDocument to prove it survives real JSON text.
        const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
        QJsonParseError perr{};
        const QJsonObject reparsed = QJsonDocument::fromJson(bytes, &perr).object();
        Descriptor back;
        const bool parsed = perr.error == QJsonParseError::NoError && Descriptor::fromJson(reparsed, back);
        const bool rt = parsed && back.boardId == desc.boardId
                              && back.channels.size() == desc.channels.size()
                              && back.capabilityHash == desc.capabilityHash;

        // Hash sensitivity: reorder = SAME hash; add a channel = DIFFERENT hash.
        std::vector<Channel> reordered = { desc.channels[1], desc.channels[0] };
        std::vector<Channel> added = desc.channels; added.push_back({ 3, "torque", "Nm", "f32" });
        const std::string hBase  = computeCapabilityHash(desc.channels);
        const std::string hReord = computeCapabilityHash(reordered);
        const std::string hAdded = computeCapabilityHash(added);
        const bool hashOk = (hReord == hBase) && (hAdded != hBase);

        const bool ok = rt && hashOk;
        printf("[ktp]   Descriptor JSON round-trip=%s; hash reorder-stable=%s add-changes=%s  %s\n",
               rt ? "yes" : "NO", (hReord == hBase) ? "y" : "n", (hAdded != hBase) ? "y" : "n",
               ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // (5) ClockSync recovers a known (a,b) from noisy (deviceMicros, host) samples.
    {
        // Ground truth: host = A*deviceUs + B. A ~ 1e-6 (device us -> host s), a
        // small skew added; B a large offset. Inject bounded deterministic noise.
        const double A = 1.0e-6 * 1.000200;           // ~200 ppm skew
        const double B = 12345.678;                   // host-time offset (s)
        ClockSync cs;
        uint32_t rng = 0x1234567u;                    // xorshift, no <random> needed
        double maxRes = 0.0;
        for (int i = 0; i < 500; ++i) {
            const uint64_t devUs = 1000000ull + uint64_t(i) * 1000ull;   // 1 kHz stream
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const double noise = (double(rng) / double(UINT32_MAX) - 0.5) * 2.0e-4;  // +-100us jitter
            cs.observe(devUs, A * double(devUs) + B + noise);
        }
        // Recovery: check the fitted line predicts a fresh (noise-free) device time.
        const uint64_t probeUs = 1000000ull + 250000ull;   // mid-range device stamp
        const double truth = A * double(probeUs) + B;
        const double est   = cs.toHost(probeUs);
        const double err   = std::abs(est - truth);
        const double aErr  = std::abs(cs.skew() - A) / A;   // relative skew error
        const bool ok = cs.ready() && err < 5.0e-5 && aErr < 1.0e-2;  // sub-50us, <1% skew
        printf("[ktp]   ClockSync recover (a=%.9e,b=%.3f): a-relerr=%.2e predict-err=%.2es  %s\n",
               A, B, aErr, err, ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    // NEG-CTRL: cobsDecode of a corrupt frame returns false, no crash (not vacuous:
    // a valid frame decodes true, an interior 0x00 / truncated code decodes false).
    {
        std::vector<uint8_t> valid = cobsEncode({ 7, 8, 9 });
        std::vector<uint8_t> tmp;
        const bool goodTrue = cobsDecode(valid, tmp);
        const bool interiorZero = !cobsDecode({ 0x03, 0x11, 0x00 }, tmp);   // 0x00 illegal inside a frame
        const bool overrunCode  = !cobsDecode({ 0x05, 0x01 }, tmp);         // code claims 4 bytes, only 1 present
        const bool emptyFalse   = !cobsDecode({}, tmp);                     // empty is not a frame
        const bool ok = goodTrue && interiorZero && overrunCode && emptyFalse;
        printf("[ktp]   NEG-CTRL corrupt cobsDecode: valid=true(%s) interior0=false(%s) overrun=false(%s) empty=false(%s)  %s\n",
               goodTrue ? "y" : "n", interiorZero ? "y" : "n", overrunCode ? "y" : "n", emptyFalse ? "y" : "n",
               ok ? "PASS" : "FAIL");
        pass &= ok;
    }

    printf("[ktp] %s\n", pass ? "ALL PASS (COBS+resync framing, self-describing sample codec, ktp/1 JSON handshake, clock-sync recovery)"
                              : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::ktp
