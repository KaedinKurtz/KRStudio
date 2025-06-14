#pragma once

#include <QDebug>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <QString>
#include <iomanip>

// This operator overload allows you to stream entt::entity directly to qDebug.
// It's marked 'inline' so it can be defined in a header without causing linker errors.
inline QDebug operator<<(QDebug dbg, const entt::entity entity) {
    dbg.nospace() << "e(" << static_cast<uint32_t>(entity) << ")";
    return dbg.space();
}

// Helper to print a glm::mat4 clearly to the console for debugging.
inline void printMatrix(const glm::mat4& mat, const QString& label) {
    qDebug().noquote() << label;
    for (int i = 0; i < 4; ++i) {
        QString row = QString("  | %1 %2 %3 %4 |")
            .arg(mat[i][0], 8, 'f', 2)
            .arg(mat[i][1], 8, 'f', 2)
            .arg(mat[i][2], 8, 'f', 2)
            .arg(mat[i][3], 8, 'f', 2);
        qDebug().noquote() << row;
    }
}