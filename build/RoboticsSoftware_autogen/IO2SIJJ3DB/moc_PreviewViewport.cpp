/****************************************************************************
** Meta object code from reading C++ file 'PreviewViewport.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UIHeaders/PreviewViewport.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PreviewViewport.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15PreviewViewportE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN15PreviewViewportE = QtMocHelpers::stringData(
    "PreviewViewport"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN15PreviewViewportE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

Q_CONSTINIT const QMetaObject PreviewViewport::staticMetaObject = { {
    QMetaObject::SuperData::link<ViewportWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN15PreviewViewportE.offsetsAndSizes,
    qt_meta_data_ZN15PreviewViewportE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN15PreviewViewportE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<PreviewViewport, std::true_type>
    >,
    nullptr
} };

void PreviewViewport::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PreviewViewport *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *PreviewViewport::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PreviewViewport::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN15PreviewViewportE.stringdata0))
        return static_cast<void*>(this);
    return ViewportWidget::qt_metacast(_clname);
}

int PreviewViewport::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ViewportWidget::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
