#include "SensorStats.hpp"
#include <cmath>
#include <algorithm>

namespace krs::sensor::stats {

double mean(const std::vector<double>& x) {
    if (x.empty()) return 0.0;
    double s = 0.0; for (double v : x) s += v; return s / double(x.size());
}

double variance(const std::vector<double>& x) {
    if (x.size() < 2) return 0.0;
    const double m = mean(x);
    double s = 0.0; for (double v : x) s += (v - m) * (v - m);
    return s / double(x.size() - 1);
}

double stddev(const std::vector<double>& x) { return std::sqrt(variance(x)); }

double rms(const std::vector<double>& x) {
    if (x.empty()) return 0.0;
    double s = 0.0; for (double v : x) s += v * v; return std::sqrt(s / double(x.size()));
}

double rmsError(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0;
    double s = 0.0; for (size_t i = 0; i < n; ++i) { const double d = a[i] - b[i]; s += d * d; }
    return std::sqrt(s / double(n));
}

LinFit linearFit(const std::vector<double>& x, const std::vector<double>& y) {
    LinFit f;
    const size_t n = std::min(x.size(), y.size());
    if (n < 2) return f;
    const double mx = mean(std::vector<double>(x.begin(), x.begin() + n));
    const double my = mean(std::vector<double>(y.begin(), y.begin() + n));
    double sxx = 0, sxy = 0, syy = 0;
    for (size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mx, dy = y[i] - my;
        sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
    }
    if (sxx <= 0) return f;
    f.slope = sxy / sxx;
    f.intercept = my - f.slope * mx;
    f.r2 = (syy > 0) ? (sxy * sxy) / (sxx * syy) : 1.0;
    return f;
}

PowerFit powerFit(const std::vector<double>& x, const std::vector<double>& y) {
    PowerFit f;
    std::vector<double> lx, ly;
    const size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; ++i) if (x[i] > 0 && y[i] > 0) { lx.push_back(std::log(x[i])); ly.push_back(std::log(y[i])); }
    if (lx.size() < 2) return f;
    const LinFit lf = linearFit(lx, ly);
    f.exponent = lf.slope;
    f.coeff = std::exp(lf.intercept);
    f.r2 = lf.r2;
    return f;
}

std::vector<int> histogram(const std::vector<double>& x, double lo, double hi, int bins) {
    std::vector<int> h(std::max(1, bins), 0);
    if (hi <= lo || bins <= 0) return h;
    const double w = (hi - lo) / bins;
    for (double v : x) {
        if (v < lo || v >= hi) continue;
        int b = int((v - lo) / w); if (b >= bins) b = bins - 1; if (b < 0) b = 0;
        ++h[b];
    }
    return h;
}

double autocorr(const std::vector<double>& x, int lag) {
    const int n = int(x.size());
    if (lag < 0 || lag >= n) return 0.0;
    const double m = mean(x);
    double num = 0, den = 0;
    for (int i = 0; i < n; ++i) den += (x[i] - m) * (x[i] - m);
    for (int i = 0; i + lag < n; ++i) num += (x[i] - m) * (x[i + lag] - m);
    return (den > 0) ? num / den : 0.0;
}

std::vector<std::pair<double, double>> allanDeviation(const std::vector<double>& series, double dt,
                                                      const std::vector<int>& clusterSizes) {
    std::vector<std::pair<double, double>> out;
    const int N = int(series.size());
    for (int m : clusterSizes) {
        if (m < 1) continue;
        const int K = N / m;                          // number of non-overlapping clusters
        if (K < 3) continue;
        std::vector<double> ybar(K, 0.0);
        for (int j = 0; j < K; ++j) {
            double s = 0.0; for (int i = 0; i < m; ++i) s += series[j * m + i];
            ybar[j] = s / m;
        }
        double av = 0.0;
        for (int j = 0; j + 1 < K; ++j) { const double d = ybar[j + 1] - ybar[j]; av += d * d; }
        av /= (2.0 * (K - 1));
        out.emplace_back(m * dt, std::sqrt(av));
    }
    return out;
}

double holeRate(const std::vector<double>& vals, double invalidSentinel) {
    if (vals.empty()) return 0.0;
    long holes = 0;
    for (double v : vals) if (std::isnan(v) || v == invalidSentinel) ++holes;
    return double(holes) / double(vals.size());
}

} // namespace krs::sensor::stats
