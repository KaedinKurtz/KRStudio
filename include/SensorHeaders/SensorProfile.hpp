#pragma once
// SensorProfile.hpp -- the DATA layer of the synthetic-sensor pipeline (Phase 0). A sensor profile is the
// sensor CLASS + PARAMETERS; the noise MODEL is code that consumes this profile (data separated from model,
// per 0.1). Every parameter is tagged with PROVENANCE so a reader can see exactly what is real-from-datasheet
// vs second-source vs a placeholder awaiting calibration. Numbers are the Intel RealSense D456 datasheet
// (RGB/depth) + Bosch BMI085 (IMU). Anything not in a datasheet is Defaulted and flagged.
#include <cstdint>
#include <string>

namespace krs::sensor {

enum class Provenance { Datasheet, SecondSource, DefaultedPendingCalibration };

inline const char* provenanceStr(Provenance p) {
    switch (p) {
        case Provenance::Datasheet: return "datasheet";
        case Provenance::SecondSource: return "second-source";
        case Provenance::DefaultedPendingCalibration: return "defaulted-pending-calibration";
    }
    return "?";
}

// A parameter value carrying its provenance. Implicitly converts to the value for use; .prov is visible.
template <typename T>
struct Param {
    T value{};
    Provenance prov{Provenance::DefaultedPendingCalibration};
    const char* note{""};
    constexpr Param() = default;
    constexpr Param(T v, Provenance p, const char* n = "") : value(v), prov(p), note(n) {}
    constexpr operator T() const { return value; }
    Param& operator=(T v) { value = v; return *this; }   // edit the value (provenance unchanged)
};

// ---- RGB camera (D456: 1280x800, f/2.0, 1.93mm, FOV 90x65) ----
struct RgbProfile {
    Param<int>    width   {1280, Provenance::Datasheet};
    Param<int>    height  {800,  Provenance::Datasheet};
    Param<double> focalMm {1.93, Provenance::Datasheet};
    Param<double> fNumber {2.0,  Provenance::Datasheet};
    Param<double> fovHdeg {90.0, Provenance::Datasheet};
    Param<double> fovVdeg {65.0, Provenance::Datasheet};
    // pixel size chosen so fx = (W/2)/tan(fovH/2) matches the datasheet FOV+focal (1.93/0.003016 ~= 640 px).
    Param<double> pixelUm {3.016, Provenance::DefaultedPendingCalibration, "set so fx matches datasheet FOV"};
    // Brown-Conrady distortion -- NOT in the datasheet.
    Param<double> k1 {-0.10, Provenance::DefaultedPendingCalibration};
    Param<double> k2 { 0.04, Provenance::DefaultedPendingCalibration};
    Param<double> k3 { 0.00, Provenance::DefaultedPendingCalibration};
    Param<double> p1 { 0.0008, Provenance::DefaultedPendingCalibration};
    Param<double> p2 {-0.0006, Provenance::DefaultedPendingCalibration};
    // noise stack -- NOT in the datasheet. read noise in DN (dark floor), full-scale signal, bit depth.
    Param<double> readNoiseDN {2.0,   Provenance::DefaultedPendingCalibration, "read noise std, DN"};
    Param<double> fullScaleDN {255.0, Provenance::DefaultedPendingCalibration};
    Param<double> photonsPerDN {30.0, Provenance::DefaultedPendingCalibration, "e-/DN gain -> shot variance = signal/photonsPerDN"};
    Param<int>    bitDepth   {8,      Provenance::Datasheet};
};

// ---- depth camera (D456 stereo: OV9782, baseline 95mm, depth FOV 90x64) ----
struct DepthProfile {
    Param<int>    width      {1280, Provenance::Datasheet};
    Param<int>    height     {720,  Provenance::Datasheet};
    Param<double> baselineMm {95.0, Provenance::Datasheet};
    Param<double> fovHdeg    {90.0, Provenance::Datasheet};
    Param<double> fovVdeg    {64.0, Provenance::Datasheet};
    Param<double> minZmm     {520.0,Provenance::Datasheet, "min-Z @1280x720"};
    Param<double> minZmm_848 {350.0,Provenance::Datasheet, "min-Z @848x480"};
    Param<double> maxZm      {6.0,  Provenance::Datasheet, "range >6m; using 6m"};
    Param<double> distortMaxPct {1.5, Provenance::Datasheet};
    // structured error model -- NOT in the datasheet (the differentiator). Quadratic depth noise:
    // sigmaZ(mm) = depthNoiseCoeff * Z(m)^2 (the defining stereo characteristic, ~ subpixel * Z^2 / (f*b)).
    Param<double> depthNoiseCoeff {2.5, Provenance::DefaultedPendingCalibration, "sigmaZ_mm = coeff * Z_m^2"};
    Param<double> subpixelErr     {0.1, Provenance::DefaultedPendingCalibration, "disparity matching error, px"};
    // hole rates conditioned on material (specular/dark drop stereo matches).
    Param<double> holeRateSpecular {0.35, Provenance::DefaultedPendingCalibration};
    Param<double> holeRateLambert  {0.01, Provenance::DefaultedPendingCalibration};
    Param<double> flyingPixelRate  {0.25, Provenance::DefaultedPendingCalibration, "frac of edge pixels"};
    Param<double> speckleStdMm     {3.0,  Provenance::DefaultedPendingCalibration, "IR speckle local noise"};
};

// ---- IMU (Bosch BMI085: accel +-4g, gyro +-1000 dps, 100/200/400 Hz, 50us ts) ----
struct ImuProfile {
    Param<double> accelRangeG  {4.0,    Provenance::Datasheet};
    Param<double> gyroRangeDps {1000.0, Provenance::Datasheet};
    Param<double> accelRateHz  {200.0,  Provenance::Datasheet};
    Param<double> gyroRateHz   {400.0,  Provenance::Datasheet};
    Param<double> timestampUs  {50.0,   Provenance::Datasheet};
    // noise density + bias from the BMI085 component datasheet (second source) where available, else default.
    Param<double> accelNoiseDensity {160e-6 * 9.80665, Provenance::SecondSource, "160 ug/sqrt(Hz) -> m/s^2/sqrt(Hz)"};
    Param<double> gyroNoiseDensity  {0.014 * 3.14159265/180.0, Provenance::SecondSource, "0.014 dps/sqrt(Hz) -> rad/s/sqrt(Hz)"};
    Param<double> accelBiasInstab   {0.02, Provenance::DefaultedPendingCalibration, "m/s^2 bias-instability floor"};
    Param<double> gyroBiasInstab    {0.0005, Provenance::DefaultedPendingCalibration, "rad/s bias-instability floor"};
    Param<double> accelBiasTau      {200.0, Provenance::DefaultedPendingCalibration, "bias correlation time s"};
    Param<double> gyroBiasTau       {300.0, Provenance::DefaultedPendingCalibration, "bias correlation time s"};
    Param<double> accelRandomWalk   {0.001, Provenance::DefaultedPendingCalibration};
    Param<double> gyroRandomWalk    {1e-5,  Provenance::DefaultedPendingCalibration};
    Param<double> scaleFactorErr    {0.005, Provenance::DefaultedPendingCalibration, "0.5% scale error"};
    Param<double> axisMisalignRad   {0.002, Provenance::DefaultedPendingCalibration};
    Param<double> tempBiasCoeff     {0.001, Provenance::DefaultedPendingCalibration, "bias per degC"};
};

struct SensorProfile {
    RgbProfile   rgb;
    DepthProfile depth;
    ImuProfile   imu;
    std::uint64_t seed{0x5E11507Eull};   // determinism per seed
};

} // namespace krs::sensor
