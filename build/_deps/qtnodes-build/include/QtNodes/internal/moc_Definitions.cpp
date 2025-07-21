/****************************************************************************
** Meta object code from reading C++ file 'Definitions.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtnodes-src/include/QtNodes/internal/Definitions.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Definitions.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN7QtNodesE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN7QtNodesE = QtMocHelpers::stringData(
    "QtNodes",
    "NodeRole",
    "Type",
    "Position",
    "Size",
    "CaptionVisible",
    "Caption",
    "Style",
    "InternalData",
    "InPortCount",
    "OutPortCount",
    "Widget",
    "NodeFlags",
    "NodeFlag",
    "NoFlags",
    "Resizable",
    "Locked",
    "PortRole",
    "Data",
    "DataType",
    "ConnectionPolicyRole",
    "ConnectionPolicy",
    "One",
    "Many",
    "PortType",
    "In",
    "Out",
    "None"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN7QtNodesE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       5,   14, // enums/sets
       0,    0, // constructors
       4,       // flags
       0,       // signalCount

 // enums: name, alias, flags, count, data
       1,    1, 0x2,   10,   39,
      12,   13, 0x1,    3,   59,
      17,   17, 0x2,    5,   65,
      21,   21, 0x2,    2,   75,
      24,   24, 0x2,    3,   79,

 // enum data: key, value
       2, uint(QtNodes::NodeRole::Type),
       3, uint(QtNodes::NodeRole::Position),
       4, uint(QtNodes::NodeRole::Size),
       5, uint(QtNodes::NodeRole::CaptionVisible),
       6, uint(QtNodes::NodeRole::Caption),
       7, uint(QtNodes::NodeRole::Style),
       8, uint(QtNodes::NodeRole::InternalData),
       9, uint(QtNodes::NodeRole::InPortCount),
      10, uint(QtNodes::NodeRole::OutPortCount),
      11, uint(QtNodes::NodeRole::Widget),
      14, uint(QtNodes::NoFlags),
      15, uint(QtNodes::Resizable),
      16, uint(QtNodes::Locked),
      18, uint(QtNodes::PortRole::Data),
      19, uint(QtNodes::PortRole::DataType),
      20, uint(QtNodes::PortRole::ConnectionPolicyRole),
       5, uint(QtNodes::PortRole::CaptionVisible),
       6, uint(QtNodes::PortRole::Caption),
      22, uint(QtNodes::ConnectionPolicy::One),
      23, uint(QtNodes::ConnectionPolicy::Many),
      25, uint(QtNodes::PortType::In),
      26, uint(QtNodes::PortType::Out),
      27, uint(QtNodes::PortType::None),

       0        // eod
};

Q_CONSTINIT const QMetaObject QtNodes::staticMetaObject = { {
    nullptr,
    qt_meta_stringdata_ZN7QtNodesE.offsetsAndSizes,
    qt_meta_data_ZN7QtNodesE,
    nullptr,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN7QtNodesE_t,
        // enum 'NodeRole'
        QtPrivate::TypeAndForceComplete<QtNodes::NodeRole, std::true_type>,
        // enum 'NodeFlags'
        QtPrivate::TypeAndForceComplete<QtNodes::NodeFlags, std::true_type>,
        // enum 'PortRole'
        QtPrivate::TypeAndForceComplete<QtNodes::PortRole, std::true_type>,
        // enum 'ConnectionPolicy'
        QtPrivate::TypeAndForceComplete<QtNodes::ConnectionPolicy, std::true_type>,
        // enum 'PortType'
        QtPrivate::TypeAndForceComplete<QtNodes::PortType, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<void, std::true_type>
    >,
    nullptr
} };

QT_WARNING_POP
