#pragma once
#include <QDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <entt/entt.hpp>
#include "components.hpp"           // Vertex / TransformComponent / tags

class LedTweakDialog : public QDialog
{
    Q_OBJECT
public:
    LedTweakDialog(entt::registry& r, entt::entity led, QWidget* parent = nullptr)
        : QDialog(parent), reg(r), ledE(led)
    {
        setWindowTitle("LED Offset");
        setAttribute(Qt::WA_DeleteOnClose);          // auto-delete

        auto makeSpin = [&](const char* name) {
            auto* sb = new QDoubleSpinBox(this);
            sb->setRange(-1.0, 1.0);
            sb->setDecimals(3);
            sb->setSingleStep(0.005);
            sb->setPrefix(name);
            sb->setKeyboardTracking(false);          // only emit on Enter/Done
            return sb;
            };

        sbX = makeSpin("X ");
        sbY = makeSpin("Y ");
        sbZ = makeSpin("Z ");

        auto layout = new QHBoxLayout(this);
        layout->addWidget(sbX); layout->addWidget(sbY); layout->addWidget(sbZ);
        setLayout(layout);

        // initialise from current transform
        const auto& tf = reg.get<TransformComponent>(ledE);
        sbX->setValue(tf.translation.x);
        sbY->setValue(tf.translation.y);
        sbZ->setValue(tf.translation.z);

        connect(sbX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LedTweakDialog::apply);
        connect(sbY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LedTweakDialog::apply);
        connect(sbZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LedTweakDialog::apply);
    }

private slots:
    void apply()
    {
        auto& tf = reg.get<TransformComponent>(ledE);
        tf.translation = { float(sbX->value()),
                           float(sbY->value()),
                           float(sbZ->value()) };
    }

private:
    entt::registry& reg;
    entt::entity    ledE;
    QDoubleSpinBox* sbX{}, * sbY{}, * sbZ{};
};