// DataNodes.cpp -- wire the .krec / .klut DATA ECOSYSTEM into the node graph.
//
// Four nodes make recorded/characterized data a first-class citizen of the live graph:
//   data_log         -- accumulate (t, Value) rows into an in-node krs::rec::DataTable while Enable
//                       is high (every Nth eval) and flush() them to CSV/JSON under an auto-created folder.
//   data_import      -- lazily load a CSV/JSON DataTable and sampleAt(axisCol,valueCol,interp) at the
//                       Axis input -> Value (real experimental data drives the scene parametrically).
//   lut_sample       -- lazily load a .klut Lut and sample it by axis count (sample1D(X) / sample2D(X,Y)).
//   data_characterize-- bake a CSV column pair into a reusable .klut (characterizeFromSamples + saveKLut).
//
// Each node subclasses Node (see include/NodeHeaders/Node.hpp), pushes its ports in the ctor, reads
// inputs/params in compute(), and emits via setOutput. All are file-local and self-register with the
// NodeFactory. The gate (KRS_DATANODES_SELFTEST) exercises all four against the real krs::rec/krs::klut
// round-trips, modeled on src/Utility/KNode.cpp runKNodeGate.
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "DataNodes.hpp"

#include "Rec.hpp"    // krs::rec::DataTable, writeCSV/readCSV/writeJSON/readJSON, sampleAt, Interp
#include "KLut.hpp"   // krs::klut::Lut, saveKLut/loadKLut, characterizeFromSamples, Interp

#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QCoreApplication>
#include "ProxyComboBox.hpp"    // combo that pops correctly inside a QtNodes proxy widget
#include "PropertyCatalog.hpp"  // catalog().now(): the shared wall-clock time axis for recordings

#include <cstdio>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <memory>

namespace krs::datanodes {
namespace {

// --------------------------------------------------------------------------------------------------
// small helpers shared by the nodes below
// --------------------------------------------------------------------------------------------------

// Map an enum-combo index -> krs::rec::Interp (Nearest/Linear/Cubic).
krs::rec::Interp recInterp(int idx) {
    switch (idx) {
        case 0:  return krs::rec::Interp::Nearest;
        case 2:  return krs::rec::Interp::Cubic;
        default: return krs::rec::Interp::Linear;
    }
}
// Map an enum-combo index -> krs::klut::Interp.
krs::klut::Interp lutInterp(int idx) {
    switch (idx) {
        case 0:  return krs::klut::Interp::Nearest;
        case 2:  return krs::klut::Interp::Cubic;
        default: return krs::klut::Interp::Linear;
    }
}

// --- in-node widget builders (bind a control to a node param / enum-port, so the node is usable
//     from the canvas without wiring a string/int source into it). Each row is a small QWidget. ---
QWidget* labeledRow(const QString& label, QWidget* control) {
    auto* row = new QWidget; auto* h = new QHBoxLayout(row);
    h->setContentsMargins(2, 1, 2, 1); h->setSpacing(4);
    auto* lab = new QLabel(label); lab->setMinimumWidth(58);
    h->addWidget(lab); h->addWidget(control, 1);
    return row;
}
// QLineEdit bound to a STRING param; optional Browse button (fileFilter != "" ; save = getSaveFileName).
QWidget* strRow(Node* n, const QString& label, const std::string& param, const QString& fileFilter = {}, bool save = false) {
    auto* edit = new QLineEdit(QString::fromStdString(n->getParam<std::string>(param, std::string())));
    edit->setMinimumWidth(120);
    QObject::connect(edit, &QLineEdit::editingFinished, [n, param, edit]() {
        n->setParam<std::string>(param, edit->text().toStdString());
    });
    QWidget* control = edit;
    if (!fileFilter.isEmpty()) {
        control = new QWidget; auto* h = new QHBoxLayout(control); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(2);
        auto* browse = new QPushButton(QStringLiteral("...")); browse->setMaximumWidth(28);
        QObject::connect(browse, &QPushButton::clicked, [n, param, edit, fileFilter, save]() {
            const QString p = save ? QFileDialog::getSaveFileName(nullptr, QStringLiteral("Select file"), edit->text(), fileFilter)
                                   : QFileDialog::getOpenFileName(nullptr, QStringLiteral("Select file"), edit->text(), fileFilter);
            if (!p.isEmpty()) { edit->setText(p); n->setParam<std::string>(param, p.toStdString()); }
        });
        h->addWidget(edit, 1); h->addWidget(browse);
    }
    return labeledRow(label, control);
}
// QSpinBox bound to an INT param.
QWidget* intRow(Node* n, const QString& label, const std::string& param, int lo, int hi) {
    auto* sp = new QSpinBox; sp->setRange(lo, hi); sp->setValue(n->getParam<int>(param, lo));
    QObject::connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), [n, param](int v) { n->setParam<int>(param, v); });
    return labeledRow(label, sp);
}
// Combo bound to an ENUM input-port literal (setPortLiteral<int>). MUST be a ProxyComboBox: a plain
// QComboBox popup mis-positions/instantly closes inside the QtNodes graphics proxy (unclickable).
QWidget* enumRow(Node* n, const QString& label, const std::string& enumPort, const QStringList& opts) {
    auto* cb = new ProxyComboBox; cb->addItems(opts);
    cb->setCurrentIndex(n->getInput<int>(enumPort).value_or(0));
    QObject::connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), [n, enumPort](int i) { n->setPortLiteral<int>(enumPort, i); });
    return labeledRow(label, cb);
}
// A momentary button that sets a BOOL param true (a one-shot trigger, e.g. "bake now").
QWidget* boolButtonRow(Node* n, const QString& label, const std::string& param) {
    auto* b = new QPushButton(label);
    QObject::connect(b, &QPushButton::clicked, [n, param]() { n->setParam<bool>(param, true); });
    return b;
}
// Stack rows into one embeddable widget.
QWidget* stackRows(std::initializer_list<QWidget*> rows) {
    auto* w = new QWidget; auto* v = new QVBoxLayout(w); v->setContentsMargins(3, 3, 3, 3); v->setSpacing(2);
    for (auto* r : rows) v->addWidget(r);
    return w;
}

// ==================================================================================================
// 1) data_log -- the LOG node.
//    Accumulates (t, Value) into an internal DataTable while Enable is high (every Nth eval), and
//    flush() writes the whole table once to a dedicated log folder (created on first flush). We do
//    NOT touch the file per eval -- accumulate, then flush.
// ==================================================================================================
class DataLogNode : public Node {
public:
    DataLogNode() {
        m_id = "data_log";
        m_ports.push_back({ "Value",  { "double", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Enable", { "bool",   "unitless" }, Port::Direction::Input,  this });
        addEnumInputPort("Format", { "CSV", "JSON" });                              // 0=CSV 1=JSON

        setPortLiteral<double>("Value", 0.0);
        setPortLiteral<bool>("Enable", false);

        setParam<std::string>("folder", "logs");
        setParam<std::string>("file",   "log");
        setParam<int>("decimate", 1);

        m_table.columns.push_back({ "t",     "s"        });
        m_table.columns.push_back({ "value", "unitless" });
    }

    QWidget* createCustomWidget() override {
        // Live status + a manual flush: rows accumulate while Enable is high; the file is written on
        // the Enable FALLING EDGE automatically, or on demand here.
        auto* status = new QLabel(QStringLiteral("0 rows"));
        auto* flushBtn = new QPushButton(QStringLiteral("Write file now"));
        QObject::connect(flushBtn, &QPushButton::clicked, [this, status]() {
            const QString p = flush();
            status->setText(p.isEmpty() ? QStringLiteral("write FAILED")
                                        : QStringLiteral("%1 rows -> %2").arg(sampleCount()).arg(p));
            status->setToolTip(p);
        });
        m_statusLabel = status;
        return stackRows({ strRow(this, QStringLiteral("Folder"), "folder"),
                           strRow(this, QStringLiteral("File"),   "file"),
                           enumRow(this, QStringLiteral("Format"), "Format", { QStringLiteral("CSV"), QStringLiteral("JSON") }),
                           intRow(this, QStringLiteral("Every N"), "decimate", 1, 100000),
                           flushBtn, status });
    }

    // Push the live row count into the status label at the capped UI rate (never from compute()).
    bool refreshUi() override {
        if (!m_statusLabel) return false;
        const QString txt = m_lastWritten.isEmpty()
            ? QStringLiteral("%1 rows%2").arg(m_table.rows.size()).arg(m_lastEnable ? QStringLiteral(" (recording)") : QString())
            : QStringLiteral("%1 rows -> %2").arg(m_table.rows.size()).arg(m_lastWritten);
        if (txt == m_statusLabel->text()) return false;
        m_statusLabel->setText(txt);
        return true;
    }

    bool needsExecutionControls() const override { return false; }
    bool isPureInputFunction()      const override { return false; }               // stateful accumulator

    void compute() override {
        const double value  = getInputD("Value", 0.0);
        const bool   enable = getInput<bool>("Enable").value_or(false);
        int decimate = getParam<int>("decimate", 1);
        if (decimate < 1) decimate = 1;

        if (enable) {
            if ((m_evalIndex % decimate) == 0) {
                // Time axis: the catalog's WALL-CLOCK now() (shared with the Data Recorder panel)
                // when it advances; a synthetic increment otherwise (headless gates, no publisher).
                const double catNow = krs::twin::catalog().now();
                if (catNow > m_time) m_time = catNow; else m_time += kDt;
                m_table.addRow({ m_time, value });
            }
            ++m_evalIndex;
        }
        // Enable FALLING EDGE -> auto-flush: stopping a recording writes the file without any extra
        // click (the intuitive "I logged it, where is it?" contract).
        if (m_lastEnable && !enable && !m_table.rows.empty()) m_lastWritten = flush();
        m_lastEnable = enable;
        setOutput<double>("Count", double(m_table.rows.size()));
    }

    // Write the accumulated table to <folder>/<file>.<csv|json>. Creates the folder on first flush
    // (the dedicated log folder generated on first log). A RELATIVE folder resolves against the
    // application directory (predictable: logs land next to the exe), not the launch CWD. Returns
    // the absolute path written, or "".
    QString flush() {
        const std::string folder = getParam<std::string>("folder", "logs");
        const std::string file   = getParam<std::string>("file",   "log");
        const int fmt            = getInput<int>("Format").value_or(0);            // 0=CSV 1=JSON

        QString folderQ = QString::fromStdString(folder);
        if (QDir::isRelativePath(folderQ) && QCoreApplication::instance())
            folderQ = QDir(QCoreApplication::applicationDirPath()).filePath(folderQ);
        QDir dir(folderQ);
        dir.mkpath(".");                                                           // folder auto-created here
        const QString ext  = (fmt == 1) ? QStringLiteral(".json") : QStringLiteral(".csv");
        const QString path = dir.absoluteFilePath(QString::fromStdString(file) + ext);

        const bool ok = (fmt == 1) ? krs::rec::writeJSON(m_table, path)
                                   : krs::rec::writeCSV(m_table, path);
        if (ok) m_lastWritten = path;
        return ok ? path : QString();
    }

    int sampleCount() const { return int(m_table.rows.size()); }

private:
    static constexpr double kDt = 0.01;   // fixed synthetic timestep between evals
    krs::rec::DataTable m_table;
    double m_time      = 0.0;
    long long m_evalIndex = 0;
    bool m_lastEnable = false;            // falling-edge detector for auto-flush
    QString m_lastWritten;                // last file written (shown in the node UI)
    QLabel* m_statusLabel = nullptr;      // owned by the embedded widget
};

// ==================================================================================================
// 2) data_import -- the IMPORTER node.
//    Lazily loads a CSV/JSON DataTable from the `file` param (reloads if the path changed), then
//    sampleAt(axisCol,valueCol,interp) at the Axis input -> Value. Real experimental data drives
//    the scene. A missing/corrupt file leaves the table empty -> sampleAt returns NaN, which we
//    normalize to a defined 0.0 output (never crashes).
// ==================================================================================================
class DataImportNode : public Node {
public:
    DataImportNode() {
        m_id = "data_import";
        m_ports.push_back({ "Axis",  { "double", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Value", { "double", "unitless" }, Port::Direction::Output, this });
        addEnumInputPort("Interp", { "Nearest", "Linear", "Cubic" });              // 0/1/2

        setPortLiteral<double>("Axis", 0.0);
        setPortLiteral<int>("Interp", 1);                                          // default Linear

        setParam<std::string>("file", "");
        setParam<int>("axisCol",  0);
        setParam<int>("valueCol", 1);
    }

    QWidget* createCustomWidget() override {
        return stackRows({ strRow(this, QStringLiteral("File"), "file", QStringLiteral("Data (*.csv *.json)")),
                           intRow(this, QStringLiteral("Axis col"),  "axisCol",  0, 64),
                           intRow(this, QStringLiteral("Value col"), "valueCol", 0, 64),
                           enumRow(this, QStringLiteral("Interp"), "Interp", { QStringLiteral("Nearest"), QStringLiteral("Linear"), QStringLiteral("Cubic") }) });
    }

    // Value = f(Axis) only GIVEN a file -- an external resource, so the output is NOT a pure
    // function of the editable inputs (GATE INPUT-BIND: without a file the Axis widget provably
    // cannot drive Value, which is correct behavior, not a binding defect).
    bool isPureInputFunction() const override { return false; }

    void compute() override {
        ensureLoaded();
        const int    axisCol  = getParam<int>("axisCol",  0);
        const int    valueCol = getParam<int>("valueCol", 1);
        const int    interp   = getInput<int>("Interp").value_or(1);
        const double axis     = getInputD("Axis", 0.0);

        double v = m_table.rows.empty()
                       ? std::numeric_limits<double>::quiet_NaN()
                       : m_table.sampleAt(axisCol, valueCol, axis, recInterp(interp));
        if (!std::isfinite(v)) v = 0.0;                                            // defined default (no crash)
        setOutput<double>("Value", v);
    }

private:
    // Lazily (re)load the table when the file param changes. A failed load empties the cache so the
    // node keeps producing the defined default rather than sampling stale data.
    void ensureLoaded() {
        const std::string file = getParam<std::string>("file", "");
        if (file == m_loadedPath && m_loaded) return;
        m_loadedPath = file;
        m_table = {};
        m_loaded = false;
        if (file.empty()) return;
        const QString p = QString::fromStdString(file);
        QString err;
        bool ok = false;
        if (p.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
            ok = krs::rec::readJSON(p, m_table, &err);
        else
            ok = krs::rec::readCSV(p, m_table, &err, nullptr);
        if (!ok) m_table = {};                                                     // missing/corrupt -> empty
        m_loaded = true;                                                           // "attempted"; empty table => default
    }

    krs::rec::DataTable m_table;
    std::string m_loadedPath;
    bool m_loaded = false;
};

// ==================================================================================================
// 3) lut_sample -- the LUT node.
//    Lazily loads a .klut Lut from the `file` param and samples it by axis count: sample1D(X) for a
//    1-axis table, sample2D(X,Y) for a 2-axis table -> Value. A missing/invalid file -> 0.0.
// ==================================================================================================
class LutSampleNode : public Node {
public:
    LutSampleNode() {
        m_id = "lut_sample";
        m_ports.push_back({ "X",     { "double", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Y",     { "double", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Value", { "double", "unitless" }, Port::Direction::Output, this });

        setPortLiteral<double>("X", 0.0);
        setPortLiteral<double>("Y", 0.0);
        setParam<std::string>("file", "");
    }

    QWidget* createCustomWidget() override {
        return stackRows({ strRow(this, QStringLiteral("LUT"), "file", QStringLiteral("LUT (*.klut)")) });
    }

    // Value = f(X,Y) only GIVEN a .klut -- an external resource, so NOT a pure function of the
    // editable inputs (GATE INPUT-BIND: no file -> X/Y widgets provably cannot drive Value).
    bool isPureInputFunction() const override { return false; }

    void compute() override {
        ensureLoaded();
        const double x = getInputD("X", 0.0);
        const double y = getInputD("Y", 0.0);

        double v = 0.0;
        if (m_loaded && m_lut.valid()) {
            v = (m_lut.axes.size() >= 2) ? m_lut.sample2D(x, y) : m_lut.sample1D(x);
        }
        if (!std::isfinite(v)) v = 0.0;
        setOutput<double>("Value", v);
    }

private:
    void ensureLoaded() {
        const std::string file = getParam<std::string>("file", "");
        if (file == m_loadedPath && m_loaded) return;
        m_loadedPath = file;
        m_lut = {};
        m_loaded = false;
        if (file.empty()) return;
        QString err;
        if (krs::klut::loadKLut(QString::fromStdString(file), m_lut, &err))
            m_loaded = true;                                                       // else m_loaded stays false -> 0.0
    }

    krs::klut::Lut m_lut;
    std::string m_loadedPath;
    bool m_loaded = false;
};

// ==================================================================================================
// 4) data_characterize -- the CHARACTERIZE node.
//    On a bake request (the `bake` param true, or a rising Trigger edge), loads inCsv, pulls
//    axisCol/valueCol into (xs,ys), calls characterizeFromSamples(...), and saveKLut()s to outKlut.
//    Turns recorded data into a reusable .klut. Output Baked(bool) reflects the last bake result.
// ==================================================================================================
class DataCharacterizeNode : public Node {
public:
    DataCharacterizeNode() {
        m_id = "data_characterize";
        m_ports.push_back({ "Baked", { "bool", "unitless" }, Port::Direction::Output, this });
        addEnumInputPort("Interp", { "Nearest", "Linear", "Cubic" });              // 0/1/2

        setPortLiteral<int>("Interp", 1);                                          // default Linear
        setParam<std::string>("inCsv",   "");
        setParam<std::string>("outKlut", "");
        setParam<int>("axisCol",       0);
        setParam<int>("valueCol",      1);
        setParam<int>("nBreakpoints", 16);
        setParam<bool>("bake",       false);
    }

    QWidget* createCustomWidget() override {
        return stackRows({ strRow(this, QStringLiteral("In CSV"), "inCsv", QStringLiteral("Data (*.csv *.json)")),
                           strRow(this, QStringLiteral("Out LUT"), "outKlut", QStringLiteral("LUT (*.klut)"), /*save*/true),
                           intRow(this, QStringLiteral("Axis col"),  "axisCol",  0, 64),
                           intRow(this, QStringLiteral("Value col"), "valueCol", 0, 64),
                           intRow(this, QStringLiteral("Breakpts"),  "nBreakpoints", 2, 4096),
                           enumRow(this, QStringLiteral("Interp"), "Interp", { QStringLiteral("Nearest"), QStringLiteral("Linear"), QStringLiteral("Cubic") }),
                           boolButtonRow(this, QStringLiteral("Bake now"), "bake") });
    }

    bool needsExecutionControls() const override { return false; }
    bool isPureInputFunction()      const override { return false; }               // fires only on a bake request

    void compute() override {
        // A rising Trigger edge OR a latched `bake` param requests one bake.
        const bool trig       = getInput<bool>("Trigger").value_or(false);
        const bool risingEdge = trig && !m_lastTrig;
        m_lastTrig = trig;
        const bool bakeReq    = getParam<bool>("bake", false) || risingEdge;

        if (bakeReq) m_baked = bakeNow();
        setOutput<bool>("Baked", m_baked);
    }

    // Public so the gate (and a future button) can drive one bake regardless of the trigger wiring.
    bool bakeNow() {
        const std::string inCsv   = getParam<std::string>("inCsv",   "");
        const std::string outKlut = getParam<std::string>("outKlut", "");
        const int axisCol         = getParam<int>("axisCol",  0);
        const int valueCol        = getParam<int>("valueCol", 1);
        const int nbp             = getParam<int>("nBreakpoints", 16);
        const int interp          = getInput<int>("Interp").value_or(1);
        if (inCsv.empty() || outKlut.empty()) return false;

        krs::rec::DataTable t; QString err;
        if (!krs::rec::readCSV(QString::fromStdString(inCsv), t, &err, nullptr)) return false;
        if (axisCol < 0 || valueCol < 0 ||
            axisCol >= int(t.columns.size()) || valueCol >= int(t.columns.size())) return false;

        std::vector<double> xs, ys;
        xs.reserve(t.rows.size());
        ys.reserve(t.rows.size());
        for (const auto& r : t.rows) {
            if (int(r.size()) <= axisCol || int(r.size()) <= valueCol) continue;
            xs.push_back(r[axisCol]);
            ys.push_back(r[valueCol]);
        }

        krs::klut::Lut lut = krs::klut::characterizeFromSamples(
            xs, ys, nbp, lutInterp(interp),
            t.columns[valueCol].name, t.columns[valueCol].unit);
        const QString id = krs::klut::saveKLut(lut, QString::fromStdString(outKlut));
        return !id.isEmpty();
    }

private:
    bool m_lastTrig = false;
    bool m_baked    = false;
};

// --------------------------------------------------------------------------------------------------
// factory registration -- one registrar per node type
// --------------------------------------------------------------------------------------------------
struct DataNodesRegistrar {
    DataNodesRegistrar() {
        auto& F = NodeFactory::instance();
        F.registerNodeType("data_log",
            { "Data Log", "Data/IO",
              "Accumulate (t, Value) rows while Enable is high and flush() to CSV/JSON in a log folder." },
            []() { return std::make_unique<DataLogNode>(); });
        F.registerNodeType("data_import",
            { "Data Import", "Data/IO",
              "Sample a recorded CSV/JSON table (axisCol,valueCol,interp) at the Axis input -> Value." },
            []() { return std::make_unique<DataImportNode>(); });
        F.registerNodeType("lut_sample",
            { "LUT Sample", "Data/IO",
              "Sample a .klut lookup table by axis count: sample1D(X) or sample2D(X,Y) -> Value." },
            []() { return std::make_unique<LutSampleNode>(); });
        F.registerNodeType("data_characterize",
            { "Data Characterize", "Data/IO",
              "Bake a CSV column pair into a reusable .klut (characterizeFromSamples + saveKLut)." },
            []() { return std::make_unique<DataCharacterizeNode>(); });
    }
};
static DataNodesRegistrar g_dataNodesRegistrar;

// read an output double straight off a node's port packet (post-process()).
double readOutD(Node& n, const char* port) {
    if (const PortDataPacket* pk = n.outputPacket(port)) {
        try { return std::any_cast<double>(pk->data); } catch (...) {}
        try { return double(std::any_cast<float>(pk->data)); } catch (...) {}
        try { return double(std::any_cast<int>(pk->data)); } catch (...) {}
    }
    return 0.0;
}
// read an output bool.
bool readOutB(Node& n, const char* port) {
    if (const PortDataPacket* pk = n.outputPacket(port)) {
        try { return std::any_cast<bool>(pk->data); } catch (...) {}
    }
    return false;
}

} // namespace

// ==================================================================================================
// GATE DATANODES
// ==================================================================================================
bool runDataNodesGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[datanodes] GATE DATANODES -- log / import / lut_sample / characterize wire the .krec/.klut ecosystem into the graph\n");

    const QString root = QDir::temp().filePath("krs_datanodes_gate");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    auto& F = NodeFactory::instance();

    // ---- (1) data_log: Enable=true logs a ramp for N evals then flush() -> file exists under an
    //          auto-created logs/ folder with ~N rows; Enable=false logs nothing (neg-ctrl). --------
    bool logPass = false;
    {
        const int N = 20;
        const QString logFolder = QDir(root).filePath("logs");
        QDir(logFolder).removeRecursively();                                       // ensure it does NOT pre-exist

        auto n = F.createNode("data_log");
        auto* logNode = dynamic_cast<DataLogNode*>(n.get());
        bool posOk = false, negOk = false; QString wrote;
        long rowsPos = 0;
        if (logNode) {
            logNode->setParam<std::string>("folder", logFolder.toStdString());
            logNode->setParam<std::string>("file",   "ramp");
            logNode->setPortLiteral<int>("Format", 0);                             // CSV
            logNode->setPortLiteral<bool>("Enable", true);
            for (int i = 0; i < N; ++i) { logNode->setPortLiteral<double>("Value", double(i)); logNode->process(); }
            rowsPos = logNode->sampleCount();
            const bool folderBefore = QDir(logFolder).exists();                    // must NOT pre-exist (removed above)
            wrote = logNode->flush();                                              // flush() creates the folder
            const bool folderAutoCreated = !folderBefore && QDir(logFolder).exists();
            posOk = folderAutoCreated && !wrote.isEmpty() && QFile::exists(wrote)
                 && rowsPos == N;

            // NEG-CTRL: a fresh node with Enable=false logs nothing.
            auto n2 = F.createNode("data_log");
            auto* logNode2 = dynamic_cast<DataLogNode*>(n2.get());
            if (logNode2) {
                logNode2->setPortLiteral<bool>("Enable", false);
                for (int i = 0; i < N; ++i) { logNode2->setPortLiteral<double>("Value", double(i)); logNode2->process(); }
                negOk = (logNode2->sampleCount() == 0);
            }
        }
        logPass = posOk && negOk;
        printf("[datanodes]   (1) data_log: folder auto-created + flushed '%s' with %ld rows (=N=20)=%s ; Enable=false logs 0 (neg)=%s  %s\n",
               wrote.isEmpty() ? "(none)" : QFileInfo(wrote).fileName().toUtf8().constData(),
               rowsPos, posOk ? "yes" : "NO", negOk ? "yes" : "NO", logPass ? "PASS" : "FAIL");
    }

    // ---- (2) data_import: point at a known CSV, feed an Axis between samples -> interpolated truth. -
    bool importPass = false;
    {
        // Build a known table t=[0,1,2,3], value=10*t via krs::rec, write CSV.
        krs::rec::DataTable src;
        src.columns.push_back({ "t",     "s"  });
        src.columns.push_back({ "value", "u"  });
        for (int i = 0; i <= 3; ++i) src.addRow({ double(i), 10.0 * i });
        const QString csv = QDir(root).filePath("ramp10.csv");
        const bool wroteCsv = krs::rec::writeCSV(src, csv);

        auto n = F.createNode("data_import");
        double got = 0.0; const double truth = 25.0;                               // 10*2.5 (Linear)
        if (n && wroteCsv) {
            n->setParam<std::string>("file", csv.toStdString());
            n->setParam<int>("axisCol", 0);
            n->setParam<int>("valueCol", 1);
            n->setPortLiteral<int>("Interp", 1);                                   // Linear
            n->setPortLiteral<double>("Axis", 2.5);
            n->process();
            got = readOutD(*n, "Value");
        }
        importPass = wroteCsv && std::abs(got - truth) < 1e-9;
        printf("[datanodes]   (2) data_import: sample(t=2.5) -> %.4f (truth 10*t=%.4f)  %s\n",
               got, truth, importPass ? "PASS" : "FAIL");
    }

    // ---- (3) lut_sample: author a .klut via krs::klut, point a node at it -> equals sample1D. -------
    bool lutPass = false;
    {
        krs::klut::Lut lut;
        lut.name = "gate_line"; lut.quantity = "torque"; lut.unit = "N*m";
        lut.axes.push_back({ "x", "", { 0.0, 1.0, 2.0, 3.0 } });
        lut.values = { 0.0, 10.0, 20.0, 30.0 };                                    // y = 10x
        lut.interp = krs::klut::Interp::Linear;
        lut.extrap = krs::klut::Extrap::Clamp;
        const QString klut = QDir(root).filePath("line.klut");
        const QString id   = krs::klut::saveKLut(lut, klut);

        const double x = 1.7;
        const double truth = lut.sample1D(x);                                      // the object we authored
        auto n = F.createNode("lut_sample");
        double got = 0.0;
        if (n && !id.isEmpty()) {
            n->setParam<std::string>("file", klut.toStdString());
            n->setPortLiteral<double>("X", x);
            n->process();
            got = readOutD(*n, "Value");
        }
        lutPass = !id.isEmpty() && std::abs(got - truth) < 1e-9 && std::abs(truth - 17.0) < 1e-9;
        printf("[datanodes]   (3) lut_sample: node(x=1.7) -> %.4f == sample1D=%.4f (=17)  %s\n",
               got, truth, lutPass ? "PASS" : "FAIL");
    }

    // ---- (4) data_characterize: read a CSV of y=2x+1, bake a .klut, reload+sample -> ~2x+1. ---------
    bool charPass = false;
    {
        krs::rec::DataTable src;
        src.columns.push_back({ "x", "" });
        src.columns.push_back({ "y", "" });
        for (int i = 0; i <= 10; ++i) { const double x = double(i); src.addRow({ x, 2.0 * x + 1.0 }); }
        const QString inCsv   = QDir(root).filePath("line_2x1.csv");
        const QString outKlut = QDir(root).filePath("baked_2x1.klut");
        const bool wroteCsv = krs::rec::writeCSV(src, inCsv);

        auto n = F.createNode("data_characterize");
        auto* ch = dynamic_cast<DataCharacterizeNode*>(n.get());
        bool baked = false; double s3 = 0.0, s7 = 0.0;
        if (ch && wroteCsv) {
            ch->setParam<std::string>("inCsv",   inCsv.toStdString());
            ch->setParam<std::string>("outKlut", outKlut.toStdString());
            ch->setParam<int>("axisCol", 0);
            ch->setParam<int>("valueCol", 1);
            ch->setParam<int>("nBreakpoints", 16);
            ch->setPortLiteral<int>("Interp", 1);                                  // Linear
            ch->setParam<bool>("bake", true);
            ch->process();
            baked = readOutB(*ch, "Baked");

            // Reload the baked .klut through lut_sample and check ~2x+1 at two interior points.
            krs::klut::Lut back; QString lerr;
            if (baked && krs::klut::loadKLut(outKlut, back, &lerr)) {
                s3 = back.sample1D(3.0);
                s7 = back.sample1D(7.0);
            }
        }
        charPass = baked && std::abs(s3 - 7.0) < 1e-6 && std::abs(s7 - 15.0) < 1e-6;
        printf("[datanodes]   (4) data_characterize: baked=%s ; reload sample1D(3)=%.4f(~7) sample1D(7)=%.4f(~15)  %s\n",
               baked ? "yes" : "NO", s3, s7, charPass ? "PASS" : "FAIL");
    }

    // ---- NEG-CTRL: data_import on a MISSING file outputs a defined default (0) and does not crash. --
    bool negPass = false;
    {
        auto n = F.createNode("data_import");
        double got = -999.0;
        if (n) {
            n->setParam<std::string>("file", QDir(root).filePath("does_not_exist.csv").toStdString());
            n->setPortLiteral<double>("Axis", 1.23);
            n->process();                                                          // must not crash
            got = readOutD(*n, "Value");
        }
        negPass = (got == 0.0);
        printf("[datanodes]   NEG-CTRL: import(missing file) -> %.4f defined default (0), no crash=%s  %s\n",
               got, negPass ? "yes" : "NO", negPass ? "REJECTS(non-vacuous)" : "VACUOUS!");
    }

    const bool pass = logPass && importPass && lutPass && charPass && negPass;
    printf("[datanodes] %s\n", pass
        ? "ALL PASS (data_log accumulates+flushes to an auto-created folder; data_import samples real CSV truth; lut_sample reproduces .klut sample1D; data_characterize bakes y=2x+1 into a reusable .klut; missing-file import yields a defined default)"
        : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::datanodes
