#pragma once
// ===========================================================================
// SKILL -- the Process & Skills layer's core contract (krs::skill).
//
// A SKILL is a named, parameterized, shareable behavior: its BODY realizes the
// motion/effect, its exposed inputs are typed PARAMETERS (ExposedPort role +
// default on the .knode), its POSTCONDITION decides success, and it actuates
// ONLY through the robot-keyed command bus (ArticulationCommandComponent) --
// re-asserted every control tick, so releasing/deleting a skill releases its
// DOFs to manual control per the bus's per-pass contract.
//
// HYBRID BODIES (the chosen architecture): a body is any Node. A pure-subgraph
// skill instantiates a SubgraphNode from a .knode (fully shareable via
// .knodepack); a motion skill may instead wrap the proven C++ cinematics tower
// (planCartesianMotion -> playTrajectory) behind the same contract. Either way
// the task layer sees ONE thing: a BT Action leaf (krs::policy::Action::Fn)
// that is Running until the postcondition holds -- wrap it in policy::Timeout
// for an honest Failure instead of an infinite wait.
// ===========================================================================
#include "BehaviorTree.hpp"    // krs::policy::Action::Fn / Status
#include <Eigen/Core>
#include <functional>
#include <map>
#include <memory>
#include <string>

class Node;
class Scene;
namespace krs::robot { struct LiveRobot; }
namespace krs::world { struct WorldState; }

namespace krs::skill {

// A parameter value bound at invocation. P1 covers the two kinds the worked skills
// need -- scalars and whole joint configs; further kinds ride the kdoc union later.
struct ParamValue {
    enum class Kind { Number, Config };
    Kind kind = Kind::Number;
    double number = 0.0;
    Eigen::VectorXd config;
    static ParamValue num(double v) { ParamValue p; p.kind = Kind::Number; p.number = v; return p; }
    static ParamValue cfg(Eigen::VectorXd q) { ParamValue p; p.kind = Kind::Config; p.config = std::move(q); return p; }
};
using ParamMap = std::map<std::string, ParamValue>;

// Build the BT Action leaf realizing a skill:
//  - `body` is ticked (process()) EVERY control tick, so its command-bus entries re-assert per pass;
//  - `params` bind onto the body's exposed INPUT ports by name (Number -> literal, Config -> packet)
//    on the first tick;
//  - `post` is evaluated after each body tick: true => Success, else Running (Timeout => Failure).
// The returned Fn owns its own bound-state, so one functor is re-enterable across ticks.
krs::policy::Action::Fn makeSkillLeaf(std::shared_ptr<Node> body, Scene* scene,
                                      ParamMap params, std::function<bool()> post);

// Canonical P1 postcondition: the live robot's q is within `tol` of `target` on every DOF.
std::function<bool()> qNear(const krs::robot::LiveRobot* lr, const Eigen::VectorXd& target, double tol = 1e-4);

// ---- P4: typed pre/post-conditions + composition ------------------------------------------------
// A typed predicate over the WorldState (the enum-typed representation: new kinds are code, which
// keeps evaluation + the composition validator gateable). `negated` flips the sense.
struct Predicate {
    enum class Kind { GripperOpen, Holding, At, Near, Fresh };
    Kind kind = Kind::GripperOpen;
    bool negated = false;
    int robotId = 0;
    std::string a, b;          // At/Near: a vs b ; Holding: a = object ; Fresh: a = object
    double tol = 0.1;          // At/Near tolerance ; Fresh max age [s]
    bool eval(const krs::world::WorldState& ws) const;
    void apply(krs::world::WorldState& ws) const;   // assert this predicate as a FACT (skill effect)
    std::string text() const;                        // human-readable ("holding(cup)")
};

// A parameter a skill's execution model REQUIRES (authored ONCE on the primitive type, never per
// goal): the named object parameter must be known to at most maxSigma before confident execution.
// SimOnly-tagged objects auto-satisfy (the best guess is accepted); R2S objects demand honing.
struct ParamNeed { std::string param; double maxSigma = 0.05; };

// A composable SKILL SPEC: the outer contract a task layer sequences. `realize` builds the BT leaf
// for one invocation (hybrid: a SubgraphNode body, or a wrapper over the C++ motion tower).
struct SkillSpec {
    std::string name;
    std::vector<Predicate> pre;   // must hold before the body runs
    std::vector<Predicate> eff;   // asserted onto the WorldState after the body succeeds
    std::vector<ParamNeed> needs; // knowledge REQUIREMENTS (intrinsic to the primitive type)
    std::map<std::string, double> intrinsicEnvelope;   // the primitive's own bounds ("maxAccel", ...)
    // EPISTEMIC skill (an excitation routine): honing `honesParam` down to `honedSigma` is this
    // skill's real effect. Empty honesParam = a normal physical skill.
    std::string honesParam;
    double      honedSigma = 0.0;
    std::function<krs::policy::Action::Fn(Scene*, const ParamMap&)> realize;
    double timeoutSec = 5.0;
};
struct SkillStep {
    const SkillSpec* spec = nullptr;
    ParamMap params;
    std::string object;                        // the bound object (needs/traits resolve against it)
    std::map<std::string, double> envelope;    // COMPOSED constraint envelope, stamped at plan time
};

// The trait ontology's constraint templates (best-guess seeds; user-extensible later): binding a
// skill to an object INJECTS the object's traits' constraints into the step's envelope.
//   liquid-container -> { maxTiltDeg 15, maxAccel 2 } ; fragile -> { maxForceN 10 }
std::map<std::string, double> traitConstraints(const std::string& trait);
// Envelope composition = INTERSECTION (per-key minimum) of the primitive's intrinsic envelope and
// every constraint template of the bound object's traits. Most restrictive wins.
std::map<std::string, double> composeEnvelope(const SkillSpec& spec, const krs::world::WorldState& ws,
                                              const std::string& objectName);

// STATIC validation: walk the steps, simulating pre/effects over a COPY of the world -- a step whose
// precondition is not established (by the start state or a prior step's effects) fails loud with WHY.
struct ComposeReport { bool valid = false; std::string why; };
ComposeReport validateSequence(const std::vector<SkillStep>& steps, const krs::world::WorldState& start);

// Build the executable task: Sequence over [pre-Condition -> Timeout(leaf) -> apply-effects] per
// step. A step whose runtime precondition fails ends the task Failure (validation is static; the
// world can still diverge). Effects write the live WorldState on that step's success.
krs::policy::NodePtr composeSequence(const std::vector<SkillStep>& steps, Scene* scene,
                                     krs::world::WorldState& ws);

// Headless gate (KRS_PICKPLACE_SELFTEST): pick + place SkillSpecs (typed pre/eff, subgraph bodies)
// compose into a validated Sequence and execute under the SkillRuntime against a live demo robot +
// WorldState: pick requires gripperOpen + establishes holding(cup); place requires holding(cup) +
// releases it; the task ends Success with the robot at the place config and the facts correct.
// Mis-ordered [place, pick] is REJECTED by static validation naming the unmet precondition;
// NEG-CTRL: executing with the gripper already closed fails the pick step at RUNTIME (Failure), so
// the pre-Conditions are live guards, not decoration.
bool runPickPlaceGate();

// Headless gate (KRS_SKILL_SELFTEST): a "move_to_config" skill -- a .knode whose interior is a
// physics_config_drive, exposed input "target" tagged role=targetConfig with a manifest -- is
// instantiated, its leaf ticked under the real per-pass bus lifecycle (clearForEvalPass -> tick ->
// drainCommandBusIntoRobots) against a live demo robot: q reaches the target and the leaf returns
// Success; after success nothing re-asserts (the DOFs release). The manifest + param role/default
// round-trip the .knode file, and a .knodepack now carries the description. NEG-CTRL: a postcondition
// that never holds hits policy::Timeout and returns Failure (not an infinite Running).
bool runSkillGate();

} // namespace krs::skill
