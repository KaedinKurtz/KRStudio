/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UIHeaders/MainWindow.hpp"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN10MainWindowE = QtMocHelpers::stringData(
    "MainWindow",
    "addViewport",
    "",
    "removeViewport",
    "onTestNewViewport",
    "updateViewportLayouts",
    "onShowViewportRequested",
    "ads::CDockWidget*",
    "dock",
    "onResetViewports",
    "onViewportDockClosed",
    "closedDock",
    "onLoadRobotClicked",
    "onMasterRender",
    "onFlowVisualizerTransformChanged",
    "onFlowVisualizerSettingsChanged",
    "onSceneReloadRequested",
    "sceneName"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10MainWindowE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   86,    2, 0x0a,    1 /* Public */,
       3,    0,   87,    2, 0x0a,    2 /* Public */,
       4,    0,   88,    2, 0x08,    3 /* Private */,
       5,    0,   89,    2, 0x08,    4 /* Private */,
       6,    1,   90,    2, 0x08,    5 /* Private */,
       9,    0,   93,    2, 0x08,    7 /* Private */,
      10,    1,   94,    2, 0x08,    8 /* Private */,
      12,    0,   97,    2, 0x09,   10 /* Protected */,
      13,    0,   98,    2, 0x09,   11 /* Protected */,
      14,    0,   99,    2, 0x09,   12 /* Protected */,
      15,    0,  100,    2, 0x09,   13 /* Protected */,
      16,    1,  101,    2, 0x09,   14 /* Protected */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,   11,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   17,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_ZN10MainWindowE.offsetsAndSizes,
    qt_meta_data_ZN10MainWindowE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN10MainWindowE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'addViewport'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'removeViewport'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onTestNewViewport'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateViewportLayouts'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onShowViewportRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'onResetViewports'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onViewportDockClosed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<ads::CDockWidget *, std::false_type>,
        // method 'onLoadRobotClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onMasterRender'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFlowVisualizerTransformChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFlowVisualizerSettingsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSceneReloadRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->addViewport(); break;
        case 1: _t->removeViewport(); break;
        case 2: _t->onTestNewViewport(); break;
        case 3: _t->updateViewportLayouts(); break;
        case 4: _t->onShowViewportRequested((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1]))); break;
        case 5: _t->onResetViewports(); break;
        case 6: _t->onViewportDockClosed((*reinterpret_cast< std::add_pointer_t<ads::CDockWidget*>>(_a[1]))); break;
        case 7: _t->onLoadRobotClicked(); break;
        case 8: _t->onMasterRender(); break;
        case 9: _t->onFlowVisualizerTransformChanged(); break;
        case 10: _t->onFlowVisualizerSettingsChanged(); break;
        case 11: _t->onSceneReloadRequested((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ads::CDockWidget* >(); break;
            }
            break;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN10MainWindowE.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}
QT_WARNING_POP
