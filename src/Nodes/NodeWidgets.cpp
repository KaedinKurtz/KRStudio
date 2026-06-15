#include "NodeWidgets.hpp"
#include "Node.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QDial>
#include <QDoubleSpinBox>
#include <cmath>
#include <algorithm>

namespace krs::nodeui {

QWidget* buildControlWidget(Node* node, const std::vector<ControlSpec>& controls)
{
    auto* box = new QWidget();
    auto* outer = new QVBoxLayout(box);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(3);

    for (const auto& c : controls) {
        // seed the param with the control's default so compute() has a value before any user input.
        if (!node->hasParam(c.param)) node->setParam<double>(c.param, c.def);

        auto* row = new QWidget(box);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(4);
        auto* lab = new QLabel(QString::fromStdString(c.label), row);
        lab->setMinimumWidth(48);
        rl->addWidget(lab);

        const std::string param = c.param;
        const ControlSpec spec = c;
        Node* n = node;

        const QString paramProp = QString::fromStdString(c.param);   // tag controls so a test can find them
        if (c.kind == ControlSpec::SpinBox || c.kind == ControlSpec::Readout) {
            auto* sb = new QDoubleSpinBox(row);
            sb->setRange(c.min, c.max); sb->setSingleStep(c.step); sb->setValue(c.def);
            sb->setReadOnly(c.kind == ControlSpec::Readout);
            sb->setProperty("krs_param", paramProp);
            QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                             [n, param](double v) { n->setParam<double>(param, v); n->process(); });
            rl->addWidget(sb);
        } else if (c.kind == ControlSpec::Dial) {
            auto* d = new QDial(row);
            d->setRange(0, 1000); d->setValue(toTick(c.min, c.max, c.def)); d->setNotchesVisible(true);
            d->setProperty("krs_param", paramProp);
            QObject::connect(d, &QDial::valueChanged,
                             [n, param, spec](int tick) { n->setParam<double>(param, fromTick(spec.min, spec.max, tick)); n->process(); });
            rl->addWidget(d);
        } else { // Slider
            auto* s = new QSlider(Qt::Horizontal, row);
            s->setRange(0, 1000); s->setValue(toTick(c.min, c.max, c.def));
            s->setProperty("krs_param", paramProp);
            QObject::connect(s, &QSlider::valueChanged,
                             [n, param, spec](int tick) { n->setParam<double>(param, fromTick(spec.min, spec.max, tick)); n->process(); });
            rl->addWidget(s);
        }
        outer->addWidget(row);
    }

    // Size to the gated estimate -- the rendered bounds ARE estimateFootprint(controls), so nodes stay
    // compact and never expand with content/ports.
    const Footprint fp = estimateFootprint(controls);
    box->setFixedSize(fp.w, fp.h);
    return box;
}

} // namespace krs::nodeui
