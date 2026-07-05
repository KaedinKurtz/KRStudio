// DataRecorderPanel.cpp -- see DataRecorderPanel.hpp. The live data-logging
// oscilloscope/recorder surface: a checkable channel tree over the published-state
// catalog, a pure-QPainter strip chart, an arm/trigger/record control row, and
// CSV/JSON save + .klut bake of the recorded DataTable.
//
// GUI-only (eyes-on), no gate. Everything is driven off two QTimers and the
// process-wide krs::twin::catalog(); no global state is added.

#include "DataRecorderPanel.hpp"

#include "PropertyCatalog.hpp"   // krs::twin::catalog(), ObjectEntry, PropertyEntry, PropType
#include "KLut.hpp"              // krs::klut::characterizeFromSamples / saveKLut

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTimer>
#include <QScrollBar>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// Channel identity is stashed on each leaf QTreeWidgetItem via item data roles so
// the tree IS the source of truth for "what is a channel / is it checked".
// ---------------------------------------------------------------------------
namespace {
constexpr int kRoleObjectId = Qt::UserRole + 1;
constexpr int kRolePropName = Qt::UserRole + 2;
constexpr int kRoleComp     = Qt::UserRole + 3;  // -1 sentinel unused; 0..2, isComp flag separate
constexpr int kRoleIsComp   = Qt::UserRole + 4;
constexpr int kRoleUnit     = Qt::UserRole + 5;
constexpr int kRoleLabel    = Qt::UserRole + 6;
constexpr int kRoleIsLeaf   = Qt::UserRole + 7;

// A fixed, high-contrast palette cycled per drawn channel.
const QColor kChannelColors[] = {
    QColor(0x4F, 0xC3, 0xF7), // light blue
    QColor(0xFF, 0xB7, 0x4D), // amber
    QColor(0x81, 0xC7, 0x84), // green
    QColor(0xE5, 0x73, 0x73), // red
    QColor(0xBA, 0x68, 0xC8), // purple
    QColor(0xFF, 0xF1, 0x76), // yellow
    QColor(0x4D, 0xB6, 0xAC), // teal
    QColor(0xF0, 0x62, 0x92), // pink
};
constexpr int kChannelColorCount = int(sizeof(kChannelColors) / sizeof(kChannelColors[0]));

QColor channelColor(int i) { return kChannelColors[((i % kChannelColorCount) + kChannelColorCount) % kChannelColorCount]; }

// Suffix for a component index. Vec3 stores {x,y,z}; Quat stores {w,x,y,z}
// (see publishSceneState) so its index 0 is the scalar w, not x. `isQuat` picks the
// correct labelling so a quaternion channel isn't mislabelled.
const char* compSuffix(int c, bool isQuat) {
    if (isQuat) return c == 0 ? ".w" : c == 1 ? ".x" : c == 2 ? ".y" : ".z";
    return c == 0 ? ".x" : c == 1 ? ".y" : c == 2 ? ".z" : ".w";
}
} // namespace

// ===========================================================================
// DataRecorderChartView -- pure-QPainter strip chart (nested view widget).
// Non-owning: it points at the panel's DataTable and a list of (columnIndex,label)
// pairs to draw. update() is called from the record tick.
// ===========================================================================
class DataRecorderChartView : public QWidget
{
public:
    explicit DataRecorderChartView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(220, 160);
        setAutoFillBackground(true);
    }

    // Point the view at the recording buffer and the currently-checked channels.
    // `cols` are column indices into `table` (>=1; column 0 is time). Labels/colors
    // parallel `cols`.
    void setSource(const krs::rec::DataTable* table,
                   std::vector<int> cols,
                   std::vector<QString> labels,
                   std::vector<QColor> colors) {
        m_table = table;
        m_cols = std::move(cols);
        m_labels = std::move(labels);
        m_colors = std::move(colors);
    }

    void setWindowRows(int n) { m_windowRows = std::max(2, n); }
    int  windowRows() const { return m_windowRows; }
    // Back-scroll: how many rows BACK from the live end the window starts (0 = follow live).
    void setScrollBack(int rowsFromEnd) { m_scrollBack = std::max(0, rowsFromEnd); }
    // Y scaling: auto (min/max over the visible window + margin% headroom) or manual bounds.
    void setYAuto(double marginPct) { m_autoY = true; m_marginPct = std::max(0.0, marginPct); }
    void setYManual(double lo, double hi) { m_autoY = false; m_manMin = std::min(lo, hi); m_manMax = std::max(lo, hi); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect full = rect();
        // Background.
        p.fillRect(full, QColor(0x1E, 0x1E, 0x1E));

        // Plot area (leave a margin for axis labels + legend).
        const int mL = 54, mR = 12, mT = 12, mB = 22;
        QRect plot(full.left() + mL, full.top() + mT,
                   std::max(1, full.width() - mL - mR),
                   std::max(1, full.height() - mT - mB));

        // Border.
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.drawRect(plot.adjusted(0, 0, -1, -1));

        const bool haveData = m_table && !m_table->rows.empty() && !m_cols.empty();

        if (!haveData) {
            p.setPen(QColor(0x88, 0x88, 0x88));
            p.drawText(plot, Qt::AlignCenter, QStringLiteral("No data -- check channels and Record"));
            return;
        }

        const int totalRows = int(m_table->rows.size());
        const int nCols = int(m_table->columns.size());
        // FIFO window with back-scroll: the window ends m_scrollBack rows before the live end.
        const int windowEnd = std::max(1, totalRows - std::min(m_scrollBack, std::max(0, totalRows - 1)));
        const int first = std::max(0, windowEnd - m_windowRows);
        const int totalRowsW = windowEnd;                 // paint loops run [first, totalRowsW)
        const int visRows = totalRowsW - first;

        // --- X range (time column 0), guard against a single row / zero span ---
        double tMin = std::numeric_limits<double>::infinity();
        double tMax = -std::numeric_limits<double>::infinity();
        for (int r = first; r < totalRowsW; ++r) {
            const double t = m_table->rows[r].empty() ? 0.0 : m_table->rows[r][0];
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
        }
        if (!std::isfinite(tMin) || !std::isfinite(tMax)) { tMin = 0.0; tMax = 1.0; }
        double tSpan = tMax - tMin;
        if (tSpan < 1e-12) { tSpan = 1.0; tMax = tMin + 1.0; } // one row / flat time

        // --- Y range: manual bounds, or auto over the VISIBLE window + margin% headroom ---
        double yMin, yMax;
        if (!m_autoY) {
            yMin = m_manMin; yMax = m_manMax;
        } else {
            yMin = std::numeric_limits<double>::infinity();
            yMax = -std::numeric_limits<double>::infinity();
            for (int ci : m_cols) {
                if (ci < 0 || ci >= nCols) continue;
                for (int r = first; r < totalRowsW; ++r) {
                    if (ci >= int(m_table->rows[r].size())) continue;
                    const double y = m_table->rows[r][ci];
                    if (!std::isfinite(y)) continue;
                    yMin = std::min(yMin, y);
                    yMax = std::max(yMax, y);
                }
            }
            if (!std::isfinite(yMin) || !std::isfinite(yMax)) { yMin = 0.0; yMax = 1.0; }
            const double m = (yMax - yMin) * (m_marginPct / 100.0);   // headroom above + below
            yMin -= m; yMax += m;
        }
        double ySpan = yMax - yMin;
        if (ySpan < 1e-12) { const double pad = (std::abs(yMax) > 1e-9 ? std::abs(yMax) * 0.5 : 1.0); yMin -= pad; yMax += pad; ySpan = yMax - yMin; }

        auto xOf = [&](double t) -> double {
            return plot.left() + (t - tMin) / tSpan * (plot.width() - 1);
        };
        auto yOf = [&](double v) -> double {
            // invert (screen y grows downward)
            return plot.top() + (1.0 - (v - yMin) / ySpan) * (plot.height() - 1);
        };

        // --- grid (4x4) + axis tick labels ---
        p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1, Qt::DotLine));
        const int gx = 4, gy = 4;
        for (int i = 1; i < gx; ++i) {
            const double x = plot.left() + double(i) / gx * (plot.width() - 1);
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (int i = 1; i < gy; ++i) {
            const double y = plot.top() + double(i) / gy * (plot.height() - 1);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        // Y axis min/max labels.
        p.setPen(QColor(0x99, 0x99, 0x99));
        p.drawText(QRect(full.left(), plot.top() - 6, mL - 4, 14),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(yMax, 'g', 4));
        p.drawText(QRect(full.left(), plot.bottom() - 8, mL - 4, 14),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(yMin, 'g', 4));
        // X axis min/max labels.
        p.drawText(QRect(plot.left(), plot.bottom() + 4, 80, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(tMin, 'g', 5) + "s");
        p.drawText(QRect(plot.right() - 80, plot.bottom() + 4, 80, 16),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(tMax, 'g', 5) + "s");

        // --- polylines, one per channel ---
        QVector<QPointF> pts;
        pts.reserve(visRows);
        for (int k = 0; k < int(m_cols.size()); ++k) {
            const int ci = m_cols[k];
            if (ci < 0 || ci >= nCols) continue;
            const QColor col = (k < int(m_colors.size())) ? m_colors[k] : channelColor(k);

            pts.clear();
            for (int r = first; r < totalRowsW; ++r) {
                const auto& row = m_table->rows[r];
                if (row.empty() || ci >= int(row.size())) continue;
                const double t = row[0];
                const double v = row[ci];
                if (!std::isfinite(v)) continue;
                pts.push_back(QPointF(xOf(t), yOf(v)));
            }
            p.setPen(QPen(col, 1.5));
            if (pts.size() == 1) {
                p.drawEllipse(pts.front(), 2.0, 2.0);   // one-row: a dot, no polyline
            } else if (pts.size() >= 2) {
                p.drawPolyline(pts.constData(), int(pts.size()));
            }
        }

        // --- legend + latest-value readout (top-left of the plot) ---
        int ly = plot.top() + 4;
        const auto& lastRow = m_table->rows.back();
        for (int k = 0; k < int(m_cols.size()); ++k) {
            const int ci = m_cols[k];
            if (ci < 0 || ci >= nCols) continue;
            const QColor col = (k < int(m_colors.size())) ? m_colors[k] : channelColor(k);
            const QString name = (k < int(m_labels.size())) ? m_labels[k] : QString::number(ci);

            QString valTxt = "--";
            if (ci < int(lastRow.size()) && std::isfinite(lastRow[ci]))
                valTxt = QString::number(lastRow[ci], 'g', 5);

            // color swatch
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawRect(plot.left() + 6, ly + 3, 10, 8);
            // label + value
            p.setPen(col);
            p.drawText(plot.left() + 22, ly + 11, name + "  " + valTxt);
            ly += 15;
            if (ly > plot.bottom() - 12) break; // don't overrun the plot
        }
    }

private:
    const krs::rec::DataTable* m_table = nullptr;
    std::vector<int>     m_cols;
    std::vector<QString> m_labels;
    std::vector<QColor>  m_colors;
    int m_windowRows = 600;
    int m_scrollBack = 0;          // rows back from the live end (0 = follow live)
    bool m_autoY = true;
    double m_marginPct = 5.0;      // autoscale headroom (% of span, applied top + bottom)
    double m_manMin = -1.0, m_manMax = 1.0;
};

// ===========================================================================
// DataRecorderPanel
// ===========================================================================
DataRecorderPanel::DataRecorderPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ---------------- top control row ----------------
    auto* controlRow = new QHBoxLayout();
    controlRow->setSpacing(6);

    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    controlRow->addWidget(m_refreshBtn);

    m_arm = new QCheckBox(QStringLiteral("Arm"), this);
    controlRow->addWidget(m_arm);

    controlRow->addWidget(new QLabel(QStringLiteral("Trigger:"), this));
    m_triggerMode = new QComboBox(this);
    m_triggerMode->addItem(QStringLiteral("Manual"));
    m_triggerMode->addItem(QStringLiteral("Rising edge"));
    m_triggerMode->addItem(QStringLiteral("Level >"));
    controlRow->addWidget(m_triggerMode);

    controlRow->addWidget(new QLabel(QStringLiteral("Thresh:"), this));
    m_threshold = new QDoubleSpinBox(this);
    m_threshold->setRange(-1e9, 1e9);
    m_threshold->setDecimals(4);
    m_threshold->setValue(0.0);
    m_threshold->setSingleStep(0.1);
    controlRow->addWidget(m_threshold);

    m_triggerBtn = new QPushButton(QStringLiteral("Trigger"), this);
    controlRow->addWidget(m_triggerBtn);

    m_recordBtn = new QPushButton(QStringLiteral("Record"), this);
    m_recordBtn->setCheckable(true);
    controlRow->addWidget(m_recordBtn);

    m_clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    controlRow->addWidget(m_clearBtn);

    controlRow->addWidget(new QLabel(QStringLiteral("Decimate:"), this));
    m_decimation = new QSpinBox(this);
    m_decimation->setRange(1, 1000);
    m_decimation->setValue(1);
    controlRow->addWidget(m_decimation);

    controlRow->addStretch(1);
    root->addLayout(controlRow);

    // ---------------- chart scale row: FIFO window + Y scaling ----------------
    auto* scaleRow = new QHBoxLayout();
    scaleRow->setSpacing(6);
    scaleRow->addWidget(new QLabel(QStringLiteral("Window (pts):"), this));
    m_windowSpin = new QSpinBox(this);
    m_windowSpin->setRange(10, 1000000);
    m_windowSpin->setValue(600);
    m_windowSpin->setSingleStep(100);
    scaleRow->addWidget(m_windowSpin);
    scaleRow->addWidget(new QLabel(QStringLiteral("Y:"), this));
    m_yMode = new QComboBox(this);
    m_yMode->addItem(QStringLiteral("Auto"));
    m_yMode->addItem(QStringLiteral("Manual"));
    scaleRow->addWidget(m_yMode);
    scaleRow->addWidget(new QLabel(QStringLiteral("Margin %:"), this));
    m_yMargin = new QDoubleSpinBox(this);
    m_yMargin->setRange(0.0, 100.0);
    m_yMargin->setValue(5.0);
    m_yMargin->setSingleStep(1.0);
    scaleRow->addWidget(m_yMargin);
    scaleRow->addWidget(new QLabel(QStringLiteral("Min:"), this));
    m_yMinSpin = new QDoubleSpinBox(this);
    m_yMinSpin->setRange(-1e9, 1e9); m_yMinSpin->setDecimals(4); m_yMinSpin->setValue(-1.0);
    m_yMinSpin->setEnabled(false);
    scaleRow->addWidget(m_yMinSpin);
    scaleRow->addWidget(new QLabel(QStringLiteral("Max:"), this));
    m_yMaxSpin = new QDoubleSpinBox(this);
    m_yMaxSpin->setRange(-1e9, 1e9); m_yMaxSpin->setDecimals(4); m_yMaxSpin->setValue(1.0);
    m_yMaxSpin->setEnabled(false);
    scaleRow->addWidget(m_yMaxSpin);
    scaleRow->addStretch(1);
    root->addLayout(scaleRow);

    // status line
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#bbbbbb; padding:2px 0;"));
    root->addWidget(m_status);

    // ---------------- body: tree | chart ----------------
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_tree = new QTreeWidget(m_splitter);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ QStringLiteral("Channel"), QStringLiteral("Value / Hz") });
    m_tree->setColumnWidth(0, 220);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_splitter->addWidget(m_tree);

    m_chart = new DataRecorderChartView(m_splitter);
    m_splitter->addWidget(m_chart);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({ 300, 520 });
    root->addWidget(m_splitter, 1);

    // back-scroll through the FIFO history (pinned right = follow live).
    m_scroll = new QScrollBar(Qt::Horizontal, this);
    m_scroll->setRange(0, 0);
    root->addWidget(m_scroll);

    // ---------------- bottom actions ----------------
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(6);
    m_saveBtn = new QPushButton(QStringLiteral("Save Recording..."), this);
    m_bakeBtn = new QPushButton(QStringLiteral("Bake LUT..."), this);
    bottomRow->addWidget(m_saveBtn);
    bottomRow->addWidget(m_bakeBtn);
    bottomRow->addStretch(1);
    root->addLayout(bottomRow);

    // ---------------- wiring ----------------
    connect(m_refreshBtn, &QPushButton::clicked, this, &DataRecorderPanel::onRefresh);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DataRecorderPanel::onItemChanged);
    connect(m_recordBtn, &QPushButton::clicked, this, &DataRecorderPanel::onRecordToggle);
    connect(m_triggerBtn, &QPushButton::clicked, this, &DataRecorderPanel::onManualTrigger);
    connect(m_clearBtn, &QPushButton::clicked, this, &DataRecorderPanel::onClear);
    connect(m_saveBtn, &QPushButton::clicked, this, &DataRecorderPanel::onSave);
    connect(m_bakeBtn, &QPushButton::clicked, this, &DataRecorderPanel::onBakeLut);

    // chart scale wiring
    auto applyYMode = [this]() {
        const bool manual = m_yMode->currentIndex() == 1;
        m_yMargin->setEnabled(!manual);
        m_yMinSpin->setEnabled(manual);
        m_yMaxSpin->setEnabled(manual);
        if (manual) m_chart->setYManual(m_yMinSpin->value(), m_yMaxSpin->value());
        else        m_chart->setYAuto(m_yMargin->value());
        m_chart->update();
    };
    connect(m_windowSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) {
        m_chart->setWindowRows(n); syncScrollBar(); m_chart->update();
    });
    connect(m_yMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyYMode);
    connect(m_yMargin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyYMode);
    connect(m_yMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyYMode);
    connect(m_yMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyYMode);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        m_followLive = (v >= m_scroll->maximum());
        m_chart->setScrollBack(m_scroll->maximum() - v);
        m_chart->update();
    });

    // ---------------- timers ----------------
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(50);   // ~20 Hz tree value refresh
    connect(m_liveTimer, &QTimer::timeout, this, &DataRecorderPanel::onLiveTick);
    m_liveTimer->start();

    m_recordTimer = new QTimer(this);
    m_recordTimer->setInterval(20); // ~50 Hz sampling
    connect(m_recordTimer, &QTimer::timeout, this, &DataRecorderPanel::onRecordTick);
    m_recordTimer->start();

    refreshChannels();
    updateStatus();
}

DataRecorderPanel::~DataRecorderPanel() = default;

// ---------------------------------------------------------------------------
void DataRecorderPanel::refreshChannels() { onRefresh(); }

// Rebuild the channel tree from the live catalog. Preserves nothing across a rebuild
// except that we re-check channels whose (objectId,prop,comp) identity was checked
// before -- so a Refresh mid-session doesn't silently lose the user's selection.
void DataRecorderPanel::onRefresh()
{
    // remember prior checked identities
    std::vector<RecorderChannel> prevChecked = checkedChannels();
    auto wasChecked = [&](std::uint32_t id, const std::string& prop, int comp, bool isComp) {
        for (const auto& c : prevChecked)
            if (c.objectId == id && c.propName == prop && c.comp == comp && c.isComponent == isComp)
                return true;
        return false;
    };

    const QSignalBlocker block(m_tree); // don't fire itemChanged during rebuild
    m_tree->clear();

    auto& cat = krs::twin::catalog();
    const double now = cat.now();
    const std::vector<std::uint32_t> ids = cat.objectIds();

    for (std::uint32_t id : ids) {
        // Public introspection only (obj() is private): name + property-name list,
        // and get(id,prop) for each property's PropType (scalar vs vec3/quat).
        const std::string name = cat.objectName(id);
        const std::vector<std::string> propNames = cat.propertiesOf(id);
        const std::string base = name.empty() ? ("obj-" + std::to_string(id)) : name;

        auto* objItem = new QTreeWidgetItem(m_tree);
        objItem->setText(0, QString::fromStdString(base));
        objItem->setFirstColumnSpanned(true);
        objItem->setData(0, kRoleIsLeaf, false);
        objItem->setExpanded(true);

        for (const std::string& propName : propNames) {
            const krs::twin::PropertyEntry* pe = cat.get(id, propName);
            if (!pe) continue;
            if (pe->type == krs::twin::PropType::Scalar) {
                auto* leaf = new QTreeWidgetItem(objItem);
                const std::string label = base + "." + propName;
                leaf->setText(0, QString::fromStdString(propName));
                leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
                leaf->setData(0, kRoleObjectId, id);
                leaf->setData(0, kRolePropName, QString::fromStdString(propName));
                leaf->setData(0, kRoleComp, 0);
                leaf->setData(0, kRoleIsComp, false);
                leaf->setData(0, kRoleUnit, QString()); // unit unknown from catalog; blank
                leaf->setData(0, kRoleLabel, QString::fromStdString(label));
                leaf->setData(0, kRoleIsLeaf, true);
                leaf->setCheckState(0, wasChecked(id, propName, 0, false) ? Qt::Checked : Qt::Unchecked);
            } else {
                // Vec3 / Quat -> one checkable child per component.
                const int n = int(pe->type); // 3 for Vec3, 4 for Quat
                const bool isQuat = (pe->type == krs::twin::PropType::Quat);
                for (int c = 0; c < n; ++c) {
                    auto* leaf = new QTreeWidgetItem(objItem);
                    const std::string comp = propName + compSuffix(c, isQuat);
                    const std::string label = base + "." + comp;
                    leaf->setText(0, QString::fromStdString(comp));
                    leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
                    leaf->setData(0, kRoleObjectId, id);
                    leaf->setData(0, kRolePropName, QString::fromStdString(propName));
                    leaf->setData(0, kRoleComp, c);
                    leaf->setData(0, kRoleIsComp, true);
                    leaf->setData(0, kRoleUnit, QString());
                    leaf->setData(0, kRoleLabel, QString::fromStdString(label));
                    leaf->setData(0, kRoleIsLeaf, true);
                    leaf->setCheckState(0, wasChecked(id, propName, c, true) ? Qt::Checked : Qt::Unchecked);
                }
            }
        }
    }
    (void)now;

    rebuildTableColumns();
    onLiveTick();     // immediate value fill
    updateStatus();
}

// Gather the checked channels in tree order.
std::vector<RecorderChannel> DataRecorderPanel::checkedChannels() const
{
    std::vector<RecorderChannel> out;
    if (!m_tree) return out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* obj = m_tree->topLevelItem(i);
        for (int j = 0; j < obj->childCount(); ++j) {
            QTreeWidgetItem* leaf = obj->child(j);
            if (!leaf->data(0, kRoleIsLeaf).toBool()) continue;
            if (leaf->checkState(0) != Qt::Checked) continue;
            RecorderChannel ch;
            ch.objectId    = leaf->data(0, kRoleObjectId).toUInt();
            ch.propName    = leaf->data(0, kRolePropName).toString().toStdString();
            ch.comp        = leaf->data(0, kRoleComp).toInt();
            ch.isComponent = leaf->data(0, kRoleIsComp).toBool();
            ch.unit        = leaf->data(0, kRoleUnit).toString().toStdString();
            ch.label       = leaf->data(0, kRoleLabel).toString().toStdString();
            ch.objName     = obj->text(0).toStdString();
            out.push_back(ch);
        }
    }
    return out;
}

// Guarded catalog read for one channel. Sets *found=false if the object/prop vanished.
double DataRecorderPanel::catalogValueOf(const RecorderChannel& ch, bool* found) const
{
    if (found) *found = false;
    const auto& cat = krs::twin::catalog();
    const krs::twin::PropertyEntry* e = cat.get(ch.objectId, ch.propName);
    if (!e) return 0.0;
    const int n = int(e->type);
    const int c = (ch.comp >= 0 && ch.comp < n) ? ch.comp : 0;
    if (c >= 4) return 0.0;
    if (found) *found = true;
    return e->v[c];
}

// (Re)shape the recording buffer to t[s] + one column per checked channel. Called
// whenever the checked set changes OR on a fresh recording -- it clears old rows
// (a column-shape change invalidates the existing rows).
void DataRecorderPanel::rebuildTableColumns()
{
    const std::vector<RecorderChannel> chans = checkedChannels();

    // Rebuild columns/labels/colors for the chart + table.
    m_recChannels = chans;
    m_table.columns.clear();
    m_table.rows.clear();
    m_lastGood.assign(chans.size(), 0.0);

    m_table.columns.push_back({ "t", "s" });
    for (const auto& ch : chans) {
        krs::rec::Column col;
        col.name = ch.label;      // "obj.prop" or "obj.prop.x"
        col.unit = ch.unit;       // "" if unknown
        m_table.columns.push_back(col);
    }

    // Reset trigger/decimation state on a shape change (rows are gone).
    m_triggered = false;
    m_haveTriggerTime = false;
    m_havePrevTrigVal = false;
    m_tickCounter = 0;

    // Push the (empty) source into the chart.
    std::vector<int> cols;
    std::vector<QString> labels;
    std::vector<QColor> colors;
    for (int k = 0; k < int(chans.size()); ++k) {
        cols.push_back(k + 1); // column 0 is time
        labels.push_back(QString::fromStdString(chans[k].label));
        colors.push_back(channelColor(k));
    }
    m_chart->setSource(&m_table, std::move(cols), std::move(labels), std::move(colors));
    m_chart->update();
}

void DataRecorderPanel::onItemChanged()
{
    // The checked set changed -> reshape the table (drops existing rows) + repaint.
    rebuildTableColumns();
    updateStatus();
}

// ~20 Hz: refresh the live value/Hz text on every VISIBLE leaf item.
void DataRecorderPanel::onLiveTick()
{
    if (!m_tree) return;
    const auto& cat = krs::twin::catalog();
    const double now = cat.now();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* obj = m_tree->topLevelItem(i);
        if (!obj->isExpanded()) continue; // only visible children
        for (int j = 0; j < obj->childCount(); ++j) {
            QTreeWidgetItem* leaf = obj->child(j);
            if (!leaf->data(0, kRoleIsLeaf).toBool()) continue;

            const std::uint32_t id = leaf->data(0, kRoleObjectId).toUInt();
            const std::string prop = leaf->data(0, kRolePropName).toString().toStdString();
            const int comp = leaf->data(0, kRoleComp).toInt();

            const krs::twin::PropertyEntry* e = cat.get(id, prop);
            if (!e) { leaf->setText(1, QStringLiteral("--")); continue; }
            const int n = int(e->type);
            const int c = (comp >= 0 && comp < n && comp < 4) ? comp : 0;
            const double v = e->v[c];
            const double hz = e->reportedHz(now);
            leaf->setText(1, QString::number(v, 'g', 5) + QStringLiteral("  @ ") +
                              QString::number(hz, 'f', 1) + QStringLiteral(" Hz"));
        }
    }
}

// Monotone sample clock: prefer catalog().now(); if it does not advance between ticks
// use an internal counter so time keeps moving (rows never share an identical t that
// would collapse the X axis).
double DataRecorderPanel::sampleClock()
{
    const double now = krs::twin::catalog().now();
    if (now > m_lastNow) {
        m_lastNow = now;
        m_monoClock = now;        // keep the fallback in sync with real time
        return now;
    }
    // now() did not advance -> synthesize a small increment at the record interval.
    m_monoClock += (m_recordTimer ? m_recordTimer->interval() : 20) / 1000.0;
    return m_monoClock;
}

// The internal ~50 Hz timer: once the main loop drives sampling (externalRecordTick), this becomes
// a repaint-only fallback -- otherwise every timer row would DOUBLE the externally-sampled ones.
void DataRecorderPanel::onRecordTick()
{
    if (m_externallyTicked) { m_chart->update(); return; }
    recordCore();
}

// FIREHOSE: called by the main loop once per eval pass, right after the catalog publisher -- the
// recorder samples exactly as fast as fresh data arrives (crank the eval rate, the log follows).
void DataRecorderPanel::externalRecordTick()
{
    m_externallyTicked = true;
    recordCore();
}

// Keep the back-scroll range in step with the buffer; stay pinned to live unless the user scrolled.
void DataRecorderPanel::syncScrollBar()
{
    if (!m_scroll || !m_chart) return;
    const int total = int(m_table.rows.size());
    const int maxBack = std::max(0, total - m_chart->windowRows());
    QSignalBlocker block(m_scroll);
    m_scroll->setRange(0, maxBack);
    m_scroll->setPageStep(std::max(1, m_chart->windowRows()));
    if (m_followLive) { m_scroll->setValue(maxBack); m_chart->setScrollBack(0); }
}

// One sampling step: evaluate the trigger, then (when recording) every Nth call append a row of the
// checked channels at the current clock.
void DataRecorderPanel::recordCore()
{
    // Repaint the chart regardless (cheap; keeps latest readout live).
    // Only actually record when the Record toggle is on.
    const bool recordEnabled = m_recording;
    if (!recordEnabled) { m_chart->update(); return; }

    const std::vector<RecorderChannel>& chans = m_recChannels;
    if (chans.empty()) { m_chart->update(); return; }

    const int mode = m_triggerMode ? m_triggerMode->currentIndex() : 0; // 0 Manual,1 Rising,2 Level
    const bool armed = m_arm && m_arm->isChecked();
    const double thr = m_threshold ? m_threshold->value() : 0.0;

    // --- trigger evaluation on the FIRST checked channel ---
    // Manual mode: pressing Record (or Trigger) already set m_triggered.
    if (armed && (mode == 1 || mode == 2)) {
        bool found = false;
        const double v0 = catalogValueOf(chans.front(), &found);
        if (found && !m_triggered) {
            if (mode == 2) { // Level >
                if (v0 > thr) {
                    m_triggered = true;
                    if (!m_haveTriggerTime) { m_triggerTime = krs::twin::catalog().now(); m_haveTriggerTime = true; }
                }
            } else { // Rising edge: prev <= thr and cur > thr
                if (m_havePrevTrigVal && m_prevTrigVal <= thr && v0 > thr) {
                    m_triggered = true;
                    if (!m_haveTriggerTime) { m_triggerTime = krs::twin::catalog().now(); m_haveTriggerTime = true; }
                }
            }
        }
        if (found) { m_prevTrigVal = v0; m_havePrevTrigVal = true; }
    } else if (!armed) {
        // Not armed -> manual recording: record continuously once Record is on.
        m_triggered = true;
        if (!m_haveTriggerTime) { m_triggerTime = krs::twin::catalog().now(); m_haveTriggerTime = true; }
    }

    if (!m_triggered) { m_chart->update(); updateStatus(); return; }

    // --- decimation: record every Nth tick ---
    const int dec = m_decimation ? std::max(1, m_decimation->value()) : 1;
    if ((m_tickCounter++ % dec) != 0) { m_chart->update(); return; }

    // --- append a row: t + one value per checked channel ---
    const double t = sampleClock();
    std::vector<double> row;
    row.reserve(chans.size() + 1);
    row.push_back(t);
    for (int k = 0; k < int(chans.size()); ++k) {
        bool found = false;
        double v = catalogValueOf(chans[k], &found);
        if (!found) {
            // channel vanished mid-record: log its last good value (or 0 if never seen).
            v = (k < int(m_lastGood.size())) ? m_lastGood[k] : 0.0;
        } else if (k < int(m_lastGood.size())) {
            m_lastGood[k] = v;
        }
        row.push_back(v);
    }
    m_table.addRow(row);

    syncScrollBar();                      // FIFO grew: keep the back-scroll range honest
    m_chart->update();
    updateStatus();
}

void DataRecorderPanel::onRecordToggle()
{
    m_recording = m_recordBtn->isChecked();
    m_recordBtn->setText(m_recording ? QStringLiteral("Stop") : QStringLiteral("Record"));

    if (m_recording) {
        // Make sure the buffer is shaped to the current checked set. If there are
        // already rows from a prior run we keep appending (Clear wipes explicitly).
        if (m_table.columns.empty() || int(m_table.columns.size()) != int(m_recChannels.size()) + 1)
            rebuildTableColumns();

        const int mode = m_triggerMode ? m_triggerMode->currentIndex() : 0;
        const bool armed = m_arm && m_arm->isChecked();
        // Manual-mode Record (or un-armed) triggers immediately.
        if (mode == 0 || !armed) {
            m_triggered = true;
            m_triggerTime = krs::twin::catalog().now();
            m_haveTriggerTime = true;
        }
        m_lastNow = -1.0; // resync the sample clock on start
    }
    updateStatus();
}

void DataRecorderPanel::onManualTrigger()
{
    m_triggered = true;
    m_triggerTime = krs::twin::catalog().now();
    m_haveTriggerTime = true;
    // A manual trigger also starts recording if it isn't already on.
    if (!m_recording) {
        m_recordBtn->setChecked(true);
        onRecordToggle();
    }
    updateStatus();
}

void DataRecorderPanel::onClear()
{
    m_table.rows.clear();
    m_triggered = false;
    m_haveTriggerTime = false;
    m_havePrevTrigVal = false;
    m_tickCounter = 0;
    std::fill(m_lastGood.begin(), m_lastGood.end(), 0.0);
    m_followLive = true;
    syncScrollBar();
    m_chart->update();
    updateStatus();
}

void DataRecorderPanel::updateStatus()
{
    if (!m_status) return;
    QString s;
    const bool armed = m_arm && m_arm->isChecked();
    const int nChecked = int(m_recChannels.size());

    if (m_recording) {
        if (m_triggered) {
            s = QStringLiteral("Recording (%1 rows)").arg(int(m_table.rows.size()));
            if (m_haveTriggerTime)
                s += QStringLiteral("  |  Triggered @ t=%1s").arg(QString::number(m_triggerTime, 'f', 3));
        } else {
            s = armed ? QStringLiteral("Armed -- waiting for trigger...")
                      : QStringLiteral("Recording (waiting)");
        }
    } else if (armed) {
        s = QStringLiteral("Armed");
    } else {
        s = QStringLiteral("Idle");
    }
    s += QStringLiteral("   [%1 channel%2 checked]").arg(nChecked).arg(nChecked == 1 ? "" : "s");
    m_status->setText(s);
}

// ---------------------------------------------------------------------------
// Save Recording... -> CSV or JSON by extension.
void DataRecorderPanel::onSave()
{
    if (m_table.columns.empty() || m_table.rows.empty()) {
        QMessageBox::information(this, QStringLiteral("Save Recording"),
                                 QStringLiteral("Nothing to save -- record some data first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Recording"), QString(),
        QStringLiteral("CSV (*.csv);;JSON (*.json)"));
    if (path.isEmpty()) return;

    const QString ext = QFileInfo(path).suffix().toLower();
    bool ok = false;
    if (ext == QStringLiteral("json"))
        ok = krs::rec::writeJSON(m_table, path);
    else
        ok = krs::rec::writeCSV(m_table, path); // default/csv

    if (ok)
        m_status->setText(QStringLiteral("Saved %1 rows -> %2").arg(int(m_table.rows.size())).arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("Save Recording"),
                             QStringLiteral("Failed to write %1").arg(path));
}

// ---------------------------------------------------------------------------
// Bake LUT... -> pick an axis column + a value column from the checked/recorded set,
// nBreakpoints + interp, pull those two columns into xs/ys, characterize, save .klut.
void DataRecorderPanel::onBakeLut()
{
    if (m_table.columns.size() < 2 || m_table.rows.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Bake LUT"),
                                 QStringLiteral("Need at least 2 recorded rows and a channel to bake a LUT."));
        return;
    }

    // Build the column choice list (all recorded columns incl. time).
    QStringList colNames;
    for (const auto& c : m_table.columns) {
        QString n = QString::fromStdString(c.name);
        if (!c.unit.empty()) n += " [" + QString::fromStdString(c.unit) + "]";
        colNames << n;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Bake LUT"));
    auto* form = new QFormLayout(&dlg);

    auto* axisCombo = new QComboBox(&dlg);
    axisCombo->addItems(colNames);
    // default axis = time (col 0), value = first data column (col 1)
    axisCombo->setCurrentIndex(0);
    auto* valueCombo = new QComboBox(&dlg);
    valueCombo->addItems(colNames);
    valueCombo->setCurrentIndex(1);

    auto* bpSpin = new QSpinBox(&dlg);
    bpSpin->setRange(2, 4096);
    bpSpin->setValue(32);

    auto* interpCombo = new QComboBox(&dlg);
    interpCombo->addItem(QStringLiteral("Nearest"));
    interpCombo->addItem(QStringLiteral("Linear"));
    interpCombo->addItem(QStringLiteral("Cubic"));
    interpCombo->setCurrentIndex(1);

    form->addRow(QStringLiteral("Axis (X) channel:"), axisCombo);
    form->addRow(QStringLiteral("Value (Y) channel:"), valueCombo);
    form->addRow(QStringLiteral("Breakpoints:"), bpSpin);
    form->addRow(QStringLiteral("Interp:"), interpCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const int axisCol = axisCombo->currentIndex();
    const int valueCol = valueCombo->currentIndex();
    if (axisCol == valueCol) {
        QMessageBox::warning(this, QStringLiteral("Bake LUT"),
                             QStringLiteral("Axis and Value channels must differ."));
        return;
    }

    // Pull the two columns.
    std::vector<double> xs, ys;
    xs.reserve(m_table.rows.size());
    ys.reserve(m_table.rows.size());
    for (const auto& r : m_table.rows) {
        if (axisCol >= int(r.size()) || valueCol >= int(r.size())) continue;
        const double x = r[axisCol], y = r[valueCol];
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        xs.push_back(x);
        ys.push_back(y);
    }
    if (xs.size() < 2) {
        QMessageBox::warning(this, QStringLiteral("Bake LUT"),
                             QStringLiteral("Not enough finite samples in the selected columns."));
        return;
    }

    krs::klut::Interp interp = krs::klut::Interp::Linear;
    switch (interpCombo->currentIndex()) {
        case 0: interp = krs::klut::Interp::Nearest; break;
        case 2: interp = krs::klut::Interp::Cubic;   break;
        default: interp = krs::klut::Interp::Linear; break;
    }

    const std::string quantity = m_table.columns[valueCol].name;
    const std::string unit     = m_table.columns[valueCol].unit;

    krs::klut::Lut lut = krs::klut::characterizeFromSamples(xs, ys, bpSpin->value(), interp, quantity, unit);
    std::string why;
    if (!lut.valid(&why)) {
        QMessageBox::warning(this, QStringLiteral("Bake LUT"),
                             QStringLiteral("Characterization produced an invalid LUT: %1")
                                 .arg(QString::fromStdString(why)));
        return;
    }
    lut.name = quantity + " vs " + m_table.columns[axisCol].name;

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save LUT"), QString(), QStringLiteral("KLut (*.klut)"));
    if (path.isEmpty()) return;

    const QString id = krs::klut::saveKLut(lut, path);
    if (!id.isEmpty())
        m_status->setText(QStringLiteral("Baked LUT (%1 bp) -> %2").arg(bpSpin->value()).arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("Bake LUT"),
                             QStringLiteral("Failed to write LUT to %1").arg(path));
}
