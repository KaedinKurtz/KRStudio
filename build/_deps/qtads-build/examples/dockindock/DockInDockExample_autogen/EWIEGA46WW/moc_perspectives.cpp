/****************************************************************************
** Meta object code from reading C++ file 'perspectives.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../qtads-src/examples/dockindock/perspectives.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'perspectives.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto QtAdsUtl::PerspectivesManager::qt_create_metaobjectdata<qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QtAdsUtl::PerspectivesManager",
        "perspectivesListChanged",
        "",
        "openingPerspective",
        "openedPerspective"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'perspectivesListChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openingPerspective'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openedPerspective'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PerspectivesManager, qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QtAdsUtl::PerspectivesManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>.metaTypes,
    nullptr
} };

void QtAdsUtl::PerspectivesManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PerspectivesManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->perspectivesListChanged(); break;
        case 1: _t->openingPerspective(); break;
        case 2: _t->openedPerspective(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PerspectivesManager::*)()>(_a, &PerspectivesManager::perspectivesListChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PerspectivesManager::*)()>(_a, &PerspectivesManager::openingPerspective, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PerspectivesManager::*)()>(_a, &PerspectivesManager::openedPerspective, 2))
            return;
    }
}

const QMetaObject *QtAdsUtl::PerspectivesManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtAdsUtl::PerspectivesManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl19PerspectivesManagerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int QtAdsUtl::PerspectivesManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void QtAdsUtl::PerspectivesManager::perspectivesListChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void QtAdsUtl::PerspectivesManager::openingPerspective()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void QtAdsUtl::PerspectivesManager::openedPerspective()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
