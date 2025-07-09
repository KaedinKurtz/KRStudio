/****************************************************************************
** Meta object code from reading C++ file 'DockOverlay.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockOverlay.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockOverlay.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads12CDockOverlayE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockOverlay::qt_create_metaobjectdata<qt_meta_tag_ZN3ads12CDockOverlayE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockOverlay"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockOverlay, qt_meta_tag_ZN3ads12CDockOverlayE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockOverlay::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockOverlayE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockOverlayE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads12CDockOverlayE_t>.metaTypes,
    nullptr
} };

void ads::CDockOverlay::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockOverlay *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *ads::CDockOverlay::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockOverlay::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads12CDockOverlayE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockOverlay::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN3ads17CDockOverlayCrossE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::CDockOverlayCross::qt_create_metaobjectdata<qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads::CDockOverlayCross",
        "iconColors",
        "iconFrameColor",
        "iconBackgroundColor",
        "iconOverlayColor",
        "iconArrowColor",
        "iconShadowColor"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
        // property 'iconColors'
        QtMocHelpers::PropertyData<QString>(1, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
        // property 'iconFrameColor'
        QtMocHelpers::PropertyData<QColor>(2, QMetaType::QColor, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
        // property 'iconBackgroundColor'
        QtMocHelpers::PropertyData<QColor>(3, QMetaType::QColor, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
        // property 'iconOverlayColor'
        QtMocHelpers::PropertyData<QColor>(4, QMetaType::QColor, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
        // property 'iconArrowColor'
        QtMocHelpers::PropertyData<QColor>(5, QMetaType::QColor, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
        // property 'iconShadowColor'
        QtMocHelpers::PropertyData<QColor>(6, QMetaType::QColor, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CDockOverlayCross, qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ads::CDockOverlayCross::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>.metaTypes,
    nullptr
} };

void ads::CDockOverlayCross::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockOverlayCross *>(_o);
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->iconColors(); break;
        case 1: *reinterpret_cast<QColor*>(_v) = _t->iconColor(); break;
        case 2: *reinterpret_cast<QColor*>(_v) = _t->iconColor(); break;
        case 3: *reinterpret_cast<QColor*>(_v) = _t->iconColor(); break;
        case 4: *reinterpret_cast<QColor*>(_v) = _t->iconColor(); break;
        case 5: *reinterpret_cast<QColor*>(_v) = _t->iconColor(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setIconColors(*reinterpret_cast<QString*>(_v)); break;
        case 1: _t->setIconFrameColor(*reinterpret_cast<QColor*>(_v)); break;
        case 2: _t->setIconBackgroundColor(*reinterpret_cast<QColor*>(_v)); break;
        case 3: _t->setIconOverlayColor(*reinterpret_cast<QColor*>(_v)); break;
        case 4: _t->setIconArrowColor(*reinterpret_cast<QColor*>(_v)); break;
        case 5: _t->setIconShadowColor(*reinterpret_cast<QColor*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *ads::CDockOverlayCross::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockOverlayCross::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3ads17CDockOverlayCrossE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ads::CDockOverlayCross::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
