#pragma once
#include <QCoreApplication>
#include <QString>

inline QString dataDir()                // e.g. ".../RoboticsSoftware"
{
    return QCoreApplication::applicationDirPath();
}

inline QString shaderDir()              // ".../RoboticsSoftware/shaders/"
{
    return dataDir() + "/shaders/";
}
