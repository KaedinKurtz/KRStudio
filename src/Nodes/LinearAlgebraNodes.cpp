#include "LinearAlgebraNodes.hpp"
#include <memory> // For std::make_unique

namespace NodeLibrary {

    // --- Vector Operation Nodes ---

    // VectorMagnitudeNode
    VectorMagnitudeNode::VectorMagnitudeNode() {
	m_id = "linalg_vec_magnitude";
        m_ports.push_back({ "Vector", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Magnitude", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void VectorMagnitudeNode::compute() {
        auto vec = getInput<Eigen::VectorXf>("Vector");
        if (vec) setOutput("Magnitude", vec->norm());
    }
    namespace {
        struct VectorMagnitudeRegistrar {
            VectorMagnitudeRegistrar() {
                NodeDescriptor desc = { "Vector Magnitude", "Linear Algebra/Vector", "Calculates the length (L2 norm) of a vector." };
                NodeFactory::instance().registerNodeType("linalg_vec_magnitude", desc, []() { return std::make_unique<VectorMagnitudeNode>(); });
            }
        };
        static VectorMagnitudeRegistrar g_vectorMagnitudeRegistrar;
    }


    // NormalizeVectorNode
    NormalizeVectorNode::NormalizeVectorNode() {
	m_id = "linalg_vec_normalize";
        m_ports.push_back({ "Input", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
    }
    void NormalizeVectorNode::compute() {
        auto vec = getInput<Eigen::VectorXf>("Input");
        if (vec) {
            vec->normalize();
            setOutput("Output", *vec);
        }
    }
    namespace {
        struct NormalizeVectorRegistrar {
            NormalizeVectorRegistrar() {
                NodeDescriptor desc = { "Normalize Vector", "Linear Algebra/Vector", "Scales a vector to have a magnitude of 1." };
                NodeFactory::instance().registerNodeType("linalg_vec_normalize", desc, []() { return std::make_unique<NormalizeVectorNode>(); });
            }
        };
        static NormalizeVectorRegistrar g_normalizeVectorRegistrar;
    }


    // VectorCrossProductNode
    VectorCrossProductNode::VectorCrossProductNode() {
	m_id = "linalg_vec_cross";
        m_ports.push_back({ "A", {"Eigen::Vector3f", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"Eigen::Vector3f", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"Eigen::Vector3f", "vector"}, Port::Direction::Output, this });
    }
    void VectorCrossProductNode::compute() {
        auto a = getInput<Eigen::Vector3f>("A");
        auto b = getInput<Eigen::Vector3f>("B");
        if (a && b) setOutput("Result", a->cross(*b));
    }
    namespace {
        struct VectorCrossProductRegistrar {
            VectorCrossProductRegistrar() {
                NodeDescriptor desc = { "Cross Product", "Linear Algebra/Vector", "Calculates the cross product of two 3D vectors." };
                NodeFactory::instance().registerNodeType("linalg_vec_cross", desc, []() { return std::make_unique<VectorCrossProductNode>(); });
            }
        };
        static VectorCrossProductRegistrar g_vectorCrossProductRegistrar;
    }


    // --- Matrix Operation Nodes ---

    // MatrixMultiplyNode
    MatrixMultiplyNode::MatrixMultiplyNode() {
	m_id = "linalg_mat_multiply";
        m_ports.push_back({ "A", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "B", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Output, this });
    }
    void MatrixMultiplyNode::compute() {
        auto a = getInput<Eigen::MatrixXf>("A");
        auto b = getInput<Eigen::MatrixXf>("B");
        if (a && b && a->cols() == b->rows()) {
            setOutput("Result", (*a) * (*b));
        }
    }
    namespace {
        struct MatrixMultiplyRegistrar {
            MatrixMultiplyRegistrar() {
                NodeDescriptor desc = { "Matrix Multiply", "Linear Algebra/Matrix", "Outputs the product of two matrices A * B." };
                NodeFactory::instance().registerNodeType("linalg_mat_multiply", desc, []() { return std::make_unique<MatrixMultiplyNode>(); });
            }
        };
        static MatrixMultiplyRegistrar g_matrixMultiplyRegistrar;
    }


    // MatrixInverseNode
    MatrixInverseNode::MatrixInverseNode() {
	m_id = "linalg_mat_inverse";
        m_ports.push_back({ "Input", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Inverse", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Output, this });
        m_ports.push_back({ "Success", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void MatrixInverseNode::compute() {
        auto mat = getInput<Eigen::MatrixXf>("Input");
        if (mat && mat->rows() == mat->cols()) {
            if (mat->determinant() != 0) {
                setOutput("Inverse", mat->inverse());
                setOutput("Success", true);
                return;
            }
        }
        setOutput("Success", false);
    }
    namespace {
        struct MatrixInverseRegistrar {
            MatrixInverseRegistrar() {
                NodeDescriptor desc = { "Matrix Inverse", "Linear Algebra/Matrix", "Calculates the inverse of a square matrix." };
                NodeFactory::instance().registerNodeType("linalg_mat_inverse", desc, []() { return std::make_unique<MatrixInverseNode>(); });
            }
        };
        static MatrixInverseRegistrar g_matrixInverseRegistrar;
    }


    // MatrixTransposeNode
    MatrixTransposeNode::MatrixTransposeNode() {
	m_id = "linalg_mat_transpose";
        m_ports.push_back({ "Input", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Transpose", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Output, this });
    }
    void MatrixTransposeNode::compute() {
        auto mat = getInput<Eigen::MatrixXf>("Input");
        if (mat) setOutput("Transpose", mat->transpose());
    }
    namespace {
        struct MatrixTransposeRegistrar {
            MatrixTransposeRegistrar() {
                NodeDescriptor desc = { "Matrix Transpose", "Linear Algebra/Matrix", "Swaps the rows and columns of a matrix." };
                NodeFactory::instance().registerNodeType("linalg_mat_transpose", desc, []() { return std::make_unique<MatrixTransposeNode>(); });
            }
        };
        static MatrixTransposeRegistrar g_matrixTransposeRegistrar;
    }


    // MatrixDeterminantNode
    MatrixDeterminantNode::MatrixDeterminantNode() {
	m_id = "linalg_mat_determinant";
        m_ports.push_back({ "Input", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Determinant", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void MatrixDeterminantNode::compute() {
        auto mat = getInput<Eigen::MatrixXf>("Input");
        if (mat && mat->rows() == mat->cols()) {
            setOutput("Determinant", mat->determinant());
        }
    }
    namespace {
        struct MatrixDeterminantRegistrar {
            MatrixDeterminantRegistrar() {
                NodeDescriptor desc = { "Matrix Determinant", "Linear Algebra/Matrix", "Calculates the determinant of a square matrix." };
                NodeFactory::instance().registerNodeType("linalg_mat_determinant", desc, []() { return std::make_unique<MatrixDeterminantNode>(); });
            }
        };
        static MatrixDeterminantRegistrar g_matrixDeterminantRegistrar;
    }


    // --- Matrix-Vector Operation Nodes ---

    // MatrixVectorMultiplyNode
    MatrixVectorMultiplyNode::MatrixVectorMultiplyNode() {
	m_id = "linalg_mat_vec_multiply";
        m_ports.push_back({ "Matrix", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Vector", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "Result", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
    }
    void MatrixVectorMultiplyNode::compute() {
        auto mat = getInput<Eigen::MatrixXf>("Matrix");
        auto vec = getInput<Eigen::VectorXf>("Vector");
        if (mat && vec && mat->cols() == vec->rows()) {
            setOutput("Result", (*mat) * (*vec));
        }
    }
    namespace {
        struct MatrixVectorMultiplyRegistrar {
            MatrixVectorMultiplyRegistrar() {
                NodeDescriptor desc = { "Matrix-Vector Multiply", "Linear Algebra/Transform", "Transforms a vector by a matrix." };
                NodeFactory::instance().registerNodeType("linalg_mat_vec_multiply", desc, []() { return std::make_unique<MatrixVectorMultiplyNode>(); });
            }
        };
        static MatrixVectorMultiplyRegistrar g_matrixVectorMultiplyRegistrar;
    }


    // --- Solvers & Decompositions ---

    // SolveLinearSystemNode
    SolveLinearSystemNode::SolveLinearSystemNode() {
	m_id = "linalg_solve";
        m_ports.push_back({ "A (Matrix)", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "b (Vector)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "x (Solution)", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
        m_ports.push_back({ "Success", {"bool", "boolean"}, Port::Direction::Output, this });
    }
    void SolveLinearSystemNode::compute() {
        auto a = getInput<Eigen::MatrixXf>("A (Matrix)");
        auto b = getInput<Eigen::VectorXf>("b (Vector)");
        if (a && b && a->rows() == a->cols() && a->rows() == b->rows()) {
            // Using PartialPivLU is robust for general square matrices.
            // Store the decomposition to be more efficient.
            auto lu = a->partialPivLu();
            // FIX: The method is 'determinant()', not 'isInvertible()'.
            if (lu.determinant() != 0) {
                setOutput("x (Solution)", lu.solve(*b));
                setOutput("Success", true);
                return;
            }
        }
        setOutput("Success", false);
    }
    namespace {
        struct SolveLinearSystemRegistrar {
            SolveLinearSystemRegistrar() {
                NodeDescriptor desc = { "Solve Linear System", "Linear Algebra/Solvers", "Solves the system Ax = b for x." };
                NodeFactory::instance().registerNodeType("linalg_solve", desc, []() { return std::make_unique<SolveLinearSystemNode>(); });
            }
        };
        static SolveLinearSystemRegistrar g_solveLinearSystemRegistrar;
    }


    // EigenvalueSolverNode
    EigenvalueSolverNode::EigenvalueSolverNode() {
	m_id = "linalg_eigen_solver";
        m_ports.push_back({ "Matrix", {"Eigen::MatrixXf", "matrix"}, Port::Direction::Input, this });
        m_ports.push_back({ "Eigenvalues", {"Eigen::VectorXcf", "vector"}, Port::Direction::Output, this });
        m_ports.push_back({ "Eigenvectors", {"Eigen::MatrixXcf", "matrix"}, Port::Direction::Output, this });
    }
    void EigenvalueSolverNode::compute() {
        auto mat = getInput<Eigen::MatrixXf>("Matrix");
        if (mat && mat->rows() == mat->cols()) {
            Eigen::EigenSolver<Eigen::MatrixXf> es(*mat);
            setOutput("Eigenvalues", es.eigenvalues());
            setOutput("Eigenvectors", es.eigenvectors());
        }
    }
    namespace {
        struct EigenvalueSolverRegistrar {
            EigenvalueSolverRegistrar() {
                NodeDescriptor desc = { "Eigenvalue Solver", "Linear Algebra/Solvers", "Computes eigenvalues and eigenvectors of a matrix." };
                NodeFactory::instance().registerNodeType("linalg_eigen_solver", desc, []() { return std::make_unique<EigenvalueSolverNode>(); });
            }
        };
        static EigenvalueSolverRegistrar g_eigenvalueSolverRegistrar;
    }


    // --- Construction & Deconstruction ---

    // ComposeVector3Node
    ComposeVector3Node::ComposeVector3Node() {
	m_id = "linalg_compose_vec3";
        m_ports.push_back({ "X", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Y", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Z", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Vector", {"Eigen::Vector3f", "vector"}, Port::Direction::Output, this });
    }
    void ComposeVector3Node::compute() {
        auto x = getInput<float>("X");
        auto y = getInput<float>("Y");
        auto z = getInput<float>("Z");
        if (x && y && z) {
            setOutput("Vector", Eigen::Vector3f(*x, *y, *z));
        }
    }
    namespace {
        struct ComposeVector3Registrar {
            ComposeVector3Registrar() {
                NodeDescriptor desc = { "Compose Vector3", "Linear Algebra/Vector", "Creates a 3D vector from X, Y, Z components." };
                NodeFactory::instance().registerNodeType("linalg_compose_vec3", desc, []() { return std::make_unique<ComposeVector3Node>(); });
            }
        };
        static ComposeVector3Registrar g_composeVector3Registrar;
    }


    // DecomposeVector3Node
    DecomposeVector3Node::DecomposeVector3Node() {
	m_id = "linalg_decompose_vec3";
        m_ports.push_back({ "Vector", {"Eigen::Vector3f", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "X", {"float", "unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "Y", {"float", "unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "Z", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void DecomposeVector3Node::compute() {
        auto vec = getInput<Eigen::Vector3f>("Vector");
        if (vec) {
            setOutput("X", (*vec)(0));
            setOutput("Y", (*vec)(1));
            setOutput("Z", (*vec)(2));
        }
    }
    namespace {
        struct DecomposeVector3Registrar {
            DecomposeVector3Registrar() {
                NodeDescriptor desc = { "Decompose Vector3", "Linear Algebra/Vector", "Extracts the X, Y, Z components from a 3D vector." };
                NodeFactory::instance().registerNodeType("linalg_decompose_vec3", desc, []() { return std::make_unique<DecomposeVector3Node>(); });
            }
        };
        static DecomposeVector3Registrar g_decomposeVector3Registrar;
    }

    // --- Differential Equation Solvers ---

    // RK4SolverNode
    RK4SolverNode::RK4SolverNode() {
	m_id = "linalg_ode_rk4";
        m_ports.push_back({ "f(t, y)", {"std::function<Eigen::VectorXf(float, Eigen::VectorXf)>", "function"}, Port::Direction::Input, this });
        m_ports.push_back({ "y0 (Initial)", {"Eigen::VectorXf", "vector"}, Port::Direction::Input, this });
        m_ports.push_back({ "dt (Time Step)", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Reset", {"bool", "boolean"}, Port::Direction::Input, this });
        m_ports.push_back({ "y (State)", {"Eigen::VectorXf", "vector"}, Port::Direction::Output, this });
        m_ports.push_back({ "t (Time)", {"float", "seconds"}, Port::Direction::Output, this });
    }

    void RK4SolverNode::compute() {
        auto y0 = getInput<Eigen::VectorXf>("y0 (Initial)");
        auto reset = getInput<bool>("Reset");

        if (reset && *reset) {
            m_is_initialized = false;
        }

        if (!m_is_initialized && y0) {
            m_y = *y0;
            m_t = 0.0;
            m_is_initialized = true;
        }

        if (!m_is_initialized) {
            return; // Waiting for initial conditions
        }

        auto f = getInput<std::function<Eigen::VectorXf(float, Eigen::VectorXf)>>("f(t, y)");
        auto dt_opt = getInput<float>("dt (Time Step)");

        if (f && dt_opt) {
            float dt = *dt_opt;

            // RK4 Algorithm
            Eigen::VectorXf k1 = (*f)(static_cast<float>(m_t), m_y);
            Eigen::VectorXf k2 = (*f)(static_cast<float>(m_t + 0.5 * dt), m_y + 0.5f * dt * k1);
            Eigen::VectorXf k3 = (*f)(static_cast<float>(m_t + 0.5 * dt), m_y + 0.5f * dt * k2);
            Eigen::VectorXf k4 = (*f)(static_cast<float>(m_t + dt), m_y + dt * k3);

            m_y = m_y + (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
            m_t += dt;

            setOutput("y (State)", m_y);
            setOutput("t (Time)", static_cast<float>(m_t));
        }
    }
    namespace {
        struct RK4SolverRegistrar {
            RK4SolverRegistrar() {
                NodeDescriptor desc = { "ODE Solver (RK4)", "Linear Algebra/Differential Equations", "Solves dy/dt = f(t, y) using Runge-Kutta 4." };
                NodeFactory::instance().registerNodeType("linalg_ode_rk4", desc, []() { return std::make_unique<RK4SolverNode>(); });
            }
        };
        static RK4SolverRegistrar g_rk4SolverRegistrar;
    }

} // namespace NodeLibrary