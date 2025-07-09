/****************************************************************************
** Meta object code from reading C++ file 'DockAreaWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/DockAreaWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DockAreaWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3ads15CDockAreaWidgetE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN3ads15CDockAreaWidgetE = QtMocHelpers::stringData(
    "ads::CDockAreaWidget",
    "tabBarClicked",
    "",
    "index",
    "currentChanging",
    "currentChanged",
    "viewToggled",
    "Open",
    "onTabCloseRequested",
    "Index",
    "reorderDockWidget",
    "fromIndex",
    "toIndex",
    "updateAutoHideButtonCheckState",
    "updateTitleBarButtonsToolTips",
    "calculateSideTabBarArea",
    "SideBarLocation",
    "toggleView",
    "setCurrentIndex",
    "closeArea",
    "setAutoHide",
    "Enable",
    "Location",
    "TabIndex",
    "toggleAutoHide",
    "closeOtherAreas",
    "setFloating"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN3ads15CDockAreaWidgetE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      19,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  128,    2, 0x06,    1 /* Public */,
       4,    1,  131,    2, 0x06,    3 /* Public */,
       5,    1,  134,    2, 0x06,    5 /* Public */,
       6,    1,  137,    2, 0x06,    7 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       8,    1,  140,    2, 0x08,    9 /* Private */,
      10,    2,  143,    2, 0x08,   11 /* Private */,
      13,    0,  148,    2, 0x08,   14 /* Private */,
      14,    0,  149,    2, 0x08,   15 /* Private */,
      15,    0,  150,    2, 0x108,   16 /* Private | MethodIsConst  */,
      17,    1,  151,    2, 0x09,   17 /* Protected */,
      18,    1,  154,    2, 0x0a,   19 /* Public */,
      19,    0,  157,    2, 0x0a,   21 /* Public */,
      20,    3,  158,    2, 0x0a,   22 /* Public */,
      20,    2,  165,    2, 0x2a,   26 /* Public | MethodCloned */,
      20,    1,  170,    2, 0x2a,   29 /* Public | MethodCloned */,
      24,    1,  173,    2, 0x0a,   31 /* Public */,
      24,    0,  176,    2, 0x2a,   33 /* Public | MethodCloned */,
      25,    0,  177,    2, 0x0a,   34 /* Public */,
      26,    0,  178,    2, 0x0a,   35 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Bool,    7,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,   11,   12,
    QMetaType::Void,
    QMetaType::Void,
    0x80000000 | 16,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool, 0x80000000 | 16, QMetaType::Int,   21,   22,   23,
    QMetaType::Void, QMetaType::Bool, 0x80000000 | 16,   21,   22,
    QMetaType::Void, QMetaType::Bool,   21,
    QMetaType::Void, 0x80000000 | 16,   22,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ads::CDockAreaWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_meta_stringdata_ZN3ads15CDockAreaWidgetE.offsetsAndSizes,
    qt_meta_data_ZN3ads15CDockAreaWidgetE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN3ads15CDockAreaWidgetE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CDockAreaWidget, std::true_type>,
        // method 'tabBarClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'currentChanging'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'currentChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'viewToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onTabCloseRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'reorderDockWidget'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateAutoHideButtonCheckState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateTitleBarButtonsToolTips'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'calculateSideTabBarArea'
        QtPrivate::TypeAndForceComplete<SideBarLocation, std::false_type>,
        // method 'toggleView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setCurrentIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'closeArea'
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
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'closeOtherAreas'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setFloating'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ads::CDockAreaWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CDockAreaWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->tabBarClicked((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->currentChanging((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->currentChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->viewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->onTabCloseRequested((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->reorderDockWidget((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 6: _t->updateAutoHideButtonCheckState(); break;
        case 7: _t->updateTitleBarButtonsToolTips(); break;
        case 8: { SideBarLocation _r = _t->calculateSideTabBarArea();
            if (_a[0]) *reinterpret_cast< SideBarLocation*>(_a[0]) = std::move(_r); }  break;
        case 9: _t->toggleView((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->setCurrentIndex((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->closeArea(); break;
        case 12: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 13: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[2]))); break;
        case 14: _t->setAutoHide((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 15: _t->toggleAutoHide((*reinterpret_cast< std::add_pointer_t<SideBarLocation>>(_a[1]))); break;
        case 16: _t->toggleAutoHide(); break;
        case 17: _t->closeOtherAreas(); break;
        case 18: _t->setFloating(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (CDockAreaWidget::*)(int );
            if (_q_method_type _q_method = &CDockAreaWidget::tabBarClicked; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (CDockAreaWidget::*)(int );
            if (_q_method_type _q_method = &CDockAreaWidget::currentChanging; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (CDockAreaWidget::*)(int );
            if (_q_method_type _q_method = &CDockAreaWidget::currentChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (CDockAreaWidget::*)(bool );
            if (_q_method_type _q_method = &CDockAreaWidget::viewToggled; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *ads::CDockAreaWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ads::CDockAreaWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN3ads15CDockAreaWidgetE.stringdata0))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int ads::CDockAreaWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 19)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 19;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 19)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 19;
    }
    return _id;
}

// SIGNAL 0
void ads::CDockAreaWidget::tabBarClicked(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ads::CDockAreaWidget::currentChanging(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ads::CDockAreaWidget::currentChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ads::CDockAreaWidget::viewToggled(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
