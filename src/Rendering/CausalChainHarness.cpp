// CausalChainHarness.cpp -- Phase 0 harness B: the CAUSAL-CHAIN instrument + GATE 0b.
// The instrument records a residual per pipeline stage and LOCALIZES the earliest break. GATE 0b
// proves the localization on a pipeline-shaped chain (cmd->FK->collisionXform->fluidResponse->
// objectMotion): intact -> all stages pass; a SEVERED stage -> firstBreak() points at exactly that
// stage (not a downstream one). The real subsystem values plug into these stages in Phase 1.3 / 2.

#include "IntegrationHarness.hpp"

#include <cstdio>

namespace krs::integ {

void CausalChain::print(const char* tag) const
{
    for (size_t i = 0; i < m_stages.size(); ++i) {
        const auto& s = m_stages[i];
        std::printf("[%s]   stage %zu '%s': measured=%.6g expected=%.6g (tol %.1e)  %s\n",
                    tag, i, s.name.c_str(), s.measured, s.expected, s.tol, s.ok() ? "PASS" : "FAIL");
    }
}

bool runCausalChainGate0()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[causal] GATE 0b -- causal-chain instrument localizes a severed pipeline stage + neg-ctrl\n");

    // A 4-stage pipeline shaped like the canonical chain. Each stage propagates a value; `expected`
    // is the analytically-correct propagation, `measured` is what actually flowed (corruptible by a
    // break). breakStage<0 = intact.
    auto runChain = [](int breakStage) {
        const double input = 0.5;
        const double ea = 2.0 * input;          // cmd -> FK
        const double eb = ea + 3.0;             // FK -> collisionXform
        const double ec = 10.0 * eb;            // collisionXform -> fluidResponse
        const double ed = ec - 5.0;             // fluidResponse -> objectMotion
        const double a = 2.0 * input;
        const double b = a + (breakStage == 1 ? 0.0 : 3.0);
        const double c = (breakStage == 2 ? b /*dropped *10*/ : 10.0 * b);
        const double d = c - 5.0;
        CausalChain ch;
        ch.stage("cmd->FK", a, ea, 1e-9);
        ch.stage("FK->collisionXform", b, eb, 1e-9);
        ch.stage("collisionXform->fluidResponse", c, ec, 1e-9);
        ch.stage("fluidResponse->objectMotion", d, ed, 1e-9);
        return ch;
    };

    const CausalChain intact = runChain(-1);
    const CausalChain sev1 = runChain(1);   // sever stage 1
    const CausalChain sev2 = runChain(2);   // sever stage 2

    printf("[causal]  INTACT chain:\n");
    intact.print("causal");
    printf("[causal]  SEVERED@1 chain (FK->collisionXform dropped):\n");
    sev1.print("causal");

    const bool intactOk = intact.allPass() && intact.firstBreak() == -1;
    // Localization: a break at stage k must make firstBreak()==k (the EARLIEST), not a later stage.
    const bool localized1 = sev1.firstBreak() == 1;
    const bool localized2 = sev2.firstBreak() == 2;
    const bool pass = intactOk && localized1 && localized2;

    printf("[causal]  intact allPass=%d firstBreak=%d (want -1)  %s\n",
           int(intact.allPass()), intact.firstBreak(), intactOk ? "PASS" : "FAIL");
    printf("[causal]  NEG-CTRL severed@1 -> firstBreak=%d (want 1); severed@2 -> firstBreak=%d (want 2)  %s\n",
           sev1.firstBreak(), sev2.firstBreak(), (localized1 && localized2) ? "REJECTS(localized)" : "VACUOUS!");
    printf("[causal] %s\n", pass ? "ALL PASS (intact passes; severed stages localized exactly)" : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::integ
