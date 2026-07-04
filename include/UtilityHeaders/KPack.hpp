#pragma once
// ===========================================================================
// KPack -- the cross-user SUBGRAPH SHARING layer (krs::kpack). A .knodepack is ONE
// portable file bundling a root .knode subgraph PLUS its entire transitive nested
// closure, so a colleague receives a single self-contained artifact, imports it
// into their own library, and the subgraph appears as a draggable knode:<id> node.
//
// FORMAT ("kpack/1"): a JSON manifest whose every member is embedded VERBATIM as
// base64 of the exact original file bytes -- NEVER a re-serialization -- so each
// member's whole-file sha1 still equals its content-address on the recipient
// (matching PartLibrary's scan hash). Each entry also carries the menu-surfacing
// fields (name, category, revision, description) the bare .knode codec does not.
//
// SAFETY: importing a .knodepack executes NO code. A member is pure data; a node's
// only restorable state is the krs::kdoc closed value union {double,float,int,
// long long,bool,string,vec3} applied through node setters -- there is no path,
// exec, or dlopen surface in the decode. installPack additionally validates every
// entry id/hash against a strict charset BEFORE using it in a destination path, so
// a hostile pack cannot traverse out of the library root.
// ===========================================================================
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <vector>

namespace krs::parts { class PartLibrary; }

namespace krs::kpack {

// One bundled member (a .knode file) + its manifest metadata.
struct PackEntry {
    QString    id;            // the member's stable uuid (its content-address KEY, with contentHash)
    QString    contentHash;   // sha1 of the verbatim file bytes
    QString    ext;           // ".knode"
    QString    name;          // menu label
    QString    category;      // menu grouping
    QString    description;   // tooltip (the datum the .knode codec drops -> the manifest carries it)
    int        revision = 1;
    QByteArray bytes;         // the exact original file bytes (base64 in the file)
};
struct PackDoc {
    QString rootId, rootHash;         // which entry is the shared subgraph's root
    std::vector<PackEntry> entries;   // root + transitive nested closure, deduped by (id,hash)
};

// Import outcome (shown to the user; nothing is silently dropped).
struct InstallReport {
    int installed = 0;                // members newly written into the library
    int skippedDuplicate = 0;         // members already present at the same (id,hash) -> content dedupe
    QStringList registered;           // "knode:<id>" types now creatable
    QStringList missingDeps;          // interior node typeIds an imported subgraph needs but the recipient lacks
    QStringList warnings;             // rejected entries (bad charset), shadowed ids, etc.
};

bool packFormatOk(const QString& formatString);   // "kpack/1" -> major 1 only

// The transitive dependency closure of a root .knode: its nested[] ids + its "knode:<id>" interior
// typeIds, resolved to library files by id, recursively, with a VISITED/cycle guard (a self- or
// mutually-referential nest never loops). Returns the closure's absolute paths (root first). Any
// dependency id with no library file is appended to `missing` (a report, not a crash).
QStringList collectClosure(const QString& rootKnodeAbsPath, krs::parts::PartLibrary& lib,
                           QStringList* missing = nullptr, QString* err = nullptr);

// Write a .knodepack bundling the root subgraph + its closure (members read VERBATIM). Returns the
// root id, or "" on failure.
QString writePack(const QString& rootKnodeAbsPath, krs::parts::PartLibrary& lib,
                  const QString& outAbsPath, QString* err = nullptr);

// Parse + verify a .knodepack: unknown major refused; every entry's sha1(bytes)==contentHash; every
// member re-validated as a structurally-sound .knode. Returns false (out untouched) on any failure.
bool loadPack(const QString& absPath, PackDoc& out, QString* err = nullptr);

// Write a verified pack's members into `libRoot` (content-addressed filenames). Validates each entry's
// id/hash charset first (path-traversal defense); an already-present (id,hash) is a dedupe skip.
bool installPack(const PackDoc& pack, const QString& libRoot, InstallReport& rep, QString* err = nullptr);

// One-call import: loadPack -> installPack -> rescan `lib` over libRoot -> register discovered
// knode:<id> types -> report any interior typeId a member needs that the recipient still lacks.
bool importPack(const QString& absPath, const QString& libRoot, krs::parts::PartLibrary& lib,
                InstallReport& rep, QString* err = nullptr);

// Headless gate (KRS_KPACK_SELFTEST): author a nested subgraph (leaf A + B-nests-A) in an author
// library; pack B; the closure is {B,A}; a FRESH recipient library imports the single file, both land,
// knode:<B> registers AND evaluates end-to-end (nested A resolves) matching the flattened math; a
// second import de-dupes; NEG-CTRLs: a tampered member byte fails the sha1 check; an unknown major is
// refused; a hostile entry id ("../evil") is rejected before it can write outside the library; a
// self-referential nest does not loop the closure walker.
bool runKPackGate();

} // namespace krs::kpack
