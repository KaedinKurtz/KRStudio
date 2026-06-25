#include "SettingsManager.hpp"

#include <QStringList>
#include <QColor>
#include <QDebug>
#include <algorithm>

namespace krs {

SettingsManager& SettingsManager::instance() {
    static SettingsManager s;
    return s;
}

SettingsManager::SettingsManager()
    : m_settings()   // QSettings() uses the org/app name set in main.cpp
{
    buildRegistry();
}

// ---------------------------------------------------------------------------
// Registry. Defaults MIRROR the current hardcoded behaviour so a fresh install
// reproduces today's look exactly. Grown tier by tier; each entry must have a
// matching applier registered by MainWindow (validated by the self-test).
// ---------------------------------------------------------------------------
void SettingsManager::buildRegistry() {
    auto F = [](QString key, QString label, QString cat, double def, double mn, double mx,
                double step, int dec, QString note = {}) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Float;
        d.defaultValue = def; d.min = mn; d.max = mx; d.step = step; d.decimals = dec; d.note = note;
        return d;
    };
    auto B = [](QString key, QString label, QString cat, bool def, QString note = {}) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Bool;
        d.defaultValue = def; d.note = note; return d;
    };
    auto C = [](QString key, QString label, QString cat, QColor def) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Color;
        d.defaultValue = def; return d;
    };
    auto V3 = [](QString key, QString label, QString cat, glm::vec3 def) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Vec3;
        d.defaultValue = vec3ToString(def); return d;
    };
    auto E = [](QString key, QString label, QString cat, QString def, QStringList labels, QVariantList vals) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Enum;
        d.defaultValue = def; d.enumLabels = labels; d.enumValues = vals; return d;
    };

    // --- Lighting ---
    m_defs.push_back(F("render/iblIntensity", "Ambient (IBL) Intensity", "Lighting", 0.40, 0.0, 2.0, 0.05, 2));
    m_defs.push_back(F("render/sunIntensity", "Sun (Key Light) Intensity", "Lighting", 3.00, 0.0, 10.0, 0.1, 2));
    m_defs.push_back(C("render/sunColor", "Sun Color", "Lighting", QColor::fromRgbF(1.0, 0.967, 0.9)));
    m_defs.push_back(V3("render/sunDirection", "Sun Direction", "Lighting", glm::vec3(-0.4f, -1.0f, -0.3f)));

    // --- Rendering Quality ---
    m_defs.push_back(F("render/tonemapExposure", "Tonemap Exposure", "Rendering Quality", 1.00, 0.1, 4.0, 0.05, 2));
    m_defs.push_back(F("render/specFireflyClamp", "Specular Firefly Clamp", "Rendering Quality", 4.00, 0.5, 16.0, 0.5, 1));
    m_defs.push_back(B("render/hdrEnabled", "HDR Pipeline (ACES Tonemap)", "Rendering Quality", true));

    // --- Viewport (SceneProperties) ---
    m_defs.push_back(C("scene/backgroundColor", "Background (no-skybox)", "Viewport", QColor::fromRgbF(0.1, 0.1, 0.15)));
    m_defs.push_back(B("scene/fogEnabled", "Distance Fog", "Viewport", true));
    m_defs.push_back(C("scene/fogColor", "Fog Color", "Viewport", QColor::fromRgbF(0.1, 0.1, 0.15)));
    m_defs.push_back(F("scene/fogStartDistance", "Fog Start Distance", "Viewport", 15.0, 0.0, 500.0, 1.0, 1));
    m_defs.push_back(F("scene/fogEndDistance", "Fog End Distance", "Viewport", 100.0, 1.0, 2000.0, 5.0, 0));
    m_defs.push_back(B("scene/showCollisionShapes", "Show Collision Shapes", "Viewport", false));

    // --- Viewport (Camera) ---
    m_defs.push_back(F("viewport/cameraFovDeg",     "Camera Field of View (deg)", "Viewport", 45.0,   20.0,   110.0,   1.0,   0));
    m_defs.push_back(F("viewport/cameraNearPlane",  "Camera Near Clip",           "Viewport", 0.001,  0.0001, 1.0,     0.001, 4));
    m_defs.push_back(F("viewport/cameraFarPlane",   "Camera Far Clip",            "Viewport", 300.0,  10.0,   10000.0, 10.0,  0));
    m_defs.push_back(F("viewport/orbitSensitivity", "Orbit Sensitivity",          "Viewport", 0.01,   0.001,  0.05,    0.001, 3));
    m_defs.push_back(F("viewport/zoomFactor",       "Zoom Step Factor",           "Viewport", 0.95,   0.80,   0.99,    0.01,  2,
                       "Lower = faster zoom per wheel click."));
    m_defs.push_back(F("viewport/lookSmoothing",    "Look Smoothing (fly cam)",   "Viewport", 0.25,   0.0,    0.95,    0.05,  2));
    m_defs.push_back(B("viewport/invertLookY",      "Invert Look/Orbit Y",        "Viewport", false));
    m_defs.push_back(E("viewport/defaultNavMode",   "Default Navigation Mode",    "Viewport", QStringLiteral("ORBIT"),
                       { QStringLiteral("Orbit"), QStringLiteral("Fly") },
                       { QStringLiteral("ORBIT"), QStringLiteral("FLY") }));
}

const SettingDef* SettingsManager::def(const QString& key) const {
    for (const auto& d : m_defs) if (d.key == key) return &d;
    return nullptr;
}

QStringList SettingsManager::categories() const {
    QStringList out;
    for (const auto& d : m_defs) if (!out.contains(d.category)) out << d.category;
    return out;
}

QVariant SettingsManager::clampToDef(const SettingDef& d, const QVariant& v) const {
    if (d.type == SettingDef::Float)
        return std::clamp(v.toDouble(), d.min, d.max);
    if (d.type == SettingDef::Int)
        return std::clamp(v.toInt(), int(d.min), int(d.max));
    return v;
}

QVariant SettingsManager::value(const QString& key) const {
    const SettingDef* d = def(key);
    if (!d) return m_settings.value(key);
    return clampToDef(*d, m_settings.value(key, d->defaultValue));
}

void SettingsManager::set(const QString& key, const QVariant& v) {
    const SettingDef* d = def(key);
    const QVariant nv = d ? clampToDef(*d, v) : v;
    if (m_settings.value(key, d ? d->defaultValue : QVariant()) == nv) return; // no-op
    m_settings.setValue(key, nv);
    m_settings.sync();
    emit changed(key, nv);
}

void SettingsManager::resetKey(const QString& key) {
    const SettingDef* d = def(key);
    if (!d) return;
    m_settings.remove(key);
    m_settings.sync();
    emit changed(key, d->defaultValue);
}

void SettingsManager::resetCategory(const QString& category) {
    for (const auto& d : m_defs) if (d.category == category) resetKey(d.key);
}

void SettingsManager::resetAll() {
    for (const auto& d : m_defs) resetKey(d.key);
}

float   SettingsManager::getFloat(const QString& key) const { return float(value(key).toDouble()); }
int     SettingsManager::getInt(const QString& key) const   { return value(key).toInt(); }
bool    SettingsManager::getBool(const QString& key) const  { return value(key).toBool(); }
QString SettingsManager::getString(const QString& key) const{ return value(key).toString(); }
QColor  SettingsManager::getColor(const QString& key) const { return value(key).value<QColor>(); }

glm::vec3 SettingsManager::getVec3(const QString& key) const { return vec3FromVariant(value(key)); }

glm::vec3 SettingsManager::getColorVec3(const QString& key) const {
    const QColor c = getColor(key);
    return glm::vec3(float(c.redF()), float(c.greenF()), float(c.blueF()));
}

QString SettingsManager::vec3ToString(const glm::vec3& v) {
    return QStringLiteral("%1,%2,%3").arg(v.x).arg(v.y).arg(v.z);
}

glm::vec3 SettingsManager::vec3FromVariant(const QVariant& v) {
    const QStringList p = v.toString().split(',');
    if (p.size() != 3) return glm::vec3(0.0f);
    return glm::vec3(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
}

void SettingsManager::setFromString(const QString& key, const QString& s) {
    const SettingDef* d = def(key);
    if (!d) { set(key, s); return; }
    switch (d->type) {
    case SettingDef::Bool:  set(key, s == "1" || s.compare("true", Qt::CaseInsensitive) == 0); break;
    case SettingDef::Int:   set(key, s.toInt()); break;
    case SettingDef::Float: set(key, s.toDouble()); break;
    case SettingDef::Color: set(key, QColor(s)); break;
    case SettingDef::Vec3:  set(key, s); break;  // "x,y,z"
    default:                set(key, s); break;
    }
}

int SettingsManager::selfTest() {
    SettingsManager& m = instance();
    int fails = 0;
    auto eq = [](const SettingDef& d, const QVariant& a, const QVariant& b) -> bool {
        switch (d.type) {
        case SettingDef::Float: return std::abs(a.toDouble() - b.toDouble()) < 1e-4;
        case SettingDef::Int:   return a.toInt() == b.toInt();
        case SettingDef::Bool:  return a.toBool() == b.toBool();
        case SettingDef::Color: return a.value<QColor>() == b.value<QColor>();
        default:                return a.toString() == b.toString();
        }
    };
    auto chk = [&](const QString& k, const char* what, bool ok) {
        if (!ok) { ++fails; qWarning().noquote() << "[SETTINGS] FAIL" << k << what; }
    };

    // Snapshot the real store so the test is non-destructive.
    struct Saved { bool had; QVariant v; };
    QHash<QString, Saved> snap;
    for (const auto& d : m.m_defs) snap.insert(d.key, { m.m_settings.contains(d.key), m.m_settings.value(d.key) });

    for (const auto& d : m.m_defs) {
        const QString k = d.key;

        // (1) default when unset
        m.m_settings.remove(k); m.m_settings.sync();
        chk(k, "default", eq(d, m.value(k), m.clampToDef(d, d.defaultValue)));

        // (2) round-trip + persistence (fresh QSettings reads it back)
        QVariant test;
        switch (d.type) {
        case SettingDef::Bool:  test = !d.defaultValue.toBool(); break;
        case SettingDef::Int:   test = std::clamp(d.defaultValue.toInt() + 1, int(d.min), int(d.max)); break;
        case SettingDef::Float: test = std::clamp(d.defaultValue.toDouble() + d.step, d.min, d.max); break;
        case SettingDef::Color: test = QColor(Qt::magenta); break;
        case SettingDef::Vec3:  test = QStringLiteral("0.1,0.2,0.3"); break;
        default: {
            // Enum/String: pick a value that DIFFERS from the default, otherwise
            // set() no-ops (value==default) and nothing is persisted to verify.
            if (!d.enumValues.isEmpty()) {
                QVariant t = d.enumValues.value(0);
                for (const QVariant& ev : d.enumValues)
                    if (ev.toString() != d.defaultValue.toString()) { t = ev; break; }
                test = t;
            } else {
                test = QStringLiteral("selftest-x");
            }
            break;
        }
        }
        m.set(k, test);
        chk(k, "roundtrip", eq(d, m.value(k), m.clampToDef(d, test)));
        { QSettings fresh; chk(k, "persist", eq(d, fresh.value(k), m.clampToDef(d, test))); }

        // (3) clamp (numeric out-of-range)
        if (d.type == SettingDef::Float) { m.set(k, d.max + 1000.0); chk(k, "clampHi", std::abs(m.getFloat(k) - float(d.max)) < 1e-3); }
        if (d.type == SettingDef::Int)   { m.set(k, int(d.max) + 1000); chk(k, "clampHi", m.getInt(k) == int(d.max)); }

        // (4) reset to default
        m.resetKey(k);
        chk(k, "reset", eq(d, m.value(k), m.clampToDef(d, d.defaultValue)));
    }

    // Restore the snapshot exactly.
    for (auto it = snap.begin(); it != snap.end(); ++it) {
        if (it.value().had) m.m_settings.setValue(it.key(), it.value().v);
        else                m.m_settings.remove(it.key());
    }
    m.m_settings.sync();

    qInfo().noquote().nospace() << "[SETTINGS] self-test " << (fails == 0 ? "PASS" : "FAIL")
                                << " (" << int(m.m_defs.size()) << " keys, " << fails << " failures)";
    return fails;
}

} // namespace krs
