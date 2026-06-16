// StaticGate.cpp -- GATE STATIC-CONST: the constant ("static") math nodes now have real value-editor
// fields, and driving the field sets the constant the node emits. Covers the clean scalar/vector/string
// constants; matrix/quat/Eigen constants are deferred (no simple single-field editor). NEG-CTRL: before any
// edit the node emits its default, and a non-editable (matrix) constant has NO value field.

#include "Node.hpp"
#include "NodeDelegate.hpp"
#include "NodeEditorGate.hpp"
#include "NodeEditQueue.hpp"

#include <QApplication>
#include <QWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>

#include <glm/glm.hpp>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <any>

namespace krs::nodes {
namespace {
double outScalar(Node& n) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == "Value" && p.packet.has_value()) {
            try { return double(std::any_cast<float>(p.packet->data)); } catch (...) {}
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
            try { return double(std::any_cast<int>(p.packet->data)); } catch (...) {}
            try { return std::any_cast<bool>(p.packet->data) ? 1.0 : 0.0; } catch (...) {}
        }
    return std::nan("");
}
} // namespace

bool runStaticConstGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[static] GATE STATIC-CONST -- constant math nodes have a value field whose edit sets the emitted constant\n");
    if (!QApplication::instance()) { printf("[static] FAIL: needs QApplication\n"); return false; }

    int M = 0, ok = 0;
    std::vector<std::string> fail;

    auto scalarCase = [&](const char* typeId, auto driver, double drive, double expect) {
        ++M;
        NodeDelegate d(typeId); Node* n = d.backendNode(); QWidget* body = n ? d.embeddedWidget() : nullptr;
        if (!n || !body) { fail.push_back(std::string(typeId) + "(no-body)"); return; }
        const double before = outScalar(*n);
        if (!driver(body, drive)) { fail.push_back(std::string(typeId) + "(no-field)"); return; }
        d.recomputeAndPropagate();
        const double after = outScalar(*n);
        const bool good = std::abs(after - expect) < 1e-4 && std::abs(before - expect) > 1e-9;  // changed AND correct
        if (good) ++ok; else fail.push_back(std::string(typeId) + "(before=" + std::to_string(before) + " after=" + std::to_string(after) + ")");
        printf("[static]   %-26s field -> %.4f : default %.4f -> %.4f (want %.4f)  %s\n", typeId, drive, before, after, expect, good ? "ok" : "FAIL");
    };

    scalarCase("static_StaticFloatNode",  [](QWidget* b, double v){ if (auto* s=b->findChild<QDoubleSpinBox*>()){ s->setValue(v); return true;} return false; }, 3.5, 3.5);
    scalarCase("static_StaticDoubleNode", [](QWidget* b, double v){ if (auto* s=b->findChild<QDoubleSpinBox*>()){ s->setValue(v); return true;} return false; }, 2.25, 2.25);
    scalarCase("static_StaticIntNode",    [](QWidget* b, double v){ if (auto* s=b->findChild<QSpinBox*>()){ s->setValue(int(v)); return true;} return false; }, 7.0, 7.0);
    scalarCase("static_StaticBoolNode",   [](QWidget* b, double v){ if (auto* s=b->findChild<QCheckBox*>()){ s->setChecked(v!=0.0); return true;} return false; }, 1.0, 1.0);

    // vec3: drive 3 component spinboxes; assert the emitted glm::vec3 matches.
    {
        ++M;
        NodeDelegate d("static_StaticVec3Node"); Node* n = d.backendNode(); QWidget* body = n ? d.embeddedWidget() : nullptr;
        auto boxes = body ? body->findChildren<QDoubleSpinBox*>() : QList<QDoubleSpinBox*>();
        if (n && boxes.size() == 3) {
            const double comp[3] = { 1.0, 2.0, 3.0 };
            for (int k = 0; k < 3; ++k) boxes[k]->setValue(comp[k]);
            d.recomputeAndPropagate();
            glm::vec3 v(0); bool found = false;
            for (const auto& p : n->getPorts())
                if (p.direction == Port::Direction::Output && p.name == "Value" && p.packet.has_value())
                    { try { v = std::any_cast<glm::vec3>(p.packet->data); found = true; } catch (...) {} }
            const bool good = found && std::abs(v.x-1)<1e-4 && std::abs(v.y-2)<1e-4 && std::abs(v.z-3)<1e-4;
            if (good) ++ok; else fail.push_back("static_StaticVec3Node");
            printf("[static]   %-26s field -> (1,2,3) : emitted (%.2f,%.2f,%.2f)  %s\n", "static_StaticVec3Node", v.x, v.y, v.z, good ? "ok" : "FAIL");
        } else fail.push_back("static_StaticVec3Node(fields)");
    }
    // string: drive the line edit; assert the emitted std::string matches.
    {
        ++M;
        NodeDelegate d("static_StaticStringNode"); Node* n = d.backendNode(); QWidget* body = n ? d.embeddedWidget() : nullptr;
        auto* le = body ? body->findChild<QLineEdit*>() : nullptr;
        if (n && le) {
            le->setText("hello"); d.recomputeAndPropagate();
            std::string s; bool found = false;
            for (const auto& p : n->getPorts())
                if (p.direction == Port::Direction::Output && p.name == "Value" && p.packet.has_value())
                    { try { s = std::any_cast<std::string>(p.packet->data); found = true; } catch (...) {} }
            const bool good = found && s == "hello";
            if (good) ++ok; else fail.push_back("static_StaticStringNode");
            printf("[static]   %-26s field -> \"hello\" : emitted \"%s\"  %s\n", "static_StaticStringNode", s.c_str(), good ? "ok" : "FAIL");
        } else fail.push_back("static_StaticStringNode(field)");
    }

    // NEG-CTRL: a matrix constant has NO single-field value editor (deferred), so no krs_static_value field.
    bool negOk = false;
    {
        NodeDelegate d("static_StaticMat4Node"); QWidget* body = d.embeddedWidget();
        bool hasField = false;
        if (body) for (QWidget* w : body->findChildren<QWidget*>())
            if (w->property("krs_static_value").isValid()) hasField = true;
        negOk = !hasField;     // correctly field-less (not falsely claimed editable)
    }

    // DEFERRED-PATH test: the LIVE app runs the NodeEditQueue in deferred mode -- an edit must survive
    // post -> drain (and a vec3's three components, edited in ONE frame, must NOT coalesce/drop).
    bool deferredOk = false, vecDeferOk = false;
    {
        auto& Q = NodeEditQueue::instance();
        Q.setDeferred(true); Q.drain();
        NodeDelegate d("static_StaticFloatNode"); Node* n = d.backendNode(); QWidget* body = n ? d.embeddedWidget() : nullptr;
        if (auto* sb = body ? body->findChild<QDoubleSpinBox*>() : nullptr) sb->setValue(4.25);
        const double beforeDrain = n ? outScalar(*n) : std::nan("");   // queued, not yet applied
        Q.drain();
        const double afterDrain = n ? outScalar(*n) : std::nan("");
        deferredOk = std::abs(beforeDrain - 4.25) > 1e-6 && std::abs(afterDrain - 4.25) < 1e-4;

        NodeDelegate dv("static_StaticVec3Node"); Node* nv = dv.backendNode(); QWidget* bv = nv ? dv.embeddedWidget() : nullptr;
        auto vb = bv ? bv->findChildren<QDoubleSpinBox*>() : QList<QDoubleSpinBox*>();
        if (vb.size() == 3) { vb[0]->setValue(5); vb[1]->setValue(6); vb[2]->setValue(7); }   // all 3 in one frame
        Q.drain();
        glm::vec3 vv(0); bool found = false;
        if (nv) for (const auto& p : nv->getPorts())
            if (p.direction == Port::Direction::Output && p.name == "Value" && p.packet.has_value())
                { try { vv = std::any_cast<glm::vec3>(p.packet->data); found = true; } catch (...) {} }
        vecDeferOk = found && std::abs(vv.x - 5) < 1e-4 && std::abs(vv.y - 6) < 1e-4 && std::abs(vv.z - 7) < 1e-4;
        Q.setDeferred(false);
        printf("[static]   DEFERRED: edit queued then drained -> %.4f (want 4.25)=%s; vec3 3 components in one frame -> (%.1f,%.1f,%.1f) no coalesce=%s\n",
               afterDrain, deferredOk ? "ok" : "FAIL", vv.x, vv.y, vv.z, vecDeferOk ? "ok" : "FAIL");
    }

    const bool pass = (ok == M) && (M > 0) && negOk && deferredOk && vecDeferOk;
    printf("[static]   coverage: %d/%d constant types' value field sets the emitted constant; NEG-CTRL matrix const has no field=%s\n",
           ok, M, negOk ? "yes" : "NO");
    if (!fail.empty()) { printf("[static]   FAILURES: "); for (auto& s : fail) printf("%s ", s.c_str()); printf("\n"); }
    printf("[static] %s\n", pass ? "ALL PASS (scalar/vector/string constants are field-editable + backed; matrix constants deferred)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
