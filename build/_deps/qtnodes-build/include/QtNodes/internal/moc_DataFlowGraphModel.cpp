/****************************************************************************
** Meta object code from reading C++ file 'DataFlowGraphModel.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtnodes-src/include/QtNodes/internal/DataFlowGraphModel.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DataFlowGraphModel.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7QtNodes18DataFlowGraphModelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN7QtNodes18DataFlowGraphModelE = QtMocHelpers::stringData(
    "QtNodes::DataFlowGraphModel",
    "inPortDataWasSet",
    "",
    "NodeId",
    "PortType",
    "PortIndex",
    "onOutPortDataUpdated",
    "nodeId",
    "portIndex",
    "propagateEmptyDataTo"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN7QtNodes18DataFlowGraphModelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    3,   32,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       6,    2,   39,    2, 0x08,    5 /* Private */,
       9,    2,   44,    2, 0x08,    8 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 4, 0x80000000 | 5,    2,    2,    2,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5,    7,    8,
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5,    7,    8,

       0        // eod
};

Q_CONSTINIT const QMetaObject QtNodes::DataFlowGraphModel::staticMetaObject = { {
    QMetaObject::SuperData::link<AbstractGraphModel::staticMetaObject>(),
    qt_meta_stringdata_ZN7QtNodes18DataFlowGraphModelE.offsetsAndSizes,
    qt_meta_data_ZN7QtNodes18DataFlowGraphModelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN7QtNodes18DataFlowGraphModelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DataFlowGraphModel, std::true_type>,
        // method 'inPortDataWasSet'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortType const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'onOutPortDataUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'propagateEmptyDataTo'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>
    >,
    nullptr
} };

void QtNodes::DataFlowGraphModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DataFlowGraphModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->inPortDataWasSet((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<PortType>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[3]))); break;
        case 1: _t->onOutPortDataUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[2]))); break;
        case 2: _t->propagateEmptyDataTo((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DataFlowGraphModel::*)(NodeId const , PortType const , PortIndex const );
            if (_q_method_type _q_method = &DataFlowGraphModel::inPortDataWasSet; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *QtNodes::DataFlowGraphModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtNodes::DataFlowGraphModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN7QtNodes18DataFlowGraphModelE.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "Serializable"))
        return static_cast< Serializable*>(this);
    return AbstractGraphModel::qt_metacast(_clname);
}

int QtNodes::DataFlowGraphModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AbstractGraphModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void QtNodes::DataFlowGraphModel::inPortDataWasSet(NodeId const _t1, PortType const _t2, PortIndex const _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
