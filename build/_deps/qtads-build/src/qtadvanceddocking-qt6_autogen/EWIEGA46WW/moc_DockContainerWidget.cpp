/****************************************************************************
** Meta object code from reading C++ file 'DockContainerWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockContainerWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockContainerWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads20CDockContainerWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockContainerWidget::qt_create_metaobjectdata<qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockContainerWidget",
        "dockAreasAdded",
        "",
        "autoHideWidgetCreated",
        "ads::CAutoHideDockContainer*",
        "AutoHideWidget",
        "dockAreasRemoved",
        "dockAreaViewToggled",
        "ads::CDockAreaWidget*",
        "DockArea",
        "Open"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'dockAreasAdded'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'autoHideWidgetCreated'
        QtMocHelpers::SignalData<void(ads::CAutoHideDockContainer *)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 4, 5 },
        }}),
        // Signal 'dockAreasRemoved'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dockAreaViewToggled'
        QtMocHelpers::SignalData<void(ads::CDockAreaWidget *, bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 }, { QMetaType::Bool, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockContainerWidget, qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockContainerWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>.metaTypes,
    nullptr
} };

void ads::CDockContainerWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockContainerWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->dockAreasAdded(); break;
        case 1: _t->autoHideWidgetCreated((*reinterpret_cast< std::add_pointer_t<ads::CAutoHideDockContainer*>>(_a[1]))); break;
        case 2: _t->dockAreasRemoved(); break;
        case 3: _t->dockAreaViewToggled((*reinterpret_cast< std::add_pointer_t<ads::CDockAreaWidget*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockContainerWidget::*)()>(_a, &CDockContainerWidget::dockAreasAdded, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockContainerWidget::*)(ads::CAutoHideDockContainer * )>(_a, &CDockContainerWidget::autoHideWidgetCreated, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockContainerWidget::*)()>(_a, &CDockContainerWidget::dockAreasRemoved, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockContainerWidget::*)(ads::CDockAreaWidget * , bool )>(_a, &CDockContainerWidget::dockAreaViewToggled, 3))
            return;
    }
}

const QMetaObject *ads::CDockContainerWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockContainerWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads20CDockContainerWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockContainerWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockContainerWidget::dockAreasAdded()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ads::CDockContainerWidget::autoHideWidgetCreated(ads::CAutoHideDockContainer * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void ads::CDockContainerWidget::dockAreasRemoved()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ads::CDockContainerWidget::dockAreaViewToggled(ads::CDockAreaWidget * _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}
QT_WARNING_POP
