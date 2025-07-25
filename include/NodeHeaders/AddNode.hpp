#pragma once

#include <QWidget>
#include "Node.hpp"

namespace NodeLibrary {
    class AddNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        AddNode();
        void compute() override;
    };
}
