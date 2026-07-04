// KLut.cpp -- see KLut.hpp. The .klut LUT / characterized-data standard object: 1D/2D breakpoint
// tables with Nearest/Linear/Cubic (Catmull-Rom) interpolation + Clamp/Linear extrapolation, a
// scattered-data characterizer, and a content-addressed JSON round-trip in the .k* family.
#include "KLut.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace krs::klut {

// ================================================================================================
// 1D sampling primitive: interpolate a value curve v[] defined over strictly-ascending b[].
// Honors `interp` in-range and `extrap` out-of-range. Shared by sample1D and (per-axis) sample2D.
// ================================================================================================
namespace {

// Catmull-Rom tangent at breakpoint k, scaled to the neighbouring interval so it works on a
// NON-uniform grid. Natural at the ends: one-sided secant. Interior: centered secant.
double crTangentLeft(const std::vector<double>& b, const std::vector<double>& v, int i) {
    // tangent at the LEFT knot i of interval [i, i+1], expressed as dv/dt for t in [0,1] across
    // that interval (i.e. already multiplied by the interval width h = b[i+1]-b[i]).
    const double h = b[i + 1] - b[i];
    if (i == 0) return v[i + 1] - v[i];                    // natural: one-sided
    const double hPrev = b[i] - b[i - 1];
    // centered secant slope in value-per-x, scaled to this interval's width
    const double slope = (v[i + 1] - v[i - 1]) / (hPrev + h);
    return slope * h;
}
double crTangentRight(const std::vector<double>& b, const std::vector<double>& v, int i) {
    // tangent at the RIGHT knot i+1 of interval [i, i+1], scaled by this interval's width h.
    const double h = b[i + 1] - b[i];
    const int n = int(b.size());
    if (i + 1 == n - 1) return v[i + 1] - v[i];            // natural: one-sided
    const double hNext = b[i + 2] - b[i + 1];
    const double slope = (v[i + 2] - v[i]) / (h + hNext);
    return slope * h;
}

double sampleCurve1D(const std::vector<double>& b, const std::vector<double>& v,
                     double x, Interp interp, Extrap extrap) {
    const int n = int(b.size());
    if (n == 0 || int(v.size()) != n) return 0.0;
    if (n == 1) return v[0];

    // ---- out of range: extrapolation ----
    if (x <= b.front()) {
        if (x == b.front()) return v.front();
        if (extrap == Extrap::Clamp) return v.front();
        const double slope = (v[1] - v[0]) / (b[1] - b[0]);          // Linear: extend end slope
        return v.front() + slope * (x - b.front());
    }
    if (x >= b.back()) {
        if (x == b.back()) return v.back();
        if (extrap == Extrap::Clamp) return v.back();
        const double slope = (v[n - 1] - v[n - 2]) / (b[n - 1] - b[n - 2]);
        return v.back() + slope * (x - b.back());
    }

    // ---- in range: locate bracketing interval [i, i+1] (b strictly ascending) ----
    const int i = int(std::upper_bound(b.begin(), b.end(), x) - b.begin()) - 1;
    const int lo = std::clamp(i, 0, n - 2);
    const double h = b[lo + 1] - b[lo];
    const double t = (h > 0.0) ? (x - b[lo]) / h : 0.0;              // local fraction in [0,1]

    switch (interp) {
        case Interp::Nearest:
            return (t < 0.5) ? v[lo] : v[lo + 1];
        case Interp::Linear:
            return v[lo] + t * (v[lo + 1] - v[lo]);
        case Interp::Cubic: {
            // Hermite basis with Catmull-Rom tangents (natural at the ends).
            const double p0 = v[lo], p1 = v[lo + 1];
            const double m0 = crTangentLeft(b, v, lo);
            const double m1 = crTangentRight(b, v, lo);
            const double t2 = t * t, t3 = t2 * t;
            const double h00 =  2 * t3 - 3 * t2 + 1;
            const double h10 =      t3 - 2 * t2 + t;
            const double h01 = -2 * t3 + 3 * t2;
            const double h11 =      t3 -     t2;
            return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
        }
    }
    return v[lo] + t * (v[lo + 1] - v[lo]);
}

// Content hash of a written file (re-declared local, per the KSave file-static helper idiom).
QString fileSha1(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}

const char* interpName(Interp i) {
    switch (i) { case Interp::Nearest: return "nearest"; case Interp::Cubic: return "cubic"; default: return "linear"; }
}
Interp interpFrom(const QString& s) {
    if (s == QLatin1String("nearest")) return Interp::Nearest;
    if (s == QLatin1String("cubic"))   return Interp::Cubic;
    return Interp::Linear;
}
const char* extrapName(Extrap e) { return e == Extrap::Linear ? "linear" : "clamp"; }
Extrap extrapFrom(const QString& s) { return s == QLatin1String("linear") ? Extrap::Linear : Extrap::Clamp; }

} // namespace

// ================================================================================================
// Lut members
// ================================================================================================
bool Lut::valid(std::string* why) const {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (axes.empty() || axes.size() > 2) return fail("a LUT must have 1 or 2 axes");
    std::size_t product = 1;
    for (const auto& ax : axes) {
        if (ax.breakpoints.size() < 2) return fail("each axis needs >= 2 breakpoints");
        for (std::size_t k = 1; k < ax.breakpoints.size(); ++k)
            if (!(ax.breakpoints[k] > ax.breakpoints[k - 1]))
                return fail("axis breakpoints must be STRICTLY ascending");
        product *= ax.breakpoints.size();
    }
    if (values.size() != product) return fail("values size does not match the product of axis breakpoint counts");
    return true;
}

double Lut::sample1D(double x) const {
    if (axes.size() != 1 || !valid(nullptr)) return 0.0;
    return sampleCurve1D(axes[0].breakpoints, values, x, interp, extrap);
}

double Lut::sample2D(double x, double y) const {
    if (axes.size() != 2 || !valid(nullptr)) return 0.0;
    const auto& bx = axes[0].breakpoints;   // outer (row) axis
    const auto& by = axes[1].breakpoints;   // inner (col) axis
    const int nx = int(bx.size()), ny = int(by.size());
    // For each x-row, sample along y to get one value; then sample those across x. Separable, so
    // this is exact bilinear for Linear and separable Catmull-Rom for Cubic.
    std::vector<double> rowByY(nx);
    for (int r = 0; r < nx; ++r) {
        std::vector<double> row(ny);
        for (int c = 0; c < ny; ++c) row[c] = values[std::size_t(r) * ny + c];
        rowByY[r] = sampleCurve1D(by, row, y, interp, extrap);
    }
    return sampleCurve1D(bx, rowByY, x, interp, extrap);
}

double Lut::sample(const std::vector<double>& coords) const {
    if (axes.size() == 1 && coords.size() >= 1) return sample1D(coords[0]);
    if (axes.size() == 2 && coords.size() >= 2) return sample2D(coords[0], coords[1]);
    return 0.0;
}

// ================================================================================================
// characterizeFromSamples: bake scattered (xs, ys) into an evenly-spaced 1D table.
// ================================================================================================
Lut characterizeFromSamples(const std::vector<double>& xs, const std::vector<double>& ys,
                            int nBreakpoints, Interp interp,
                            const std::string& quantity, const std::string& unit) {
    Lut lut;
    lut.quantity = quantity;
    lut.unit = unit;
    lut.name = quantity.empty() ? std::string("characterized") : quantity;
    lut.interp = interp;
    lut.extrap = Extrap::Clamp;

    // Sort by x + de-dupe (a later duplicate x wins). Pair up only where both xs[i] and ys[i] exist.
    const std::size_t m = std::min(xs.size(), ys.size());
    std::vector<std::pair<double, double>> pts;
    pts.reserve(m);
    for (std::size_t i = 0; i < m; ++i) pts.emplace_back(xs[i], ys[i]);
    std::stable_sort(pts.begin(), pts.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::pair<double, double>> uniq;
    uniq.reserve(pts.size());
    for (const auto& p : pts) {
        if (!uniq.empty() && uniq.back().first == p.first) uniq.back().second = p.second;  // later wins
        else uniq.push_back(p);
    }

    Axis ax; ax.name = "x"; ax.unit = "";
    lut.axes.push_back(ax);

    // Degenerate: cannot form a >= 2-breakpoint ascending axis -> intentionally invalid Lut.
    if (uniq.size() < 2 || nBreakpoints < 2) return lut;

    std::vector<double> sx(uniq.size()), sy(uniq.size());
    for (std::size_t i = 0; i < uniq.size(); ++i) { sx[i] = uniq[i].first; sy[i] = uniq[i].second; }
    const double xMin = sx.front(), xMax = sx.back();

    auto& bkps = lut.axes[0].breakpoints;
    bkps.resize(nBreakpoints);
    lut.values.resize(nBreakpoints);
    for (int k = 0; k < nBreakpoints; ++k) {
        const double frac = double(k) / double(nBreakpoints - 1);
        const double xk = xMin + frac * (xMax - xMin);
        bkps[k] = xk;
        // LINEAR interp of the sorted curve at xk (edges held outside the data).
        lut.values[k] = sampleCurve1D(sx, sy, xk, Interp::Linear, Extrap::Clamp);
    }
    // Guard the last breakpoint against fp drift breaking strict ascent (xMax reproduced exactly).
    bkps.back() = xMax;
    return lut;
}

// ================================================================================================
// JSON round-trip (content-addressed .k* family; "format":"klut/1")
// ================================================================================================
namespace {

QJsonObject lutToJson(const Lut& lut) {
    QJsonObject root;
    root["format"]   = QStringLiteral("klut/1");
    root["id"]       = QString::fromStdString(lut.id);
    root["name"]     = QString::fromStdString(lut.name);
    root["quantity"] = QString::fromStdString(lut.quantity);
    root["unit"]     = QString::fromStdString(lut.unit);
    root["interp"]   = QString::fromLatin1(interpName(lut.interp));
    root["extrap"]   = QString::fromLatin1(extrapName(lut.extrap));

    QJsonArray axes;
    for (const auto& ax : lut.axes) {
        QJsonObject ao;
        ao["name"] = QString::fromStdString(ax.name);
        ao["unit"] = QString::fromStdString(ax.unit);
        QJsonArray bk;
        for (double b : ax.breakpoints) bk.push_back(b);
        ao["breakpoints"] = bk;
        axes.push_back(ao);
    }
    root["axes"] = axes;

    QJsonArray vals;
    for (double v : lut.values) vals.push_back(v);
    root["values"] = vals;
    return root;
}

bool lutFromJson(const QJsonObject& root, Lut& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    const QString fmt = root["format"].toString();
    // family match + refuse unknown MAJOR (only "klut/1").
    if (!fmt.startsWith(QLatin1String("klut/")) || fmt.section('/', 1, 1).toInt() != 1)
        return fail(QStringLiteral("unknown klut format: %1").arg(fmt));

    Lut lut;
    lut.id       = root["id"].toString().toStdString();
    lut.name     = root["name"].toString().toStdString();
    lut.quantity = root["quantity"].toString().toStdString();
    lut.unit     = root["unit"].toString().toStdString();
    lut.interp   = interpFrom(root["interp"].toString());
    lut.extrap   = extrapFrom(root["extrap"].toString());

    for (const QJsonValue& av : root["axes"].toArray()) {
        const QJsonObject ao = av.toObject();
        Axis ax;
        ax.name = ao["name"].toString().toStdString();
        ax.unit = ao["unit"].toString().toStdString();
        for (const QJsonValue& bv : ao["breakpoints"].toArray()) ax.breakpoints.push_back(bv.toDouble());
        lut.axes.push_back(std::move(ax));
    }
    for (const QJsonValue& vv : root["values"].toArray()) lut.values.push_back(vv.toDouble());

    std::string why;
    if (!lut.valid(&why)) return fail(QStringLiteral("invalid klut document: %1").arg(QString::fromStdString(why)));
    out = std::move(lut);
    return true;
}

} // namespace

QString saveKLut(Lut& lut, const QString& absPath) {
    if (!lut.valid(nullptr)) return {};
    if (lut.id.empty()) lut.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const QJsonObject root = lutToJson(lut);
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return QString::fromStdString(lut.id);
}

bool loadKLut(const QString& absPath, Lut& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    QJsonParseError perr{};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject()) return fail(QStringLiteral("parse error"));
    return lutFromJson(d.object(), out, err);
}

// ================================================================================================
// GATE KLUT
// ================================================================================================
bool runKLutGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[klut] GATE KLUT -- .klut characterized-data object: 1D/2D breakpoint tables, "
           "Nearest/Linear/Cubic interp + Clamp/Linear extrap, scattered-data bake, content-addressed round-trip\n");

    bool pass = true;
    auto check = [&](bool ok) { pass = pass && ok; return ok; };

    // ---- (1) 1D Linear over [0,1,2,3] -> [0,10,20,30] reproduces 10*x at + between knots ----
    Lut lin;
    lin.name = "ten-x"; lin.quantity = "value"; lin.unit = "u";
    lin.interp = Interp::Linear; lin.extrap = Extrap::Clamp;
    { Axis a; a.name = "x"; a.breakpoints = { 0, 1, 2, 3 }; lin.axes.push_back(a); }
    lin.values = { 0, 10, 20, 30 };
    double linMaxErr = 0.0;
    for (double x = 0.0; x <= 3.0 + 1e-9; x += 0.1) linMaxErr = std::max(linMaxErr, std::abs(lin.sample1D(x) - 10.0 * x));
    // exact at knots too
    for (double x : { 0.0, 1.0, 2.0, 3.0 }) linMaxErr = std::max(linMaxErr, std::abs(lin.sample1D(x) - 10.0 * x));
    const bool linOk = check(linMaxErr < 1e-9);
    printf("[klut]   1D Linear reproduces 10*x (max err %.3e)  %s\n", linMaxErr, linOk ? "PASS" : "FAIL");

    // ---- (2) Cubic reproduces y=x^2 within tol AND is smoother than Linear (lower max |2nd diff|) ----
    Lut cub;
    cub.name = "x-squared"; cub.quantity = "y"; cub.unit = "u";
    cub.interp = Interp::Cubic; cub.extrap = Extrap::Clamp;
    { Axis a; a.name = "x"; for (int k = 0; k <= 10; ++k) a.breakpoints.push_back(k * 0.5); cub.axes.push_back(a); }
    for (int k = 0; k <= 10; ++k) { const double x = k * 0.5; cub.values.push_back(x * x); }  // x^2 at the knots
    Lut lin2 = cub; lin2.interp = Interp::Linear;           // same knots, Linear, for the smoothness comparison

    double cubMaxErr = 0.0;
    // Sample a fine grid, compare to the true x^2 and gather each curve's samples for 2nd-difference.
    std::vector<double> cubS, linS;
    for (double x = 0.0; x <= 5.0 + 1e-9; x += 0.05) {
        const double c = cub.sample1D(x);
        cubMaxErr = std::max(cubMaxErr, std::abs(c - x * x));
        cubS.push_back(c);
        linS.push_back(lin2.sample1D(x));
    }
    auto maxAbs2ndDiff = [](const std::vector<double>& s) {
        double m = 0.0;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) m = std::max(m, std::abs(s[i + 1] - 2 * s[i] + s[i - 1]));
        return m;
    };
    const double cub2 = maxAbs2ndDiff(cubS), lin2d = maxAbs2ndDiff(linS);
    const bool cubOk = check(cubMaxErr < 0.1 && cub2 < lin2d);
    printf("[klut]   Cubic reproduces x^2 (max err %.3e) AND smoother than Linear (max|2nd-diff| cubic %.3e < linear %.3e)  %s\n",
           cubMaxErr, cub2, lin2d, cubOk ? "PASS" : "FAIL");

    // ---- (3) 2D bilinear on z = 2x + 3y samples EXACTLY (at + between grid nodes) ----
    Lut two;
    two.name = "plane"; two.quantity = "z"; two.unit = "u";
    two.interp = Interp::Linear; two.extrap = Extrap::Clamp;
    { Axis ax; ax.name = "x"; ax.breakpoints = { 0, 1, 2 }; two.axes.push_back(ax); }
    { Axis ay; ay.name = "y"; ay.breakpoints = { 0, 1, 2, 3 }; two.axes.push_back(ay); }
    // row-major over (x rows) x (y cols): value = 2x + 3y
    for (double x : { 0.0, 1.0, 2.0 })
        for (double y : { 0.0, 1.0, 2.0, 3.0 })
            two.values.push_back(2 * x + 3 * y);
    double twoMaxErr = 0.0;
    for (double x = 0.0; x <= 2.0 + 1e-9; x += 0.25)
        for (double y = 0.0; y <= 3.0 + 1e-9; y += 0.25)
            twoMaxErr = std::max(twoMaxErr, std::abs(two.sample2D(x, y) - (2 * x + 3 * y)));
    // sample() dispatches to sample2D
    twoMaxErr = std::max(twoMaxErr, std::abs(two.sample({ 1.5, 2.5 }) - (2 * 1.5 + 3 * 2.5)));
    const bool twoOk = check(twoMaxErr < 1e-9);
    printf("[klut]   2D bilinear reproduces 2x+3y (max err %.3e)  %s\n", twoMaxErr, twoOk ? "PASS" : "FAIL");

    // ---- (4) Extrap Clamp HOLDS the edge; Extrap Linear EXTENDS the end slope (distinct + correct) ----
    Lut cl = lin; cl.extrap = Extrap::Clamp;
    Lut ex = lin; ex.extrap = Extrap::Linear;
    // above the top knot (x=3, v=30, end slope 10/unit): at x=5 -> Clamp=30, Linear=50
    const double clHi = cl.sample1D(5.0), exHi = ex.sample1D(5.0);
    // below the bottom knot (x=0, v=0, start slope 10): at x=-2 -> Clamp=0, Linear=-20
    const double clLo = cl.sample1D(-2.0), exLo = ex.sample1D(-2.0);
    const bool extrapOk = check(std::abs(clHi - 30.0) < 1e-9 && std::abs(exHi - 50.0) < 1e-9 &&
                                std::abs(clLo - 0.0) < 1e-9  && std::abs(exLo + 20.0) < 1e-9 &&
                                std::abs(clHi - exHi) > 1.0);   // genuinely distinct
    printf("[klut]   Extrap: Clamp holds edge (hi %.2f lo %.2f) vs Linear extends slope (hi %.2f lo %.2f)  %s\n",
           clHi, clLo, exHi, exLo, extrapOk ? "PASS" : "FAIL");

    // ---- (5) saveKLut -> loadKLut round-trips; content hash changes on an edit ----
    const QString dir = QDir::temp().filePath("krs_klut_gate");
    QDir(dir).removeRecursively();
    const QString path = QDir(dir).filePath("torque_curve.klut");
    Lut rt = two;                                           // save the 2D plane (exercises 2 axes + interp)
    rt.id.clear();
    const QString id = saveKLut(rt, path);
    const QString h1 = fileSha1(path);
    Lut back; QString lerr;
    const bool loaded = loadKLut(path, back, &lerr);
    const bool rtOk = check(!id.isEmpty() && !h1.isEmpty() && loaded &&
                            back.id == rt.id && back.quantity == "z" &&
                            back.axes.size() == 2 && back.axes[1].breakpoints.size() == 4 &&
                            back.values.size() == rt.values.size() && back.interp == Interp::Linear &&
                            std::abs(back.sample2D(1.5, 2.5) - (2 * 1.5 + 3 * 2.5)) < 1e-9);
    // edit an interior value + re-save -> different content hash
    back.values[0] += 1.0;
    saveKLut(back, path);
    const QString h2 = fileSha1(path);
    const bool hashChanged = check((h2 != h1) && !h2.isEmpty());
    printf("[klut]   round-trip (axes/values/interp/quantity)=%s ; content hash changes on edit=%s  %s\n",
           (rtOk ? "yes" : "no"), (hashChanged ? "yes" : "NO"),
           (rtOk && hashChanged) ? "PASS" : "FAIL");

    // ---- (6) characterizeFromSamples on a noisy y = 2x + 1 recovers slope~2 / intercept~1 ----
    std::vector<double> xs, ys;
    // deterministic zero-mean "noise" so the test is reproducible
    const double noise[] = { 0.05, -0.04, 0.03, -0.02, 0.04, -0.05, 0.02, -0.03, 0.01, -0.01 };
    for (int i = 0; i < 40; ++i) {
        const double x = i * 0.25;                          // 0 .. 9.75
        xs.push_back(x);
        ys.push_back(2.0 * x + 1.0 + noise[i % 10]);
    }
    Lut ch = characterizeFromSamples(xs, ys, 16, Interp::Linear, "force", "N");
    // Fit slope/intercept from the baked breakpoints via least squares.
    const auto& b = ch.axes[0].breakpoints;
    double sX = 0, sY = 0, sXX = 0, sXY = 0; const double N = double(b.size());
    for (std::size_t i = 0; i < b.size(); ++i) { sX += b[i]; sY += ch.values[i]; sXX += b[i] * b[i]; sXY += b[i] * ch.values[i]; }
    const double slope = (N * sXY - sX * sY) / (N * sXX - sX * sX);
    const double intercept = (sY - slope * sX) / N;
    const bool charOk = check(ch.valid(nullptr) && std::abs(slope - 2.0) < 0.05 &&
                              std::abs(intercept - 1.0) < 0.1 && ch.quantity == "force" && ch.unit == "N");
    printf("[klut]   characterizeFromSamples(noisy 2x+1) -> slope %.4f (~2), intercept %.4f (~1)  %s\n",
           slope, intercept, charOk ? "PASS" : "FAIL");

    // ---- NEG-CTRL: a non-ascending / too-short axis is REJECTED by valid() ----
    Lut badAsc = lin;
    badAsc.axes[0].breakpoints = { 0, 2, 1, 3 };            // not strictly ascending
    const bool ascRejected = !badAsc.valid(nullptr);
    Lut badShort = lin;
    badShort.axes[0].breakpoints = { 5 };                  // < 2 breakpoints
    badShort.values = { 5 };
    const bool shortRejected = !badShort.valid(nullptr);
    // Prove non-vacuous: the GOOD table validates.
    const bool goodValidates = lin.valid(nullptr);
    const bool negOk = check(ascRejected && shortRejected && goodValidates);
    printf("[klut]   NEG-CTRL: non-ascending axis rejected=%s ; too-short axis rejected=%s ; good validates=%s  %s\n",
           ascRejected ? "yes" : "NO", shortRejected ? "yes" : "NO", goodValidates ? "yes" : "no",
           (ascRejected && shortRejected && goodValidates) ? "REJECTS(non-vacuous)" : "VACUOUS!");

    printf("[klut] %s\n", pass
        ? "ALL PASS (.klut 1D Linear=10x exact; Cubic reproduces x^2 + smoother than Linear; 2D bilinear=2x+3y exact; "
          "Clamp vs Linear extrap distinct+correct; save/load round-trips + content-hash detection; "
          "characterize recovers slope~2/intercept~1; non-ascending/too-short axes rejected)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::klut
