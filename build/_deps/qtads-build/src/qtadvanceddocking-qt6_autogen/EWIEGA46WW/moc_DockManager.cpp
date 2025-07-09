/****************************************************************************
** Meta object code from reading C++ file 'DockManager.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockManager.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockManager.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads12CDockManagerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN3ads12CDockManagerE = QtMocHelpers::stringData(
    "ads::CDockManager",
    "perspectiveListChanged",
    "",
    "perspectiveListLoaded",
    "perspectivesRemoved",
    "restoringState",
    "stateRestored",
    "openingPerspective",
    "PerspectiveName",
    "perspectiveOpened",
    "floatingWidgetCreated",
    "ads::CFloatingDockContainer*",
    "FloatingWidget",
    "dockAreaCreated",
    "ads::CDockAreaWidget*",
    "DockArea",
    "dockWidgetAdded",
    "ads::CDockWidget*",
    "DockWidget",
    "dockWidgetAboutToBeRemoved",
    "dockWidgetRemoved",
    "focusedDockWidgetChanged",
    "old",
    "now",
    "endLeavingMinimizedState",
    "openPerspective",
    "setDockWidgetFocused",
    "CDockWidget*",
    "hideManagerAndFloatingWidgets"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN3ads12CDockManagerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      17,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      13,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  116,    2, 0x06,    1 /* Public */,
       3,    0,  117,    2, 0x06,    2 /* Public */,
       4,    0,  118,    2, 0x06,    3 /* Public */,
       5,    0,  119,    2, 0x06,    4 /* Public */,
       6,    0,  120,    2, 0x06,    5 /* Public */,
       7,    1,  121,    2, 0x06,    6 /* Public */,
       9,    1,  124,    2, 0x06,    8 /* Public */,
      10,    1,  127,    2, 0x06,   10 /* Public */,
      13,    1,  130,    2, 0x06,   12 /* Public */,
      16,    1,  133,    2, 0x06,   14 /* Public */,
      19,    1,  136,    2, 0x06,   16 /* Public */,
      20,    1,  139,    2, 0x06,   18 /* Public */,
      21,    2,  142,    2, 0x06,   20 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      24,    0,  147,    2, 0x0a,   23 /* Public */,
      25,    1,  148,    2, 0x0a,   24 /* Public */,
      26,    1,  151,    2, 0x0a,   26 /* Public */,
      28,    0,  154,    2, 0x0a,   28 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void, 0x80000000 | 17,   18,
    QMetaType::Void, 0x80000000 | 17,   18,
    QMetaType::Void, 0x80000000 | 17,   18,
    QMetaType::Void, 0x80000000 | 17, 0x80000000 | 17,   22,   23,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, 0x80000000 | 27,   18,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ads::CDockManager::staticMetaObject = { {
    QMetaObject::SuperData::link<CDockContainerWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN3ads12CDockManagerE.offsetsAndSizes,
    qt_meta_data_ZN3ads12CDockManagerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN3ads12CDockManagerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CDockManager, std::true_type>,
        // method 'perspectiveListChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'perspectiveListLoaded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'perspectivesRemoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'restoringState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stateRestored'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openingPerspective'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'perspectiveOpened'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'floatingWidgetCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CFloatingDockContainer *, std::false_type>,
        // method 'dockAreaCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockAreaWidget *, std::false_type>,
        // method 'dockWidgetAdded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'dockWidgetAboutToBeRemoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'dockWidgetRemoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'focusedDockWidgetChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'endLeavingMinimizedState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openPerspective'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'setDockWidgetFocused'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<CDockWidget *, std::false_type>,
        // method 'hideManagerAndFloatingWidgets'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ads::CDockManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->perspectiveListChanged(); break;
        case 1: _t->perspectiveListLoaded(); break;
        case 2: _t->perspectivesRemoved(); break;
        case 3: _t->restoringState(); break;
        case 4: _t->stateRestored(); break;
        case 5: _t->openingPerspective((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->perspectiveOpened((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->floatingWidgetCreated((*reinterpret_cast< std::add_pointer_t<ads::CFloatingDockContainer*>>(_a[1]))); break;
        case 8: _t->dockAreaCreated((*reinterpret_cast< std::add_pointer_t<ads::CDockAreaWidget*>>(_a[1]))); break;
        case 9: _t->dockWidgetAdded((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1]))); break;
        case 10: _t->dockWidgetAboutToBeRemoved((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1]))); break;
        case 11: _t->dockWidgetRemoved((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1]))); break;
        case 12: _t->focusedDockWidgetChanged((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[2]))); break;
        case 13: _t->endLeavingMinimizedState(); break;
        case 14: _t->openPerspective((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: _t->setDockWidgetFocused((*reinterpret_cast< std::add_pointer_t<CDockWidget*>>(_a[1]))); break;
        case 16: _t->hideManagerAndFloatingWidgets(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CFloatingDockContainer* >(); break;
            }
            break;
        case 9:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        case 10:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        case 12:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        case 15:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CDockWidget* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (CDockManager::*)();
            if (_q_method_type _q_method = &CDockManager::perspectiveListChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)();
            if (_q_method_type _q_method = &CDockManager::perspectiveListLoaded; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)();
            if (_q_method_type _q_method = &CDockManager::perspectivesRemoved; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)();
            if (_q_method_type _q_method = &CDockManager::restoringState; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)();
            if (_q_method_type _q_method = &CDockManager::stateRestored; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(const QString & );
            if (_q_method_type _q_method = &CDockManager::openingPerspective; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(const QString & );
            if (_q_method_type _q_method = &CDockManager::perspectiveOpened; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CFloatingDockContainer * );
            if (_q_method_type _q_method = &CDockManager::floatingWidgetCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CDockAreaWidget * );
            if (_q_method_type _q_method = &CDockManager::dockAreaCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CDockWidget * );
            if (_q_method_type _q_method = &CDockManager::dockWidgetAdded; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CDockWidget * );
            if (_q_method_type _q_method = &CDockManager::dockWidgetAboutToBeRemoved; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CDockWidget * );
            if (_q_method_type _q_method = &CDockManager::dockWidgetRemoved; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _q_method_type = void (CDockManager::*)(ads::CDockWidget * , ads::CDockWidget * );
            if (_q_method_type _q_method = &CDockManager::focusedDockWidgetChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
    }
}

const QMetaObject *ads::CDockManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN3ads12CDockManagerE.stringdata0))
        return static_cast<void*>(this);
    return CDockContainerWidget::qt_metacast(_clname);
}

int ads::CDockManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = CDockContainerWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockManager::perspectiveListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ads::CDockManager::perspectiveListLoaded()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ads::CDockManager::perspectivesRemoved()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ads::CDockManager::restoringState()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ads::CDockManager::stateRestored()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void ads::CDockManager::openingPerspective(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void ads::CDockManager::perspectiveOpened(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void ads::CDockManager::floatingWidgetCreated(ads::CFloatingDockContainer * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void ads::CDockManager::dockAreaCreated(ads::CDockAreaWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void ads::CDockManager::dockWidgetAdded(ads::CDockWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void ads::CDockManager::dockWidgetAboutToBeRemoved(ads::CDockWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void ads::CDockManager::dockWidgetRemoved(ads::CDockWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void ads::CDockManager::focusedDockWidgetChanged(ads::CDockWidget * _t1, ads::CDockWidget * _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}
QT_WARNING_POP
