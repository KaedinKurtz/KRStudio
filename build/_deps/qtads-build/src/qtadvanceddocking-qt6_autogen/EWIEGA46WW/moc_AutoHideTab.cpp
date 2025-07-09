/****************************************************************************
** Meta object code from reading C++ file 'AutoHideTab.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/AutoHideTab.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AutoHideTab.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads12CAutoHideTabE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CAutoHideTab::qt_create_metaobjectdata<qt_meta_tag_ZN3ads12CAutoHideTabE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CAutoHideTab",
        "onAutoHideToActionClicked",
        "",
        "onDragHoverDelayExpired",
        "setDockWidgetFloating",
        "unpinDockWidget",
        "requestCloseDockWidget",
        "sideBarLocation",
        "orientation",
        "Qt::Orientation",
        "activeTab",
        "iconOnly"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onAutoHideToActionClicked'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDragHoverDelayExpired'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'setDockWidgetFloating'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'unpinDockWidget'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestCloseDockWidget'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'sideBarLocation'
        QtMocHelpers::PropertyData<int>(7, QMetaType::Int, QMC::DefaultPropertyFlags),
        // property 'orientation'
        QtMocHelpers::PropertyData<Qt::Orientation>(8, 0x80000000 | 9, QMC::DefaultPropertyFlags | QMC::EnumOrFlag),
        // property 'activeTab'
        QtMocHelpers::PropertyData<bool>(10, QMetaType::Bool, QMC::DefaultPropertyFlags),
        // property 'iconOnly'
        QtMocHelpers::PropertyData<bool>(11, QMetaType::Bool, QMC::DefaultPropertyFlags),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CAutoHideTab, qt_meta_tag_ZN3ads12CAutoHideTabE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CAutoHideTab::staticMetaObject = { {
    QMetaObject::SuperData::link<CPushButton::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CAutoHideTabE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CAutoHideTabE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads12CAutoHideTabE_t>.metaTypes,
    nullptr
} };

void ads::CAutoHideTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CAutoHideTab *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onAutoHideToActionClicked(); break;
        case 1: _t->onDragHoverDelayExpired(); break;
        case 2: _t->setDockWidgetFloating(); break;
        case 3: _t->unpinDockWidget(); break;
        case 4: _t->requestCloseDockWidget(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<int*>(_v) = _t->sideBarLocation(); break;
        case 1: *reinterpret_cast<Qt::Orientation*>(_v) = _t->orientation(); break;
        case 2: *reinterpret_cast<bool*>(_v) = _t->isActiveTab(); break;
        case 3: *reinterpret_cast<bool*>(_v) = _t->iconOnly(); break;
        default: break;
        }
    }
}

const QMetaObject *ads::CAutoHideTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CAutoHideTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CAutoHideTabE_t>.strings))
        return static_cast<void*>(this);
    return CPushButton::qt_metacast(_clname);
}

int ads::CAutoHideTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = CPushButton::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}
QT_WARNING_POP
