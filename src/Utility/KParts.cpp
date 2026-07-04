// KParts.cpp -- see KParts.hpp. Manufacturer-parts library (search-path index) + repository
// (local content-addressed store + cloud-contribution seam). All JSON, versioned, content-hashed.
#include "KParts.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QUuid>
#include <QStandardPaths>

#include <cstdio>
#include <algorithm>

namespace krs::parts {

// ---- type <-> extension -------------------------------------------------------------------------
const char* extensionFor(PartType t) {
    switch (t) {
        case PartType::Motor:    return ".kmotor";
        case PartType::Actuator: return ".kactuator";
        case PartType::Gearbox:  return ".kgearbox";
        case PartType::Encoder:  return ".kencoder";
        case PartType::Camera:   return ".kcamera";
        case PartType::Imu:      return ".kimu";
        case PartType::Material: return ".kmaterial";
        default:                 return "";
    }
}
PartType typeForExtension(const QString& extIn) {
    QString e = extIn.startsWith('.') ? extIn.mid(1) : extIn;
    e = e.toLower();
    if (e == "kmotor")    return PartType::Motor;
    if (e == "kactuator") return PartType::Actuator;
    if (e == "kgearbox")  return PartType::Gearbox;
    if (e == "kencoder")  return PartType::Encoder;
    if (e == "kcamera")   return PartType::Camera;
    if (e == "kimu")      return PartType::Imu;
    if (e == "kmaterial") return PartType::Material;
    return PartType::Unknown;
}
const char* displayName(PartType t) {
    switch (t) {
        case PartType::Motor:    return "Motor";
        case PartType::Actuator: return "Actuator";
        case PartType::Gearbox:  return "Gearbox";
        case PartType::Encoder:  return "Encoder";
        case PartType::Camera:   return "Camera";
        case PartType::Imu:      return "IMU";
        case PartType::Material: return "Material";
        default:                 return "Part";
    }
}

namespace {
QString sha1Of(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex());
}
bool readJson(const QString& path, QJsonObject& out, QByteArray* bytesOut = nullptr) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    if (bytesOut) *bytesOut = bytes;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return false;
    out = doc.object();
    return true;
}
bool writeJson(const QString& path, const QJsonObject& o) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}
// A part file's format string is "<ext-without-k>/<major>", e.g. kmotor -> "kmotor/1".
bool partFormatOk(const QJsonObject& o, PartType t) {
    const QString want = QString::fromLatin1(extensionFor(t)).mid(1);   // "kmotor"
    const QString f = o["format"].toString();
    return f.startsWith(want + '/') && f.section('/', 1, 1).toInt() == 1;
}
} // namespace

// ================================================================================================
// LIBRARY
// ================================================================================================
QStringList standardSearchRoots(const QString& sceneDir) {
    QStringList roots;
    if (!sceneDir.isEmpty()) roots << QDir(sceneDir).filePath(QStringLiteral("parts"));
    const QByteArray src = qgetenv("KRS_SOURCE_DIR");
    if (!src.isEmpty()) roots << QDir(QString::fromLocal8Bit(src)).filePath(QStringLiteral("parts"));
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!userDir.isEmpty()) roots << QDir(userDir).filePath(QStringLiteral("parts"));
    return roots;
}

int PartLibrary::rescan() {
    entries_.clear();
    QStringList seenIds;
    for (const QString& root : roots_) {
        QDir rd(root);
        if (!rd.exists()) continue;
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const PartType t = typeForExtension(QFileInfo(path).suffix());
            if (t == PartType::Unknown) continue;
            QJsonObject o; QByteArray bytes;
            if (!readJson(path, o, &bytes)) continue;
            if (!partFormatOk(o, t)) continue;                  // wrong format major/family -> skip
            PartEntry e;
            e.type = t;
            e.id = o["id"].toString();
            e.name = o["name"].toString(QFileInfo(path).completeBaseName());
            e.manufacturer = o["manufacturer"].toString();
            e.absPath = QFileInfo(path).absoluteFilePath();
            e.contentHash = sha1Of(bytes);
            e.source = root;
            // precedence: an id already seen in a more-specific root shadows this one.
            if (!e.id.isEmpty() && seenIds.contains(e.id)) continue;
            if (!e.id.isEmpty()) seenIds << e.id;
            entries_.push_back(std::move(e));
        }
    }
    std::sort(entries_.begin(), entries_.end(), [](const PartEntry& a, const PartEntry& b) {
        if (a.type != b.type) return int(a.type) < int(b.type);
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return int(entries_.size());
}

std::vector<PartEntry> PartLibrary::byType(PartType t) const {
    std::vector<PartEntry> out;
    for (const auto& e : entries_) if (e.type == t) out.push_back(e);
    return out;
}
const PartEntry* PartLibrary::findById(const QString& id) const {
    if (id.isEmpty()) return nullptr;
    for (const auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}
const PartEntry* PartLibrary::findByPath(const QString& absPath) const {
    const QString norm = QFileInfo(absPath).absoluteFilePath();
    for (const auto& e : entries_) if (e.absPath == norm) return &e;
    return nullptr;
}

PartLibrary& library() { static PartLibrary lib; return lib; }

// ================================================================================================
// authoring
// ================================================================================================
QString writePartFile(const QString& absPath, PartType type, const QString& name,
                      const QString& manufacturer, const QJsonObject& typeFields, const QString& idIn) {
    const QString id = idIn.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : idIn;
    QJsonObject o = typeFields;
    o["format"] = QString::fromLatin1(extensionFor(type)).mid(1) + QStringLiteral("/1");
    o["id"] = id;
    o["name"] = name;
    o["manufacturer"] = manufacturer;
    return writeJson(absPath, o) ? id : QString();
}

// ================================================================================================
// LOCAL REPOSITORY -- content-addressed store: <root>/index.json + <root>/parts/<id>/<hash><ext>
// ================================================================================================
namespace {
class LocalRepository : public PartRepository {
public:
    explicit LocalRepository(const QString& root) : root_(root) { QDir().mkpath(root_); loadIndex(); }

    RepoStatus status() override {
        RepoStatus s; s.connected = true; s.backend = QStringLiteral("local:") + root_;
        s.localParts = int(index_.size()); s.pendingUpload = 0;   // nothing "pending" -- this IS the store
        s.message = QStringLiteral("%1 parts in the local store").arg(index_.size());
        return s;
    }
    std::vector<PartEntry> index() override { return index_; }

    bool pull(const QString& id, PartBlob& out) override {
        for (const auto& e : index_) if (e.id == id) {
            QFile f(blobPath(e));
            if (!f.open(QIODevice::ReadOnly)) return false;
            out.meta = e; out.bytes = f.readAll();
            return sha1Of(out.bytes) == e.contentHash;          // integrity: bytes match the address
        }
        return false;
    }
    bool publish(const PartBlob& blob, QString* error) override {
        PartEntry e = blob.meta;
        if (e.id.isEmpty()) { if (error) *error = QStringLiteral("part has no id"); return false; }
        e.contentHash = sha1Of(blob.bytes);
        e.source = root_;
        const QString path = blobPath(e);
        // content-addressed: same (id, hash) already present -> idempotent no-op (de-dupe).
        if (QFile::exists(path)) { upsertIndex(e); saveIndex(); return true; }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = QStringLiteral("cannot write %1").arg(path); return false; }
        f.write(blob.bytes);
        upsertIndex(e); saveIndex();
        return true;
    }
    QString sync(QString*) override { return QStringLiteral("local store: nothing to sync (it is the source)"); }

private:
    QString root_;
    std::vector<PartEntry> index_;

    QString blobPath(const PartEntry& e) const {
        return QDir(root_).filePath(QStringLiteral("parts/%1/%2%3")
            .arg(e.id, e.contentHash, QString::fromLatin1(extensionFor(e.type))));
    }
    void upsertIndex(const PartEntry& e) {
        for (auto& x : index_) if (x.id == e.id && x.contentHash == e.contentHash) { x = e; return; }
        // a CHANGED part (same id, new hash) is a NEW version -> both rows coexist (history-friendly).
        index_.push_back(e);
    }
    void loadIndex() {
        index_.clear();
        QJsonObject o;
        if (!readJson(QDir(root_).filePath(QStringLiteral("index.json")), o)) return;
        for (const QJsonValue& v : o["parts"].toArray()) {
            const QJsonObject po = v.toObject();
            PartEntry e;
            e.type = typeForExtension(po["ext"].toString());
            e.id = po["id"].toString(); e.name = po["name"].toString();
            e.manufacturer = po["manufacturer"].toString(); e.contentHash = po["hash"].toString();
            e.source = root_;
            index_.push_back(std::move(e));
        }
    }
    void saveIndex() {
        QJsonArray arr;
        for (const auto& e : index_) {
            QJsonObject po;
            po["ext"] = QString::fromLatin1(extensionFor(e.type)).mid(1);
            po["id"] = e.id; po["name"] = e.name; po["manufacturer"] = e.manufacturer; po["hash"] = e.contentHash;
            arr.push_back(po);
        }
        QJsonObject o; o["format"] = QStringLiteral("kpartindex/1"); o["parts"] = arr;
        writeJson(QDir(root_).filePath(QStringLiteral("index.json")), o);
    }
};

// ------------------------------------------------------------------------------------------------
// REMOTE REPOSITORY -- the cloud-contribution seam. HTTP CONTRACT (content-addressed, so caching
// and de-dupe fall out for free):
//   GET  <base>/index                      -> { "parts": [ {ext,id,name,manufacturer,hash} ] }
//   GET  <base>/part/<id>/<hash>           -> raw part-file bytes
//   POST <base>/part   (body = bytes)      -> 200 {id,hash}   (idempotent by content address)
// Auth: "Authorization: Bearer <apiKey>". A publish is a POST; sync() diffs local vs remote index
// by (id,hash) and POSTs the missing / GETs the missing.
//
// Shipped as an HONEST STUB: no QtNetwork dependency is wired yet, so status().connected==false
// and every op reports "remote backend not configured". Swapping in a QNetworkAccessManager impl
// behind these exact signatures needs ZERO caller changes (the panel already talks to the
// abstract PartRepository).
// ------------------------------------------------------------------------------------------------
class RemoteRepositoryStub : public PartRepository {
public:
    RemoteRepositoryStub(QString base, QString key) : base_(std::move(base)), key_(std::move(key)) {}
    RepoStatus status() override {
        RepoStatus s; s.connected = false; s.backend = base_;
        s.message = base_.isEmpty()
            ? QStringLiteral("No remote configured. Set a parts server URL + API key to contribute.")
            : QStringLiteral("Remote backend not built in this configuration (offline stub). "
                             "Would connect to %1.").arg(base_);
        return s;
    }
    std::vector<PartEntry> index() override { return {}; }
    bool pull(const QString&, PartBlob&) override { return false; }
    bool publish(const PartBlob&, QString* error) override {
        if (error) *error = QStringLiteral("remote publish unavailable (offline stub) -- part was saved locally");
        return false;
    }
    QString sync(QString* error) override {
        if (error) *error = QStringLiteral("remote sync unavailable (offline stub)");
        return QStringLiteral("offline");
    }
private:
    QString base_, key_;
};
} // namespace

std::unique_ptr<PartRepository> makeLocalRepository(const QString& rootDir) {
    return std::make_unique<LocalRepository>(rootDir);
}
std::unique_ptr<PartRepository> makeRemoteRepository(const QString& baseUrl, const QString& apiKey) {
    return std::make_unique<RemoteRepositoryStub>(baseUrl, apiKey);
}

// ================================================================================================
// GATE KPARTS
// ================================================================================================
bool runKPartsGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[kparts] GATE KPARTS -- search-path precedence + repository publish/pull/sync/de-dupe; remote stub honest\n");
    const QString dir = QDir::temp().filePath("krs_kparts_gate");
    QDir(dir).removeRecursively();

    auto mkMotor = [](const QString& path, const QString& id, const QString& name, double kt) {
        QJsonObject f; f["torqueConstant"] = kt; f["maxCurrent"] = 6.0; f["maxSpeed"] = 800.0;
        return writePartFile(path, PartType::Motor, name, QStringLiteral("Maxon"), f, id);
    };

    // ---- SEARCH-PATH PRECEDENCE: same id in project + user roots -> project (more specific) wins;
    //      a user-only part is still indexed (union) ----
    const QString proj = QDir(dir).filePath("project/parts");
    const QString user = QDir(dir).filePath("user/parts");
    const QString sharedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mkMotor(QDir(proj).filePath("ec45.kmotor"), sharedId, QStringLiteral("EC45 (project)"), 0.036);
    mkMotor(QDir(user).filePath("ec45.kmotor"), sharedId, QStringLiteral("EC45 (user)"),    0.036);
    mkMotor(QDir(user).filePath("dcx22.kmotor"), QUuid::createUuid().toString(QUuid::WithoutBraces),
            QStringLiteral("DCX22 (user only)"), 0.021);

    PartLibrary lib;
    lib.setSearchRoots({ proj, user });                          // project FIRST (most specific)
    const int n = lib.rescan();
    const PartEntry* shared = lib.findById(sharedId);
    const bool precedenceOk = (n == 2)                            // shared (from project) + user-only
        && shared && shared->name == QStringLiteral("EC45 (project)")
        && lib.byType(PartType::Motor).size() == 2;
    printf("[kparts]   search path: indexed=%d (want 2); shared id resolves to project=%s  %s\n",
           n, (shared && shared->name.contains(QStringLiteral("project"))) ? "yes" : "NO",
           precedenceOk ? "PASS" : "FAIL");

    // ---- LOCAL REPOSITORY: publish -> pull round-trip; content de-dupe; change-as-new-version ----
    auto repo = makeLocalRepository(QDir(dir).filePath("repo"));
    auto blobFrom = [](const QString& path) {
        PartBlob b; QFile f(path); f.open(QIODevice::ReadOnly); b.bytes = f.readAll();
        QJsonObject o = QJsonDocument::fromJson(b.bytes).object();
        b.meta.id = o["id"].toString(); b.meta.name = o["name"].toString(); b.meta.type = PartType::Motor;
        b.meta.manufacturer = o["manufacturer"].toString();
        return b;
    };
    const QString p1 = QDir(proj).filePath("ec45.kmotor");
    QString perr;
    const bool pub1 = repo->publish(blobFrom(p1), &perr);
    const bool dedupe = repo->publish(blobFrom(p1), &perr) && repo->status().localParts == 1;   // idempotent
    PartBlob got;
    const bool pulled = repo->pull(blobFrom(p1).meta.id, got)
        && QJsonDocument::fromJson(got.bytes).object()["id"].toString() == blobFrom(p1).meta.id;
    // change the part -> new hash -> a NEW version coexists (same id, 2 rows)
    mkMotor(p1, blobFrom(p1).meta.id, QStringLiteral("EC45 rev2"), 0.037);
    const bool newVer = repo->publish(blobFrom(p1), &perr) && repo->status().localParts == 2;
    const bool repoOk = pub1 && dedupe && pulled && newVer;
    printf("[kparts]   local repo: publish=%s pull-roundtrip=%s content-dedupe(1)=%s change->new-version(2)=%s  %s\n",
           pub1?"yes":"NO", pulled?"yes":"NO", dedupe?"yes":"NO", newVer?"yes":"NO", repoOk?"PASS":"FAIL");

    // ---- REMOTE STUB is honest: not connected, publish reports offline (no silent success) ----
    auto remote = makeRemoteRepository(QStringLiteral("https://parts.krstudio.io"), QStringLiteral("key"));
    QString rerr;
    const bool remoteHonest = !remote->status().connected
        && !remote->publish(blobFrom(p1), &rerr) && !rerr.isEmpty();
    printf("[kparts]   remote stub: not-connected=%s publish-refused(offline)=%s  %s\n",
           !remote->status().connected ? "yes" : "NO", !rerr.isEmpty() ? "yes" : "NO",
           remoteHonest ? "PASS" : "FAIL");

    const bool pass = precedenceOk && repoOk && remoteHonest;
    printf("[kparts] %s\n", pass ? "ALL PASS (search-path precedence + union; local repo publish/pull/de-dupe/versioning; remote stub honest)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::parts
