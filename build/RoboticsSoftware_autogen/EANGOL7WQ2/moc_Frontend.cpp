/****************************************************************************
** Meta object code from reading C++ file 'Frontend.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/SLAMHeaders/Frontend.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Frontend.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8FrontendE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN8FrontendE = QtMocHelpers::stringData(
    "Frontend",
    "keyframeCreated",
    "",
    "KeyFrame::Ptr",
    "keyframe",
    "poseUpdatedForRender",
    "glm::mat4",
    "pose",
    "std::shared_ptr<rs2::points>",
    "points",
    "std::shared_ptr<rs2::video_frame>",
    "colorFrame",
    "processNewFrame",
    "timestamp"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN8FrontendE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   32,    2, 0x06,    1 /* Public */,
       5,    3,   35,    2, 0x06,    3 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      12,    3,   42,    2, 0x0a,    7 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 6, 0x80000000 | 8, 0x80000000 | 10,    7,    9,   11,

 // slots: parameters
    QMetaType::Void, QMetaType::Double, 0x80000000 | 8, 0x80000000 | 10,   13,    9,   11,

       0        // eod
};

Q_CONSTINIT const QMetaObject Frontend::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN8FrontendE.offsetsAndSizes,
    qt_meta_data_ZN8FrontendE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN8FrontendE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<Frontend, std::true_type>,
        // method 'keyframeCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<KeyFrame::Ptr, std::false_type>,
        // method 'poseUpdatedForRender'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const glm::mat4 &, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::points>, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::video_frame>, std::false_type>,
        // method 'processNewFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::points>, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<rs2::video_frame>, std::false_type>
    >,
    nullptr
} };

void Frontend::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Frontend *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->keyframeCreated((*reinterpret_cast< std::add_pointer_t<KeyFrame::Ptr>>(_a[1]))); break;
        case 1: _t->poseUpdatedForRender((*reinterpret_cast< std::add_pointer_t<glm::mat4>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::points>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::video_frame>>>(_a[3]))); break;
        case 2: _t->processNewFrame((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::points>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<std::shared_ptr<rs2::video_frame>>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (Frontend::*)(KeyFrame::Ptr );
            if (_q_method_type _q_method = &Frontend::keyframeCreated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (Frontend::*)(const glm::mat4 & , std::shared_ptr<rs2::points> , std::shared_ptr<rs2::video_frame> );
            if (_q_method_type _q_method = &Frontend::poseUpdatedForRender; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *Frontend::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Frontend::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN8FrontendE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Frontend::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void Frontend::keyframeCreated(KeyFrame::Ptr _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Frontend::poseUpdatedForRender(const glm::mat4 & _t1, std::shared_ptr<rs2::points> _t2, std::shared_ptr<rs2::video_frame> _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
