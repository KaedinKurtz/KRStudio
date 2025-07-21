/****************************************************************************
** Meta object code from reading C++ file 'NodeDelegateModel.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtnodes-src/include/QtNodes/internal/NodeDelegateModel.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'NodeDelegateModel.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7QtNodes17NodeDelegateModelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN7QtNodes17NodeDelegateModelE = QtMocHelpers::stringData(
    "QtNodes::NodeDelegateModel",
    "dataUpdated",
    "",
    "PortIndex",
    "index",
    "dataInvalidated",
    "computingStarted",
    "computingFinished",
    "embeddedWidgetSizeUpdated",
    "portsAboutToBeDeleted",
    "PortType",
    "portType",
    "first",
    "last",
    "portsDeleted",
    "portsAboutToBeInserted",
    "portsInserted",
    "inputConnectionCreated",
    "ConnectionId",
    "inputConnectionDeleted",
    "outputConnectionCreated",
    "outputConnectionDeleted"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN7QtNodes17NodeDelegateModelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       9,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   92,    2, 0x06,    1 /* Public */,
       5,    1,   95,    2, 0x06,    3 /* Public */,
       6,    0,   98,    2, 0x06,    5 /* Public */,
       7,    0,   99,    2, 0x06,    6 /* Public */,
       8,    0,  100,    2, 0x06,    7 /* Public */,
       9,    3,  101,    2, 0x06,    8 /* Public */,
      14,    0,  108,    2, 0x06,   12 /* Public */,
      15,    3,  109,    2, 0x06,   13 /* Public */,
      16,    0,  116,    2, 0x06,   17 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      17,    1,  117,    2, 0x0a,   18 /* Public */,
      19,    1,  120,    2, 0x0a,   20 /* Public */,
      20,    1,  123,    2, 0x0a,   22 /* Public */,
      21,    1,  126,    2, 0x0a,   24 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 10, 0x80000000 | 3, 0x80000000 | 3,   11,   12,   13,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 10, 0x80000000 | 3, 0x80000000 | 3,   11,   12,   13,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 18,    2,
    QMetaType::Void, 0x80000000 | 18,    2,
    QMetaType::Void, 0x80000000 | 18,    2,
    QMetaType::Void, 0x80000000 | 18,    2,

       0        // eod
};

Q_CONSTINIT const QMetaObject QtNodes::NodeDelegateModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN7QtNodes17NodeDelegateModelE.offsetsAndSizes,
    qt_meta_data_ZN7QtNodes17NodeDelegateModelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN7QtNodes17NodeDelegateModelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<NodeDelegateModel, std::true_type>,
        // method 'dataUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'dataInvalidated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'computingStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'computingFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'embeddedWidgetSizeUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'portsAboutToBeDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortType const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'portsDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'portsAboutToBeInserted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortType const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        QtPrivate::TypeAndForceComplete<PortIndex const, std::false_type>,
        // method 'portsInserted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'inputConnectionCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const &, std::false_type>,
        // method 'inputConnectionDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const &, std::false_type>,
        // method 'outputConnectionCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const &, std::false_type>,
        // method 'outputConnectionDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const &, std::false_type>
    >,
    nullptr
} };

void QtNodes::NodeDelegateModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<NodeDelegateModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->dataUpdated((*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[1]))); break;
        case 1: _t->dataInvalidated((*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[1]))); break;
        case 2: _t->computingStarted(); break;
        case 3: _t->computingFinished(); break;
        case 4: _t->embeddedWidgetSizeUpdated(); break;
        case 5: _t->portsAboutToBeDeleted((*reinterpret_cast< std::add_pointer_t<PortType>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[3]))); break;
        case 6: _t->portsDeleted(); break;
        case 7: _t->portsAboutToBeInserted((*reinterpret_cast< std::add_pointer_t<PortType>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<PortIndex>>(_a[3]))); break;
        case 8: _t->portsInserted(); break;
        case 9: _t->inputConnectionCreated((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 10: _t->inputConnectionDeleted((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 11: _t->outputConnectionCreated((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 12: _t->outputConnectionDeleted((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (NodeDelegateModel::*)(PortIndex const );
            if (_q_method_type _q_method = &NodeDelegateModel::dataUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)(PortIndex const );
            if (_q_method_type _q_method = &NodeDelegateModel::dataInvalidated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)();
            if (_q_method_type _q_method = &NodeDelegateModel::computingStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)();
            if (_q_method_type _q_method = &NodeDelegateModel::computingFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)();
            if (_q_method_type _q_method = &NodeDelegateModel::embeddedWidgetSizeUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)(PortType const , PortIndex const , PortIndex const );
            if (_q_method_type _q_method = &NodeDelegateModel::portsAboutToBeDeleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)();
            if (_q_method_type _q_method = &NodeDelegateModel::portsDeleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)(PortType const , PortIndex const , PortIndex const );
            if (_q_method_type _q_method = &NodeDelegateModel::portsAboutToBeInserted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (NodeDelegateModel::*)();
            if (_q_method_type _q_method = &NodeDelegateModel::portsInserted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
    }
}

const QMetaObject *QtNodes::NodeDelegateModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtNodes::NodeDelegateModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN7QtNodes17NodeDelegateModelE.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "Serializable"))
        return static_cast< Serializable*>(this);
    return QObject::qt_metacast(_clname);
}

int QtNodes::NodeDelegateModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void QtNodes::NodeDelegateModel::dataUpdated(PortIndex const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void QtNodes::NodeDelegateModel::dataInvalidated(PortIndex const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void QtNodes::NodeDelegateModel::computingStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void QtNodes::NodeDelegateModel::computingFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void QtNodes::NodeDelegateModel::embeddedWidgetSizeUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void QtNodes::NodeDelegateModel::portsAboutToBeDeleted(PortType const _t1, PortIndex const _t2, PortIndex const _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void QtNodes::NodeDelegateModel::portsDeleted()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void QtNodes::NodeDelegateModel::portsAboutToBeInserted(PortType const _t1, PortIndex const _t2, PortIndex const _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void QtNodes::NodeDelegateModel::portsInserted()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}
QT_WARNING_POP
