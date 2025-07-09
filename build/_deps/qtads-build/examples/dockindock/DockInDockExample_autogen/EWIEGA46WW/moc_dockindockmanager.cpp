/****************************************************************************
** Meta object code from reading C++ file 'dockindockmanager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../qtads-src/examples/dockindock/dockindockmanager.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'dockindockmanager.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto QtAdsUtl::DockInDockManager::qt_create_metaobjectdata<qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QtAdsUtl::DockInDockManager"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DockInDockManager, qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QtAdsUtl::DockInDockManager::staticMetaObject = { {
    QMetaObject::SuperData::link<ads::CDockManager::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>.metaTypes,
    nullptr
} };

void QtAdsUtl::DockInDockManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DockInDockManager *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *QtAdsUtl::DockInDockManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtAdsUtl::DockInDockManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl17DockInDockManagerE_t>.strings))
        return static_cast<void*>(this);
    return ads::CDockManager::qt_metacast(_clname);
}

int QtAdsUtl::DockInDockManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ads::CDockManager::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t {};
} // unnamed namespace

template <> constexpr inline auto QtAdsUtl::CreateChildDockAction::qt_create_metaobjectdata<qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QtAdsUtl::CreateChildDockAction",
        "createGroup",
        ""
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'createGroup'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CreateChildDockAction, qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QtAdsUtl::CreateChildDockAction::staticMetaObject = { {
    QMetaObject::SuperData::link<QAction::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>.metaTypes,
    nullptr
} };

void QtAdsUtl::CreateChildDockAction::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CreateChildDockAction *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->createGroup(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *QtAdsUtl::CreateChildDockAction::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtAdsUtl::CreateChildDockAction::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl21CreateChildDockActionE_t>.strings))
        return static_cast<void*>(this);
    return QAction::qt_metacast(_clname);
}

int QtAdsUtl::CreateChildDockAction::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAction::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t {};
} // unnamed namespace

template <> constexpr inline auto QtAdsUtl::DestroyGroupAction::qt_create_metaobjectdata<qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QtAdsUtl::DestroyGroupAction",
        "destroyGroup",
        ""
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'destroyGroup'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DestroyGroupAction, qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QtAdsUtl::DestroyGroupAction::staticMetaObject = { {
    QMetaObject::SuperData::link<QAction::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>.metaTypes,
    nullptr
} };

void QtAdsUtl::DestroyGroupAction::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DestroyGroupAction *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->destroyGroup(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *QtAdsUtl::DestroyGroupAction::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtAdsUtl::DestroyGroupAction::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl18DestroyGroupActionE_t>.strings))
        return static_cast<void*>(this);
    return QAction::qt_metacast(_clname);
}

int QtAdsUtl::DestroyGroupAction::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAction::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t {};
} // unnamed namespace

template <> constexpr inline auto QtAdsUtl::MoveDockWidgetAction::qt_create_metaobjectdata<qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "QtAdsUtl::MoveDockWidgetAction",
        "move",
        ""
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'move'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MoveDockWidgetAction, qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject QtAdsUtl::MoveDockWidgetAction::staticMetaObject = { {
    QMetaObject::SuperData::link<QAction::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>.metaTypes,
    nullptr
} };

void QtAdsUtl::MoveDockWidgetAction::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MoveDockWidgetAction *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->move(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *QtAdsUtl::MoveDockWidgetAction::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *QtAdsUtl::MoveDockWidgetAction::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8QtAdsUtl20MoveDockWidgetActionE_t>.strings))
        return static_cast<void*>(this);
    return QAction::qt_metacast(_clname);
}

int QtAdsUtl::MoveDockWidgetAction::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAction::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
QT_WARNING_POP
