#pragma once
// ===========================================================================
// DataRecorderPanel -- the live data-logging OSCILLOSCOPE / RECORDER surface.
//
// A GUI-only (eyes-on, no gate) panel that turns the process-wide published-state
// CATALOG (krs::twin::catalog(); PropertyCatalog.hpp) into a channel-oriented
// strip-chart recorder. The left tree exposes every published object's scalar
// properties and vec3 COMPONENTS (name.x/.y/.z) as checkable channels; the right
// chart plots the checked channels over a rolling time window; the controls arm a
// trigger (manual / rising-edge / level) and record every Nth tick into a
// krs::rec::DataTable. The recording can be saved to CSV/JSON (krs::rec) or two of
// its columns baked into a .klut lookup table (krs::klut::characterizeFromSamples).
//
// The main loop only needs to construct this widget and dock it -- it is entirely
// self-driven off two internal QTimers and the catalog singleton.
// ===========================================================================
#include <QWidget>
#include <cstdint>
#include <string>
#include <vector>

#include "Rec.hpp"   // krs::rec::DataTable (stored by value as the recording buffer)

class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;
class QSplitter;
class QScrollBar;

// A single loggable scalar signal: object id + property name + which component of a
// vec3 (or -1 for a plain scalar), plus a cached unit + display label. `comp` indexes
// PropertyEntry::v[comp]; for a scalar channel comp==0 (v[0]).
struct RecorderChannel {
    std::uint32_t objectId = 0;
    std::string   objName;      // cached at build time (for the column header / legend)
    std::string   propName;
    int           comp = 0;     // 0..2 for a vec3 component; 0 for a scalar
    bool          isComponent = false; // true => label is prop.x/.y/.z
    std::string   unit;         // "" if unknown
    std::string   label;        // "obj.prop" or "obj.prop.x" -- legend / header text
};

// A ChartView is a pure-QPainter strip chart over a DataTable's checked channels.
// It holds only non-owning views (a pointer to the panel's table + the list of
// column indices to draw and their colors); the panel calls setSource()+update().
class DataRecorderChartView; // defined in the .cpp (nested, QWidget subclass)

class DataRecorderPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DataRecorderPanel(QWidget* parent = nullptr);
    ~DataRecorderPanel() override;

    // Convenience the main loop MAY call after docking to populate the tree from the
    // current catalog immediately (otherwise the user clicks "Refresh"). Safe to call
    // any time; equivalent to pressing the Refresh button.
    void refreshChannels();

    // FIREHOSE RECORDING: the main loop calls this once per eval pass (right after the catalog
    // publisher), so sampling rides the eval rate -- crank the eval-rate spinbox and the recorder
    // follows -- instead of a fixed 50 Hz UI timer. The first call retires the internal timer as a
    // sampler (it remains a chart-repaint fallback only).
    void externalRecordTick();

private slots:
    void onRefresh();          // rescan catalog() and rebuild the channel tree
    void onItemChanged();      // a channel was (un)checked -> rebuild chart columns
    void onRecordToggle();     // Record/Stop button
    void onManualTrigger();    // manual "Trigger" button
    void onClear();            // wipe the recording buffer + chart
    void onSave();             // Save Recording... (CSV/JSON by extension)
    void onBakeLut();          // Bake LUT... (pick axis+value channel -> .klut)
    void onLiveTick();         // ~20 Hz: refresh visible tree value/Hz text
    void onRecordTick();       // ~50 Hz: sample catalog + append rows when recording

private:
    // ---- helpers (all self-contained in the .cpp) ----
    std::vector<RecorderChannel> checkedChannels() const;   // in tree order
    void rebuildTableColumns();     // (re)shape m_table to t[s] + one col per checked channel
    void updateStatus();            // refresh the status label text
    double catalogValueOf(const RecorderChannel& ch, bool* found) const; // guarded lookup
    double sampleClock();           // catalog().now() if advancing, else a monotone counter
    void recordCore();              // one sampling step (trigger eval + row append + chart sync)
    void syncScrollBar();           // keep the back-scroll range in step with the buffer

    // ---- top control row ----
    QCheckBox*       m_arm = nullptr;
    QComboBox*       m_triggerMode = nullptr;   // Manual / Rising edge / Level >
    QDoubleSpinBox*  m_threshold = nullptr;
    QPushButton*     m_triggerBtn = nullptr;
    QPushButton*     m_recordBtn = nullptr;
    QPushButton*     m_clearBtn = nullptr;
    QSpinBox*        m_decimation = nullptr;
    QLabel*          m_status = nullptr;
    QPushButton*     m_refreshBtn = nullptr;

    // ---- body ----
    QSplitter*             m_splitter = nullptr;
    QTreeWidget*           m_tree = nullptr;
    DataRecorderChartView* m_chart = nullptr;

    // ---- chart scale controls + back-scroll ----
    QSpinBox*        m_windowSpin = nullptr;   // points across the X axis (FIFO window)
    QComboBox*       m_yMode = nullptr;        // Auto / Manual
    QDoubleSpinBox*  m_yMargin = nullptr;      // autoscale headroom, % of span (top+bottom)
    QDoubleSpinBox*  m_yMinSpin = nullptr;     // manual Y min/max
    QDoubleSpinBox*  m_yMaxSpin = nullptr;
    QScrollBar*      m_scroll = nullptr;       // back-scroll through the FIFO history
    bool             m_followLive = true;      // scrollbar pinned at the newest window

    // ---- bottom actions ----
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_bakeBtn = nullptr;

    // ---- timers ----
    QTimer* m_liveTimer = nullptr;    // ~20 Hz tree value refresh
    QTimer* m_recordTimer = nullptr;  // ~50 Hz record sampling

    // ---- recording state ----
    krs::rec::DataTable m_table;      // the recording buffer (t[s] + one col per checked channel)
    bool   m_recording = false;       // Record/Stop toggle is on
    bool   m_triggered = false;       // trigger has fired (edge/level satisfied or manual)
    double m_triggerTime = 0.0;       // sim time the trigger fired
    bool   m_haveTriggerTime = false;
    int    m_tickCounter = 0;         // for decimation (record every Nth tick)
    double m_prevTrigVal = 0.0;       // previous value of the trigger channel (rising-edge)
    bool   m_havePrevTrigVal = false;
    double m_monoClock = 0.0;         // fallback clock when catalog().now() does not advance
    double m_lastNow = -1.0;          // last observed catalog().now()
    bool   m_externallyTicked = false; // main loop drives sampling (internal timer repaints only)

    // remembered channel identity per recording column (index 1..N map to these);
    // parallel to m_table columns[1..] so a vanished channel still logs its last/0.
    std::vector<RecorderChannel> m_recChannels;
    std::vector<double>          m_lastGood;   // last good value per rec channel (for dropouts)
};
