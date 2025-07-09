/****************************************************************************
** Meta object code from reading C++ file 'AutoHideSideBar.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/AutoHideSideBar.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AutoHideSideBar.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads16CAutoHideSideBarE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CAutoHideSideBar::qt_create_metaobjectdata<qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CAutoHideSideBar",
        "sideBarLocation",
        "orientation",
        "Qt::Orientation",
        "spacing"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
        // property 'sideBarLocation'
        QtMocHelpers::PropertyData<int>(1, QMetaType::Int, QMC::DefaultPropertyFlags),
        // property 'orientation'
        QtMocHelpers::PropertyData<Qt::Orientation>(2, 0x80000000 | 3, QMC::DefaultPropertyFlags | QMC::EnumOrFlag),
        // property 'spacing'
        QtMocHelpers::PropertyData<int>(4, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CAutoHideSideBar, qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CAutoHideSideBar::staticMetaObject = { {
    QMetaObject::SuperData::link<QScrollArea::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>.metaTypes,
    nullptr
} };

void ads::CAutoHideSideBar::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CAutoHideSideBar *>(_o);
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<int*>(_v) = _t->sideBarLocation(); break;
        case 1: *reinterpret_cast<Qt::Orientation*>(_v) = _t->orientation(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->spacing(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setSpacing(*reinterpret_cast<int*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *ads::CAutoHideSideBar::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CAutoHideSideBar::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads16CAutoHideSideBarE_t>.strings))
        return static_cast<void*>(this);
    return QScrollArea::qt_metacast(_clname);
}

int ads::CAutoHideSideBar::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QScrollArea::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}
QT_WARNING_POP
