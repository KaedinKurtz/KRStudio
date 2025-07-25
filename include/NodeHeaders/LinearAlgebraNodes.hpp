#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <Eigen/Dense>
#include <functional>

namespace NodeLibrary {

    // --- Vector Operations ---
    class VectorMagnitudeNode : public Node { public:
        QWidget* createCustomWidget() override; VectorMagnitudeNode(); void compute() override; };
    class NormalizeVectorNode : public Node { public:
        QWidget* createCustomWidget() override; NormalizeVectorNode(); void compute() override; };
    class VectorCrossProductNode : public Node { public:
        QWidget* createCustomWidget() override; VectorCrossProductNode(); void compute() override; };

    // --- Matrix Operations ---
    class MatrixMultiplyNode : public Node { public:
        QWidget* createCustomWidget() override; MatrixMultiplyNode(); void compute() override; };
    class MatrixInverseNode : public Node { public:
        QWidget* createCustomWidget() override; MatrixInverseNode(); void compute() override; };
    class MatrixTransposeNode : public Node { public:
        QWidget* createCustomWidget() override; MatrixTransposeNode(); void compute() override; };
    class MatrixDeterminantNode : public Node { public:
        QWidget* createCustomWidget() override; MatrixDeterminantNode(); void compute() override; };

    // --- Matrix-Vector Operations ---
    class MatrixVectorMultiplyNode : public Node { public:
        QWidget* createCustomWidget() override; MatrixVectorMultiplyNode(); void compute() override; };

    // --- Solvers & Decompositions ---
    class SolveLinearSystemNode : public Node { public:
        QWidget* createCustomWidget() override; SolveLinearSystemNode(); void compute() override; };
    class EigenvalueSolverNode : public Node { public:
        QWidget* createCustomWidget() override; EigenvalueSolverNode(); void compute() override; };

    // --- Construction & Deconstruction ---
    class ComposeVector3Node : public Node { public:
        QWidget* createCustomWidget() override; ComposeVector3Node(); void compute() override; };
    class DecomposeVector3Node : public Node { public:
        QWidget* createCustomWidget() override; DecomposeVector3Node(); void compute() override; };

    // --- Differential Equations ---
    /**
     * @brief A stateful node that solves an ordinary differential equation dy/dt = f(t, y)
     * using the 4th Order Runge-Kutta method.
     */
    class RK4SolverNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        RK4SolverNode();
        void compute() override;
    private:
        // Internal state of the solver
        double m_t = 0.0; // Current time
        Eigen::VectorXf m_y; // Current state vector y
        bool m_is_initialized = false;
    };


} // namespace NodeLibrary
