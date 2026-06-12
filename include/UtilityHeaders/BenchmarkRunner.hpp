#pragma once

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <vector>

class Scene;
class SimulationController;
class RenderingSystem;

/**
 * @brief First-principles physics validation (KRS_BENCH=1).
 *
 * Runs a sequence of analytic scenarios against the live engine — free
 * fall, restitution, Coulomb friction, projectile range, fluid column
 * height — and compares measured values to closed-form physics with
 * documented tolerances. Logs a PASS/FAIL table and exits the app with
 * the number of failures as the exit code (CI-able).
 *
 * All quantities are SI: metres, seconds, kilograms, m/s^2.
 */
class BenchmarkRunner : public QObject
{
    Q_OBJECT

public:
    BenchmarkRunner(Scene* scene, SimulationController* sim, RenderingSystem* renderer,
                    QObject* parent = nullptr);

    void start();

private slots:
    void sample(); // 120 Hz polling of the running scenario

private:
    struct Scenario {
        QString name;
        std::function<void()> setup;            // spawn entities, set state
        std::function<bool()> finished;         // sampled every tick
        std::function<void()> evaluate;         // log PASS/FAIL via check()
        float timeoutSeconds = 10.0f;
    };

    void beginScenario(int index);
    void endScenario();
    void check(const QString& metric, double measured, double expected, double tolFraction);
    entt::entity spawnSphere(const glm::vec3& pos, float radius, float restitution, float friction);
    entt::entity spawnBox(const glm::vec3& pos, const glm::vec3& halfExt, float friction,
                          bool dynamic, const glm::quat& rot = glm::quat(1, 0, 0, 0));
    void clearSpawned();

    Scene* m_scene;
    SimulationController* m_sim;
    RenderingSystem* m_renderer;
    std::vector<Scenario> m_scenarios;
    std::vector<entt::entity> m_spawned;
    int m_current = -1;
    int m_failures = 0;
    int m_checks = 0;
    float m_elapsed = 0.0f;   // real seconds since scenario start
    QElapsedTimer m_scenarioClock;

    // scratch shared between setup/finished/evaluate closures
    entt::entity m_subject = entt::null;
    double m_t0 = 0.0, m_v0 = 0.0, m_h0 = 0.0;
    double m_measure = 0.0, m_measure2 = 0.0;
    bool m_flag = false;
};
