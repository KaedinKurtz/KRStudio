#pragma once
// ===========================================================================
// KPARTS -- the manufacturer-parts ecosystem: a searchable LIBRARY of reusable component
// definitions (.kmotor / .kactuator / .kgearbox / .kcamera / .kimu / .kmaterial) plus a
// REPOSITORY abstraction for local caching + cloud contribution.
//
// LIBRARY: scans a SEARCH PATH (scene-local -> project -> user vendor packs, most-specific wins)
// for part files, indexing each by {type, id, name, manufacturer, relative path, content hash}.
// The Manufacturer Parts panel lists the index; a drag drops a part's PATH (reusing the existing
// "application/x-krstudio-asset" mime) onto a joint/body to apply it.
//
// REPOSITORY: an abstract PartRepository -- pull(id)/publish(part)/sync() over a content-addressed
// index. LocalRepository (filesystem, shipped) is the offline cache AND the on-disk format the
// future RemoteRepository mirrors: publishing to the cloud is "push my LocalRepository's new/
// changed parts to the server; pull theirs". Parts are immutable-by-content (id + hash), so the
// same part from two sources de-dupes and a changed part is a NEW version -- the exact recognize-
// as-different rule the .kscene family already uses.
//
// Everything JSON, versioned ("<family>/<major>"), refuse unknown majors. Nothing silently binds.
// ===========================================================================
#include <string>
#include <vector>
#include <memory>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QJsonObject>

namespace krs::parts {

enum class PartType { Unknown = 0, Motor, Actuator, Gearbox, Encoder, Camera, Imu, Material };

const char* extensionFor(PartType t);      // ".kmotor" ... (no dot -> "" for Unknown)
PartType    typeForExtension(const QString& ext);   // "kmotor" / ".kmotor" -> Motor
const char* displayName(PartType t);

// One indexed part (the catalog row; the full spec is parsed on demand from `absPath`).
struct PartEntry {
    PartType    type = PartType::Unknown;
    QString     id;                        // stable uuid minted at authoring ("" = legacy/unset)
    QString     name;                      // "EC-45 flat 70W"
    QString     manufacturer;              // "Maxon"
    QString     absPath;                   // resolved absolute path
    QString     contentHash;               // sha1 of the file bytes (content address + change detect)
    QString     source;                    // which search-path root it came from (for the UI + sync)
};

// ---- LIBRARY (the search-path index) -------------------------------------------------------
class PartLibrary {
public:
    // Search roots, most-specific FIRST (scene-local shadows project shadows user). A part id seen
    // in an earlier root wins; a NEW id from a later root is added (union with precedence).
    void setSearchRoots(const QStringList& rootsMostSpecificFirst) { roots_ = rootsMostSpecificFirst; }
    const QStringList& searchRoots() const { return roots_; }

    // (Re)scan every root recursively for part files. Returns the number indexed.
    int rescan();

    const std::vector<PartEntry>& entries() const { return entries_; }
    std::vector<PartEntry> byType(PartType t) const;
    const PartEntry* findById(const QString& id) const;
    const PartEntry* findByPath(const QString& absPath) const;

private:
    QStringList roots_;
    std::vector<PartEntry> entries_;
};

// process-wide default library (the panel + drop handlers read this).
PartLibrary& library();

// Build the standard search path for a scene: <scene dir>/parts, <project>/parts (KRS_SOURCE_DIR),
// <user>/KRStudio/parts. Missing roots are simply skipped by rescan().
QStringList standardSearchRoots(const QString& sceneDir);

// ---- REPOSITORY (local cache + cloud-contribution seam) -------------------------------------
struct RepoStatus {
    bool   connected = false;              // a remote is reachable (always false for LocalRepository)
    int    localParts = 0;
    int    pendingUpload = 0;              // local parts not yet on the remote (by id+hash)
    QString backend;                       // "local" / "https://parts.krstudio.io" / ...
    QString message;
};

// A published part = its file bytes + the index metadata. Content-addressed by (id, hash).
struct PartBlob {
    PartEntry meta;
    QByteArray bytes;
};

class PartRepository {
public:
    virtual ~PartRepository() = default;
    virtual RepoStatus status() = 0;
    virtual std::vector<PartEntry> index() = 0;                       // catalog of everything available
    virtual bool pull(const QString& id, PartBlob& out) = 0;          // fetch one part's bytes
    virtual bool publish(const PartBlob& blob, QString* error) = 0;   // contribute a part
    // Push every local part the remote lacks (by id+hash) and pull every remote part we lack.
    // Returns a human-readable summary; sets counts via status() afterward. LocalRepository is a
    // no-op sync (it IS the local store).
    virtual QString sync(QString* error) = 0;
};

// Filesystem repository: an on-disk content-addressed store (index.json + parts/<id>/<hash><ext>).
// This is the offline cache the app reads/writes; it is ALSO the wire format a RemoteRepository
// mirrors, so publish/pull semantics are identical online and off.
std::unique_ptr<PartRepository> makeLocalRepository(const QString& rootDir);

// Cloud repository seam: today returns a not-connected stub whose status().message explains how to
// point it at a server; the HTTP contract (GET /index, GET /part/<id>/<hash>, POST /part) is
// documented at the definition. Swapping in a live backend needs no caller changes.
std::unique_ptr<PartRepository> makeRemoteRepository(const QString& baseUrl, const QString& apiKey);

// ---- authoring helpers (write a part file with correct format + id) --------------------------
// Mint a versioned part-file JSON object skeleton ({format, id, type, name, manufacturer}); the
// caller fills the type-specific fields. `id` empty -> a fresh uuid.
QString writePartFile(const QString& absPath, PartType type, const QString& name,
                      const QString& manufacturer, const QJsonObject& typeFields,
                      const QString& id = QString());

// Headless gate (KRS_KPARTS_SELFTEST): search-path precedence + rescan; local repository publish/
// pull/sync round-trip + content de-dupe + change-as-new-version; remote stub is honestly
// not-connected. Returns true iff all pass.
bool runKPartsGate();

} // namespace krs::parts
