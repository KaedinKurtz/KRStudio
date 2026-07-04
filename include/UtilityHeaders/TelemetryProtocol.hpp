#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QJsonObject>   // capability descriptor handshake only (NOT streamed at rate)

/**
 * @brief KRStudio Telemetry Protocol (ktp) -- HOST-side Phase-1 wire codec for
 * TELEMETRY_PROTOCOL.md. Pure CPU, no hardware: everything a self-describing
 * USB-CDC telemetry node needs on the wire, verifiable against a loopback.
 *
 * Split transport (per the design doc):
 *  - Announce/config: JSON capability descriptor, versioned "ktp/1" (Descriptor).
 *  - Telemetry stream: compact BINARY samples (Sample), COBS-framed, zero-delimited.
 *
 * COBS gives mid-stream resync: a dropped/garbage byte only corrupts one frame;
 * the reader recovers on the next zero delimiter (FrameReader). Every sample
 * carries a device monotonic microsecond timestamp; ClockSync fits that device
 * clock onto the host timeline so downstream data shares one coherent timeline.
 */
namespace krs::ktp {

// --- COBS framing -----------------------------------------------------------
// Consistent Overhead Byte Stuffing: the encoded payload contains NO zero bytes,
// so a single 0x00 delimiter marks frame boundaries. `cobsEncode` returns the
// stuffed payload WITHOUT the trailing delimiter (the framer/reader owns that).
// `cobsDecode` takes a stuffed payload (also WITHOUT the delimiter) and recovers
// the original bytes; returns false on a structurally corrupt frame (never throws).
// Runs of >254 non-zero bytes are split across code groups, as the algorithm requires.
std::vector<uint8_t> cobsEncode(const std::vector<uint8_t>& in);
bool                 cobsDecode(const std::vector<uint8_t>& frame, std::vector<uint8_t>& out);

// --- Streaming resync decoder ----------------------------------------------
// Accumulates raw serial bytes and emits complete zero-delimited frames. On a
// dropped/garbage byte the accumulator holds junk until the next 0x00 delimiter,
// where it flushes (junk fails cobsDecode and is discarded) and resynchronizes.
struct FrameReader {
    // Feed one received byte.
    void push(uint8_t b);
    // Pop the next fully-decoded frame, if one is ready. Returns true and fills
    // `frame` with decoded (un-stuffed) payload bytes; false if none pending.
    // Corrupt/garbage segments between delimiters are silently dropped (resync).
    bool next(std::vector<uint8_t>& frame);

private:
    std::vector<uint8_t> m_acc;                 // bytes since the last delimiter
    std::vector<std::vector<uint8_t>> m_ready;  // decoded frames awaiting next()
    size_t m_readIdx = 0;                       // consume cursor into m_ready
};

// Convenience: COBS-encode a payload and append the 0x00 delimiter -> a byte
// stream a FrameReader on the far end can consume. (Encoder side of the pipe.)
std::vector<uint8_t> frameEncode(const std::vector<uint8_t>& payload);

// --- Binary telemetry packet -----------------------------------------------
// One sample-batch for a channel. NO field names on the wire (the JSON handshake
// mapped channelId -> name/unit/type); just id + device timestamp + typed floats.
struct Sample {
    uint16_t           channelId = 0;
    uint64_t           deviceMicros = 0;   // device monotonic microsecond counter
    std::vector<float> values;             // one or more floats per sample-batch
};

// Little-endian, self-describing length: [u16 channelId][u64 deviceMicros]
// [u16 count][f32 values...]. `decodeSample` returns false if the buffer is
// truncated or the declared count overruns it (never throws).
std::vector<uint8_t> encodeSample(const Sample& s);
bool                 decodeSample(const std::vector<uint8_t>& buf, Sample& out);

// --- Capability descriptor (JSON handshake, "ktp/1") -----------------------
struct Channel {
    uint16_t    id = 0;
    std::string name;
    std::string unit;
    std::string type;   // scalar type tag, e.g. "f32" / "u16" (declared, host-decoded)
};

struct Descriptor {
    std::string          boardId;         // stable board UUID (survives reflashing)
    std::string          capabilityHash;  // content hash of the channel set
    std::vector<Channel> channels;

    QJsonObject toJson() const;                       // adds "format":"ktp/1"
    static bool fromJson(const QJsonObject& o, Descriptor& out);  // false on bad/missing schema
};

// Content-addressed hash of the channel SET (id/name/unit/type). Order-independent
// so a re-announce with reordered channels is recognized as the SAME capability;
// adding/removing/renaming a channel changes the hash (recognize-as-different).
std::string computeCapabilityHash(const std::vector<Channel>& channels);

// --- Host clock-sync estimator ---------------------------------------------
// Online least-squares fit of hostSeconds ~= a*deviceMicros + b over observed
// (deviceMicros, hostArrivalSeconds) pairs. `a` recovers clock skew (host s per
// device us, ~1e-6), `b` the offset. Robust to arrival-time noise by regression
// over many samples. `toHost` maps any device timestamp onto the host timeline.
struct ClockSync {
    void   observe(uint64_t deviceMicros, double hostSeconds);
    double toHost(uint64_t deviceMicros) const;
    bool   ready() const { return m_n >= 2; }   // need >=2 points for a line
    double skew() const;                          // fitted a (host s / device us)
    double offset() const;                        // fitted b (host s)
    size_t count() const { return m_n; }

private:
    // Accumulate in double-precision, mean-centered on the first device timestamp
    // to keep sums well-conditioned (device us are large; naive Sxx overflows FP).
    size_t m_n = 0;
    double m_x0 = 0.0;               // reference device time (us), set on first obs
    double m_sx = 0.0, m_sy = 0.0;   // sum of centered x, sum of y
    double m_sxx = 0.0, m_sxy = 0.0; // sum x^2, sum x*y (centered)
};

// --- Verification gate ------------------------------------------------------
// GATE TELEMETRY: COBS round-trip (incl. zeros + a >254 run), FrameReader resync
// across a garbage byte, sample encode/decode, descriptor JSON round-trip + hash
// change, ClockSync recovery of a known (a,b). Wired via KRS_TELEMETRY_SELFTEST.
bool runTelemetryGate();

} // namespace krs::ktp
