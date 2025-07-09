/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/demo/MainWindow.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11CMainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto CMainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN11CMainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CMainWindow",
        "on_actionSaveState_triggered",
        "",
        "on_actionRestoreState_triggered",
        "savePerspective",
        "onViewToggled",
        "Open",
        "onViewVisibilityChanged",
        "Visible",
        "createEditor",
        "createTable",
        "onEditorCloseRequested",
        "onImageViewerCloseRequested",
        "showStatusDialog",
        "toggleDockWidgetWindowTitle",
        "applyVsStyle",
        "createImageViewer",
        "lockWorkspace",
        "Value"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'on_actionSaveState_triggered'
        QtMocHelpers::SlotData<void(bool)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Slot 'on_actionRestoreState_triggered'
        QtMocHelpers::SlotData<void(bool)>(3, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 2 },
        }}),
        // Slot 'savePerspective'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onViewToggled'
        QtMocHelpers::SlotData<void(bool)>(5, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Slot 'onViewVisibilityChanged'
        QtMocHelpers::SlotData<void(bool)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Slot 'createEditor'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'createTable'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEditorCloseRequested'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onImageViewerCloseRequested'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showStatusDialog'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'toggleDockWidgetWindowTitle'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'applyVsStyle'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'createImageViewer'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'lockWorkspace'
        QtMocHelpers::SlotData<void(bool)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 18 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CMainWindow, qt_meta_tag_ZN11CMainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CMainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11CMainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11CMainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11CMainWindowE_t>.metaTypes,
    nullptr
} };

void CMainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CMainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->on_actionSaveState_triggered((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->on_actionRestoreState_triggered((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->savePerspective(); break;
        case 3: _t->onViewToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->onViewVisibilityChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->createEditor(); break;
        case 6: _t->createTable(); break;
        case 7: _t->onEditorCloseRequested(); break;
        case 8: _t->onImageViewerCloseRequested(); break;
        case 9: _t->showStatusDialog(); break;
        case 10: _t->toggleDockWidgetWindowTitle(); break;
        case 11: _t->applyVsStyle(); break;
        case 12: _t->createImageViewer(); break;
        case 13: _t->lockWorkspace((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *CMainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CMainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11CMainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int CMainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
