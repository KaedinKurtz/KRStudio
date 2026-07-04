// SkeletonClip.cpp -- self-contained BVH text parser + hierarchy composition (krs::anim).
// See SkeletonClip.hpp for the format/frame/unit contract. No external parser: BVH is a
// trivial two-section ASCII grammar and we tokenise it by whitespace, tracking a brace/
// scope stack for the HIERARCHY tree. Robustness is a hard requirement (untrusted files),
// so every structural expectation is validated and reported via `err` rather than asserted.
#include "UtilityHeaders/SkeletonClip.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace krs::anim {

// ------------------------------------------------------------------ hierarchy compose

std::vector<Eigen::Matrix4d> SkeletonClip::worldXforms(int frame) const {
    std::vector<Eigen::Matrix4d> world;
    if (frame < 0 || frame >= frameCount || frame >= int(localXforms.size())) return world;
    const auto& L = localXforms[frame];
    if (L.size() != joints.size()) return world;   // corrupt clip: refuse rather than index OOB
    world.resize(joints.size());
    for (size_t j = 0; j < joints.size(); ++j) {
        const int p = joints[j].parent;
        // parent index is guaranteed < j by construction (topological order), so world[p]
        // is already resolved. A malformed clip with p>=j would read a default-constructed
        // (garbage) matrix, so guard it: treat an out-of-order/invalid parent as a root.
        if (p >= 0 && p < int(j)) world[j] = world[p] * L[j];
        else                      world[j] = L[j];
    }
    return world;
}

Eigen::Vector3d SkeletonClip::jointWorldPos(int frame, int jointIdx) const {
    if (jointIdx < 0 || jointIdx >= int(joints.size())) return Eigen::Vector3d::Zero();
    const std::vector<Eigen::Matrix4d> world = worldXforms(frame);
    if (jointIdx >= int(world.size())) return Eigen::Vector3d::Zero();
    return world[jointIdx].block<3,1>(0,3);
}

// ------------------------------------------------------------------ euler helpers

namespace {

// Single-axis rotation about the named axis by `rad`. Kept explicit (not Eigen's
// AngleAxis) so the BVH intrinsic-Euler composition order is unambiguous in the source.
Eigen::Matrix3d axisRot(Axis a, double rad) {
    const double c = std::cos(rad), s = std::sin(rad);
    Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
    switch (a) {
        case Axis::X: R << 1,0,0,  0,c,-s,  0,s,c; break;
        case Axis::Y: R << c,0,s,  0,1,0,  -s,0,c; break;
        case Axis::Z: R << c,-s,0, s,c,0,   0,0,1; break;
    }
    return R;
}

// Compose the three Euler angles in the joint's declared channel order. rotOrder[0] is
// the channel listed FIRST and is the OUTERMOST factor (BVH convention: v_parent = R v_child
// with R = R(first)*R(second)*R(third)). angDeg are indexed to match rotOrder slots.
Eigen::Matrix3d eulerToMatrix(const Axis order[3], double angDeg[3]) {
    const double d2r = 3.14159265358979323846 / 180.0;
    return axisRot(order[0], angDeg[0]*d2r)
         * axisRot(order[1], angDeg[1]*d2r)
         * axisRot(order[2], angDeg[2]*d2r);
}

bool axisFromRotToken(const std::string& tok, Axis& out) {
    if (tok == "Xrotation") { out = Axis::X; return true; }
    if (tok == "Yrotation") { out = Axis::Y; return true; }
    if (tok == "Zrotation") { out = Axis::Z; return true; }
    return false;
}

// A minimal fail helper: set the error string (if provided) and return false so callers
// can `return fail(err, "...")` in one line and never leave the parser in a throwing path.
bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

} // namespace

// ------------------------------------------------------------------ BVH parser

bool parseBVH(const std::string& text, SkeletonClip& out, std::string* err) {
    out = SkeletonClip{};   // defined starting state on every entry
    if (err) err->clear();

    // Whitespace-tokenise the whole document. BVH is insensitive to line breaks except
    // that "Frame Time:" style keys are two tokens; we handle those as token pairs. This
    // stream-of-tokens approach also naturally tolerates CRLF and odd indentation.
    std::istringstream ss(text);
    std::vector<std::string> tok;
    { std::string w; while (ss >> w) tok.push_back(w); }
    if (tok.empty()) return fail(err, "empty input");

    size_t i = 0;
    auto peek = [&](size_t k = 0) -> const std::string& {
        static const std::string kEmpty;
        return (i + k < tok.size()) ? tok[i + k] : kEmpty;
    };
    auto more = [&]() { return i < tok.size(); };
    // Parse the next token as a double; false (no throw) on non-numeric/eof.
    auto nextNum = [&](double& v) -> bool {
        if (!more()) return false;
        const std::string& s = tok[i];
        try { size_t pos = 0; v = std::stod(s, &pos); if (pos != s.size()) return false; }
        catch (...) { return false; }
        ++i; return true;
    };
    auto nextInt = [&](int& v) -> bool {
        double d; if (!nextNum(d)) return false; v = int(d); return true;
    };

    // ---- HIERARCHY ----
    if (peek() != "HIERARCHY") return fail(err, "missing HIERARCHY header");
    ++i;
    if (peek() != "ROOT") return fail(err, "expected ROOT after HIERARCHY");

    int channelCursor = 0;                 // running column index into a MOTION frame row
    std::vector<int> scope;                // stack of open-brace joint indices (parents)

    // Recursive-descent over the token stream via an explicit scope stack (avoids deep
    // recursion on pathological files). We consume ROOT/JOINT/End Site blocks in order.
    while (more()) {
        const std::string& t = peek();

        if (t == "ROOT" || t == "JOINT") {
            const bool isRoot = (t == "ROOT");
            if (isRoot && !out.joints.empty())
                return fail(err, "multiple ROOTs (single-skeleton clips only)");
            ++i;
            if (!more()) return fail(err, "joint name expected");
            SkeletonJoint jn;
            jn.name = tok[i++];
            jn.parent = scope.empty() ? -1 : scope.back();
            if (isRoot && jn.parent != -1) return fail(err, "ROOT cannot be nested");
            if (peek() != "{") return fail(err, "expected '{' after joint " + jn.name);
            ++i;
            out.joints.push_back(jn);
            scope.push_back(int(out.joints.size()) - 1);
        }
        else if (t == "End") {
            // "End Site" leaf: OFFSET only, no channels.
            ++i;
            if (peek() != "Site") return fail(err, "expected 'Site' after 'End'");
            ++i;
            if (peek() != "{") return fail(err, "expected '{' after End Site");
            ++i;
            SkeletonJoint jn;
            jn.parent = scope.empty() ? -1 : scope.back();
            if (jn.parent < 0) return fail(err, "End Site with no parent joint");
            jn.name = out.joints[jn.parent].name + "_End";
            jn.isEndSite = true;
            out.joints.push_back(jn);
            scope.push_back(int(out.joints.size()) - 1);
        }
        else if (t == "OFFSET") {
            ++i;
            if (scope.empty()) return fail(err, "OFFSET outside any joint");
            SkeletonJoint& jn = out.joints[scope.back()];
            double x, y, z;
            if (!nextNum(x) || !nextNum(y) || !nextNum(z))
                return fail(err, "OFFSET needs 3 numeric components");
            jn.offset = Eigen::Vector3d(x, y, z);
        }
        else if (t == "CHANNELS") {
            ++i;
            if (scope.empty()) return fail(err, "CHANNELS outside any joint");
            SkeletonJoint& jn = out.joints[scope.back()];
            if (jn.isEndSite) return fail(err, "End Site cannot declare CHANNELS");
            int n; if (!nextInt(n)) return fail(err, "CHANNELS needs a count");
            if (n != 3 && n != 6) return fail(err, "only 3- or 6-channel joints supported");
            if (i + size_t(n) > tok.size()) return fail(err, "truncated CHANNELS list");
            jn.nChannels = n;
            jn.channelStart = channelCursor;
            int rotSlot = 0;
            bool sawPos = false;
            for (int k = 0; k < n; ++k) {
                const std::string& ch = tok[i++];
                if (ch == "Xposition" || ch == "Yposition" || ch == "Zposition") {
                    sawPos = true;
                } else {
                    Axis ax;
                    if (!axisFromRotToken(ch, ax)) return fail(err, "unknown channel '" + ch + "'");
                    if (rotSlot >= 3) return fail(err, "more than 3 rotation channels on a joint");
                    jn.rotOrder[rotSlot++] = ax;
                }
            }
            if (rotSlot != 3) return fail(err, "joint '" + jn.name + "' must declare 3 rotation channels");
            jn.hasPosition = sawPos;
            channelCursor += n;
        }
        else if (t == "}") {
            ++i;
            if (scope.empty()) return fail(err, "unmatched '}'");
            scope.pop_back();
            if (scope.empty()) break;   // closed the ROOT block -> HIERARCHY done
        }
        else if (t == "MOTION") {
            // MOTION reached with an unclosed HIERARCHY brace stack -> malformed.
            return fail(err, "MOTION encountered before HIERARCHY closed");
        }
        else {
            return fail(err, "unexpected token in HIERARCHY: '" + t + "'");
        }
    }

    if (!scope.empty()) return fail(err, "unterminated HIERARCHY (missing '}')");
    if (out.joints.empty()) return fail(err, "no joints parsed");

    const int totalChannels = channelCursor;

    // ---- MOTION ----
    if (peek() != "MOTION") return fail(err, "missing MOTION section");
    ++i;
    if (peek() != "Frames:") return fail(err, "expected 'Frames:'");
    ++i;
    int frames; if (!nextInt(frames)) return fail(err, "Frames: needs an integer");
    if (frames < 0) return fail(err, "negative Frames count");
    // "Frame Time:" is two tokens.
    if (peek() != "Frame" || peek(1) != "Time:") return fail(err, "expected 'Frame Time:'");
    i += 2;
    double frameTime; if (!nextNum(frameTime)) return fail(err, "Frame Time: needs a number");
    if (frameTime < 0.0) return fail(err, "negative Frame Time");

    out.frameCount = frames;
    out.frameTime  = frameTime;

    // Each frame row has exactly `totalChannels` numbers. Read them, then materialise the
    // per-joint local transforms directly (we don't retain the raw channel table).
    out.localXforms.assign(size_t(frames), std::vector<Eigen::Matrix4d>(out.joints.size(),
                                                                        Eigen::Matrix4d::Identity()));
    std::vector<double> row(size_t(totalChannels), 0.0);
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < totalChannels; ++c) {
            if (!nextNum(row[size_t(c)]))
                return fail(err, "MOTION data truncated at frame " + std::to_string(f));
        }
        for (size_t j = 0; j < out.joints.size(); ++j) {
            const SkeletonJoint& jn = out.joints[j];
            Eigen::Matrix4d M = Eigen::Matrix4d::Identity();
            Eigen::Vector3d trans = jn.offset;   // fixed parent->child offset...

            if (jn.nChannels > 0) {
                int col = jn.channelStart;
                if (jn.hasPosition) {
                    // Root position channels REPLACE the (usually zero) root offset origin.
                    // BVH lists X/Y/Zposition first when present; consume them in that order.
                    trans = Eigen::Vector3d(row[size_t(col)], row[size_t(col+1)], row[size_t(col+2)]);
                    col += 3;
                }
                double ang[3] = {
                    row[size_t(col)], row[size_t(col+1)], row[size_t(col+2)]
                };
                const Eigen::Matrix3d R = eulerToMatrix(jn.rotOrder, ang);
                M.block<3,3>(0,0) = R;
            }
            M.block<3,1>(0,3) = trans;
            out.localXforms[size_t(f)][j] = M;
        }
    }

    // Trailing garbage after the declared frames is tolerated (some exporters pad); but a
    // completely empty MOTION with frames>0 was already caught by the truncation check.
    return true;
}

// ------------------------------------------------------------------ GATE

// Embedded synthetic BVH: a 3-node chain (root "hip" -> "spine" -> End Site), Z/Y/X order.
// Frame 0: identity everywhere. Frame 1: hip Zrotation=90 deg (everything else 0) -- this is
// the hand-verified frame below. Frame 2: root translated to make the position-channel path
// non-vacuous.
static const char* kBVH =
"HIERARCHY\n"
"ROOT hip\n"
"{\n"
"  OFFSET 0.0 0.0 0.0\n"
"  CHANNELS 6 Xposition Yposition Zposition Zrotation Yrotation Xrotation\n"
"  JOINT spine\n"
"  {\n"
"    OFFSET 0.0 1.0 0.0\n"
"    CHANNELS 3 Zrotation Yrotation Xrotation\n"
"    End Site\n"
"    {\n"
"      OFFSET 0.0 1.0 0.0\n"
"    }\n"
"  }\n"
"}\n"
"MOTION\n"
"Frames: 3\n"
"Frame Time: 0.033333\n"
"0.0 0.0 0.0   0.0 0.0 0.0   0.0 0.0 0.0\n"
"0.0 0.0 0.0  90.0 0.0 0.0   0.0 0.0 0.0\n"
"5.0 0.0 0.0   0.0 0.0 0.0   0.0 0.0 0.0\n";

bool runSkeletonClipGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[anim] GATE SKELETON-CLIP -- BVH parse + hierarchy compose + Euler world-pos check\n");
    bool pass = true;

    // (1) Parse succeeds.
    SkeletonClip clip;
    std::string err;
    const bool parsed = parseBVH(kBVH, clip, &err);
    printf("[anim]   parse ok=%s (%s)  %s\n", parsed ? "yes" : "NO",
           parsed ? "no error" : err.c_str(), parsed ? "PASS" : "FAIL");
    pass &= parsed;
    if (!parsed) { printf("[anim] FAILURES PRESENT\n"); std::fflush(stdout); return false; }

    // (2) Hierarchy: 3 joints (hip, spine, End Site), parent chain 0 <- 1 <- 2, names correct.
    const bool jc = (clip.joints.size() == 3);
    const bool hier = jc
        && clip.joints[0].parent == -1 && clip.joints[0].name == "hip"
        && clip.joints[1].parent == 0  && clip.joints[1].name == "spine"
        && clip.joints[2].parent == 1  && clip.joints[2].isEndSite;
    printf("[anim]   hierarchy n=%zu chain/names=%s  %s\n", clip.joints.size(),
           hier ? "ok" : "NO", hier ? "PASS" : "FAIL");
    pass &= hier;

    // (3) Channel layout: root has position channels + a 6-col block at 0; spine 3-col at 6.
    const bool chans = jc
        && clip.joints[0].hasPosition && clip.joints[0].nChannels == 6 && clip.joints[0].channelStart == 0
        && !clip.joints[1].hasPosition && clip.joints[1].nChannels == 3 && clip.joints[1].channelStart == 6
        && clip.joints[2].nChannels == 0;
    printf("[anim]   channels root(pos,6@0)+spine(rot,3@6)=%s  %s\n",
           chans ? "ok" : "NO", chans ? "PASS" : "FAIL");
    pass &= chans;

    // (4) Frame metadata.
    const bool meta = (clip.frameCount == 3) && (std::fabs(clip.frameTime - 0.033333) < 1e-9);
    printf("[anim]   meta frames=%d frameTime=%.6f  %s\n", clip.frameCount, clip.frameTime,
           meta ? "PASS" : "FAIL");
    pass &= meta;

    // (5) HAND-VERIFIED world position, frame 1 (hip Zrot=90 deg):
    //     Rz(90)*(0,1,0) = (-1,0,0).  world[spine].p = (-1,0,0).
    //     world[End].p = world[spine].p + Rz(90)*(0,1,0) = (-1,0,0)+(-1,0,0) = (-2,0,0).
    Eigen::Vector3d endPos = clip.jointWorldPos(1, 2);
    const Eigen::Vector3d endExp(-2.0, 0.0, 0.0);
    const bool endOk = (endPos - endExp).norm() < 1e-9;
    printf("[anim]   frame1 End world=(%.4f,%.4f,%.4f) exp(-2,0,0)=%s  %s\n",
           endPos.x(), endPos.y(), endPos.z(), endOk ? "ok" : "NO", endOk ? "PASS" : "FAIL");
    pass &= endOk;

    // Cross-check the intermediate spine node too (isolates a compose bug from an Euler bug).
    Eigen::Vector3d spinePos = clip.jointWorldPos(1, 1);
    const bool spineOk = (spinePos - Eigen::Vector3d(-1.0, 0.0, 0.0)).norm() < 1e-9;
    printf("[anim]   frame1 spine world=(%.4f,%.4f,%.4f) exp(-1,0,0)  %s\n",
           spinePos.x(), spinePos.y(), spinePos.z(), spineOk ? "PASS" : "FAIL");
    pass &= spineOk;

    // (6) Hierarchy composition identity: child world == parent world * child local, every frame.
    bool composeOk = true;
    for (int f = 0; f < clip.frameCount && composeOk; ++f) {
        const std::vector<Eigen::Matrix4d> W = clip.worldXforms(f);
        if (int(W.size()) != int(clip.joints.size())) { composeOk = false; break; }
        for (size_t j = 0; j < clip.joints.size(); ++j) {
            const int p = clip.joints[j].parent;
            const Eigen::Matrix4d expect = (p < 0) ? clip.localXforms[f][j]
                                                   : (W[p] * clip.localXforms[f][j]);
            if ((W[j] - expect).norm() > 1e-12) { composeOk = false; break; }
        }
    }
    printf("[anim]   compose child=parent*local (all frames)=%s  %s\n",
           composeOk ? "ok" : "NO", composeOk ? "PASS" : "FAIL");
    pass &= composeOk;

    // (7) Position-channel path is exercised: frame 2 translates the whole skeleton by +5 in X.
    Eigen::Vector3d hipF2 = clip.jointWorldPos(2, 0);
    const bool posOk = (hipF2 - Eigen::Vector3d(5.0, 0.0, 0.0)).norm() < 1e-9;
    printf("[anim]   frame2 root pos-channel=(%.4f,%.4f,%.4f) exp(5,0,0)  %s\n",
           hipF2.x(), hipF2.y(), hipF2.z(), posOk ? "PASS" : "FAIL");
    pass &= posOk;

    // (8) POSITIVE-CONTROL that the world pos is not vacuously right: at frame 0 (identity
    //     rotation) the End Site sits at (0,2,0) -- a DIFFERENT value from the frame-1 answer,
    //     proving the rotation actually moved it (a stub returning offsets would fail frame 1).
    Eigen::Vector3d endF0 = clip.jointWorldPos(0, 2);
    const bool distinct = (endF0 - Eigen::Vector3d(0.0, 2.0, 0.0)).norm() < 1e-9
                          && (endF0 - endPos).norm() > 0.5;
    printf("[anim]   frame0 End=(%.4f,%.4f,%.4f) exp(0,2,0) & != frame1  %s\n",
           endF0.x(), endF0.y(), endF0.z(), distinct ? "PASS" : "FAIL");
    pass &= distinct;

    // (9) NEG-CTRL: truncated/garbage BVH must return false with err set, and NOT crash.
    //     Three distinct malformations exercise different failure paths.
    {
        SkeletonClip bad; std::string e1;
        const bool r1 = parseBVH("HIERARCHY\nROOT hip\n{\n  OFFSET 0 0 0\n", bad, &e1); // truncated: no '}', no MOTION
        const bool neg1 = (!r1 && !e1.empty());

        SkeletonClip bad2; std::string e2;
        const bool r2 = parseBVH("total garbage not a bvh file at all", bad2, &e2);
        const bool neg2 = (!r2 && !e2.empty());

        SkeletonClip bad3; std::string e3;   // valid hierarchy, but MOTION data row is short
        const char* shortMotion =
            "HIERARCHY\nROOT r\n{\nOFFSET 0 0 0\nCHANNELS 3 Zrotation Yrotation Xrotation\n}\n"
            "MOTION\nFrames: 2\nFrame Time: 0.04\n0 0 0\n";   // only 1 row for 2 frames
        const bool r3 = parseBVH(shortMotion, bad3, &e3);
        const bool neg3 = (!r3 && !e3.empty());

        const bool neg = neg1 && neg2 && neg3;
        printf("[anim]   NEG-CTRL malformed rejected: trunc=%s garbage=%s shortMotion=%s  %s\n",
               neg1 ? "yes" : "NO", neg2 ? "yes" : "NO", neg3 ? "yes" : "NO",
               neg ? "PASS" : "FAIL");
        pass &= neg;
    }

    printf("[anim] %s\n", pass ? "ALL PASS (BVH parse + FK compose + Euler world-pos verified)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::anim
