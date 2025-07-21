#pragma once
#include "Node.hpp"

namespace NodeLibrary {
    class AddNode : public Node {
    public:
        AddNode();
        void compute() override;
    };
}
