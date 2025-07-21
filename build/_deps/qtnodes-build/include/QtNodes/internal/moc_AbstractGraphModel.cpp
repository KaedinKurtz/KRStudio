/****************************************************************************
** Meta object code from reading C++ file 'AbstractGraphModel.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtnodes-src/include/QtNodes/internal/AbstractGraphModel.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AbstractGraphModel.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7QtNodes18AbstractGraphModelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN7QtNodes18AbstractGraphModelE = QtMocHelpers::stringData(
    "QtNodes::AbstractGraphModel",
    "connectionCreated",
    "",
    "ConnectionId",
    "connectionId",
    "connectionDeleted",
    "nodeCreated",
    "NodeId",
    "nodeId",
    "nodeDeleted",
    "nodeUpdated",
    "nodeFlagsUpdated",
    "nodePositionUpdated",
    "modelReset"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN7QtNodes18AbstractGraphModelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       8,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   62,    2, 0x06,    1 /* Public */,
       5,    1,   65,    2, 0x06,    3 /* Public */,
       6,    1,   68,    2, 0x06,    5 /* Public */,
       9,    1,   71,    2, 0x06,    7 /* Public */,
      10,    1,   74,    2, 0x06,    9 /* Public */,
      11,    1,   77,    2, 0x06,   11 /* Public */,
      12,    1,   80,    2, 0x06,   13 /* Public */,
      13,    0,   83,    2, 0x06,   15 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject QtNodes::AbstractGraphModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN7QtNodes18AbstractGraphModelE.offsetsAndSizes,
    qt_meta_data_ZN7QtNodes18AbstractGraphModelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN7QtNodes18AbstractGraphModelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AbstractGraphModel, std::true_type>,
        // method 'connectionCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        // method 'connectionDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        // method 'nodeCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeFlagsUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodePositionUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'modelReset'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void QtNodes::AbstractGraphModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AbstractGraphModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->connectionCreated((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 1: _t->connectionDeleted((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 2: _t->nodeCreated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 3: _t->nodeDeleted((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 4: _t->nodeUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 5: _t->nodeFlagsUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 6: _t->nodePositionUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 7: _t->modelReset(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (AbstractGraphModel::*)(ConnectionId const );
            if (_q_method_type _q_method = &AbstractGraphModel::connectionCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(ConnectionId const );
            if (_q_method_type _q_method = &AbstractGraphModel::connectionDeleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(NodeId const );
            if (_q_method_type _q_method = &AbstractGraphModel::nodeCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(NodeId const );
            if (_q_method_type _q_method = &AbstractGraphModel::nodeDeleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(NodeId const );
            if (_q_method_type _q_method = &AbstractGraphModel::nodeUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(NodeId const );
            if (_q_method_type _q_method = &AbstractGraphModel::nodeFlagsUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)(NodeId const );
            if (_q_method_type _q_method = &AbstractGraphModel::nodePositionUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (AbstractGraphModel::*)();
            if (_q_method_type _q_method = &AbstractGraphModel::modelReset; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
    }
}

const QMetaObject *QtNodes::AbstractGraphModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtNodes::AbstractGraphModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN7QtNodes18AbstractGraphModelE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int QtNodes::AbstractGraphModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void QtNodes::AbstractGraphModel::connectionCreated(ConnectionId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void QtNodes::AbstractGraphModel::connectionDeleted(ConnectionId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void QtNodes::AbstractGraphModel::nodeCreated(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void QtNodes::AbstractGraphModel::nodeDeleted(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void QtNodes::AbstractGraphModel::nodeUpdated(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void QtNodes::AbstractGraphModel::nodeFlagsUpdated(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void QtNodes::AbstractGraphModel::nodePositionUpdated(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void QtNodes::AbstractGraphModel::modelReset()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}
QT_WARNING_POP
