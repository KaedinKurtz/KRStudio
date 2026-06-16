#pragma once
// SensorStats.hpp -- the statistical test harness backbone every sensor gate uses. Pure functions over
// sample vectors: distribution moments, linear/power-law fits, histograms, autocorrelation, Allan
// deviation, hole rate. Self-tested on known-analytic inputs by GATE 0.
#include <vector>
#include <utility>

namespace krs::sensor::stats {

double mean(const std::vector<double>& x);
double variance(const std::vector<double>& x);            // sample variance (Bessel n-1)
double stddev(const std::vector<double>& x);
double rms(const std::vector<double>& x);                 // sqrt(mean(x^2))
double rmsError(const std::vector<double>& a, const std::vector<double>& b);  // sqrt(mean((a-b)^2))

struct LinFit { double slope = 0, intercept = 0, r2 = 0; };
LinFit linearFit(const std::vector<double>& x, const std::vector<double>& y);   // y = intercept + slope*x

// log-log fit y = coeff * x^exponent (x,y > 0). Used to test "depth error ~ Z^2" (exponent ~ 2).
struct PowerFit { double coeff = 0, exponent = 0, r2 = 0; };
PowerFit powerFit(const std::vector<double>& x, const std::vector<double>& y);

std::vector<int> histogram(const std::vector<double>& x, double lo, double hi, int bins);
double autocorr(const std::vector<double>& x, int lag);   // normalized autocorrelation at a lag

// Non-overlapping Allan deviation. series sampled at dt; returns (tau = m*dt, allanDev) for each cluster m.
std::vector<std::pair<double, double>> allanDeviation(const std::vector<double>& series, double dt,
                                                      const std::vector<int>& clusterSizes);

double holeRate(const std::vector<double>& vals, double invalidSentinel);   // fraction == sentinel or NaN

} // namespace krs::sensor::stats
