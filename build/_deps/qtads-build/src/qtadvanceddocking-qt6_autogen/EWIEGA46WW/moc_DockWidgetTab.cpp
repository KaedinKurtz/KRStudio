/****************************************************************************
** Meta object code from reading C++ file 'DockWidgetTab.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockWidgetTab.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockWidgetTab.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads14CDockWidgetTabE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockWidgetTab::qt_create_metaobjectdata<qt_meta_tag_ZN3ads14CDockWidgetTabE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockWidgetTab",
        "activeTabChanged",
        "",
        "clicked",
        "closeRequested",
        "closeOtherTabsRequested",
        "moved",
        "GlobalPos",
        "elidedChanged",
        "elided",
        "detachDockWidget",
        "autoHideDockWidget",
        "onAutoHideToActionClicked",
        "setVisible",
        "visible",
        "activeTab",
        "iconSize"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'activeTabChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'clicked'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'closeRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'closeOtherTabsRequested'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'moved'
        QtMocHelpers::SignalData<void(const QPoint &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QPoint, 7 },
        }}),
        // Signal 'elidedChanged'
        QtMocHelpers::SignalData<void(bool)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 9 },
        }}),
        // Slot 'detachDockWidget'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'autoHideDockWidget'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAutoHideToActionClicked'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'setVisible'
        QtMocHelpers::SlotData<void(bool)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 14 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'activeTab'
        QtMocHelpers::PropertyData<bool>(15, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'iconSize'
        QtMocHelpers::PropertyData<QSize>(16, QMetaType::QSize, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockWidgetTab, qt_meta_tag_ZN3ads14CDockWidgetTabE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockWidgetTab::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads14CDockWidgetTabE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads14CDockWidgetTabE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads14CDockWidgetTabE_t>.metaTypes,
    nullptr
} };

void ads::CDockWidgetTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockWidgetTab *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->activeTabChanged(); break;
        case 1: _t->clicked(); break;
        case 2: _t->closeRequested(); break;
        case 3: _t->closeOtherTabsRequested(); break;
        case 4: _t->moved((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 5: _t->elidedChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->detachDockWidget(); break;
        case 7: _t->autoHideDockWidget(); break;
        case 8: _t->onAutoHideToActionClicked(); break;
        case 9: _t->setVisible((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)()>(_a, &CDockWidgetTab::activeTabChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)()>(_a, &CDockWidgetTab::clicked, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)()>(_a, &CDockWidgetTab::closeRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)()>(_a, &CDockWidgetTab::closeOtherTabsRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)(const QPoint & )>(_a, &CDockWidgetTab::moved, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (CDockWidgetTab::*)(bool )>(_a, &CDockWidgetTab::elidedChanged, 5))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->isActiveTab(); break;
        case 1: *reinterpret_cast<QSize*>(_v) = _t->iconSize(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setActiveTab(*reinterpret_cast<bool*>(_v)); break;
        case 1: _t->setIconSize(*reinterpret_cast<QSize*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *ads::CDockWidgetTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockWidgetTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads14CDockWidgetTabE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockWidgetTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockWidgetTab::activeTabChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ads::CDockWidgetTab::clicked()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ads::CDockWidgetTab::closeRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ads::CDockWidgetTab::closeOtherTabsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ads::CDockWidgetTab::moved(const QPoint & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void ads::CDockWidgetTab::elidedChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
