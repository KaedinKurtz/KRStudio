#pragma once
// ===========================================================================
// KLUT -- the .klut LUT / characterized-data STANDARD OBJECT.
//
// A .klut is a content-addressed JSON document describing a lookup table: one or two STRICTLY
// ascending breakpoint axes (each with a name + unit) and a flat, row-major grid of output values
// (with their own quantity + unit, e.g. torque in N*m). It is the persistent form of any
// characterized relationship in the system -- a motor's torque-vs-speed curve, a sensor's
// calibration map, a friction model, a 2D efficiency surface -- authored once and sampled cheaply
// everywhere. Sampling supports Nearest / Linear (bilinear in 2D) / Cubic (Catmull-Rom over the
// breakpoints, natural at the ends; separable in 2D) interpolation, and Clamp / Linear
// extrapolation at the ends. The characterizeFromSamples() helper bakes scattered (xs, ys) data
// into an evenly-spaced 1D table, the standard way raw measurements become a .klut.
//
// Content-addressed exactly like the .knode/.kscene/.krobot family: the document declares
// "format":"klut/1", mints a uuid on first save, and any edit changes the file's SHA-1 content
// hash (recognize-as-different). Unknown MAJOR is refused; an invalid document is refused whole
// (nothing partially applied). Pure data -- no GUI, no GL.
// ===========================================================================
#include <string>
#include <vector>
#include <QString>

namespace krs::klut {

// How to interpolate BETWEEN breakpoints.
enum class Interp {
    Nearest,   // snap to the closest breakpoint value
    Linear,    // piecewise-linear (bilinear in 2D)
    Cubic      // Catmull-Rom over the breakpoints, natural at the ends (separable in 2D)
};

// How to evaluate OUTSIDE the breakpoint range.
enum class Extrap {
    Clamp,     // hold the edge breakpoint's value
    Linear     // linearly extend the end slope (secant of the two end breakpoints)
};

// One input axis: a named/united, STRICTLY ascending list of breakpoints (>= 2 required).
struct Axis {
    std::string name;                  // e.g. "speed"
    std::string unit;                  // e.g. "rad/s"
    std::vector<double> breakpoints;   // STRICTLY ascending
};

// The lookup table itself. 1 or 2 axes are supported. For 2 axes, `values` is row-major over
// (axis0 x axis1): index = i0 * axes[1].breakpoints.size() + i1. Its size must equal the product
// of the axes' breakpoint counts.
struct Lut {
    std::string id;         // stable uuid (minted on save if empty)
    std::string name;       // human name, e.g. "NEMA17 torque curve"
    std::string quantity;   // what the VALUES mean, e.g. "torque"
    std::string unit;       // the values' unit, e.g. "N*m"

    std::vector<Axis>   axes;     // 1 or 2 axes
    std::vector<double> values;   // size == product of axis breakpoint counts (row-major)

    Interp interp = Interp::Linear;
    Extrap extrap = Extrap::Clamp;

    // Sample the table. sample1D requires axes.size()==1; sample2D requires axes.size()==2; sample()
    // dispatches on axes.size(). All honor `interp` in-range and `extrap` out-of-range. An invalid
    // table (or a coord-count mismatch) returns 0.0 rather than reading out of bounds.
    double sample1D(double x) const;
    double sample2D(double x, double y) const;
    double sample(const std::vector<double>& coords) const;

    // Structural validity: 1..2 axes; each axis STRICTLY ascending with >= 2 breakpoints; `values`
    // size equals the product of the axes' breakpoint counts. Writes a reason to *why on failure.
    bool valid(std::string* why = nullptr) const;
};

// Bake scattered/tabular (xs, ys) samples into a 1D Lut of `nBreakpoints` breakpoints, evenly
// spaced across [min(xs), max(xs)]. The (xs, ys) pairs are sorted by x and de-duplicated (a later
// duplicate x wins); each breakpoint's value is the LINEAR interpolation of that sorted curve at
// the breakpoint's x (edges held for x outside the data). `quantity`/`unit` label the values; the
// axis is named "x"/"". The returned Lut has interp=`interp`, extrap=Clamp, and an empty id.
// Degenerate input (< 2 distinct xs, or nBreakpoints < 2) yields an intentionally invalid Lut.
Lut characterizeFromSamples(const std::vector<double>& xs, const std::vector<double>& ys,
                            int nBreakpoints, Interp interp,
                            const std::string& quantity, const std::string& unit);

// Save `lut` to `absPath` as versioned JSON ("format":"klut/1"); mints an id if empty (and writes
// it back into `lut`). Content-addressed. Returns the lut id, or "" on I/O failure or if the lut is
// structurally invalid (nothing is written in that case).
QString saveKLut(Lut& lut, const QString& absPath);

// Load a .klut document. Returns false (out untouched) on a missing/corrupt file, an unknown format
// MAJOR, or a structurally-invalid document (nothing partially applied).
bool loadKLut(const QString& absPath, Lut& out, QString* err = nullptr);

// Headless gate (KRS_KLUT_SELFTEST): a 1D Linear table [0,1,2,3]->[0,10,20,30] samples 10*x exactly
// at and between knots; Cubic reproduces y=x^2 within tol and is smoother than Linear (lower max
// |2nd difference|); a 2D bilinear table on z=2x+3y samples exactly; Extrap Clamp holds the edge
// while Extrap Linear extends the end slope (distinct + correct); saveKLut->loadKLut round-trips
// (axes/values/interp/quantity) and the content hash changes on an edit; characterizeFromSamples on
// a noisy y=2x+1 recovers slope~2 / intercept~1 within tol. NEG-CTRL: a non-ascending / too-short
// axis is rejected by valid(). Returns true iff all pass.
bool runKLutGate();

} // namespace krs::klut
