#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <string>
#include <vector>
#include <map>
#include <any>
#include <deque>
#include <optional>

namespace NodeLibrary {

    // --- Graph Organization Nodes ---

    class RerouteNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        RerouteNode();
        void compute() override;
        void setDataType(const DataType& type);
    };

    class CommentNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        std::string commentText = "This is a comment.";
        CommentNode();
        void compute() override;
    };


    // --- Logic & Flow Control Nodes ---

    class SwitchNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        SwitchNode();
        void compute() override;
    };

    class SwitchCaseNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        SwitchCaseNode();
        void compute() override;
        void addCase(int caseValue);
        void removeCase(int caseValue);
    private:
        std::vector<int> m_cases;
    };


    // --- Custom Scripting Node ---

    class CustomScriptNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        std::string scriptContent =
            "// Define ports like GLSL\n"
            "in float(meters) temperature;\n"
            "in float(pascals) pressure;\n"
            "out float(unitless) risk_factor;\n\n"
            "// Write your logic in the compute function\n"
            "void compute() {\n"
            "    risk_factor = temperature * 0.5 + pressure * 0.1;\n"
            "}";
        CustomScriptNode();
        void compute() override;
        void parseAndRebuildPorts();
    private:
        std::any m_compiledScript;
    };


    // --- Timing & State Nodes ---

    /** @brief Data packet for the DelayNode buffer. */
    struct DelayedData {
        float release_time;
        std::any data;
    };

    class LatchNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        LatchNode();
        void compute() override;
    private:
        bool m_lastLatchState = false;
        std::optional<std::any> m_latchedData;
    };

    class DelayNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        DelayNode();
        void compute() override;
    private:
        std::deque<DelayedData> m_buffer;
    };

} // namespace NodeLibrary