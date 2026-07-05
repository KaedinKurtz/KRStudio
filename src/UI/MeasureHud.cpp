// MeasureHud.cpp -- see MeasureHud.hpp. The measure-mode corner readout.
#include "MeasureHud.hpp"
#include "Measure.hpp"            // krs::measure::measureSelections + MeasureUiPrefs
#include "SelectionService.hpp"   // krs::sel::SelectionState (ctx)
#include "Scene.hpp"

#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QVBoxLayout>

QColor MeasureHud::s_panel  = QColor(0x35, 0x3b, 0x46, 235);
QColor MeasureHud::s_border = QColor(0x4a, 0x52, 0x60);
QColor MeasureHud::s_text   = QColor(0xd5, 0xd5, 0xd5);
QColor MeasureHud::s_accent = QColor(0x00, 0x78, 0xd7);

void MeasureHud::setThemeColors(const QColor& panel, const QColor& border,
                                const QColor& text, const QColor& accent)
{
    s_panel = panel; s_panel.setAlpha(235);
    s_border = border; s_text = text; s_accent = accent;
}

MeasureHud::MeasureHud(Scene* scene, QWidget* viewport)
    : QWidget(viewport), m_scene(scene)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);   // never steal viewport clicks
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    m_label = new QLabel(this);
    m_label->setTextFormat(Qt::RichText);
    lay->addWidget(m_label);
    hide();

    m_timer = new QTimer(this);
    m_timer->setInterval(200);                        // 5 Hz live readout
    connect(m_timer, &QTimer::timeout, this, [this] { tick(); });
    m_timer->start();
}

void MeasureHud::reposition()
{
    if (!parentWidget()) return;
    move(parentWidget()->width() - width() - 12, 12);
}

void MeasureHud::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(s_border, 1.0));
    p.setBrush(s_panel);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6.0, 6.0);
}

void MeasureHud::tick()
{
    if (!m_scene) { hide(); return; }
    auto& reg = m_scene->getRegistry();
    auto* st = reg.ctx().find<krs::sel::SelectionState>();
    const bool active = st && st->measureMode;
    if (!active) { if (isVisible()) hide(); m_sig.clear(); return; }

    if (st->selected.empty()) {
        const QString hint = QStringLiteral(
            "<span style='color:%1'><b>MEASURE</b></span><br>"
            "<span style='color:%2'>Click a face or bore.<br>"
            "Shift+click adds faces to the item.<br>"
            "Ctrl+click picks a vertex.<br>"
            "Two items &#8594; distance / angle.</span>")
            .arg(s_accent.name(), s_text.name());
        if (m_sig != QLatin1String("hint")) {
            m_sig = QStringLiteral("hint");
            m_label->setText(hint);
            adjustSize();
            reposition();
        }
        if (!isVisible()) { show(); raise(); }
        return;
    }

    const krs::measure::Readout r = krs::measure::measureSelections(reg, st->selected);
    const auto* prefs = reg.ctx().find<krs::measure::MeasureUiPrefs>();
    const double f = prefs ? prefs->lengthFactor : 1.0;
    const QString lu = prefs ? QString::fromStdString(prefs->lengthUnit) : QStringLiteral("m");

    QString html = QStringLiteral("<span style='color:%1'><b>MEASURE</b></span>"
                                  "<table style='color:%2' cellspacing='0' cellpadding='1'>")
                       .arg(s_accent.name(), s_text.name());
    QString sig;
    for (const auto& l : r.lines) {
        QString val;
        switch (l.unit) {
        case krs::measure::ReadoutLine::Unit::Length:
            val = QStringLiteral("%1 %2").arg(l.value * f, 0, 'g', 6).arg(lu); break;
        case krs::measure::ReadoutLine::Unit::Area:
            val = QStringLiteral("%1 %2%3").arg(l.value * f * f, 0, 'g', 6).arg(lu).arg(QChar(0x00B2)); break;
        case krs::measure::ReadoutLine::Unit::Angle:
            val = QStringLiteral("%1%2").arg(l.value, 0, 'f', 2).arg(QChar(0x00B0)); break;
        case krs::measure::ReadoutLine::Unit::Count:
            val = QString::number(int(l.value)); break;
        }
        html += QStringLiteral("<tr><td style='padding-right:10px'>%1</td><td align='right'><b>%2</b></td></tr>")
                    .arg(QString::fromStdString(l.label).toHtmlEscaped(), val);
        sig += QString::fromStdString(l.label) + QLatin1Char('=') + val + QLatin1Char(';');
    }
    html += QStringLiteral("</table>");
    for (const auto& n : r.notes) {
        html += QStringLiteral("<div style='color:%1;font-size:10px'>%2</div>")
                    .arg(s_border.lighter(140).name(), QString::fromStdString(n).toHtmlEscaped());
        sig += QString::fromStdString(n);
    }

    if (sig != m_sig) {
        m_sig = sig;
        m_label->setText(html);
        adjustSize();
        reposition();
        update();
    }
    if (!isVisible()) { show(); raise(); }
}
