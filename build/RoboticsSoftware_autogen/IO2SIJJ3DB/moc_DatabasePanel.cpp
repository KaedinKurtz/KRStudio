/****************************************************************************
** Meta object code from reading C++ file 'DatabasePanel.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UIHeaders/DatabasePanel.hpp"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DatabasePanel.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN13DatabasePanelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN13DatabasePanelE = QtMocHelpers::stringData(
    "DatabasePanel",
    "requestSceneReload",
    "",
    "sceneName",
    "onRefreshScenes",
    "onSceneSelected",
    "onSaveScene",
    "onLoadScene",
    "onBackup",
    "onRestore",
    "onRunQuery",
    "onShowEntities",
    "onShowComponents",
    "onListIndexes",
    "onCreateIndex",
    "onDropIndex",
    "onRebuildIndex",
    "onAnalyzeIndex",
    "onValidateIndex",
    "onEnableReplication",
    "onDisableReplication",
    "onSyncReplication",
    "onShowReplicationStatus"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN13DatabasePanelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  134,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    0,  137,    2, 0x08,    3 /* Private */,
       5,    1,  138,    2, 0x08,    4 /* Private */,
       6,    0,  141,    2, 0x08,    6 /* Private */,
       7,    0,  142,    2, 0x08,    7 /* Private */,
       8,    0,  143,    2, 0x08,    8 /* Private */,
       9,    0,  144,    2, 0x08,    9 /* Private */,
      10,    0,  145,    2, 0x08,   10 /* Private */,
      11,    0,  146,    2, 0x08,   11 /* Private */,
      12,    0,  147,    2, 0x08,   12 /* Private */,
      13,    0,  148,    2, 0x08,   13 /* Private */,
      14,    0,  149,    2, 0x08,   14 /* Private */,
      15,    0,  150,    2, 0x08,   15 /* Private */,
      16,    0,  151,    2, 0x08,   16 /* Private */,
      17,    0,  152,    2, 0x08,   17 /* Private */,
      18,    0,  153,    2, 0x08,   18 /* Private */,
      19,    0,  154,    2, 0x08,   19 /* Private */,
      20,    0,  155,    2, 0x08,   20 /* Private */,
      21,    0,  156,    2, 0x08,   21 /* Private */,
      22,    0,  157,    2, 0x08,   22 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject DatabasePanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN13DatabasePanelE.offsetsAndSizes,
    qt_meta_data_ZN13DatabasePanelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN13DatabasePanelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DatabasePanel, std::true_type>,
        // method 'requestSceneReload'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onRefreshScenes'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSceneSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onSaveScene'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onLoadScene'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onBackup'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRestore'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRunQuery'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onShowEntities'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onShowComponents'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onListIndexes'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCreateIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDropIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRebuildIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAnalyzeIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onValidateIndex'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onEnableReplication'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDisableReplication'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSyncReplication'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onShowReplicationStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void DatabasePanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabasePanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->requestSceneReload((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->onRefreshScenes(); break;
        case 2: _t->onSceneSelected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->onSaveScene(); break;
        case 4: _t->onLoadScene(); break;
        case 5: _t->onBackup(); break;
        case 6: _t->onRestore(); break;
        case 7: _t->onRunQuery(); break;
        case 8: _t->onShowEntities(); break;
        case 9: _t->onShowComponents(); break;
        case 10: _t->onListIndexes(); break;
        case 11: _t->onCreateIndex(); break;
        case 12: _t->onDropIndex(); break;
        case 13: _t->onRebuildIndex(); break;
        case 14: _t->onAnalyzeIndex(); break;
        case 15: _t->onValidateIndex(); break;
        case 16: _t->onEnableReplication(); break;
        case 17: _t->onDisableReplication(); break;
        case 18: _t->onSyncReplication(); break;
        case 19: _t->onShowReplicationStatus(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DatabasePanel::*)(const QString & );
            if (_q_method_type _q_method = &DatabasePanel::requestSceneReload; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *DatabasePanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DatabasePanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN13DatabasePanelE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int DatabasePanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
    return _id;
}

// SIGNAL 0
void DatabasePanel::requestSceneReload(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
