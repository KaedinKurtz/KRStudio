#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "ControlSystems.hpp" 
#include <Eigen/Dense>

namespace NodeLibrary {

    // --- System Analysis ---
    class IsControllableNode : public Node { public:
        QWidget* createCustomWidget() override; IsControllableNode(); void compute() override; };
    class IsObservableNode : public Node { public:
        QWidget* createCustomWidget() override; IsObservableNode(); void compute() override; };

    // --- Controller Design ---
    class PolePlacementNode : public Node { public:
        QWidget* createCustomWidget() override; PolePlacementNode(); void compute() override; };
    class LQRDesignNode : public Node { public:
        QWidget* createCustomWidget() override; LQRDesignNode(); void compute() override; };

    // --- State Estimation ---
    class KalmanFilterNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        KalmanFilterNode();
        void compute() override;
    private:
        ControlSystems::KalmanFilterState m_state;
        bool m_is_initialized = false;
    };

} // namespace NodeLibrary
