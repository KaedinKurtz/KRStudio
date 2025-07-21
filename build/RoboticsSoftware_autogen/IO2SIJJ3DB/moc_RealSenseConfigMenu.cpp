/****************************************************************************
** Meta object code from reading C++ file 'RealSenseConfigMenu.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UIHeaders/RealSenseConfigMenu.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'RealSenseConfigMenu.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN19RealSenseConfigMenuE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN19RealSenseConfigMenuE = QtMocHelpers::stringData(
    "RealSenseConfigMenu",
    "pointCloudReady",
    "",
    "rs2::points",
    "points",
    "rs2::video_frame",
    "colorFrame",
    "startStreamingRequested",
    "std::string",
    "serial",
    "std::vector<StreamProfile>",
    "profiles",
    "onRefreshDevicesClicked",
    "onDeviceSelectionChanged",
    "index",
    "onStartStreamingClicked",
    "onStopStreamingClicked",
    "updatePreview"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN19RealSenseConfigMenuE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,   56,    2, 0x06,    1 /* Public */,
       7,    2,   61,    2, 0x06,    4 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      12,    0,   66,    2, 0x0a,    7 /* Public */,
      13,    1,   67,    2, 0x0a,    8 /* Public */,
      15,    0,   70,    2, 0x0a,   10 /* Public */,
      16,    0,   71,    2, 0x0a,   11 /* Public */,
      17,    0,   72,    2, 0x0a,   12 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5,    4,    6,
    QMetaType::Void, 0x80000000 | 8, 0x80000000 | 10,    9,   11,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   14,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject RealSenseConfigMenu::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN19RealSenseConfigMenuE.offsetsAndSizes,
    qt_meta_data_ZN19RealSenseConfigMenuE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN19RealSenseConfigMenuE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<RealSenseConfigMenu, std::true_type>,
        // method 'pointCloudReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const rs2::points &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const rs2::video_frame &, std::false_type>,
        // method 'startStreamingRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::string &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::vector<StreamProfile> &, std::false_type>,
        // method 'onRefreshDevicesClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDeviceSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onStartStreamingClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStopStreamingClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updatePreview'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void RealSenseConfigMenu::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RealSenseConfigMenu *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->pointCloudReady((*reinterpret_cast< std::add_pointer_t<rs2::points>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<rs2::video_frame>>(_a[2]))); break;
        case 1: _t->startStreamingRequested((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::vector<StreamProfile>>>(_a[2]))); break;
        case 2: _t->onRefreshDevicesClicked(); break;
        case 3: _t->onDeviceSelectionChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onStartStreamingClicked(); break;
        case 5: _t->onStopStreamingClicked(); break;
        case 6: _t->updatePreview(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (RealSenseConfigMenu::*)(const rs2::points & , const rs2::video_frame & );
            if (_q_method_type _q_method = &RealSenseConfigMenu::pointCloudReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (RealSenseConfigMenu::*)(const std::string & , const std::vector<StreamProfile> & );
            if (_q_method_type _q_method = &RealSenseConfigMenu::startStreamingRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *RealSenseConfigMenu::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RealSenseConfigMenu::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN19RealSenseConfigMenuE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int RealSenseConfigMenu::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void RealSenseConfigMenu::pointCloudReady(const rs2::points & _t1, const rs2::video_frame & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void RealSenseConfigMenu::startStreamingRequested(const std::string & _t1, const std::vector<StreamProfile> & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
