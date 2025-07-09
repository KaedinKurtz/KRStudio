/****************************************************************************
** Meta object code from reading C++ file 'ads_globals.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../qtads-src/src/ads_globals.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ads_globals.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3adsE_t {};
} // unnamed namespace

template <> constexpr inline auto ads::qt_create_metaobjectdata<qt_meta_tag_ZN3adsE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ads",
        "SideBarLocation",
        "SideBarTop",
        "SideBarLeft",
        "SideBarRight",
        "SideBarBottom",
        "SideBarNone"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'SideBarLocation'
        QtMocHelpers::EnumData<SideBarLocation>(1, 1, QMC::EnumFlags{}).add({
            {    2, SideBarLocation::SideBarTop },
            {    3, SideBarLocation::SideBarLeft },
            {    4, SideBarLocation::SideBarRight },
            {    5, SideBarLocation::SideBarBottom },
            {    6, SideBarLocation::SideBarNone },
        }),
    };
    return QtMocHelpers::metaObjectData<void, qt_meta_tag_ZN3adsE_t>(QMC::PropertyAccessInStaticMetaCall, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}

static constexpr auto qt_staticMetaObjectContent_ZN3adsE =
    ads::qt_create_metaobjectdata<qt_meta_tag_ZN3adsE_t>();
static constexpr auto qt_staticMetaObjectStaticContent_ZN3adsE =
    qt_staticMetaObjectContent_ZN3adsE.staticData;
static constexpr auto qt_staticMetaObjectRelocatingContent_ZN3adsE =
    qt_staticMetaObjectContent_ZN3adsE.relocatingData;

Q_CONSTINIT const QMetaObject ads::staticMetaObject = { {
    nullptr,
    qt_staticMetaObjectStaticContent_ZN3adsE.stringdata,
    qt_staticMetaObjectStaticContent_ZN3adsE.data,
    nullptr,
    nullptr,
    qt_staticMetaObjectRelocatingContent_ZN3adsE.metaTypes,
    nullptr
} };

QT_WARNING_POP
