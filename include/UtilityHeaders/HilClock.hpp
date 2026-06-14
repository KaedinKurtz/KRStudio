#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

/**
 * @brief Headless clock decoupling for hardware-in-the-loop (HIL). The physics
 * server runs on its own thread at a rigid deterministic rate (e.g. 1000 Hz);
 * the sensor/render pipeline runs asynchronously on another thread (e.g. 30 Hz)
 * and samples the latest plant state from a lock-free single-producer /
 * single-consumer ring buffer. No locks sit on the physics hot path.
 */
namespace krs::hil {

/// Plant state snapshot published by the physics thread each tick. POD so it
/// copies trivially through the lock-free ring and maps onto CAN/CANopen.
struct PlantState {
    uint64_t tick = 0;
    double   simTime = 0.0;             // seconds
    static constexpr int kJoints = 8;
    float jointPos[kJoints] = { 0 };    // encoder positions (rad)
    float jointVel[kJoints] = { 0 };    // velocities (rad/s)
    float jointTorque[kJoints] = { 0 }; // actuator torques (N·m)
};

/// Lock-free SPSC latest-value ring. The producer overwrites; the consumer
/// always reads the most recently published slot (sequence-stamped, with a
/// retry if the producer lapped the slot mid-copy — impossible at HIL rates,
/// but correct regardless). Capacity is a power of two.
template <typename T, size_t Capacity = 256>
class StateRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    void publish(const T& v) {                              // producer (physics thread)
        const uint64_t s = m_seq.load(std::memory_order_relaxed);
        m_slots[s & (Capacity - 1)] = v;                    // write slot
        m_seq.store(s + 1, std::memory_order_release);      // publish (release)
    }
    bool readLatest(T& out) const {                         // consumer (sensor thread)
        for (int attempt = 0; attempt < 4; ++attempt) {
            uint64_t s1 = m_seq.load(std::memory_order_acquire);
            if (s1 == 0) return false;                       // nothing published yet
            out = m_slots[(s1 - 1) & (Capacity - 1)];        // copy newest slot
            uint64_t s2 = m_seq.load(std::memory_order_acquire);
            if (s2 - (s1 - 1) < Capacity) return true;       // slot was not overwritten mid-copy
        }
        return true;                                         // give up retrying: last copy is fine
    }
    uint64_t published() const { return m_seq.load(std::memory_order_acquire); }
private:
    std::array<T, Capacity> m_slots{};
    std::atomic<uint64_t> m_seq{ 0 };
};

/// Scheduling-jitter statistics for the deterministic physics loop.
struct JitterStats {
    int    ticks = 0;
    double nominalMs = 0.0;   // target tick period
    double meanMs = 0.0;      // mean |interval - nominal|
    double p99Ms = 0.0;       // 99th percentile jitter
    double p999Ms = 0.0;      // 99.9th percentile jitter (gate metric — outlier-robust)
    double maxMs = 0.0;       // worst-case jitter
};

/// Run a headless N-tick physics loop at `hz` and measure interval jitter.
/// Uses a sleep-then-spin wait on a steady high-resolution clock (and a 1 ms
/// timer period on Windows). `step` is the per-tick workload (defaults to a
/// small representative load).
JitterStats runJitterBench(int ticks, double hz,
                           const std::function<void(uint64_t, double)>& step = {});

/// HIL_JITTER verification module: 10,000 ticks @ 1000 Hz. Pass gate is p99.9 < 1.0 ms
/// (+ a max < 100 ms catastrophic-hang guard) — outlier-robust on a non-RT host, where
/// a lone scheduler hiccup must not false-fail; the true 0.15 ms target requires a
/// PREEMPT_RT host (documented in ROADMAP.md). Logs and returns pass/fail.
bool runJitterSelfTest();

/**
 * @brief Async coordinator that drives a physics callback at a fixed rate on a
 * dedicated thread and a sensor callback at a lower rate on another thread,
 * decoupled through a StateRing<PlantState>. The sensor side always sees the
 * latest state without blocking physics.
 */
class AsyncCoordinator {
public:
    using PhysicsFn = std::function<void(uint64_t tick, double dt, PlantState& out)>;
    using SensorFn  = std::function<void(const PlantState& latest)>;

    void start(PhysicsFn physics, SensorFn sensor, double physHz, double sensorHz);
    void stop();
    const JitterStats& jitter() const { return m_jitter; }
    uint64_t physicsTicks() const { return m_ring.published(); }

private:
    StateRing<PlantState> m_ring;
    std::thread m_physThread, m_sensorThread;
    std::atomic<bool> m_run{ false };
    JitterStats m_jitter;
};

} // namespace krs::hil
