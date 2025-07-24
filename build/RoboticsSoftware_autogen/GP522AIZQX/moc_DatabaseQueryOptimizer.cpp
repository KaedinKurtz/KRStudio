/****************************************************************************
** Meta object code from reading C++ file 'DatabaseQueryOptimizer.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UtilityHeaders/DatabaseQueryOptimizer.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DatabaseQueryOptimizer.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN2db22DatabaseQueryOptimizerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN2db22DatabaseQueryOptimizerE = QtMocHelpers::stringData(
    "db::DatabaseQueryOptimizer",
    "queryAnalyzed",
    "",
    "QueryAnalysis",
    "analysis",
    "queryOptimized",
    "original",
    "optimized",
    "cacheHit",
    "query",
    "cacheMiss",
    "slowQueryDetected",
    "executionTime",
    "optimizationSuggestionGenerated",
    "OptimizationSuggestion",
    "suggestion",
    "optimizationApplied",
    "success",
    "statisticsUpdated",
    "performanceReportGenerated",
    "filePath",
    "optimizerError",
    "error",
    "optimizerWarning",
    "warning",
    "onAutoOptimizationTimer",
    "onStatisticsUpdateTimer",
    "onCacheCleanupTimer",
    "onQueryAnalysisCompleted",
    "onQueryOptimizationCompleted",
    "onOptimizationSuggestionCompleted"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN2db22DatabaseQueryOptimizerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      17,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      11,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  116,    2, 0x06,    1 /* Public */,
       5,    2,  119,    2, 0x06,    3 /* Public */,
       8,    1,  124,    2, 0x06,    6 /* Public */,
      10,    1,  127,    2, 0x06,    8 /* Public */,
      11,    2,  130,    2, 0x06,   10 /* Public */,
      13,    1,  135,    2, 0x06,   13 /* Public */,
      16,    2,  138,    2, 0x06,   15 /* Public */,
      18,    0,  143,    2, 0x06,   18 /* Public */,
      19,    1,  144,    2, 0x06,   19 /* Public */,
      21,    1,  147,    2, 0x06,   21 /* Public */,
      23,    1,  150,    2, 0x06,   23 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      25,    0,  153,    2, 0x08,   25 /* Private */,
      26,    0,  154,    2, 0x08,   26 /* Private */,
      27,    0,  155,    2, 0x08,   27 /* Private */,
      28,    0,  156,    2, 0x08,   28 /* Private */,
      29,    0,  157,    2, 0x08,   29 /* Private */,
      30,    0,  158,    2, 0x08,   30 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,    6,    7,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, QMetaType::QString,    9,
    QMetaType::Void, QMetaType::QString, QMetaType::LongLong,    9,   12,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void, 0x80000000 | 14, QMetaType::Bool,   15,   17,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   20,
    QMetaType::Void, QMetaType::QString,   22,
    QMetaType::Void, QMetaType::QString,   24,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject db::DatabaseQueryOptimizer::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN2db22DatabaseQueryOptimizerE.offsetsAndSizes,
    qt_meta_data_ZN2db22DatabaseQueryOptimizerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN2db22DatabaseQueryOptimizerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DatabaseQueryOptimizer, std::true_type>,
        // method 'queryAnalyzed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QueryAnalysis &, std::false_type>,
        // method 'queryOptimized'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'cacheHit'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'cacheMiss'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'slowQueryDetected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'optimizationSuggestionGenerated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const OptimizationSuggestion &, std::false_type>,
        // method 'optimizationApplied'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const OptimizationSuggestion &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'statisticsUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'performanceReportGenerated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'optimizerError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'optimizerWarning'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onAutoOptimizationTimer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onStatisticsUpdateTimer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCacheCleanupTimer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onQueryAnalysisCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onQueryOptimizationCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onOptimizationSuggestionCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void db::DatabaseQueryOptimizer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabaseQueryOptimizer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->queryAnalyzed((*reinterpret_cast< std::add_pointer_t<QueryAnalysis>>(_a[1]))); break;
        case 1: _t->queryOptimized((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->cacheHit((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->cacheMiss((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->slowQueryDetected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2]))); break;
        case 5: _t->optimizationSuggestionGenerated((*reinterpret_cast< std::add_pointer_t<OptimizationSuggestion>>(_a[1]))); break;
        case 6: _t->optimizationApplied((*reinterpret_cast< std::add_pointer_t<OptimizationSuggestion>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->statisticsUpdated(); break;
        case 8: _t->performanceReportGenerated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->optimizerError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->optimizerWarning((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->onAutoOptimizationTimer(); break;
        case 12: _t->onStatisticsUpdateTimer(); break;
        case 13: _t->onCacheCleanupTimer(); break;
        case 14: _t->onQueryAnalysisCompleted(); break;
        case 15: _t->onQueryOptimizationCompleted(); break;
        case 16: _t->onOptimizationSuggestionCompleted(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QueryAnalysis & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::queryAnalyzed; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & , const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::queryOptimized; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::cacheHit; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::cacheMiss; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & , qint64 );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::slowQueryDetected; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const OptimizationSuggestion & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::optimizationSuggestionGenerated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const OptimizationSuggestion & , bool );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::optimizationApplied; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)();
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::statisticsUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::performanceReportGenerated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::optimizerError; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (DatabaseQueryOptimizer::*)(const QString & );
            if (_q_method_type _q_method = &DatabaseQueryOptimizer::optimizerWarning; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
    }
}

const QMetaObject *db::DatabaseQueryOptimizer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *db::DatabaseQueryOptimizer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN2db22DatabaseQueryOptimizerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int db::DatabaseQueryOptimizer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 17;
    }
    return _id;
}

// SIGNAL 0
void db::DatabaseQueryOptimizer::queryAnalyzed(const QueryAnalysis & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void db::DatabaseQueryOptimizer::queryOptimized(const QString & _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void db::DatabaseQueryOptimizer::cacheHit(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void db::DatabaseQueryOptimizer::cacheMiss(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void db::DatabaseQueryOptimizer::slowQueryDetected(const QString & _t1, qint64 _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void db::DatabaseQueryOptimizer::optimizationSuggestionGenerated(const OptimizationSuggestion & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void db::DatabaseQueryOptimizer::optimizationApplied(const OptimizationSuggestion & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void db::DatabaseQueryOptimizer::statisticsUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void db::DatabaseQueryOptimizer::performanceReportGenerated(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void db::DatabaseQueryOptimizer::optimizerError(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void db::DatabaseQueryOptimizer::optimizerWarning(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}
QT_WARNING_POP
