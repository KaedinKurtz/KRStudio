#pragma once
// ===========================================================================
// REC -- the recording / log DATA FORMAT: a headless, GUI-free DataTable with
// robust CSV and versioned JSON (krec/1) round-trip, plus axis-agnostic
// resampling (Nearest / Linear / Cubic Catmull-Rom).
//
// A DataTable is a set of named, unit-tagged columns (e.g. t[s], temp[C],
// torque[N*m]) over a set of equal-length numeric rows -- the canonical shape a
// black-box recorder / telemetry log / trajectory trace lands in. It is
// deliberately axis-free: any column may serve as the independent axis for a
// resample, so the SAME table can be sampled torque-vs-time or temp-vs-torque
// without transposing. CSV is the human/spreadsheet interchange (header
// "name[unit],name2[unit2],..." with the bracketed unit optional); JSON
// ("format":"krec/1") is the lossless, version-gated on-disk form, matching the
// .k* family's "<family>/<major>" convention and refusing an unknown MAJOR.
//
// Everything here is pure CPU (Eigen-free by construction: plain doubles), so it
// links into headless gates and the offline analysis path with no GUI/GL deps.
// ===========================================================================
#include <string>
#include <vector>
#include <QString>

namespace krs::rec {

// Resampling kernels for DataTable::sampleAt.
//   Nearest -- value of the nearest axis sample (ties -> lower index).
//   Linear  -- straight-line between the two bracketing samples.
//   Cubic   -- Catmull-Rom spline through the bracketing samples (C1, interpolating);
//              non-uniform axis spacing is handled; endpoints reflect a phantom point.
enum class Interp { Nearest, Linear, Cubic };

// One column's identity: a name and an (optional) physical unit (no brackets stored).
struct Column {
    std::string name;
    std::string unit;
};

// A rectangular numeric table: columns describe each field; every row carries
// exactly columns.size() values. Ragged rows are never stored -- addRow ignores a
// wrong-width row, and the CSV reader counts+skips them rather than failing.
struct DataTable {
    std::vector<Column>              columns;
    std::vector<std::vector<double>> rows;    // each row.size() == columns.size()

    // Index of the column named `name`, or -1 if there is no such column.
    int colIndex(const std::string& name) const;

    // Append a row IFF its width matches columns.size() (a mismatched row is dropped).
    void addRow(const std::vector<double>& r);

    // AXIS-AGNOSTIC sampling. Treats column `axisCol` as the independent axis and
    // column `valueCol` as the dependent value; returns the interpolated value where
    // axis == coord. Rows are conceptually sorted by axisCol ascending (the table is
    // not mutated); coord outside the axis range is CLAMPED to the endpoint value.
    // Returns NaN if either column index is out of range or the table is empty.
    // Cubic falls back to Linear when fewer than the samples a spline needs are present.
    double sampleAt(int axisCol, int valueCol, double coord, Interp interp) const;
};

// ---- CSV ---------------------------------------------------------------------
// Header: "name[unit],name2[unit2],..." -- the "[unit]" suffix is OPTIONAL per
// column (a bare "name" yields an empty unit). Body: comma-separated numeric rows.
// Robust reader: blank lines are skipped; a data row whose field count != column
// count is skipped and TALLIED (via *skippedRows) rather than being fatal.
bool writeCSV(const DataTable& t, const QString& absPath);
bool readCSV (const QString& absPath, DataTable& out, QString* err = nullptr, int* skippedRows = nullptr);

// ---- JSON (versioned) --------------------------------------------------------
// Shape: { "format":"krec/1", "columns":[{"name","unit"}], "rows":[[...],...] }.
// readJSON refuses a missing/corrupt file and any format whose MAJOR != 1 (e.g.
// "krec/2"), leaving `out` untouched and setting *err.
bool writeJSON(const DataTable& t, const QString& absPath);
bool readJSON (const QString& absPath, DataTable& out, QString* err = nullptr);

// Headless gate (KRS_REC_SELFTEST): builds a known 3-column table (t[s],temp[C],
// torque[N*m]); CSV write/read round-trips bit-identically incl. bracketed units;
// JSON write/read round-trips; Linear sampleAt hits the analytic mid-value; Cubic
// differs from Linear and is smoother on a curved signal. NEG-CTRLs: a CSV with a
// wrong-column-count row is skipped+counted (skippedRows>0, no crash); a missing
// file returns false; readJSON refuses a "krec/2" file. Returns true iff all pass.
bool runRecGate();

} // namespace krs::rec
