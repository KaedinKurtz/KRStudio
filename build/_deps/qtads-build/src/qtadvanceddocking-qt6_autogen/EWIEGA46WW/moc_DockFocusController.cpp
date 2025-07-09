/****************************************************************************
** Meta object code from reading C++ file 'DockFocusController.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockFocusController.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockFocusController.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads20CDockFocusControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockFocusController::qt_create_metaobjectdata<qt_meta_tag_ZN3ads20CDockFocusControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockFocusController",
        "onApplicationFocusChanged",
        "",
        "QWidget*",
        "old",
        "now",
        "onFocusWindowChanged",
        "QWindow*",
        "focusWindow",
        "onFocusedDockAreaViewToggled",
        "Open",
        "onStateRestored",
        "onDockWidgetVisibilityChanged",
        "Visible",
        "setDockWidgetFocused",
        "CDockWidget*",
        "focusedNow"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onApplicationFocusChanged'
        QtMocHelpers::SlotData<void(QWidget *, QWidget *)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { 0x80000000 | 3, 5 },
        }}),
        // Slot 'onFocusWindowChanged'
        QtMocHelpers::SlotData<void(QWindow *)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 7, 8 },
        }}),
        // Slot 'onFocusedDockAreaViewToggled'
        QtMocHelpers::SlotData<void(bool)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Slot 'onStateRestored'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDockWidgetVisibilityChanged'
        QtMocHelpers::SlotData<void(bool)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 13 },
        }}),
        // Slot 'setDockWidgetFocused'
        QtMocHelpers::SlotData<void(CDockWidget *)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 16 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockFocusController, qt_meta_tag_ZN3ads20CDockFocusControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockFocusController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockFocusControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockFocusControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads20CDockFocusControllerE_t>.metaTypes,
    nullptr
} };

void ads::CDockFocusController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockFocusController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onApplicationFocusChanged((*reinterpret_cast< std::add_pointer_t<QWidget*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QWidget*>>(_a[2]))); break;
        case 1: _t->onFocusWindowChanged((*reinterpret_cast< std::add_pointer_t<QWindow*>>(_a[1]))); break;
        case 2: _t->onFocusedDockAreaViewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->onStateRestored(); break;
        case 4: _t->onDockWidgetVisibilityChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->setDockWidgetFocused((*reinterpret_cast< std::add_pointer_t<CDockWidget*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QWidget* >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< CDockWidget* >(); break;
            }
            break;
        }
    }
}

const QMetaObject *ads::CDockFocusController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockFocusController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockFocusControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ads::CDockFocusController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
