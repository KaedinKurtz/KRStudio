#include "ImuModel.hpp"
#include <cmath>

namespace krs::sensor {

double InertialAxis::step(double trueVal, std::mt19937_64& rng) {
    const double d = dt();
    std::normal_distribution<double> white(0.0, noiseDensity * std::sqrt(rateHz));
    double out = trueVal + white(rng);
    if (enableGM) {
        // 1st-order Gauss-Markov bias instability (steady-state std == biasInstab). This -- not the random
        // walk -- is the Allan-deviation FLOOR; removing it drops the floor (the gate's GM discriminator).
        const double phi = std::exp(-d / biasTau);
        std::normal_distribution<double> gmDrive(0.0, biasInstab * std::sqrt(1.0 - phi * phi));
        biasGM = phi * biasGM + gmDrive(rng);
        out += biasGM;
    }
    if (enableRW) {
        // rate random walk (integrated white) -> the +1/2 Allan slope at long tau.
        std::normal_distribution<double> rwDrive(0.0, randomWalk * std::sqrt(d));
        biasRW += rwDrive(rng);
        out += biasRW;
    }
    return out;
}

double InertialAxis::stepWhiteOnly(double trueVal, std::mt19937_64& rng) const {
    std::normal_distribution<double> white(0.0, noiseDensity * std::sqrt(rateHz));
    return trueVal + white(rng);
}

ImuModel ImuModel::fromProfile(const ImuProfile& p) {
    ImuModel m;
    m.accel.rateHz = double(p.accelRateHz);
    m.accel.noiseDensity = double(p.accelNoiseDensity);
    m.accel.biasInstab = double(p.accelBiasInstab);
    m.accel.biasTau = double(p.accelBiasTau);
    m.accel.randomWalk = double(p.accelRandomWalk);
    m.gyro.rateHz = double(p.gyroRateHz);
    m.gyro.noiseDensity = double(p.gyroNoiseDensity);
    m.gyro.biasInstab = double(p.gyroBiasInstab);
    m.gyro.biasTau = double(p.gyroBiasTau);
    m.gyro.randomWalk = double(p.gyroRandomWalk);
    m.scaleFactorErr = double(p.scaleFactorErr);
    m.axisMisalignRad = double(p.axisMisalignRad);
    m.tempBiasCoeff = double(p.tempBiasCoeff);
    return m;
}

glm::dvec3 ImuModel::applySystematic(const glm::dvec3& v, double tempC) const {
    const double s = 1.0 + scaleFactorErr;     // scale factor on the diagonal
    const double a = axisMisalignRad;          // small cross-axis coupling
    glm::dvec3 out;
    out.x = s * v.x + a * v.y - a * v.z;
    out.y = -a * v.x + s * v.y + a * v.z;
    out.z = a * v.x - a * v.y + s * v.z;
    const double tempBias = tempBiasCoeff * (tempC - calibTempC);
    out += glm::dvec3(tempBias);
    return out;
}

} // namespace krs::sensor
