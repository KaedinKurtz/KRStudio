// ControlLibGate.cpp -- GATE PID + GATE FILTER. Each control/estimation node is verified against an
// INDEPENDENT closed-form reference computed here (not the node), so a wrong implementation cannot pass.
//
// GATE PID: a 1st-order plant x' = -a x + b u closed around the PID NODE must track an independent
//   reference PID+plant simulation to <tol, and the closed loop must REACH the step setpoint (integral
//   removes steady-state error). NEG-CTRL: a P-only loop retains a steady-state offset (the integral term
//   is real, and the gate can tell the difference).
// GATE FILTER: the scalar Kalman matches its reference recursion exactly AND reduces estimation error vs
//   the raw noisy measurements (a known constant-estimation problem); the low-pass matches the closed-form
//   EMA and a unity-alpha control does not smooth; the moving average matches the closed-form boxcar mean.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <any>

namespace krs::nodes {
namespace {
double outD(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value()) {
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
            try { return double(std::any_cast<float>(p.packet->data)); } catch (...) {}
        }
    return std::nan("");
}
} // namespace

bool runPidGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[pid] GATE PID -- the PID node closes a 1st-order plant onto a step, matching an independent reference\n");

    auto n = NodeFactory::instance().createNode("control_pid");
    if (!n) { printf("[pid] FAIL: no control_pid node\n"); return false; }

    const double Kp = 0.8, Ki = 2.0, Kd = 0.05, dt = 0.01, sp = 1.0, a = 1.0, b = 2.0;
    n->setPortLiteral<double>("Kp", Kp); n->setPortLiteral<double>("Ki", Ki);
    n->setPortLiteral<double>("Kd", Kd); n->setPortLiteral<double>("dt", dt);
    n->setPortLiteral<double>("Setpoint", sp);

    double nodeX = 0.0, refX = 0.0, refI = 0.0, refPrev = 0.0;
    double maxU = 0.0, maxX = 0.0, u0Full = std::nan("");
    const int N = 600;
    for (int k = 0; k < N; ++k) {
        // NODE closed loop
        n->setPortLiteral<double>("Measurement", nodeX);
        n->process();
        const double u = outD(*n, "Control"); if (k == 0) u0Full = u;
        nodeX += dt * (-a * nodeX + b * u);
        // INDEPENDENT reference PID + plant
        const double err = sp - refX;
        refI += err * dt;
        const double deriv = (err - refPrev) / dt;
        refPrev = err;
        const double refU = Kp * err + Ki * refI + Kd * deriv;
        refX += dt * (-a * refX + b * refU);
        maxU = std::max(maxU, std::abs(u - refU));
        maxX = std::max(maxX, std::abs(nodeX - refX));
    }
    const bool matches = std::isfinite(maxU) && maxU < 1e-6 && maxX < 1e-6;
    const bool reaches = std::abs(nodeX - sp) < 0.02;             // step response: integral removes the offset

    // NEG-CTRL: P-only loop keeps a steady-state error (so the integral term is genuinely doing the work).
    double pX = 0.0;
    for (int k = 0; k < N; ++k) { const double e = sp - pX; pX += dt * (-a * pX + b * (Kp * e)); }
    const bool pOnlyOffset = std::abs(pX - sp) > 0.1;

    // INDEPENDENT gain-usage checks: prove the NODE actually uses Ki and Kd (not silently ignores them).
    auto runNodeLoop = [&](double kp, double ki, double kd, double& finalX, double& firstU) {
        auto m = NodeFactory::instance().createNode("control_pid");
        m->setPortLiteral<double>("Kp", kp); m->setPortLiteral<double>("Ki", ki);
        m->setPortLiteral<double>("Kd", kd); m->setPortLiteral<double>("dt", dt);
        m->setPortLiteral<double>("Setpoint", sp);
        double x = 0.0; firstU = std::nan("");
        for (int k = 0; k < N; ++k) {
            m->setPortLiteral<double>("Measurement", x); m->process();
            const double u = outD(*m, "Control"); if (k == 0) firstU = u;
            x += dt * (-a * x + b * u);
        }
        finalX = x;
    };
    double xKi0, uKi0, xKd0, uKd0;
    runNodeLoop(Kp, 0.0, Kd, xKi0, uKi0);            // node with Ki=0
    runNodeLoop(Kp, Ki, 0.0, xKd0, uKd0);            // node with Kd=0
    const bool kiUsed = reaches && std::abs(xKi0 - sp) > 0.1;        // full node reaches; Ki=0 keeps offset -> Ki is used
    const bool kdUsed = std::abs(uKd0 - u0Full) > 1e-6;             // Kd changes the first (derivative-kick) control
    (void)uKi0;

    const bool pass = matches && reaches && pOnlyOffset && kiUsed && kdUsed;
    printf("[pid]   node vs reference: max|u-uref|=%.2e max|x-xref|=%.2e (%s); step response settles x=%.4f -> setpoint=%.1f (%s)\n",
           maxU, maxX, matches ? "match" : "MISMATCH", nodeX, sp, reaches ? "reached" : "NOT reached");
    printf("[pid]   NEG-CTRL P-only loop settles x=%.4f (offset %.3f > 0.1): %s\n", pX, std::abs(pX - sp), pOnlyOffset ? "PASS" : "FAIL!");
    printf("[pid]   gain usage: Ki=0 node keeps offset x=%.4f (Ki used=%s); Kd changes first control %.4f vs %.4f (Kd used=%s)\n",
           xKi0, kiUsed ? "yes" : "NO", uKd0, u0Full, kdUsed ? "yes" : "NO");
    printf("[pid] %s\n", pass ? "ALL PASS (PID matches reference; removes steady-state error; node provably uses Ki and Kd)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

bool runFilterGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[filter] GATE FILTER -- Kalman/low-pass/moving-average each vs an independent reference (not just 'runs')\n");

    bool kalmanOk = false, lowpassOk = false, maOk = false;

    // ---- scalar Kalman: a known constant-estimation problem (true=5.0 + deterministic zero-mean noise) ----
    {
        auto n = NodeFactory::instance().createNode("filter_kalman_1d");
        if (n) {
            const double truth = 5.0, Q = 1e-3, R = 1.0;
            n->setPortLiteral<double>("Q", Q); n->setPortLiteral<double>("R", R);
            double rx = 0.0, rP = 1.0; bool rInit = false;        // reference recursion
            double maxDiff = 0.0, sumRaw = 0.0, sumFilt = 0.0; int cnt = 0; double finalEst = std::nan("");
            const int N = 200;
            for (int i = 0; i < N; ++i) {
                const double noise = 0.8 * std::sin(1.3 * i) + 0.5 * std::cos(2.7 * i + 0.4);
                const double z = truth + noise;
                n->setPortLiteral<double>("Measurement", z); n->process();
                const double est = outD(*n, "Estimate"); finalEst = est;
                // reference
                if (!rInit) { rx = z; rP = R; rInit = true; }
                else { rP += Q; const double K = rP / (rP + R); rx += K * (z - rx); rP *= (1.0 - K); }
                maxDiff = std::max(maxDiff, std::abs(est - rx));
                if (i >= N / 2) { sumRaw += (z - truth) * (z - truth); sumFilt += (est - truth) * (est - truth); ++cnt; }
            }
            const double rawRmse = std::sqrt(sumRaw / cnt), filtRmse = std::sqrt(sumFilt / cnt);
            // (a) matches the reference recursion exactly, (b) reduces error well below raw (catches K=1
            // passthrough), and (c) INDEPENDENT TRUTH ORACLE: converges to the true 5.0 (catches a K=0
            // frozen filter stuck at the noisy first sample, which would NOT reach the truth).
            const bool converged = std::abs(finalEst - truth) < 0.1;
            kalmanOk = (maxDiff < 1e-9) && (filtRmse < rawRmse * 0.3) && converged;
            printf("[filter]   Kalman: max|node-ref|=%.2e; raw RMSE=%.4f -> filtered RMSE=%.4f; estimate=%.4f->truth %.1f (%s)  %s\n",
                   maxDiff, rawRmse, filtRmse, finalEst, truth, converged ? "converged" : "NOT converged", kalmanOk ? "ok" : "FAIL");
        }
    }

    // ---- low-pass (EMA): step response vs closed-form; unity-alpha control does not smooth ----
    {
        auto n = NodeFactory::instance().createNode("signal_lowpass");
        if (n) {
            const double alpha = 0.2;
            n->setPortLiteral<float>("Alpha", float(alpha));
            double refY = 0.0; bool refInit = false; double maxDiff = 0.0;
            for (int i = 0; i < 30; ++i) {
                const double x = (i == 0) ? 0.0 : 1.0;             // 0 then a unit step (so the EMA transient shows)
                n->setPortLiteral<float>("Input", float(x)); n->process();
                const double y = outD(*n, "Output");
                if (!refInit) { refY = x; refInit = true; }        // first sample seeds the filter
                else refY = alpha * x + (1.0 - alpha) * refY;
                maxDiff = std::max(maxDiff, std::abs(y - refY));
            }
            // NEG-CTRL: alpha=1 -> output follows input with NO lag
            auto m = NodeFactory::instance().createNode("signal_lowpass");
            m->setPortLiteral<float>("Alpha", 1.0f);
            m->setPortLiteral<float>("Input", 0.0f); m->process();   // seed
            m->setPortLiteral<float>("Input", 1.0f); m->process();
            const double yNoSmooth = outD(*m, "Output");
            const bool unityNoLag = std::abs(yNoSmooth - 1.0) < 1e-6;
            lowpassOk = (maxDiff < 1e-5) && unityNoLag;
            printf("[filter]   low-pass: max|node-EMA|=%.2e; NEG-CTRL alpha=1 -> y=%.4f (no lag, want 1.0)  %s\n",
                   maxDiff, yNoSmooth, lowpassOk ? "ok" : "FAIL");
        }
    }

    // ---- moving average (boxcar): vs closed-form mean; window=1 control is a passthrough ----
    {
        auto n = NodeFactory::instance().createNode("filter_moving_average");
        if (n) {
            const int W = 4;
            n->setPortLiteral<int>("Window", W);
            const double seq[] = { 1, 3, 5, 7, 9, 11, 2, 4 };
            std::vector<double> buf; double maxDiff = 0.0;
            for (double x : seq) {
                n->setPortLiteral<double>("Input", x); n->process();
                const double y = outD(*n, "Output");
                buf.push_back(x); if (int(buf.size()) > W) buf.erase(buf.begin());
                double s = 0; for (double v : buf) s += v; const double ref = s / buf.size();
                maxDiff = std::max(maxDiff, std::abs(y - ref));
            }
            // NEG-CTRL: window=1 -> passthrough (output == input)
            auto m = NodeFactory::instance().createNode("filter_moving_average");
            m->setPortLiteral<int>("Window", 1); m->setPortLiteral<double>("Input", 42.0); m->process();
            const bool passthrough = std::abs(outD(*m, "Output") - 42.0) < 1e-9;
            maOk = (maxDiff < 1e-9) && passthrough;
            printf("[filter]   moving-avg: max|node-mean|=%.2e; NEG-CTRL window=1 passthrough=%s  %s\n",
                   maxDiff, passthrough ? "yes" : "NO", maOk ? "ok" : "FAIL");
        }
    }

    const bool pass = kalmanOk && lowpassOk && maOk;
    printf("[filter] %s\n", pass ? "ALL PASS (each filter matches its reference + a meaningful neg-ctrl; no unverified filter)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
