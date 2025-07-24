/****************************************************************************
** Meta object code from reading C++ file 'DatabaseMigrationManager.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UtilityHeaders/DatabaseMigrationManager.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DatabaseMigrationManager.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN2db24DatabaseMigrationManagerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN2db24DatabaseMigrationManagerE = QtMocHelpers::stringData(
    "db::DatabaseMigrationManager",
    "migrationStarted",
    "",
    "version",
    "migrationCompleted",
    "success",
    "migrationProgress",
    "MigrationProgress",
    "progress",
    "rollbackStarted",
    "rollbackCompleted",
    "rollbackProgress",
    "validationStarted",
    "validationCompleted",
    "schemaChanged",
    "migrationError",
    "error",
    "migrationWarning",
    "warning",
    "onMigrationCompleted",
    "onRollbackCompleted",
    "onValidationCompleted",
    "onSchemaChanged"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN2db24DatabaseMigrationManagerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      15,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      11,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  104,    2, 0x06,    1 /* Public */,
       4,    2,  107,    2, 0x06,    3 /* Public */,
       6,    1,  112,    2, 0x06,    6 /* Public */,
       9,    1,  115,    2, 0x06,    8 /* Public */,
      10,    2,  118,    2, 0x06,   10 /* Public */,
      11,    1,  123,    2, 0x06,   13 /* Public */,
      12,    0,  126,    2, 0x06,   15 /* Public */,
      13,    1,  127,    2, 0x06,   16 /* Public */,
      14,    0,  130,    2, 0x06,   18 /* Public */,
      15,    1,  131,    2, 0x06,   19 /* Public */,
      17,    1,  134,    2, 0x06,   21 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      19,    0,  137,    2, 0x08,   23 /* Private */,
      20,    0,  138,    2, 0x08,   24 /* Private */,
      21,    0,  139,    2, 0x08,   25 /* Private */,
      22,    0,  140,    2, 0x08,   26 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    3,    5,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    3,    5,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   16,
    QMetaType::Void, QMetaType::QString,   18,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject db::DatabaseMigrationManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN2db24DatabaseMigrationManagerE.offsetsAndSizes,
    qt_meta_data_ZN2db24DatabaseMigrationManagerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN2db24DatabaseMigrationManagerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DatabaseMigrationManager, std::true_type>,
        // method 'migrationStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'migrationCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'migrationProgress'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const MigrationProgress &, std::false_type>,
        // method 'rollbackStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'rollbackCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'rollbackProgress'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const MigrationProgress &, std::false_type>,
        // method 'validationStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'validationCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'schemaChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'migrationError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'migrationWarning'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onMigrationCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRollbackCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onValidationCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSchemaChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void db::DatabaseMigrationManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabaseMigrationManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->migrationStarted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->migrationCompleted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->migrationProgress((*reinterpret_cast< std::add_pointer_t<MigrationProgress>>(_a[1]))); break;
        case 3: _t->rollbackStarted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->rollbackCompleted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->rollbackProgress((*reinterpret_cast< std::add_pointer_t<MigrationProgress>>(_a[1]))); break;
        case 6: _t->validationStarted(); break;
        case 7: _t->validationCompleted((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->schemaChanged(); break;
        case 9: _t->migrationError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->migrationWarning((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->onMigrationCompleted(); break;
        case 12: _t->onRollbackCompleted(); break;
        case 13: _t->onValidationCompleted(); break;
        case 14: _t->onSchemaChanged(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::migrationStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & , bool );
            if (_q_method_type _q_method = &DatabaseMigrationManager::migrationCompleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const MigrationProgress & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::migrationProgress; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::rollbackStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & , bool );
            if (_q_method_type _q_method = &DatabaseMigrationManager::rollbackCompleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const MigrationProgress & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::rollbackProgress; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)();
            if (_q_method_type _q_method = &DatabaseMigrationManager::validationStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(bool );
            if (_q_method_type _q_method = &DatabaseMigrationManager::validationCompleted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)();
            if (_q_method_type _q_method = &DatabaseMigrationManager::schemaChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::migrationError; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseMigrationManager::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseMigrationManager::migrationWarning; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
    }
}

const QMetaObject *db::DatabaseMigrationManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *db::DatabaseMigrationManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN2db24DatabaseMigrationManagerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int db::DatabaseMigrationManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void db::DatabaseMigrationManager::migrationStarted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void db::DatabaseMigrationManager::migrationCompleted(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void db::DatabaseMigrationManager::migrationProgress(const MigrationProgress & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void db::DatabaseMigrationManager::rollbackStarted(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void db::DatabaseMigrationManager::rollbackCompleted(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void db::DatabaseMigrationManager::rollbackProgress(const MigrationProgress & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void db::DatabaseMigrationManager::validationStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void db::DatabaseMigrationManager::validationCompleted(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void db::DatabaseMigrationManager::schemaChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void db::DatabaseMigrationManager::migrationError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void db::DatabaseMigrationManager::migrationWarning(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}
QT_WARNING_POP
