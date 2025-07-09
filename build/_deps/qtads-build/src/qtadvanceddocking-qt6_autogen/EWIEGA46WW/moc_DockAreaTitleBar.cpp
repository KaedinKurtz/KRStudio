/****************************************************************************
** Meta object code from reading C++ file 'DockAreaTitleBar.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockAreaTitleBar.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockAreaTitleBar.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads15CTitleBarButtonE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CTitleBarButton::qt_create_metaobjectdata<qt_meta_tag_ZN3ads15CTitleBarButtonE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CTitleBarButton"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CTitleBarButton, qt_meta_tag_ZN3ads15CTitleBarButtonE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CTitleBarButton::staticMetaObject = { {
    QMetaObject::SuperData::link<tTitleBarButton::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CTitleBarButtonE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CTitleBarButtonE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads15CTitleBarButtonE_t>.metaTypes,
    nullptr
} };

void ads::CTitleBarButton::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CTitleBarButton *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ads::CTitleBarButton::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CTitleBarButton::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads15CTitleBarButtonE_t>.strings))
        return static_cast<void*>(this);
    return tTitleBarButton::qt_metacast(_clname);
}

int ads::CTitleBarButton::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = tTitleBarButton::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockAreaTitleBar::qt_create_metaobjectdata<qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockAreaTitleBar",
        "tabBarClicked",
        "",
        "index",
        "onTabsMenuAboutToShow",
        "onCloseButtonClicked",
        "onAutoHideCloseActionTriggered",
        "minimizeAutoHideContainer",
        "onUndockButtonClicked",
        "onTabsMenuActionTriggered",
        "QAction*",
        "Action",
        "onCurrentTabChanged",
        "Index",
        "onAutoHideButtonClicked",
        "onAutoHideDockAreaActionClicked",
        "onAutoHideToActionClicked",
        "markTabsMenuOutdated"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'tabBarClicked'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'onTabsMenuAboutToShow'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCloseButtonClicked'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAutoHideCloseActionTriggered'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'minimizeAutoHideContainer'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onUndockButtonClicked'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabsMenuActionTriggered'
        QtMocHelpers::SlotData<void(QAction *)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
        // Slot 'onCurrentTabChanged'
        QtMocHelpers::SlotData<void(int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 13 },
        }}),
        // Slot 'onAutoHideButtonClicked'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAutoHideDockAreaActionClicked'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAutoHideToActionClicked'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'markTabsMenuOutdated'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockAreaTitleBar, qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockAreaTitleBar::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>.metaTypes,
    nullptr
} };

void ads::CDockAreaTitleBar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockAreaTitleBar *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->tabBarClicked((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->onTabsMenuAboutToShow(); break;
        case 2: _t->onCloseButtonClicked(); break;
        case 3: _t->onAutoHideCloseActionTriggered(); break;
        case 4: _t->minimizeAutoHideContainer(); break;
        case 5: _t->onUndockButtonClicked(); break;
        case 6: _t->onTabsMenuActionTriggered((*reinterpret_cast< std::add_pointer_t<QAction*>>(_a[1]))); break;
        case 7: _t->onCurrentTabChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->onAutoHideButtonClicked(); break;
        case 9: _t->onAutoHideDockAreaActionClicked(); break;
        case 10: _t->onAutoHideToActionClicked(); break;
        case 11: _t->markTabsMenuOutdated(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QAction* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockAreaTitleBar::*)(int )>(_a, &CDockAreaTitleBar::tabBarClicked, 0))
            return;
    }
}

const QMetaObject *ads::CDockAreaTitleBar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockAreaTitleBar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockAreaTitleBarE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockAreaTitleBar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockAreaTitleBar::tabBarClicked(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
QT_WARNING_POP
