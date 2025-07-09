/****************************************************************************
** Meta object code from reading C++ file 'DockManager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
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
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.0. It"
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

template <> constexpr inline auto ads::CDockManager::qt_create_metaobjectdata<qt_meta_tag_ZN3ads12CDockManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
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
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'perspectiveListChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'perspectiveListLoaded'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'perspectivesRemoved'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'restoringState'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'stateRestored'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openingPerspective'
        QtMocHelpers::SignalData<void(const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Signal 'perspectiveOpened'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Signal 'floatingWidgetCreated'
        QtMocHelpers::SignalData<void(ads::CFloatingDockContainer *)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 11, 12 },
        }}),
        // Signal 'dockAreaCreated'
        QtMocHelpers::SignalData<void(ads::CDockAreaWidget *)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 14, 15 },
        }}),
        // Signal 'dockWidgetAdded'
        QtMocHelpers::SignalData<void(ads::CDockWidget *)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Signal 'dockWidgetAboutToBeRemoved'
        QtMocHelpers::SignalData<void(ads::CDockWidget *)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Signal 'dockWidgetRemoved'
        QtMocHelpers::SignalData<void(ads::CDockWidget *)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Signal 'focusedDockWidgetChanged'
        QtMocHelpers::SignalData<void(ads::CDockWidget *, ads::CDockWidget *)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 22 }, { 0x80000000 | 17, 23 },
        }}),
        // Slot 'endLeavingMinimizedState'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'openPerspective'
        QtMocHelpers::SlotData<void(const QString &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Slot 'setDockWidgetFocused'
        QtMocHelpers::SlotData<void(CDockWidget *)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 27, 18 },
        }}),
        // Slot 'hideManagerAndFloatingWidgets'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockManager, qt_meta_tag_ZN3ads12CDockManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockManager::staticMetaObject = { {
    QMetaObject::SuperData::link<CDockContainerWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads12CDockManagerE_t>.metaTypes,
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
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)()>(_a, &CDockManager::perspectiveListChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)()>(_a, &CDockManager::perspectiveListLoaded, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)()>(_a, &CDockManager::perspectivesRemoved, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)()>(_a, &CDockManager::restoringState, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)()>(_a, &CDockManager::stateRestored, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(const QString & )>(_a, &CDockManager::openingPerspective, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(const QString & )>(_a, &CDockManager::perspectiveOpened, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CFloatingDockContainer * )>(_a, &CDockManager::floatingWidgetCreated, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CDockAreaWidget * )>(_a, &CDockManager::dockAreaCreated, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CDockWidget * )>(_a, &CDockManager::dockWidgetAdded, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CDockWidget * )>(_a, &CDockManager::dockWidgetAboutToBeRemoved, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CDockWidget * )>(_a, &CDockManager::dockWidgetRemoved, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockManager::*)(ads::CDockWidget * , ads::CDockWidget * )>(_a, &CDockManager::focusedDockWidgetChanged, 12))
            return;
    }
}

const QMetaObject *ads::CDockManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockManagerE_t>.strings))
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
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void ads::CDockManager::perspectiveOpened(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void ads::CDockManager::floatingWidgetCreated(ads::CFloatingDockContainer * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void ads::CDockManager::dockAreaCreated(ads::CDockAreaWidget * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void ads::CDockManager::dockWidgetAdded(ads::CDockWidget * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void ads::CDockManager::dockWidgetAboutToBeRemoved(ads::CDockWidget * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void ads::CDockManager::dockWidgetRemoved(ads::CDockWidget * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1);
}

// SIGNAL 12
void ads::CDockManager::focusedDockWidgetChanged(ads::CDockWidget * _t1, ads::CDockWidget * _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2);
}
QT_WARNING_POP
