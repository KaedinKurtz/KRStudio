/****************************************************************************
** Meta object code from reading C++ file 'DockWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads11CDockWidgetE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN3ads11CDockWidgetE = QtMocHelpers::stringData(
    "ads::CDockWidget",
    "viewToggled",
    "",
    "Open",
    "closed",
    "titleChanged",
    "Title",
    "topLevelChanged",
    "topLevel",
    "closeRequested",
    "visibilityChanged",
    "visible",
    "featuresChanged",
    "ads::CDockWidget::DockWidgetFeatures",
    "features",
    "setToolbarFloatingStyle",
    "toggleView",
    "setAsCurrentTab",
    "raise",
    "setFloating",
    "deleteDockWidget",
    "closeDockWidget",
    "requestCloseDockWidget",
    "showFullScreen",
    "showNormal",
    "setAutoHide",
    "Enable",
    "SideBarLocation",
    "Location",
    "TabIndex",
    "toggleAutoHide"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN3ads11CDockWidgetE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      23,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  152,    2, 0x06,    1 /* Public */,
       4,    0,  155,    2, 0x06,    3 /* Public */,
       5,    1,  156,    2, 0x06,    4 /* Public */,
       7,    1,  159,    2, 0x06,    6 /* Public */,
       9,    0,  162,    2, 0x06,    8 /* Public */,
      10,    1,  163,    2, 0x06,    9 /* Public */,
      12,    1,  166,    2, 0x06,   11 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      15,    1,  169,    2, 0x08,   13 /* Private */,
      16,    1,  172,    2, 0x0a,   15 /* Public */,
      16,    0,  175,    2, 0x2a,   17 /* Public | MethodCloned */,
      17,    0,  176,    2, 0x0a,   18 /* Public */,
      18,    0,  177,    2, 0x0a,   19 /* Public */,
      19,    0,  178,    2, 0x0a,   20 /* Public */,
      20,    0,  179,    2, 0x0a,   21 /* Public */,
      21,    0,  180,    2, 0x0a,   22 /* Public */,
      22,    0,  181,    2, 0x0a,   23 /* Public */,
      23,    0,  182,    2, 0x0a,   24 /* Public */,
      24,    0,  183,    2, 0x0a,   25 /* Public */,
      25,    3,  184,    2, 0x0a,   26 /* Public */,
      25,    2,  191,    2, 0x2a,   30 /* Public | MethodCloned */,
      25,    1,  196,    2, 0x2a,   33 /* Public | MethodCloned */,
      30,    1,  199,    2, 0x0a,   35 /* Public */,
      30,    0,  202,    2, 0x2a,   37 /* Public | MethodCloned */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void, QMetaType::Bool,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   11,
    QMetaType::Void, 0x80000000 | 13,   14,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,    8,
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool, 0x80000000 | 27, QMetaType::Int,   26,   28,   29,
    QMetaType::Void, QMetaType::Bool, 0x80000000 | 27,   26,   28,
    QMetaType::Void, QMetaType::Bool,   26,
    QMetaType::Void, 0x80000000 | 27,   28,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ads::CDockWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_meta_stringdata_ZN3ads11CDockWidgetE.offsetsAndSizes,
    qt_meta_data_ZN3ads11CDockWidgetE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN3ads11CDockWidgetE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CDockWidget, std::true_type>,
        // method 'viewToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'closed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'titleChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'topLevelChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'closeRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'visibilityChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'featuresChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget::DockWidgetFeatures, std::false_type>,
        // method 'setToolbarFloatingStyle'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'toggleView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'toggleView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setAsCurrentTab'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'raise'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setFloating'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'deleteDockWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'closeDockWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'requestCloseDockWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showFullScreen'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showNormal'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setAutoHide'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<SideBarLocation, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setAutoHide'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<SideBarLocation, std::false_type>,
        // method 'setAutoHide'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'toggleAutoHide'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<SideBarLocation, std::false_type>,
        // method 'toggleAutoHide'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ads::CDockWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->viewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->closed(); break;
        case 2: _t->titleChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->topLevelChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->closeRequested(); break;
        case 5: _t->visibilityChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->featuresChanged((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget::DockWidgetFeatures>>(_a[1]))); break;
        case 7: _t->setToolbarFloatingStyle((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->toggleView((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->toggleView(); break;
        case 10: _t->setAsCurrentTab(); break;
        case 11: _t->raise(); break;
        case 12: _t->setFloating(); break;
        case 13: _t->deleteDockWidget(); break;
        case 14: _t->closeDockWidget(); break;
        case 15: _t->requestCloseDockWidget(); break;
        case 16: _t->showFullScreen(); break;
        case 17: _t->showNormal(); break;
        case 18: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 19: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2]))); break;
        case 20: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 21: _t->toggleAutoHide((*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[1]))); break;
        case 22: _t->toggleAutoHide(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (CDockWidget::*)(bool );
            if (_q_method_type _q_method = &CDockWidget::viewToggled; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)();
            if (_q_method_type _q_method = &CDockWidget::closed; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)(const QString & );
            if (_q_method_type _q_method = &CDockWidget::titleChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)(bool );
            if (_q_method_type _q_method = &CDockWidget::topLevelChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)();
            if (_q_method_type _q_method = &CDockWidget::closeRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)(bool );
            if (_q_method_type _q_method = &CDockWidget::visibilityChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (CDockWidget::*)(ads::CDockWidget::DockWidgetFeatures );
            if (_q_method_type _q_method = &CDockWidget::featuresChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
}

const QMetaObject *ads::CDockWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN3ads11CDockWidgetE.stringdata0))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 23)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 23;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 23)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 23;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockWidget::viewToggled(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ads::CDockWidget::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ads::CDockWidget::titleChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ads::CDockWidget::topLevelChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void ads::CDockWidget::closeRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void ads::CDockWidget::visibilityChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void ads::CDockWidget::featuresChanged(ads::CDockWidget::DockWidgetFeatures _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
