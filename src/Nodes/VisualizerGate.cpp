// VisualizerGate.cpp -- GATE VIS: the visualizer instruments actually DISPLAY the value they are told to
// show. For the 7-segment numeric readout we read back the QLCDNumber's displayed value + digit count +
// the formatted "Display" output, and assert they match the input with the requested decimals. For the
// dial gauge we read the widget's tagged krs_gauge_norm/value properties. NEG-CTRL: a readout with its
// Value input disconnected does not fabricate a display. Pixel styling itself is OPERATOR VISUAL-CONFIRM.

#include "NodeDelegate.hpp"
#include "NodeFactory.hpp"
#include "Node.hpp"
#include "NodeEditorGate.hpp"

#include <QApplication>
#include <QWidget>
#include <QLCDNumber>
#include <QDoubleSpinBox>
#include <QSpinBox>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <any>

namespace krs::nodes {
namespace {
QWidget* findCtl(QWidget* body, const char* port) {
    for (QWidget* w : body->findChildren<QWidget*>())
        if (w->property("krs_input_port").isValid() && w->property("krs_input_port").toString() == QString(port))
            return w;
    return nullptr;
}
void drive(QWidget* c, double v) {
    // Force a valueChanged even when v equals the widget's current value (e.g. Min=0 on a fresh spinbox),
    // so the bound setPortLiteral fires and the literal is actually set. Still drives the real widget.
    if (auto* d = qobject_cast<QDoubleSpinBox*>(c)) { if (d->value() == v) d->setValue(v + 1.0); d->setValue(v); }
    else if (auto* s = qobject_cast<QSpinBox*>(c)) { if (s->value() == int(v)) s->setValue(int(v) + 1); s->setValue(int(v)); }
}
std::string outStr(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value())
            try { return std::any_cast<std::string>(p.packet->data); } catch (...) {}
    return std::string();
}
double outD(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value()) {
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
            try { return double(std::any_cast<float>(p.packet->data)); } catch (...) {}
        }
    return std::nan("");
}
} // namespace

bool runVisGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[vis] GATE VIS -- a visualizer's DISPLAYED value matches its input (digits/decimals respected; disconnected -> no update)\n");
    if (!QApplication::instance()) { printf("[vis] FAIL: needs QApplication\n"); return false; }

    bool readoutOk = false, gaugeOk = false, negOk = false;
    int rdChecks = 0, rdPass = 0, ggChecks = 0, ggPass = 0;

    // ---- NUMERIC READOUT (7-segment QLCDNumber) ----
    {
        NodeDelegate d("viz_numeric_readout");
        Node* n = d.backendNode();
        QWidget* body = n ? d.embeddedWidget() : nullptr;
        QLCDNumber* lcd = body ? body->findChild<QLCDNumber*>() : nullptr;
        QWidget* cv  = body ? findCtl(body, "Value")    : nullptr;
        QWidget* cdg = body ? findCtl(body, "Digits")   : nullptr;
        QWidget* cdc = body ? findCtl(body, "Decimals") : nullptr;
        if (n && lcd && cv && cdg && cdc) {
            struct T { double value; int digits; int decimals; };
            const T cases[] = { {3.14159, 8, 2}, {3.14159, 8, 4}, {2.71828, 8, 2}, {42.0, 8, 0} };
            for (const T& t : cases) {
                ++rdChecks;
                drive(cdg, t.digits); drive(cdc, t.decimals); drive(cv, t.value);
                d.recomputeAndPropagate();
                const double vIn = qobject_cast<QDoubleSpinBox*>(cv)->value();   // what the widget delivered
                const QString expect = QString::number(vIn, 'f', t.decimals);
                const std::string disp = outStr(*n, "Display");
                const double shown = lcd->value();                              // the value the LCD displays
                const bool matchOk    = std::abs(shown - expect.toDouble()) < 0.5 * std::pow(10.0, -t.decimals) + 1e-9;
                const bool decimalsOk = (disp == expect.toStdString());
                const bool digitsOk   = (lcd->digitCount() == t.digits);
                if (matchOk && decimalsOk && digitsOk) ++rdPass;
                printf("[vis]   readout v=%.5f digits=%d dec=%d -> lcd=%.5f display=\"%s\" digitCount=%d  %s\n",
                       vIn, t.digits, t.decimals, shown, disp.c_str(), lcd->digitCount(),
                       (matchOk && decimalsOk && digitsOk) ? "ok" : "FAIL");
            }
            readoutOk = (rdPass == rdChecks && rdChecks > 0);

            // NEG-CTRL: a readout with NO Value input does not invent a display; connecting one updates it.
            NodeDelegate dn("viz_numeric_readout"); Node* nn = dn.backendNode();
            QWidget* bn = nn ? dn.embeddedWidget() : nullptr;
            QLCDNumber* ln = bn ? bn->findChild<QLCDNumber*>() : nullptr;
            if (nn && ln) {
                dn.recomputeAndPropagate();                       // no Value set
                const std::string d0 = outStr(*nn, "Display");
                const double before = ln->value();
                if (auto* cdcn = findCtl(bn, "Decimals")) drive(cdcn, 2);
                if (auto* cvn = findCtl(bn, "Value")) drive(cvn, 5.0);
                dn.recomputeAndPropagate();
                const double after = ln->value();
                const std::string dAfter = outStr(*nn, "Display");
                // disconnected -> empty display + LCD at default 0; connected -> exactly 5.00 (tight).
                negOk = d0.empty() && std::abs(before) < 1e-9 && std::abs(after - 5.0) < 1e-3 && dAfter == "5.00";
                printf("[vis]   NEG-CTRL readout: no-input display=\"%s\" lcd=%.3f -> Value=5 -> lcd=%.3f display=\"%s\"  %s\n",
                       d0.c_str(), before, after, dAfter.c_str(), negOk ? "PASS" : "FAIL!");
            }
        } else {
            printf("[vis]   readout: missing widgets (lcd=%p value=%p digits=%p dec=%p)  FAIL\n",
                   (void*)lcd, (void*)cv, (void*)cdg, (void*)cdc);
        }
    }

    // ---- DIAL GAUGE (painted arc + needle) ----
    {
        NodeDelegate d("viz_dial_gauge"); Node* n = d.backendNode();
        QWidget* body = n ? d.embeddedWidget() : nullptr;
        QWidget* gauge = nullptr;
        if (body) for (QWidget* w : body->findChildren<QWidget*>())
            if (w->property("krs_gauge_norm").isValid()) { gauge = w; break; }
        QWidget* cv   = body ? findCtl(body, "Value") : nullptr;
        QWidget* cmin = body ? findCtl(body, "Min")   : nullptr;
        QWidget* cmax = body ? findCtl(body, "Max")   : nullptr;
        if (n && gauge && cv && cmin && cmax) {
            struct G { double v, mn, mx, en; };
            const G cases[] = { {7, 0, 10, 0.7}, {3, 0, 10, 0.3}, {5, 0, 20, 0.25} };
            for (const G& g : cases) {
                ++ggChecks;
                drive(cmin, g.mn); drive(cmax, g.mx); drive(cv, g.v);
                d.recomputeAndPropagate();
                const double norm  = outD(*n, "Normalized");
                const double wnorm = gauge->property("krs_gauge_norm").toDouble();
                const double wval  = gauge->property("krs_gauge_value").toDouble();
                const bool ok = std::abs(norm - g.en) < 1e-4 && std::abs(wnorm - g.en) < 1e-4 && std::abs(wval - g.v) < 1e-4;
                if (ok) ++ggPass;
                printf("[vis]   gauge v=%.2f [%.1f..%.1f] -> norm=%.4f widget(norm=%.4f val=%.2f) expect %.4f  %s\n",
                       g.v, g.mn, g.mx, norm, wnorm, wval, g.en, ok ? "ok" : "FAIL");
            }
            gaugeOk = (ggPass == ggChecks && ggChecks > 0);
        } else {
            printf("[vis]   gauge: missing widgets (gauge=%p value=%p min=%p max=%p)  FAIL\n",
                   (void*)gauge, (void*)cv, (void*)cmin, (void*)cmax);
        }
    }

    const bool pass = readoutOk && gaugeOk && negOk;
    printf("[vis]   readout %d/%d, gauge %d/%d, neg-ctrl %s\n", rdPass, rdChecks, ggPass, ggChecks, negOk ? "ok" : "FAIL");
    printf("[vis] %s (pixel styling = OPERATOR VISUAL-CONFIRM)\n",
           pass ? "ALL PASS (displayed value tracks input; digits/decimals respected; disconnected inert)"
                : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
