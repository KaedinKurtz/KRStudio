/****************************************************************************
** Meta object code from reading C++ file 'SlamManager.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/SLAMHeaders/SlamManager.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SlamManager.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
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
struct qt_meta_tag_ZN11SlamManagerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN11SlamManagerE = QtMocHelpers::stringData(
    "SlamManager",
    "newFrameData",
    "",
    "timestamp",
    "std::shared_ptr<rs2::points>",
    "points",
    "std::shared_ptr<rs2::video_frame>",
    "colorFrame",
    "mapUpdated",
    "onPointCloudReady",
    "rs2::points",
    "rs2::video_frame",
    "onPoseUpdatedForRender",
    "glm::mat4",
    "pose"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN11SlamManagerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    3,   38,    2, 0x06,    1 /* Public */,
       8,    0,   45,    2, 0x06,    5 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       9,    2,   46,    2, 0x0a,    6 /* Public */,
      12,    3,   51,    2, 0x0a,    9 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Double, 0x80000000 | 4, 0x80000000 | 6,    3,    5,    7,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 10, 0x80000000 | 11,    5,    7,
    QMetaType::Void, 0x80000000 | 13, 0x80000000 | 4, 0x80000000 | 6,   14,    5,    7,

       0        // eod
};

Q_CONSTINIT const QMetaObject SlamManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN11SlamManagerE.offsetsAndSizes,
    qt_meta_data_ZN11SlamManagerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN11SlamManagerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<SlamManager, std::true_type>,
        // method 'newFrameData'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::points>, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::video_frame>, std::false_type>,
        // method 'mapUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onPointCloudReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const rs2::points &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const rs2::video_frame &, std::false_type>,
        // method 'onPoseUpdatedForRender'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const glm::mat4 &, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::points>, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::video_frame>, std::false_type>
    >,
    nullptr
} };

void SlamManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SlamManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->newFrameData((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::points>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::video_frame>>>(_a[3]))); break;
        case 1: _t->mapUpdated(); break;
        case 2: _t->onPointCloudReady((*reinterpret_cast< std::add_pointer_t<rs2::points>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<rs2::video_frame>>(_a[2]))); break;
        case 3: _t->onPoseUpdatedForRender((*reinterpret_cast< std::add_pointer_t<glm::mat4>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::points>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::video_frame>>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (SlamManager::*)(double , std::shared_ptr<rs2::points> , std::shared_ptr<rs2::video_frame> );
            if (_q_method_type _q_method = &SlamManager::newFrameData; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (SlamManager::*)();
            if (_q_method_type _q_method = &SlamManager::mapUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *SlamManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SlamManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN11SlamManagerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SlamManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
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
void SlamManager::newFrameData(double _t1, std::shared_ptr<rs2::points> _t2, std::shared_ptr<rs2::video_frame> _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void SlamManager::mapUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
