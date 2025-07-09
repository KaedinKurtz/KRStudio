/****************************************************************************
** Meta object code from reading C++ file 'DockAreaWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockAreaWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockAreaWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads15CDockAreaWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockAreaWidget::qt_create_metaobjectdata<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockAreaWidget",
        "tabBarClicked",
        "",
        "index",
        "currentChanging",
        "currentChanged",
        "viewToggled",
        "Open",
        "onTabCloseRequested",
        "Index",
        "reorderDockWidget",
        "fromIndex",
        "toIndex",
        "updateAutoHideButtonCheckState",
        "updateTitleBarButtonsToolTips",
        "calculateSideTabBarArea",
        "SideBarLocation",
        "toggleView",
        "setCurrentIndex",
        "closeArea",
        "setAutoHide",
        "Enable",
        "Location",
        "TabIndex",
        "toggleAutoHide",
        "closeOtherAreas",
        "setFloating"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'tabBarClicked'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'currentChanging'
        QtMocHelpers::SignalData<void(int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'currentChanged'
        QtMocHelpers::SignalData<void(int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'viewToggled'
        QtMocHelpers::SignalData<void(bool)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Slot 'onTabCloseRequested'
        QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 9 },
        }}),
        // Slot 'reorderDockWidget'
        QtMocHelpers::SlotData<void(int, int)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Int, 12 },
        }}),
        // Slot 'updateAutoHideButtonCheckState'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'updateTitleBarButtonsToolTips'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'calculateSideTabBarArea'
        QtMocHelpers::SlotData<SideBarLocation() const>(15, 2, QMC::AccessPrivate, 0x80000000 | 16),
        // Slot 'toggleView'
        QtMocHelpers::SlotData<void(bool)>(17, 2, QMC::AccessProtected, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Slot 'setCurrentIndex'
        QtMocHelpers::SlotData<void(int)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'closeArea'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool, SideBarLocation, int)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 21 }, { 0x80000000 | 16, 22 }, { QMetaType::Int, 23 },
        }}),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool, SideBarLocation)>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Bool, 21 }, { 0x80000000 | 16, 22 },
        }}),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool)>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Bool, 21 },
        }}),
        // Slot 'toggleAutoHide'
        QtMocHelpers::SlotData<void(SideBarLocation)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 22 },
        }}),
        // Slot 'toggleAutoHide'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Slot 'closeOtherAreas'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setFloating'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockAreaWidget, qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockAreaWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>.metaTypes,
    nullptr
} };

void ads::CDockAreaWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockAreaWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->tabBarClicked((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->currentChanging((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->currentChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->viewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->onTabCloseRequested((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->reorderDockWidget((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 6: _t->updateAutoHideButtonCheckState(); break;
        case 7: _t->updateTitleBarButtonsToolTips(); break;
        case 8: { SideBarLocation _r = _t->calculateSideTabBarArea();
            if (_a[0]) *reinterpret_cast< SideBarLocation*>(_a[0]) = std::move(_r); }  break;
        case 9: _t->toggleView((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->setCurrentIndex((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->closeArea(); break;
        case 12: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 13: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2]))); break;
        case 14: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 15: _t->toggleAutoHide((*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[1]))); break;
        case 16: _t->toggleAutoHide(); break;
        case 17: _t->closeOtherAreas(); break;
        case 18: _t->setFloating(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockAreaWidget::*)(int )>(_a, &CDockAreaWidget::tabBarClicked, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaWidget::*)(int )>(_a, &CDockAreaWidget::currentChanging, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaWidget::*)(int )>(_a, &CDockAreaWidget::currentChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaWidget::*)(bool )>(_a, &CDockAreaWidget::viewToggled, 3))
            return;
    }
}

const QMetaObject *ads::CDockAreaWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockAreaWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockAreaWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 19)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 19;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 19)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 19;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockAreaWidget::tabBarClicked(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ads::CDockAreaWidget::currentChanging(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void ads::CDockAreaWidget::currentChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void ads::CDockAreaWidget::viewToggled(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
