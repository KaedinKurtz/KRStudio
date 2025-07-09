/****************************************************************************
** Meta object code from reading C++ file 'DockWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads11CDockWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockWidget::qt_create_metaobjectdata<qt_meta_tag_ZN3ads11CDockWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockWidget",
        "viewToggled",
        "",
        "Open",
        "closed",
        "titleChanged",
        "Title",
        "topLevelChanged",
        "topLevel",
        "closeRequested",
        "visibilityChanged",
        "visible",
        "featuresChanged",
        "ads::CDockWidget::DockWidgetFeatures",
        "features",
        "setToolbarFloatingStyle",
        "toggleView",
        "setAsCurrentTab",
        "raise",
        "setFloating",
        "deleteDockWidget",
        "closeDockWidget",
        "requestCloseDockWidget",
        "showFullScreen",
        "showNormal",
        "setAutoHide",
        "Enable",
        "SideBarLocation",
        "Location",
        "TabIndex",
        "toggleAutoHide"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'viewToggled'
        QtMocHelpers::SignalData<void(bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Signal 'closed'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'titleChanged'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'topLevelChanged'
        QtMocHelpers::SignalData<void(bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Signal 'closeRequested'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'visibilityChanged'
        QtMocHelpers::SignalData<void(bool)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 11 },
        }}),
        // Signal 'featuresChanged'
        QtMocHelpers::SignalData<void(ads::CDockWidget::DockWidgetFeatures)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Slot 'setToolbarFloatingStyle'
        QtMocHelpers::SlotData<void(bool)>(15, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Slot 'toggleView'
        QtMocHelpers::SlotData<void(bool)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Slot 'toggleView'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
        // Slot 'setAsCurrentTab'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'raise'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setFloating'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'deleteDockWidget'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'closeDockWidget'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestCloseDockWidget'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'showFullScreen'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'showNormal'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool, SideBarLocation, int)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 26 }, { 0x80000000 | 27, 28 }, { QMetaType::Int, 29 },
        }}),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool, SideBarLocation)>(25, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Bool, 26 }, { 0x80000000 | 27, 28 },
        }}),
        // Slot 'setAutoHide'
        QtMocHelpers::SlotData<void(bool)>(25, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Bool, 26 },
        }}),
        // Slot 'toggleAutoHide'
        QtMocHelpers::SlotData<void(SideBarLocation)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 27, 28 },
        }}),
        // Slot 'toggleAutoHide'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockWidget, qt_meta_tag_ZN3ads11CDockWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads11CDockWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads11CDockWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads11CDockWidgetE_t>.metaTypes,
    nullptr
} };

void ads::CDockWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->viewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->closed(); break;
        case 2: _t->titleChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->topLevelChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->closeRequested(); break;
        case 5: _t->visibilityChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->featuresChanged((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget::DockWidgetFeatures>>(_a[1]))); break;
        case 7: _t->setToolbarFloatingStyle((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->toggleView((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->toggleView(); break;
        case 10: _t->setAsCurrentTab(); break;
        case 11: _t->raise(); break;
        case 12: _t->setFloating(); break;
        case 13: _t->deleteDockWidget(); break;
        case 14: _t->closeDockWidget(); break;
        case 15: _t->requestCloseDockWidget(); break;
        case 16: _t->showFullScreen(); break;
        case 17: _t->showNormal(); break;
        case 18: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 19: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2]))); break;
        case 20: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 21: _t->toggleAutoHide((*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[1]))); break;
        case 22: _t->toggleAutoHide(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)(bool )>(_a, &CDockWidget::viewToggled, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)()>(_a, &CDockWidget::closed, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)(const QString & )>(_a, &CDockWidget::titleChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)(bool )>(_a, &CDockWidget::topLevelChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)()>(_a, &CDockWidget::closeRequested, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)(bool )>(_a, &CDockWidget::visibilityChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidget::*)(ads::CDockWidget::DockWidgetFeatures )>(_a, &CDockWidget::featuresChanged, 6))
            return;
    }
}

const QMetaObject *ads::CDockWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads11CDockWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 23)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 23;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 23)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 23;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockWidget::viewToggled(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ads::CDockWidget::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ads::CDockWidget::titleChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void ads::CDockWidget::topLevelChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void ads::CDockWidget::closeRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void ads::CDockWidget::visibilityChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void ads::CDockWidget::featuresChanged(ads::CDockWidget::DockWidgetFeatures _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}
QT_WARNING_POP
