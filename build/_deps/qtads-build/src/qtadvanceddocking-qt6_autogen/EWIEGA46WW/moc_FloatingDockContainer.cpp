/****************************************************************************
** Meta object code from reading C++ file 'FloatingDockContainer.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/FloatingDockContainer.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'FloatingDockContainer.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads22CFloatingDockContainerE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CFloatingDockContainer::qt_create_metaobjectdata<qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CFloatingDockContainer",
        "onDockAreasAddedOrRemoved",
        "",
        "onDockAreaCurrentChanged",
        "Index"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onDockAreasAddedOrRemoved'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDockAreaCurrentChanged'
        QtMocHelpers::SlotData<void(int)>(3, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CFloatingDockContainer, qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CFloatingDockContainer::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>.metaTypes,
    nullptr
} };

void ads::CFloatingDockContainer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CFloatingDockContainer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onDockAreasAddedOrRemoved(); break;
        case 1: _t->onDockAreaCurrentChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *ads::CFloatingDockContainer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CFloatingDockContainer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads22CFloatingDockContainerE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IFloatingWidget"))
        return static_cast< IFloatingWidget*>(this);
    return QWidget::qt_metacast(_clname);
}

int ads::CFloatingDockContainer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}
QT_WARNING_POP
