#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "HilBridges.hpp"
#include "ArticulationSpec.hpp"   // Phase G: live FANUC articulation spec (POD)

class Scene;

/// Lifecycle state of the scene simulation (physics + fluids).
enum class SimulationState { Stopped, Playing, Paused };

/**
 * @brief Owns the rigid-body physics world (PhysX) and the simulation
 * lifecycle driven by the toolbar play/pause/stop/step buttons.
 *
 * Play  -> snapshots all simulated transforms, builds the PhysX world from
 *          RigidBodyComponent + collider components, starts stepping.
 * Pause -> stops stepping but keeps the world alive (step button advances
 *          one fixed timestep at a time).
 * Stop  -> destroys the world and restores the pre-play snapshot.
 *
 * Stepping uses a fixed timestep with an accumulator: rendering rate and
 * physics rate stay decoupled. tick() is called from the master timer.
 */
class SimulationController : public QObject
{
    Q_OBJECT

public:
    explicit SimulationController(Scene* scene, QObject* parent = nullptr);
    ~SimulationController() override;

    SimulationState state() const { return m_state; }
    bool isPlaying() const { return m_state == SimulationState::Playing; }

    static constexpr float kFixedDt = 1.0f / 240.0f; // benchmarked: halving from 1/120 sharpens restitution (less penetration correction per impact)

public slots:
    void play();
    void pause();
    void stop();
    void singleStep(); // one fixed step; implies pause if currently stopped

public:
    // G.0 — process-wide PhysX core (borrowed singleton). Introspection for the
    // lifecycle gate, and the gate itself (borrow/release across controllers).
    static int  physxCoreRefCount();   // # SimulationControllers holding the shared core
    static bool physxCoreAlive();      // is the shared PxPhysics valid
    static bool runLifecycleSelfTest();// G0b: create/destroy + coexist with no crash/double-free

    // Phase G — live articulation (built in buildPhysicsWorld from a RobotArticSpec).
    // PhysX-free accessors so the GATE-H oracle harness can drive + read the live tree.
    void setRobotArticulationSpec(const krs::dyn::RobotArticSpec& spec);
    int  articDofCount() const;
    bool setArticJointPositions(const std::vector<float>& q);   // applyCache(ePOSITION)
    std::vector<std::array<float, 7>> articLinkPoses() const;    // per non-root link: pos.xyz + quat.xyzw
    void setSceneGravity(float gx, float gy, float gz);          // gate: isolate the loop constraint
    static bool ensurePhysxExtensions();                        // PxD6Joint needs extensions (once/process)
    bool setArticJointVelocities(const std::vector<float>& qd);  // applyCache(eVELOCITY)
    bool commandJointTorques(const std::vector<float>& tau);     // cache.jointForce + applyCache(eFORCE)
    std::vector<float> articJointAccel();                        // commonInit + computeJointAcceleration
    std::vector<float> articJointPositions();                    // copyInternalStateToCache(ePOSITION) readback
    std::vector<float> articJointVelocities();                   // copyInternalStateToCache(eVELOCITY) readback

    // Phase V (V.3): map each MOVING articulation link (0-based, matching
    // articLinkPoses ordering) to the ECS solid entities rigidly attached to it.
    // Captures the CURRENT link poses as the rest reference (call with the robot at
    // its mesh-baked rest config). writeBackArticulationViz() then drives each
    // solid's TransformComponent by its link's delta-pose every frame (the meshes
    // are world-baked at rest, so the transform is the link's motion-from-rest).
    void setArticulationVizMapping(const std::vector<std::vector<entt::entity>>& movingLinkEntities);
    void writeBackArticulationViz();

    // Phase V (V.4): drive the articulation through a smooth kinematic J1/J2/J3 sweep every
    // tick (a visible demo). Frame-rate-paced (fixed phase step per tick) so it advances even
    // in headless/test loops. The articulation must already be built + viz-mapped.
    void setArticulationDemoDrive(bool on);
    bool articulationDemoDrive() const { return m_articDemoDrive; }

    /// Advance the accumulator / step physics. Call once per frame.
    void tick();

    /// Live-edit support: rebuild a single entity's physics actor from its
    /// current components while the world is alive. No-op when stopped
    /// (the next play() reads the components anyway).
    void notifyEntityChanged(entt::entity entity);

    /// Two-way fluid coupling: apply the fluid's net reaction impulse [N·s]
    /// to the entity's dynamic actor (buoyancy, splashes pushing objects).
    /// Per-frame Δv is clamped for stability.
    void applyFluidImpulse(entt::entity entity, const glm::vec3& impulse);

signals:
    void stateChanged(SimulationState newState);

private:
    void buildPhysicsWorld();
    void buildArticulation();   // Phase G: build the live PxArticulation from m_robotSpec
    void destroyPhysicsWorld();
    bool createActorForEntity(entt::entity entity);
    void removeActorForEntity(entt::entity entity);
    void stepOnce(float dt);
    void pushKinematicTargets();
    void syncUserEdits();
    void writeBackTransforms();
    void takeSnapshot();
    void restoreSnapshot();
    void setState(SimulationState s);

    // HIL CAN telemetry (Phase 2): opened on play() when KRS_HIL_CAN is set.
    void openHilCan();
    void closeHilCan();
    void applyCanCommands();   // drain effort command frames -> body forces (pre-step)
    void publishCanState();    // body pose/velocity/effort -> state frames (post-step)

    struct TransformSnapshot {
        entt::entity entity;
        glm::vec3 translation;
        glm::quat rotation;
        glm::vec3 scale;
    };

    Scene* m_scene = nullptr;
    krs::dyn::RobotArticSpec m_robotSpec;   // Phase G: optional live FANUC articulation
    bool m_hasRobotSpec = false;
    bool m_articDemoDrive = false;          // Phase V: kinematic J1/J2/J3 sweep each tick
    double m_articDemoPhase = 0.0;
    SimulationState m_state = SimulationState::Stopped;
    QElapsedTimer m_clock;
    double m_accumulator = 0.0;
    std::vector<TransformSnapshot> m_snapshot;

    // PhysX lives behind a pimpl so PhysX headers stay out of the project's
    // include graph (and the class still compiles when KR_WITH_PHYSX is off).
    struct PxImpl;
    std::unique_ptr<PxImpl> m_px;

    std::unique_ptr<krs::hil::IVirtualCAN> m_can; // HIL telemetry bus (null = off)
};
