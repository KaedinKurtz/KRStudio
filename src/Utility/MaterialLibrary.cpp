#include "MaterialLibrary.hpp"

#include <QByteArray>
#include <QtGlobal>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QProcess>
#include <QString>
#include <algorithm>
#include <array>
#include <cctype>

namespace krs::materials {

// --- Offline canonical-material table (SI; bulk/shear from standard handbook
// values, e.g. CRC / MatWeb). Keyed by lowercase name and common mp-ids. ------
namespace {
struct Entry { const char* key; const char* name; double rho, K, G; }; // kg/m^3, Pa, Pa
const std::array<Entry, 14> kTable = { {
    { "steel",     "Steel (Fe alloy)", 7850.0, 160.0e9, 79.3e9 },
    { "iron",      "Iron (Fe)",        7874.0, 170.0e9, 82.0e9 },
    { "mp-13",     "Iron (Fe)",        7874.0, 170.0e9, 82.0e9 },
    { "aluminium", "Aluminium (Al)",   2700.0,  76.0e9, 26.0e9 },
    { "aluminum",  "Aluminium (Al)",   2700.0,  76.0e9, 26.0e9 },
    { "mp-134",    "Aluminium (Al)",   2700.0,  76.0e9, 26.0e9 },
    { "titanium",  "Titanium (Ti)",    4506.0, 110.0e9, 44.0e9 },
    { "mp-72",     "Titanium (Ti)",    4506.0, 110.0e9, 44.0e9 },
    { "copper",    "Copper (Cu)",      8960.0, 140.0e9, 48.0e9 },
    { "mp-30",     "Copper (Cu)",      8960.0, 140.0e9, 48.0e9 },
    { "tungsten",  "Tungsten (W)",    19250.0, 310.0e9,161.0e9 },
    { "titaniumdioxide", "TiO2",       4230.0, 210.0e9, 90.0e9 },
    { "abs",       "ABS plastic",      1050.0,   5.0e9,  0.9e9 },
    { "concrete",  "Concrete",         2400.0,  16.0e9, 11.0e9 },
} };

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Best-effort live query via mp-api (Python subprocess). Returns valid=false on
// any failure (no python / no mp-api / no key / network) so the caller falls
// back to the offline table.
MatProps liveQuery(const std::string& id)
{
    MatProps r;
    if (qEnvironmentVariable("MP_API_KEY").isEmpty()) return r; // no key -> skip live
    // Self-contained query script (kept inline so there is nothing to deploy).
    static const char* kScript =
        "import sys,os,json\n"
        "try:\n"
        "  from mp_api.client import MPRester\n"
        "  mid=sys.argv[1]; key=os.environ.get('MP_API_KEY','')\n"
        "  with MPRester(key) as m:\n"
        "    d=m.materials.summary.search(material_ids=[mid],"
        "fields=['material_id','formula_pretty','density','bulk_modulus','shear_modulus'])\n"
        "    if not d: print(json.dumps({'error':'not found'})); sys.exit(0)\n"
        "    x=d[0]\n"
        // density g/cm^3 -> kg/m^3 (x1000); MP elastic moduli are GPa -> Pa (x1e9)
        "    print(json.dumps({'name':str(getattr(x,'formula_pretty',mid)),"
        "'rho':float(getattr(x,'density',0) or 0)*1000.0,"
        "'K':float(getattr(x,'bulk_modulus',0) or 0)*1e9,"
        "'G':float(getattr(x,'shear_modulus',0) or 0)*1e9}))\n"
        "except Exception as e: print(json.dumps({'error':str(e)}))\n";
    QProcess py;
    py.start("python", { "-c", QString::fromLatin1(kScript), QString::fromStdString(id) });
    if (!py.waitForStarted(3000)) return r;
    if (!py.waitForFinished(15000)) { py.kill(); return r; }
    const QByteArray out = py.readAllStandardOutput();
    QJsonParseError pe{};
    const QJsonObject o = QJsonDocument::fromJson(out, &pe).object();
    if (pe.error != QJsonParseError::NoError || o.contains("error")) return r;
    r.density = o.value("rho").toDouble();
    r.bulkModulus = o.value("K").toDouble();
    r.shearModulus = o.value("G").toDouble();
    r.name = o.value("name").toString(QString::fromStdString(id)).toStdString();
    r.source = "Materials Project (" + id + ")";
    r.valid = r.density > 0.0 && r.bulkModulus > 0.0 && r.shearModulus > 0.0;
    return r;
}
} // namespace

MatProps query(const std::string& idOrName)
{
    MatProps live = liveQuery(idOrName);              // 1) live MP if key + mp-api present
    if (live.valid) return live;
    const std::string k = lower(idOrName);            // 2) offline canonical table
    for (const auto& e : kTable) {
        if (k == e.key) {
            MatProps r; r.name = e.name; r.density = e.rho; r.bulkModulus = e.K;
            r.shearModulus = e.G; r.source = "offline DB"; r.valid = true;
            return r;
        }
    }
    return {};                                        // unknown -> invalid
}

std::vector<std::string> offlineNames()
{
    std::vector<std::string> v;
    for (const auto& e : kTable) v.push_back(e.key);
    return v;
}

void deriveElastic(double K, double G, double& youngsE, double& poisson)
{
    const double denom = (3.0 * K + G);
    youngsE = (denom > 1e-9) ? (9.0 * K * G) / denom : 0.0;       // E = 9KG/(3K+G)
    poisson = (denom > 1e-9) ? (3.0 * K - 2.0 * G) / (2.0 * denom) : 0.0; // nu
}

double meshVolume(const std::vector<glm::vec3>& positions, const std::vector<unsigned int>& indices)
{
    double v6 = 0.0; // 6x the signed volume: sum of a . (b x c) over triangles
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        const glm::dvec3 a = positions[indices[t]];
        const glm::dvec3 b = positions[indices[t + 1]];
        const glm::dvec3 c = positions[indices[t + 2]];
        v6 += glm::dot(a, glm::cross(b, c));         // signed tetra (origin apex)
    }
    return std::abs(v6) / 6.0;
}

} // namespace krs::materials
