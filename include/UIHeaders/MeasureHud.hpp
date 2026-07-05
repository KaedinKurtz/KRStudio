#pragma once
// ===========================================================================
// MEASURE HUD -- the measure-mode corner readout (Onshape-style).
//
// A small theme-matched overlay pinned to the viewport's top-right corner. It
// polls the ctx SelectionState (5 Hz) and, while measure mode is armed with a
// non-empty FIFO-2 buffer, shows krs::measure::measureSelections() converted
// into the ribbon's units (mirrored into the ctx MeasureUiPrefs by MainWindow,
// so the HUD has zero widget coupling). Colors follow the active UI theme via
// setThemeColors (called from MainWindow::applyTheme).
// ===========================================================================
#include <QWidget>
#include <QColor>

class Scene;
class QLabel;
class QTimer;

class MeasureHud : public QWidget
{
    Q_OBJECT
public:
    explicit MeasureHud(Scene* scene, QWidget* viewport);
    void reposition();                        // pin to the parent's top-right corner

    // Theme hookup (MainWindow::applyTheme). Defaults match the dark side-panel palette.
    static void setThemeColors(const QColor& panel, const QColor& border,
                               const QColor& text, const QColor& accent);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void tick();                              // poll SelectionState -> measureSelections -> rich text

    Scene*  m_scene = nullptr;
    QLabel* m_label = nullptr;
    QTimer* m_timer = nullptr;
    QString m_sig;                            // last readout signature (skip redundant relayouts)

    static QColor s_panel, s_border, s_text, s_accent;
};
