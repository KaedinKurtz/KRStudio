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
    auto I = [](QString key, QString label, QString cat, int def, int mn, int mx,
                int step = 1, QString note = {}) {
        SettingDef d; d.key = key; d.label = label; d.category = cat; d.type = SettingDef::Int;
        d.defaultValue = def; d.min = double(mn); d.max = double(mx); d.step = double(step); d.note = note;
        return d;
    };

    // --- Lighting (physically-based: nits / lux, brought to display by the EV exposure) ---
    // NOTE: keys renamed from render/iblIntensity + render/sunIntensity so old (non-photometric)
    // persisted values don't fight the new scale; fresh photometric defaults apply.
    m_defs.push_back(F("render/iblNits", "Ambient (IBL) Luminance [nits]", "Lighting", 50.0, 0.0, 5000.0, 1.0, 1));
    m_defs.push_back(F("render/sunLux", "Sun (Key Light) [lux]", "Lighting", 2000.0, 0.0, 150000.0, 100.0, 0));
    m_defs.push_back(F("render/exposureEV", "Camera Exposure [EV100]", "Lighting", 10.0, 0.0, 20.0, 0.25, 2));
    m_defs.push_back(E("ui/theme", "UI Theme", "Theme", QStringLiteral("dark"),
        { QStringLiteral("Dark"), QStringLiteral("Light") },
        { QStringLiteral("dark"), QStringLiteral("light") }));
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

    // --- Physics --- (scene-desc knobs bake at createScene -> apply on next Play;
    //                  gravity & rate are live)
    m_defs.push_back(B("physics/gpuEnabled",             "GPU Rigid-Body Physics",          "Physics", true,
                       "Requires CUDA. Takes effect on next Play (Stop -> Play)."));
    m_defs.push_back(I("physics/gpuMinBodies",           "GPU Min Dynamic Bodies",          "Physics", 256, 0, 100000, 1,
                       "Use the GPU solver only above this many dynamic bodies; it only amortizes its overhead on large scenes."));
    m_defs.push_back(E("physics/solverType",             "Solver Type",                     "Physics", QStringLiteral("TGS"),
                       { QStringLiteral("TGS (accurate)"), QStringLiteral("PGS (fast)") },
                       { QStringLiteral("TGS"), QStringLiteral("PGS") }));
    m_defs.push_back(B("physics/ccdEnabled",             "Continuous Collision (CCD)",      "Physics", true,
                       "Stops fast bodies tunneling. Next Play."));
    m_defs.push_back(B("physics/stabilizationEnabled",   "Contact Stabilization",           "Physics", true, "Next Play."));
    m_defs.push_back(B("physics/pcmEnabled",             "Persistent Contact Manifolds",    "Physics", true, "Next Play."));
    m_defs.push_back(F("physics/bounceThresholdVelocity","Bounce Threshold Velocity (m/s)", "Physics", 0.50, 0.05, 5.0, 0.05, 2,
                       "Impacts slower than this don't bounce. Next Play."));
    m_defs.push_back(B("physics/weldCollisionMeshes",    "Weld Imported Collision Meshes",  "Physics", true,
                       "Removes spurious internal edges. Applies when meshes are next cooked (import / Play)."));
    m_defs.push_back(F("physics/gravity",                "Gravity (m/s^2, downward)",       "Physics", 9.81, 0.0, 30.0, 0.01, 2,
                       "Live: applies to the running scene immediately."));
    m_defs.push_back(I("physics/simRateHz",             "Physics Rate (Hz)",               "Physics", 240, 30, 1000, 10,
                       "Fixed-timestep substep rate. Live."));

    // --- Performance ---
    m_defs.push_back(B("perf/vsync",  "Vertical Sync (VSync)",            "Performance", true,
                       "Caps frames to the display refresh. Restart required."));
    m_defs.push_back(I("perf/maxFps", "Frame Rate Cap (0 = unlimited)",   "Performance", 0, 0, 360, 5,
                       "Live. Cannot exceed the display rate while VSync is on."));

    // --- Units & Display --- (the engine stores metres/radians; these are display only)
    m_defs.push_back(E("units/length", "Length Unit", "Units & Display", QStringLiteral("m"),
                       { QStringLiteral("Meters"), QStringLiteral("Centimeters"), QStringLiteral("Millimeters"),
                         QStringLiteral("Inches"), QStringLiteral("Feet") },
                       { QStringLiteral("m"), QStringLiteral("cm"), QStringLiteral("mm"),
                         QStringLiteral("in"), QStringLiteral("ft") }));
    m_defs.push_back(I("units/lengthDecimals", "Length Precision (decimals)", "Units & Display", 3, 0, 6, 1));
    m_defs.push_back(E("units/angle", "Angle Unit", "Units & Display", QStringLiteral("deg"),
                       { QStringLiteral("Degrees"), QStringLiteral("Radians") },
                       { QStringLiteral("deg"), QStringLiteral("rad") }));
    m_defs.push_back(I("units/angleDecimals", "Angle Precision (decimals)", "Units & Display", 2, 0, 6, 1));
    {
        SettingDef cad = E("units/cadImportUnit", "CAD (STEP) Import Unit", "Units & Display", QStringLiteral("mm"),
                           { QStringLiteral("Millimeters"), QStringLiteral("Centimeters"),
                             QStringLiteral("Meters"), QStringLiteral("Inches") },
                           { QStringLiteral("mm"), QStringLiteral("cm"), QStringLiteral("m"), QStringLiteral("in") });
        cad.note = QStringLiteral("Used for the next STEP import.");
        m_defs.push_back(cad);
    }

    // --- Simulation Capacity --- (these size/realloc GPU buffers at subsystem
    //     init, so they are RESTART-gated, NOT hot-swap, per design.)
    {
        SettingDef fb = E("sim/fluidBackend", "Fluid Solver Backend", "Simulation", QStringLiteral("auto"),
                          { QStringLiteral("Auto"), QStringLiteral("PBF (GPU)"), QStringLiteral("DFSPH (CPU)") },
                          { QStringLiteral("auto"), QStringLiteral("pbf"), QStringLiteral("dfsph") });
        fb.note = QStringLiteral("Restart required.");
        m_defs.push_back(fb);
    }
    m_defs.push_back(F("sim/fluidParticleRadius", "Fluid Particle Radius (m)", "Simulation", 0.025, 0.005, 0.1, 0.001, 4,
                       "Smaller = higher detail and more particles. Restart required."));
    m_defs.push_back(I("sim/smokeGridResolution", "Smoke Grid Resolution (0 = auto)", "Simulation", 0, 0, 256, 8,
                       "N^3 voxels; 0 picks the per-GPU default. Restart required."));
    m_defs.push_back(I("sim/mpmGridResolution", "MPM Grid Resolution (0 = auto)", "Simulation", 0, 0, 192, 4,
                       "N^3 cells; 0 picks the per-GPU default. Restart required."));
    m_defs.push_back(I("sim/mpmMaxParticles", "MPM Max Particles", "Simulation", 240000, 10000, 1000000, 10000,
                       "GPU particle-buffer capacity. Restart required."));
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

// ---------------------------------------------------------------------------
// Display-units helpers (see SettingsManager.hpp). Canonical storage is metres
// and radians; these convert to/from the user's chosen display units.
// ---------------------------------------------------------------------------
namespace units {

// display length units per metre.
static double lengthFactor() {
    const QString u = SettingsManager::instance().getString(QStringLiteral("units/length"));
    if (u == QLatin1String("cm")) return 100.0;
    if (u == QLatin1String("mm")) return 1000.0;
    if (u == QLatin1String("in")) return 1.0 / 0.0254;        // 39.37007874...
    if (u == QLatin1String("ft")) return 1.0 / 0.3048;        // 3.280839895...
    return 1.0;                                               // metres
}

double  metersToDisplay(double meters) { return meters * lengthFactor(); }
double  displayToMeters(double disp)   { const double f = lengthFactor(); return f != 0.0 ? disp / f : disp; }

QString lengthSuffix() {
    const QString u = SettingsManager::instance().getString(QStringLiteral("units/length"));
    if (u == QLatin1String("cm")) return QStringLiteral(" cm");
    if (u == QLatin1String("mm")) return QStringLiteral(" mm");
    if (u == QLatin1String("in")) return QStringLiteral(" in");
    if (u == QLatin1String("ft")) return QStringLiteral(" ft");
    return QStringLiteral(" m");
}

int lengthDecimals() { return SettingsManager::instance().getInt(QStringLiteral("units/lengthDecimals")); }

bool angleIsDegrees() {
    return SettingsManager::instance().getString(QStringLiteral("units/angle")) != QLatin1String("rad");
}

double radiansToDisplay(double radians) { return angleIsDegrees() ? glm::degrees(radians) : radians; }
double displayToRadians(double disp)    { return angleIsDegrees() ? glm::radians(disp)   : disp; }

QString angleSuffix()     { return angleIsDegrees() ? QString::fromUtf8("\xC2\xB0") : QStringLiteral(" rad"); } // "°"
int     angleDecimals()   { return SettingsManager::instance().getInt(QStringLiteral("units/angleDecimals")); }
double  angleDisplayLimit() { return angleIsDegrees() ? 180.0 : 3.14159265358979323846; }

double cadMetersPerUnit() {
    const QString u = SettingsManager::instance().getString(QStringLiteral("units/cadImportUnit"));
    if (u == QLatin1String("cm")) return 0.01;
    if (u == QLatin1String("m"))  return 1.0;
    if (u == QLatin1String("in")) return 0.0254;
    return 0.001;  // millimetres (STEP default)
}

} // namespace units

} // namespace krs
