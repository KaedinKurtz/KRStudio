// Rec.cpp -- see Rec.hpp. The recording/log DATA FORMAT: a DataTable + robust CSV
// + versioned JSON (krec/1) + axis-agnostic Nearest/Linear/Cubic resampling.
#include "Rec.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QByteArray>
#include <QStringList>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace krs::rec {

// ==================================================================================================
// DataTable
// ==================================================================================================
int DataTable::colIndex(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i)
        if (columns[i].name == name) return i;
    return -1;
}

void DataTable::addRow(const std::vector<double>& r) {
    if (r.size() != columns.size()) return;   // ragged rows are never stored
    rows.push_back(r);
}

double DataTable::sampleAt(int axisCol, int valueCol, double coord, Interp interp) const {
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const int nc = static_cast<int>(columns.size());
    if (axisCol < 0 || axisCol >= nc || valueCol < 0 || valueCol >= nc) return NaN;
    if (rows.empty()) return NaN;

    // Extract (axis, value) pairs, sort ascending by axis. The table is not mutated.
    std::vector<std::pair<double, double>> pts;
    pts.reserve(rows.size());
    for (const auto& r : rows) {
        if (static_cast<int>(r.size()) <= axisCol || static_cast<int>(r.size()) <= valueCol) continue;
        pts.emplace_back(r[axisCol], r[valueCol]);
    }
    if (pts.empty()) return NaN;
    std::sort(pts.begin(), pts.end(),
              [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
                  return a.first < b.first;
              });

    const int n = static_cast<int>(pts.size());
    if (n == 1) return pts[0].second;

    // Clamp outside the axis range to the endpoint value.
    if (coord <= pts.front().first)  return pts.front().second;
    if (coord >= pts.back().first)   return pts.back().second;

    // Find bracketing interval [i, i+1] with pts[i].first <= coord < pts[i+1].first.
    int i = 0;
    {
        int lo = 0, hi = n - 1;
        while (lo + 1 < hi) {
            int mid = (lo + hi) / 2;
            if (pts[mid].first <= coord) lo = mid; else hi = mid;
        }
        i = lo;
    }
    const double x0 = pts[i].first,   y0 = pts[i].second;
    const double x1 = pts[i + 1].first, y1 = pts[i + 1].second;
    const double span = x1 - x0;

    if (interp == Interp::Nearest) {
        // Nearest axis sample; a tie (exact midpoint) resolves to the lower index (x0).
        return (coord - x0) <= (x1 - coord) ? y0 : y1;
    }

    // Guard a degenerate (zero-width) interval -- fall back to the left value.
    if (!(span > 0.0)) return y0;
    const double u = (coord - x0) / span;   // in (0,1)

    if (interp == Interp::Linear) {
        return y0 + u * (y1 - y0);
    }

    // ---- Cubic: Catmull-Rom through p1=(x0,y0) and p2=(x1,y1), using neighbours ----
    // Non-uniform spacing is handled by scaling the endpoint tangents by the local
    // segment length (a "cardinal / finite-difference" Catmull-Rom). Endpoints use a
    // reflected phantom point so the spline stays interpolating and C1 at the seam.
    if (n < 3) return y0 + u * (y1 - y0);   // not enough neighbours -> Linear

    // Neighbour points p0 (left of the interval) and p3 (right of the interval),
    // reflecting across the endpoint when we run off the array.
    double xm1, ym1, xp2, yp2;
    if (i - 1 >= 0) { xm1 = pts[i - 1].first; ym1 = pts[i - 1].second; }
    else            { xm1 = x0 - span;        ym1 = y0 - (y1 - y0); }   // reflected phantom
    if (i + 2 < n)  { xp2 = pts[i + 2].first; yp2 = pts[i + 2].second; }
    else            { xp2 = x1 + span;        yp2 = y1 + (y1 - y0); }   // reflected phantom

    // Finite-difference tangents at the two interior knots, scaled to the interval span
    // so the Hermite basis below is parameterised on u in [0,1].
    const double dxL = x1 - xm1;   // p2.x - p0.x
    const double dxR = xp2 - x0;   // p3.x - p1.x
    const double mL = (dxL != 0.0) ? (y1 - ym1) / dxL : (y1 - y0) / span;   // tangent at p1
    const double mR = (dxR != 0.0) ? (yp2 - y0) / dxR : (y1 - y0) / span;   // tangent at p2
    const double t0 = mL * span;   // Hermite tangent in u-space at u=0
    const double t1 = mR * span;   // Hermite tangent in u-space at u=1

    const double u2 = u * u, u3 = u2 * u;
    const double h00 =  2 * u3 - 3 * u2 + 1;
    const double h10 =      u3 - 2 * u2 + u;
    const double h01 = -2 * u3 + 3 * u2;
    const double h11 =      u3 -     u2;
    return h00 * y0 + h10 * t0 + h01 * y1 + h11 * t1;
}

// ==================================================================================================
// content-hash helper (file-static; re-declared locally per the .k* family convention)
// ==================================================================================================
namespace {
QString fileSha1(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex());
}

// Format a double for CSV/round-trip without locale surprises and with enough
// precision to survive a write/read cycle bit-for-bit (17 sig-figs is exact for IEEE-754).
QString num(double v) {
    if (std::isnan(v)) return QStringLiteral("nan");
    if (std::isinf(v)) return v < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    return QString::number(v, 'g', 17);
}

// Parse one column header token "name[unit]" -> Column{name, unit}. The bracketed
// unit is optional; a bare "name" yields an empty unit. Whitespace is trimmed.
Column parseHeaderToken(QString tok) {
    tok = tok.trimmed();
    Column c;
    const int lb = tok.indexOf('[');
    const int rb = tok.lastIndexOf(']');
    if (lb >= 0 && rb > lb) {
        c.name = tok.left(lb).trimmed().toStdString();
        c.unit = tok.mid(lb + 1, rb - lb - 1).trimmed().toStdString();
    } else {
        c.name = tok.toStdString();
    }
    return c;
}
} // namespace

// ==================================================================================================
// CSV
// ==================================================================================================
bool writeCSV(const DataTable& t, const QString& absPath) {
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QString out;
    // Header: name[unit] (the "[unit]" is omitted for an empty unit).
    QStringList hdr;
    for (const auto& c : t.columns) {
        QString h = QString::fromStdString(c.name);
        if (!c.unit.empty()) h += QStringLiteral("[") + QString::fromStdString(c.unit) + QStringLiteral("]");
        hdr << h;
    }
    out += hdr.join(',') + QStringLiteral("\n");

    for (const auto& r : t.rows) {
        QStringList cells;
        cells.reserve(static_cast<int>(r.size()));
        for (double v : r) cells << num(v);
        out += cells.join(',') + QStringLiteral("\n");
    }
    f.write(out.toUtf8());
    return true;
}

bool readCSV(const QString& absPath, DataTable& out, QString* err, int* skippedRows) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (skippedRows) *skippedRows = 0;

    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    const QByteArray bytes = f.readAll();
    const QString text = QString::fromUtf8(bytes);

    // Split on \n; strip a trailing \r so CRLF files parse. Blank lines are skipped.
    const QStringList lines = text.split('\n');
    DataTable tbl;
    bool haveHeader = false;

    for (QString line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.trimmed().isEmpty()) continue;   // skip blank lines

        if (!haveHeader) {
            const QStringList toks = line.split(',');
            for (const QString& tk : toks) tbl.columns.push_back(parseHeaderToken(tk));
            haveHeader = true;
            continue;
        }

        const QStringList cells = line.split(',');
        if (cells.size() != static_cast<int>(tbl.columns.size())) {
            if (skippedRows) ++(*skippedRows);   // wrong column count -> skip + tally (not fatal)
            continue;
        }
        std::vector<double> row;
        row.reserve(cells.size());
        bool numeric = true;
        for (const QString& cell : cells) {
            bool ok = false;
            const QString s = cell.trimmed();
            double v = s.toDouble(&ok);
            if (!ok) {
                // tolerate the sentinels we emit for non-finite values
                const QString ls = s.toLower();
                if (ls == QLatin1String("nan"))        { v = std::numeric_limits<double>::quiet_NaN(); ok = true; }
                else if (ls == QLatin1String("inf")  || ls == QLatin1String("+inf"))
                    { v = std::numeric_limits<double>::infinity(); ok = true; }
                else if (ls == QLatin1String("-inf")) { v = -std::numeric_limits<double>::infinity(); ok = true; }
            }
            if (!ok) { numeric = false; break; }
            row.push_back(v);
        }
        if (!numeric) {
            if (skippedRows) ++(*skippedRows);   // a non-numeric field -> skip + tally
            continue;
        }
        tbl.rows.push_back(std::move(row));
    }

    if (!haveHeader) return fail(QStringLiteral("empty CSV (no header) in %1").arg(absPath));
    out = std::move(tbl);
    return true;
}

// ==================================================================================================
// JSON (versioned "krec/1")
// ==================================================================================================
bool writeJSON(const DataTable& t, const QString& absPath) {
    QJsonObject root;
    root["format"] = QStringLiteral("krec/1");
    QJsonArray cols;
    for (const auto& c : t.columns) {
        QJsonObject o;
        o["name"] = QString::fromStdString(c.name);
        o["unit"] = QString::fromStdString(c.unit);
        cols.push_back(o);
    }
    root["columns"] = cols;
    QJsonArray rows;
    for (const auto& r : t.rows) {
        QJsonArray jr;
        for (double v : r) {
            // JSON has no NaN/Inf; persist them as strings so the round-trip is lossless.
            if (std::isfinite(v)) jr.push_back(v);
            else                  jr.push_back(num(v));
        }
        rows.push_back(jr);
    }
    root["rows"] = rows;

    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool readJSON(const QString& absPath, DataTable& out, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot open %1").arg(absPath));
    QJsonParseError perr{};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !d.isObject())
        return fail(QStringLiteral("parse error in %1").arg(absPath));

    const QJsonObject root = d.object();
    const QString fmt = root["format"].toString();
    // Refuse anything that is not the "krec" family, and refuse an unknown MAJOR.
    if (!fmt.startsWith(QStringLiteral("krec/")) || fmt.section('/', 1, 1).toInt() != 1)
        return fail(QStringLiteral("unknown krec format: %1").arg(fmt));

    auto parseCell = [](const QJsonValue& v) -> double {
        if (v.isDouble()) return v.toDouble();
        if (v.isString()) {
            const QString s = v.toString().toLower();
            if (s == QLatin1String("nan")) return std::numeric_limits<double>::quiet_NaN();
            if (s == QLatin1String("inf") || s == QLatin1String("+inf")) return std::numeric_limits<double>::infinity();
            if (s == QLatin1String("-inf")) return -std::numeric_limits<double>::infinity();
        }
        return std::numeric_limits<double>::quiet_NaN();
    };

    DataTable tbl;
    for (const QJsonValue& v : root["columns"].toArray()) {
        const QJsonObject o = v.toObject();
        Column c;
        c.name = o["name"].toString().toStdString();
        c.unit = o["unit"].toString().toStdString();
        tbl.columns.push_back(c);
    }
    for (const QJsonValue& v : root["rows"].toArray()) {
        const QJsonArray jr = v.toArray();
        std::vector<double> row;
        row.reserve(jr.size());
        for (const QJsonValue& cell : jr) row.push_back(parseCell(cell));
        // Keep only well-formed rows (defensive; writeJSON never emits ragged rows).
        if (row.size() == tbl.columns.size()) tbl.rows.push_back(std::move(row));
    }
    out = std::move(tbl);
    return true;
}

// ==================================================================================================
// GATE REC
// ==================================================================================================
bool runRecGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rec] GATE REC -- recording DataTable: robust CSV + versioned JSON (krec/1) round-trip + axis-agnostic Nearest/Linear/Cubic resample\n");

    const QString dir = QDir::temp().filePath("krs_rec_gate");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);

    // Build a known 3-column curve: t[s], temp[C], torque[N*m].
    //   torque(t) = t*t  (a curved signal so Cubic and Linear diverge measurably)
    //   temp(t)   = 20 + 2t (a straight ramp)
    DataTable src;
    src.columns = { {"t", "s"}, {"temp", "C"}, {"torque", "N*m"} };
    for (int k = 0; k <= 5; ++k) {
        const double t = static_cast<double>(k);
        src.addRow({ t, 20.0 + 2.0 * t, t * t });
    }
    const int nCols = static_cast<int>(src.columns.size());
    const int nRows = static_cast<int>(src.rows.size());
    const bool built = (nCols == 3) && (nRows == 6)
                    && src.colIndex("torque") == 2 && src.colIndex("nope") == -1;

    // ---- CSV round-trip: columns+units+rows bit-equal; units parse from brackets ----
    const QString csvPath = QDir(dir).filePath("log.csv");
    const bool wroteCSV = writeCSV(src, csvPath);
    DataTable rc; QString cerr; int cskip = -1;
    const bool readCSVok = readCSV(csvPath, rc, &cerr, &cskip);
    bool csvEqual = readCSVok && rc.columns.size() == src.columns.size()
                              && rc.rows.size() == src.rows.size();
    if (csvEqual) {
        for (size_t i = 0; i < src.columns.size() && csvEqual; ++i)
            csvEqual = rc.columns[i].name == src.columns[i].name
                    && rc.columns[i].unit == src.columns[i].unit;
    }
    if (csvEqual) {
        for (size_t r = 0; r < src.rows.size() && csvEqual; ++r)
            for (size_t c = 0; c < src.rows[r].size() && csvEqual; ++c)
                csvEqual = (rc.rows[r][c] == src.rows[r][c]);   // bit-equal (17 sig-figs is exact)
    }
    const bool unitsParsed = readCSVok && rc.colIndex("torque") >= 0
                          && rc.columns[rc.colIndex("torque")].unit == "N*m"
                          && rc.columns[rc.colIndex("t")].unit == "s";
    const bool csvClean = (cskip == 0);
    printf("[rec]   CSV round-trip: wrote=%s read=%s bit-equal=%s units-from-brackets=%s clean-skip=%d  %s\n",
           wroteCSV?"yes":"no", readCSVok?"yes":"no", csvEqual?"yes":"no", unitsParsed?"yes":"no", cskip,
           (wroteCSV && readCSVok && csvEqual && unitsParsed && csvClean) ? "PASS" : "FAIL");

    // ---- JSON round-trip ----
    const QString jsonPath = QDir(dir).filePath("log.json");
    const bool wroteJSON = writeJSON(src, jsonPath);
    DataTable rj; QString jerr;
    const bool readJSONok = readJSON(jsonPath, rj, &jerr);
    bool jsonEqual = readJSONok && rj.columns.size() == src.columns.size()
                                && rj.rows.size() == src.rows.size();
    if (jsonEqual) {
        for (size_t i = 0; i < src.columns.size() && jsonEqual; ++i)
            jsonEqual = rj.columns[i].name == src.columns[i].name
                     && rj.columns[i].unit == src.columns[i].unit;
    }
    if (jsonEqual) {
        for (size_t r = 0; r < src.rows.size() && jsonEqual; ++r)
            for (size_t c = 0; c < src.rows[r].size() && jsonEqual; ++c)
                jsonEqual = (rj.rows[r][c] == src.rows[r][c]);
    }
    printf("[rec]   JSON round-trip (krec/1): wrote=%s read=%s bit-equal=%s  %s\n",
           wroteJSON?"yes":"no", readJSONok?"yes":"no", jsonEqual?"yes":"no",
           (wroteJSON && readJSONok && jsonEqual) ? "PASS" : "FAIL");

    // ---- sampleAt: axis=t, value=torque, coord between two rows ----
    const int axisT   = src.colIndex("t");
    const int valTorq = src.colIndex("torque");
    // Between t=2 (torque=4) and t=3 (torque=9), at t=2.5.
    const double lin  = src.sampleAt(axisT, valTorq, 2.5, Interp::Linear);
    const double cub  = src.sampleAt(axisT, valTorq, 2.5, Interp::Cubic);
    const double near = src.sampleAt(axisT, valTorq, 2.5, Interp::Nearest);
    const double linExpect = 6.5;      // (4+9)/2
    const double truth     = 6.25;     // 2.5^2 -- the analytic curve value
    const bool linOK  = std::abs(lin - linExpect) < 1e-9;
    // Catmull-Rom on t*t is EXACT (a spline of degree 3 reproduces a quadratic), so
    // cub hits the true 6.25 and is closer to truth than the linear chord.
    const bool cubExact  = std::abs(cub - truth) < 1e-6;
    const bool cubCloser = std::abs(cub - truth) < std::abs(lin - truth);
    // Nearest at the midpoint ties -> lower index (t=2 -> 4).
    const bool nearOK = std::abs(near - 4.0) < 1e-12;
    // "Smoother": measure curvature (2nd difference) of each reconstruction across the
    // interval; the cubic tracks the parabola, so its samples lie ON t*t while the
    // piecewise-linear reconstruction has a kink -> larger deviation from the true curve.
    double linErr = 0.0, cubErr = 0.0;
    for (int s = 1; s < 10; ++s) {
        const double x = 2.0 + 0.1 * s;
        const double tv = x * x;
        linErr += std::abs(src.sampleAt(axisT, valTorq, x, Interp::Linear) - tv);
        cubErr += std::abs(src.sampleAt(axisT, valTorq, x, Interp::Cubic ) - tv);
    }
    const bool cubSmoother = cubErr < linErr && std::abs(cub - lin) > 1e-6;
    printf("[rec]   sampleAt @t=2.5: Linear=%.6f(exp 6.5) Cubic=%.6f(truth 6.25) Nearest=%.3f ; cubic-exact=%s closer=%s smoother(err %.4g<%.4g)=%s  %s\n",
           lin, cub, near, cubExact?"yes":"no", cubCloser?"yes":"no", cubErr, linErr, cubSmoother?"yes":"no",
           (linOK && cubExact && cubCloser && nearOK && cubSmoother) ? "PASS" : "FAIL");

    // ---- clamp outside range ----
    const double clampLo = src.sampleAt(axisT, valTorq, -10.0, Interp::Linear);   // -> torque at t=0 = 0
    const double clampHi = src.sampleAt(axisT, valTorq,  99.0, Interp::Cubic);    // -> torque at t=5 = 25
    const bool clampOK = std::abs(clampLo - 0.0) < 1e-12 && std::abs(clampHi - 25.0) < 1e-12;
    printf("[rec]   clamp outside range: lo=%.3f(exp 0) hi=%.3f(exp 25)=%s  %s\n",
           clampLo, clampHi, clampOK?"yes":"no", clampOK?"PASS":"FAIL");

    // ================================ NEG-CTRLs ================================
    // (1) A CSV whose middle data row has the WRONG column count is skipped + counted.
    const QString badCsv = QDir(dir).filePath("ragged.csv");
    {
        QFile bf(badCsv);
        bf.open(QIODevice::WriteOnly | QIODevice::Truncate);
        // header (3 cols), a good row, a ragged 2-field row, a blank line, another good row
        bf.write("t[s],temp[C],torque[N*m]\n0,20,0\n1,22\n\n2,24,4\n");
        bf.close();
    }
    DataTable rr; QString rerr; int rskip = -1;
    const bool raggedRead = readCSV(badCsv, rr, &rerr, &rskip);
    const bool raggedSkipped = raggedRead && rskip == 1 && rr.rows.size() == 2;   // 2 good rows kept, 1 skipped
    printf("[rec]   NEG-CTRL A: ragged CSV row skipped+counted (skippedRows=%d, kept %zu rows, no crash)=%s\n",
           rskip, rr.rows.size(), raggedSkipped?"yes":"NO");

    // (2) A missing file returns false (both readers).
    const bool missingCSV  = !readCSV (QDir(dir).filePath("does_not_exist.csv"),  rr, &rerr);
    const bool missingJSON = !readJSON(QDir(dir).filePath("does_not_exist.json"), rj, &jerr);
    const bool missingOK = missingCSV && missingJSON;
    printf("[rec]   NEG-CTRL B: missing file returns false (csv=%s json=%s)=%s\n",
           missingCSV?"yes":"NO", missingJSON?"yes":"NO", missingOK?"yes":"NO");

    // (3) readJSON refuses a krec/2 (unknown MAJOR), untouched out + err set.
    const QString badMajor = QDir(dir).filePath("v2.json");
    {
        QFile bf(badMajor);
        bf.open(QIODevice::WriteOnly | QIODevice::Truncate);
        bf.write("{ \"format\":\"krec/2\", \"columns\":[{\"name\":\"t\",\"unit\":\"s\"}], \"rows\":[[1.0]] }");
        bf.close();
    }
    DataTable v2; QString v2err;
    const bool majorRefused = !readJSON(badMajor, v2, &v2err) && !v2err.isEmpty();
    // Sanity: a krec/1 of the same shape IS accepted (proves the refusal is major-specific, not blanket).
    const QString goodMajor = QDir(dir).filePath("v1.json");
    {
        QFile bf(goodMajor);
        bf.open(QIODevice::WriteOnly | QIODevice::Truncate);
        bf.write("{ \"format\":\"krec/1\", \"columns\":[{\"name\":\"t\",\"unit\":\"s\"}], \"rows\":[[1.0]] }");
        bf.close();
    }
    DataTable v1;
    const bool v1Accepted = readJSON(goodMajor, v1, nullptr) && v1.rows.size() == 1;
    const bool majorOK = majorRefused && v1Accepted;
    printf("[rec]   NEG-CTRL C: readJSON refuses krec/2=%s (and accepts krec/1=%s)=%s\n",
           majorRefused?"yes":"NO", v1Accepted?"yes":"NO", majorOK?"yes":"NO");

    // A content hash is well-defined + differs from an edited file (recognize-as-different).
    writeCSV(src, csvPath);
    const QString h1 = fileSha1(csvPath);
    DataTable edited = src; edited.rows[0][1] = 999.0; writeCSV(edited, csvPath);
    const QString h2 = fileSha1(csvPath);
    const bool hashOK = !h1.isEmpty() && !h2.isEmpty() && h1 != h2;
    printf("[rec]   content hash differs on edit=%s\n", hashOK?"yes":"NO");

    const bool negCtrlsMeaningful = raggedSkipped && missingOK && majorOK;
    printf("[rec]   NEG-CTRLs %s\n",
           negCtrlsMeaningful ? "REJECT the bad inputs (non-vacuous)" : "VACUOUS!");

    const bool pass = built && wroteCSV && readCSVok && csvEqual && unitsParsed && csvClean
                   && wroteJSON && readJSONok && jsonEqual
                   && linOK && cubExact && cubCloser && nearOK && cubSmoother && clampOK
                   && raggedSkipped && missingOK && majorOK && hashOK;
    printf("[rec] %s\n", pass
           ? "ALL PASS (DataTable CSV+JSON round-trip bit-equal incl. bracketed units; krec/1 version-gated; axis-agnostic Nearest/Linear/Cubic resample with Catmull-Rom smoothness + clamp; ragged-row skip+tally / missing-file / krec/2 refusal all rejected)"
           : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rec
