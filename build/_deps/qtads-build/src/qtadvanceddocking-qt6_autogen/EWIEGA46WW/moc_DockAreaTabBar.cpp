/****************************************************************************
** Meta object code from reading C++ file 'DockAreaTabBar.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockAreaTabBar.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockAreaTabBar.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads15CDockAreaTabBarE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockAreaTabBar::qt_create_metaobjectdata<qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockAreaTabBar",
        "currentChanging",
        "",
        "Index",
        "currentChanged",
        "tabBarClicked",
        "index",
        "tabCloseRequested",
        "tabClosed",
        "tabOpened",
        "tabMoved",
        "from",
        "to",
        "removingTab",
        "tabInserted",
        "elidedChanged",
        "elided",
        "onTabClicked",
        "onTabCloseRequested",
        "onCloseOtherTabsRequested",
        "onTabWidgetMoved",
        "GlobalPos",
        "setCurrentIndex",
        "closeTab"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentChanging'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'currentChanged'
        QtMocHelpers::SignalData<void(int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'tabBarClicked'
        QtMocHelpers::SignalData<void(int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'tabCloseRequested'
        QtMocHelpers::SignalData<void(int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'tabClosed'
        QtMocHelpers::SignalData<void(int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'tabOpened'
        QtMocHelpers::SignalData<void(int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'tabMoved'
        QtMocHelpers::SignalData<void(int, int)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Int, 12 },
        }}),
        // Signal 'removingTab'
        QtMocHelpers::SignalData<void(int)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'tabInserted'
        QtMocHelpers::SignalData<void(int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Signal 'elidedChanged'
        QtMocHelpers::SignalData<void(bool)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 16 },
        }}),
        // Slot 'onTabClicked'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabCloseRequested'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCloseOtherTabsRequested'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabWidgetMoved'
        QtMocHelpers::SlotData<void(const QPoint &)>(20, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QPoint, 21 },
        }}),
        // Slot 'setCurrentIndex'
        QtMocHelpers::SlotData<void(int)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'closeTab'
        QtMocHelpers::SlotData<void(int)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockAreaTabBar, qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockAreaTabBar::staticMetaObject = { {
    QMetaObject::SuperData::link<QScrollArea::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>.metaTypes,
    nullptr
} };

void ads::CDockAreaTabBar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockAreaTabBar *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentChanging((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->currentChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->tabBarClicked((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->tabCloseRequested((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->tabClosed((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->tabOpened((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->tabMoved((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 7: _t->removingTab((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->tabInserted((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->elidedChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->onTabClicked(); break;
        case 11: _t->onTabCloseRequested(); break;
        case 12: _t->onCloseOtherTabsRequested(); break;
        case 13: _t->onTabWidgetMoved((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 14: _t->setCurrentIndex((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 15: _t->closeTab((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::currentChanging, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::currentChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::tabBarClicked, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::tabCloseRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::tabClosed, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::tabOpened, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int , int )>(_a, &CDockAreaTabBar::tabMoved, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::removingTab, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(int )>(_a, &CDockAreaTabBar::tabInserted, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTabBar::*)(bool )>(_a, &CDockAreaTabBar::elidedChanged, 9))
            return;
    }
}

const QMetaObject *ads::CDockAreaTabBar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockAreaTabBar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CDockAreaTabBarE_t>.strings))
        return static_cast<void*>(this);
    return QScrollArea::qt_metacast(_clname);
}

int ads::CDockAreaTabBar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QScrollArea::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 16)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 16;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockAreaTabBar::currentChanging(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ads::CDockAreaTabBar::currentChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void ads::CDockAreaTabBar::tabBarClicked(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void ads::CDockAreaTabBar::tabCloseRequested(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void ads::CDockAreaTabBar::tabClosed(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void ads::CDockAreaTabBar::tabOpened(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void ads::CDockAreaTabBar::tabMoved(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void ads::CDockAreaTabBar::removingTab(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void ads::CDockAreaTabBar::tabInserted(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void ads::CDockAreaTabBar::elidedChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}
QT_WARNING_POP
