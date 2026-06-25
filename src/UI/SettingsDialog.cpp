#include "SettingsDialog.hpp"
#include "SettingsManager.hpp"

#include <QListWidget>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QColorDialog>
#include <QLabel>
#include <QtMath>
#include <glm/glm.hpp>

using krs::SettingsManager;
using krs::SettingDef;

namespace {

// Paint a flat color swatch onto a button.
void setSwatch(QPushButton* b, const QColor& c) {
    b->setText(c.name(QColor::HexRgb).toUpper());
    b->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid #4a5260;padding:4px;}")
        .arg(c.name(), c.lightnessF() > 0.5 ? "#111" : "#eee"));
}

// Sun direction <-> azimuth/elevation of the SUN (above the horizon).
// Stored value = direction the light TRAVELS (= -sunDir).
void dirToAzEl(const glm::vec3& travel, double& azDeg, double& elDeg) {
    glm::vec3 s = -travel;
    float len = glm::length(s);
    if (len < 1e-6f) { azDeg = 0; elDeg = 45; return; }
    s /= len;
    elDeg = qRadiansToDegrees(std::asin(std::clamp(s.y, -1.0f, 1.0f)));
    azDeg = qRadiansToDegrees(std::atan2(s.x, s.z));
    if (azDeg < 0) azDeg += 360.0;
}
glm::vec3 azElToDir(double azDeg, double elDeg) {
    const double az = qDegreesToRadians(azDeg), el = qDegreesToRadians(elDeg);
    glm::vec3 sun(float(std::cos(el) * std::sin(az)),
                  float(std::sin(el)),
                  float(std::cos(el) * std::cos(az)));
    return -sun; // travel direction
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumSize(560, 460);
    // Match the dark toolbar/panel chrome.
    setStyleSheet(QStringLiteral(
        "QDialog,QWidget{background:#2c313a;color:#e6e6e6;}"
        "QListWidget{background:#262b33;border:1px solid #4a5260;}"
        "QListWidget::item:selected{background:#0078d7;}"
        "QDoubleSpinBox,QSpinBox,QComboBox{background:#262b33;border:1px solid #4a5260;padding:2px;}"
        "QPushButton{background:#3a414d;border:1px solid #4a5260;padding:5px 12px;}"
        "QPushButton:hover{background:#454d5b;}"
        "QCheckBox{spacing:6px;}"));
    buildUi();
    // External changes (e.g. Reset) re-sync the open editors.
    connect(&SettingsManager::instance(), &SettingsManager::changed, this,
            [this](const QString& key, const QVariant&) {
                if (auto f = m_syncers.value(key)) f();
            });
}

void SettingsDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* split = new QHBoxLayout();
    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(170);
    m_stack = new QStackedWidget(this);
    split->addWidget(m_categoryList);
    split->addWidget(m_stack, 1);
    root->addLayout(split, 1);

    for (const QString& cat : SettingsManager::instance().categories()) {
        m_categoryList->addItem(cat);
        m_stack->addWidget(buildCategoryPage(cat));
    }
    connect(m_categoryList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    if (m_categoryList->count() > 0) m_categoryList->setCurrentRow(0);

    auto* buttons = new QHBoxLayout();
    auto* resetAll = new QPushButton(QStringLiteral("Reset All to Defaults"), this);
    connect(resetAll, &QPushButton::clicked, this, [] { SettingsManager::instance().resetAll(); });
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(resetAll);
    buttons->addStretch(1);
    buttons->addWidget(close);
    root->addLayout(buttons);
}

QWidget* SettingsDialog::buildCategoryPage(const QString& category) {
    auto& mgr = SettingsManager::instance();
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    for (const SettingDef& d : mgr.registry()) {
        if (d.category != category) continue;
        const QString key = d.key;
        QString label = d.label;
        if (!d.note.isEmpty()) label += QStringLiteral("  (%1)").arg(d.note);

        switch (d.type) {
        case SettingDef::Float: {
            auto* sp = new QDoubleSpinBox(page);
            sp->setRange(d.min, d.max);
            sp->setSingleStep(d.step);
            sp->setDecimals(d.decimals);
            sp->setValue(mgr.getFloat(key));
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, key](double v) {
                if (!m_updating) SettingsManager::instance().set(key, v);
            });
            m_syncers.insert(key, [this, sp, key] {
                m_updating = true; sp->setValue(SettingsManager::instance().getFloat(key)); m_updating = false;
            });
            form->addRow(label, sp);
            break;
        }
        case SettingDef::Int: {
            auto* sp = new QSpinBox(page);
            sp->setRange(int(d.min), int(d.max));
            sp->setSingleStep(int(d.step));
            sp->setValue(mgr.getInt(key));
            connect(sp, &QSpinBox::valueChanged, this, [this, key](int v) {
                if (!m_updating) SettingsManager::instance().set(key, v);
            });
            m_syncers.insert(key, [this, sp, key] {
                m_updating = true; sp->setValue(SettingsManager::instance().getInt(key)); m_updating = false;
            });
            form->addRow(label, sp);
            break;
        }
        case SettingDef::Bool: {
            auto* cb = new QCheckBox(page);
            cb->setChecked(mgr.getBool(key));
            connect(cb, &QCheckBox::toggled, this, [this, key](bool v) {
                if (!m_updating) SettingsManager::instance().set(key, v);
            });
            m_syncers.insert(key, [this, cb, key] {
                m_updating = true; cb->setChecked(SettingsManager::instance().getBool(key)); m_updating = false;
            });
            form->addRow(label, cb);
            break;
        }
        case SettingDef::Enum: {
            auto* combo = new QComboBox(page);
            for (int i = 0; i < d.enumLabels.size(); ++i)
                combo->addItem(d.enumLabels[i], d.enumValues.value(i));
            const QVariant cur = mgr.value(key);
            int idx = combo->findData(cur);
            if (idx < 0) idx = 0;
            combo->setCurrentIndex(idx);
            connect(combo, &QComboBox::currentIndexChanged, this, [this, key, combo](int) {
                if (!m_updating) SettingsManager::instance().set(key, combo->currentData());
            });
            m_syncers.insert(key, [this, combo, key] {
                m_updating = true;
                int i = combo->findData(SettingsManager::instance().value(key));
                if (i >= 0) combo->setCurrentIndex(i);
                m_updating = false;
            });
            form->addRow(label, combo);
            break;
        }
        case SettingDef::Color: {
            auto* btn = new QPushButton(page);
            setSwatch(btn, mgr.getColor(key));
            connect(btn, &QPushButton::clicked, this, [this, key, btn] {
                const QColor cur = SettingsManager::instance().getColor(key);
                const QColor c = QColorDialog::getColor(cur, this, QStringLiteral("Pick Color"));
                if (c.isValid()) { setSwatch(btn, c); SettingsManager::instance().set(key, c); }
            });
            m_syncers.insert(key, [btn, key] {
                setSwatch(btn, SettingsManager::instance().getColor(key));
            });
            form->addRow(label, btn);
            break;
        }
        case SettingDef::Vec3: {
            // Present as Azimuth (0-360) + Elevation (-90..90) of the sun.
            auto* row = new QWidget(page);
            auto* hb = new QHBoxLayout(row); hb->setContentsMargins(0, 0, 0, 0);
            auto* az = new QDoubleSpinBox(row); az->setRange(0, 360); az->setSingleStep(5); az->setDecimals(1); az->setSuffix(QStringLiteral(" az"));
            auto* el = new QDoubleSpinBox(row); el->setRange(-90, 90); el->setSingleStep(5); el->setDecimals(1); el->setSuffix(QStringLiteral(" el"));
            hb->addWidget(az); hb->addWidget(el);
            double a, e; dirToAzEl(mgr.getVec3(key), a, e); az->setValue(a); el->setValue(e);
            auto push = [this, key, az, el] {
                if (m_updating) return;
                SettingsManager::instance().set(key, krs::SettingsManager::vec3ToString(azElToDir(az->value(), el->value())));
            };
            connect(az, &QDoubleSpinBox::valueChanged, this, [push](double) { push(); });
            connect(el, &QDoubleSpinBox::valueChanged, this, [push](double) { push(); });
            m_syncers.insert(key, [this, az, el, key] {
                m_updating = true; double a2, e2; dirToAzEl(SettingsManager::instance().getVec3(key), a2, e2);
                az->setValue(a2); el->setValue(e2); m_updating = false;
            });
            form->addRow(label, row);
            break;
        }
        case SettingDef::String:
        default: {
            auto* lab = new QLabel(mgr.getString(key), page);
            form->addRow(label, lab);
            break;
        }
        }
    }
    return page;
}
