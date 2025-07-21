/****************************************************************************
** Meta object code from reading C++ file 'BasicGraphicsScene.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtnodes-src/include/QtNodes/internal/BasicGraphicsScene.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BasicGraphicsScene.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7QtNodes18BasicGraphicsSceneE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN7QtNodes18BasicGraphicsSceneE = QtMocHelpers::stringData(
    "QtNodes::BasicGraphicsScene",
    "modified",
    "",
    "BasicGraphicsScene*",
    "nodeMoved",
    "NodeId",
    "nodeId",
    "newLocation",
    "nodeClicked",
    "nodeSelected",
    "nodeDoubleClicked",
    "nodeHovered",
    "screenPos",
    "nodeHoverLeft",
    "connectionHovered",
    "ConnectionId",
    "connectionId",
    "connectionHoverLeft",
    "nodeContextMenu",
    "pos",
    "onConnectionDeleted",
    "onConnectionCreated",
    "onNodeDeleted",
    "onNodeCreated",
    "onNodePositionUpdated",
    "onNodeUpdated",
    "onNodeClicked",
    "onModelReset"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN7QtNodes18BasicGraphicsSceneE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      18,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      10,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  122,    2, 0x06,    1 /* Public */,
       4,    2,  125,    2, 0x06,    3 /* Public */,
       8,    1,  130,    2, 0x06,    6 /* Public */,
       9,    1,  133,    2, 0x06,    8 /* Public */,
      10,    1,  136,    2, 0x06,   10 /* Public */,
      11,    2,  139,    2, 0x06,   12 /* Public */,
      13,    1,  144,    2, 0x06,   15 /* Public */,
      14,    2,  147,    2, 0x06,   17 /* Public */,
      17,    1,  152,    2, 0x06,   20 /* Public */,
      18,    2,  155,    2, 0x06,   22 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      20,    1,  160,    2, 0x0a,   25 /* Public */,
      21,    1,  163,    2, 0x0a,   27 /* Public */,
      22,    1,  166,    2, 0x0a,   29 /* Public */,
      23,    1,  169,    2, 0x0a,   31 /* Public */,
      24,    1,  172,    2, 0x0a,   33 /* Public */,
      25,    1,  175,    2, 0x0a,   35 /* Public */,
      26,    1,  178,    2, 0x0a,   37 /* Public */,
      27,    0,  181,    2, 0x0a,   39 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    2,
    QMetaType::Void, 0x80000000 | 5, QMetaType::QPointF,    6,    7,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5, QMetaType::QPoint,    6,   12,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 15, QMetaType::QPoint,   16,   12,
    QMetaType::Void, 0x80000000 | 15,   16,
    QMetaType::Void, 0x80000000 | 5, QMetaType::QPointF,    6,   19,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 15,   16,
    QMetaType::Void, 0x80000000 | 15,   16,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject QtNodes::BasicGraphicsScene::staticMetaObject = { {
    QMetaObject::SuperData::link<QGraphicsScene::staticMetaObject>(),
    qt_meta_stringdata_ZN7QtNodes18BasicGraphicsSceneE.offsetsAndSizes,
    qt_meta_data_ZN7QtNodes18BasicGraphicsSceneE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN7QtNodes18BasicGraphicsSceneE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<BasicGraphicsScene, std::true_type>,
        // method 'modified'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<BasicGraphicsScene *, std::false_type>,
        // method 'nodeMoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPointF const &, std::false_type>,
        // method 'nodeClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeDoubleClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'nodeHovered'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPoint const, std::false_type>,
        // method 'nodeHoverLeft'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'connectionHovered'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPoint const, std::false_type>,
        // method 'connectionHoverLeft'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        // method 'nodeContextMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPointF const, std::false_type>,
        // method 'onConnectionDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        // method 'onConnectionCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ConnectionId const, std::false_type>,
        // method 'onNodeDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'onNodeCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'onNodePositionUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'onNodeUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'onNodeClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<NodeId const, std::false_type>,
        // method 'onModelReset'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void QtNodes::BasicGraphicsScene::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BasicGraphicsScene *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->modified((*reinterpret_cast< std::add_pointer_t<BasicGraphicsScene*>>(_a[1]))); break;
        case 1: _t->nodeMoved((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QPointF>>(_a[2]))); break;
        case 2: _t->nodeClicked((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 3: _t->nodeSelected((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 4: _t->nodeDoubleClicked((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 5: _t->nodeHovered((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[2]))); break;
        case 6: _t->nodeHoverLeft((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 7: _t->connectionHovered((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[2]))); break;
        case 8: _t->connectionHoverLeft((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 9: _t->nodeContextMenu((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QPointF>>(_a[2]))); break;
        case 10: _t->onConnectionDeleted((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 11: _t->onConnectionCreated((*reinterpret_cast< std::add_pointer_t<ConnectionId>>(_a[1]))); break;
        case 12: _t->onNodeDeleted((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 13: _t->onNodeCreated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 14: _t->onNodePositionUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 15: _t->onNodeUpdated((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 16: _t->onNodeClicked((*reinterpret_cast< std::add_pointer_t<NodeId>>(_a[1]))); break;
        case 17: _t->onModelReset(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< BasicGraphicsScene* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (BasicGraphicsScene::*)(BasicGraphicsScene * );
            if (_q_method_type _q_method = &BasicGraphicsScene::modified; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const , QPointF const & );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeMoved; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeClicked; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeSelected; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeDoubleClicked; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const , QPoint const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeHovered; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeHoverLeft; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(ConnectionId const , QPoint const );
            if (_q_method_type _q_method = &BasicGraphicsScene::connectionHovered; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(ConnectionId const );
            if (_q_method_type _q_method = &BasicGraphicsScene::connectionHoverLeft; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (BasicGraphicsScene::*)(NodeId const , QPointF const );
            if (_q_method_type _q_method = &BasicGraphicsScene::nodeContextMenu; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
    }
}

const QMetaObject *QtNodes::BasicGraphicsScene::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtNodes::BasicGraphicsScene::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN7QtNodes18BasicGraphicsSceneE.stringdata0))
        return static_cast<void*>(this);
    return QGraphicsScene::qt_metacast(_clname);
}

int QtNodes::BasicGraphicsScene::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QGraphicsScene::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    return _id;
}

// SIGNAL 0
void QtNodes::BasicGraphicsScene::modified(BasicGraphicsScene * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void QtNodes::BasicGraphicsScene::nodeMoved(NodeId const _t1, QPointF const & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void QtNodes::BasicGraphicsScene::nodeClicked(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void QtNodes::BasicGraphicsScene::nodeSelected(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void QtNodes::BasicGraphicsScene::nodeDoubleClicked(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void QtNodes::BasicGraphicsScene::nodeHovered(NodeId const _t1, QPoint const _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void QtNodes::BasicGraphicsScene::nodeHoverLeft(NodeId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void QtNodes::BasicGraphicsScene::connectionHovered(ConnectionId const _t1, QPoint const _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void QtNodes::BasicGraphicsScene::connectionHoverLeft(ConnectionId const _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void QtNodes::BasicGraphicsScene::nodeContextMenu(NodeId const _t1, QPointF const _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}
QT_WARNING_POP
