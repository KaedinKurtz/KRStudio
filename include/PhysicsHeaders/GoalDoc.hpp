#pragma once
// ===========================================================================
// GOAL DOCUMENT (.kgoal) -- a declarative goal state (krs::goal), Goal Workspace G1.
//
// A goal is a NAMED, content-addressed SET of typed predicates over the
// WorldState ("at(cup, drop_pose) +-0.1 AND holding(r0, cup)"). The Goal
// Workspace panel EDITS this document; the C++ GoalBuilder WRITES the same
// document -- block UI and API are two front-ends to one format ("kgoal/1",
// same versioned-JSON + refuse-unknown-major discipline as the whole .k*
// family). A goal document is pure declaration: no plan, no skills -- the
// regression planner turns it into an executable plan against a live world.
// ===========================================================================
#include "Skill.hpp"     // krs::skill::Predicate (the shared typed predicate)
#include <QString>
#include <QJsonObject>
#include <vector>

namespace krs::world { struct WorldState; }

namespace krs::goal {

struct GoalDoc {
    QString id;                                   // stable uuid (minted on save if empty)
    QString name;                                 // "put the glass on the shelf"
    int     revision = 1;
    std::vector<krs::skill::Predicate> require;   // AND-composed goal predicates
};

// Predicate <-> JSON (shared by .kgoal and any future plan artifact).
QJsonObject predicateToJson(const krs::skill::Predicate& p);
krs::skill::Predicate predicateFromJson(const QJsonObject& o);

// Is the goal ALREADY satisfied in this world? (every predicate holds)
bool satisfied(const GoalDoc& g, const krs::world::WorldState& ws);
// The unmet subset (what the planner must establish; what the panel paints red).
std::vector<krs::skill::Predicate> unmet(const GoalDoc& g, const krs::world::WorldState& ws);

// File I/O: "kgoal/1", content-addressed like the rest of the family. saveKGoal mints an id if
// empty and returns it ("" on failure); loadKGoal refuses unknown majors / malformed docs whole.
QString saveKGoal(GoalDoc& g, const QString& absPath);
bool    loadKGoal(const QString& absPath, GoalDoc& out, QString* err = nullptr);

// The PROGRAMMATIC front-end (the "API to build goal states exactly like the blocks"):
//   GoalDoc g = GoalBuilder("shelve the glass")
//                   .at("glass", "shelf_pose", 0.1)
//                   .notHolding(0, "glass")
//                   .gripperOpen(0)
//                   .doc();
class GoalBuilder {
public:
    explicit GoalBuilder(QString name) { d_.name = std::move(name); }
    GoalBuilder& at(const std::string& obj, const std::string& frame, double tol) {
        d_.require.push_back({ krs::skill::Predicate::Kind::At, false, 0, obj, frame, tol }); return *this;
    }
    GoalBuilder& nearTo(const std::string& a, const std::string& b, double tol) {
        d_.require.push_back({ krs::skill::Predicate::Kind::Near, false, 0, a, b, tol }); return *this;
    }
    GoalBuilder& holding(int robotId, const std::string& obj) {
        d_.require.push_back({ krs::skill::Predicate::Kind::Holding, false, robotId, obj }); return *this;
    }
    GoalBuilder& notHolding(int robotId, const std::string& obj) {
        d_.require.push_back({ krs::skill::Predicate::Kind::Holding, true, robotId, obj }); return *this;
    }
    GoalBuilder& gripperOpen(int robotId, bool open = true) {
        d_.require.push_back({ krs::skill::Predicate::Kind::GripperOpen, !open, robotId }); return *this;
    }
    GoalDoc doc() const { return d_; }
private:
    GoalDoc d_;
};

// Headless gate (KRS_KGOAL_SELFTEST): a builder-authored goal saves to .kgoal, reloads bit-equal
// (predicates/tols/negation), and satisfied()/unmet() evaluate correctly against a live WorldState
// (unmet shrinks as the world approaches the goal); the builder and a hand-authored JSON round-trip
// to the SAME document (two front-ends, one format); content hash changes on edit. NEG-CTRLs: an
// unknown major is refused; a malformed predicate object is refused whole (nothing half-applied).
bool runKGoalGate();

} // namespace krs::goal
