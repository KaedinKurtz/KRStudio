/****************************************************************************
** Meta object code from reading C++ file 'FlowVisualizerMenu.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../include/UIHeaders/FlowVisualizerMenu.hpp"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'FlowVisualizerMenu.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN18FlowVisualizerMenuE_t {};
} // unnamed namespace

template <> constexpr inline auto FlowVisualizerMenu::qt_create_metaobjectdata<qt_meta_tag_ZN18FlowVisualizerMenuE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "FlowVisualizerMenu",
        "settingsChanged",
        "",
        "transformChanged",
        "testViewportRequested",
        "onSettingChanged",
        "onMasterVisibilityChanged",
        "checked",
        "onResetVisualizerClicked",
        "onVisualizationTypeChanged",
        "index",
        "onBoundaryTypeChanged",
        "onStaticColoringStyleChanged",
        "onStaticDirectionalColorClicked",
        "onStaticAddColorStop",
        "onStaticRemoveColorStop",
        "onStaticColorStopTableChanged",
        "onDynamicColoringStyleChanged",
        "onDynamicDirectionalColorClicked",
        "onDynamicAddIntensityStop",
        "onDynamicRemoveIntensityStop",
        "onDynamicIntensityTableChanged",
        "onDynamicAddLifetimeStop",
        "onDynamicRemoveLifetimeStop",
        "onDynamicLifetimeTableChanged",
        "onParticleTypeToggleChanged",
        "onParticleColoringStyleChanged",
        "onParticleDirectionalColorClicked",
        "onParticleAddIntensityStop",
        "onParticleRemoveIntensityStop",
        "onParticleIntensityTableChanged",
        "onParticleAddLifetimeStop",
        "onParticleRemoveLifetimeStop",
        "onParticleLifetimeTableChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'settingsChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'transformChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'testViewportRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onSettingChanged'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onMasterVisibilityChanged'
        QtMocHelpers::SlotData<void(bool)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Slot 'onResetVisualizerClicked'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onVisualizationTypeChanged'
        QtMocHelpers::SlotData<void(int)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onBoundaryTypeChanged'
        QtMocHelpers::SlotData<void(int)>(11, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onStaticColoringStyleChanged'
        QtMocHelpers::SlotData<void(int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onStaticDirectionalColorClicked'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStaticAddColorStop'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStaticRemoveColorStop'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStaticColorStopTableChanged'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicColoringStyleChanged'
        QtMocHelpers::SlotData<void(int)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onDynamicDirectionalColorClicked'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicAddIntensityStop'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicRemoveIntensityStop'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicIntensityTableChanged'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicAddLifetimeStop'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicRemoveLifetimeStop'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDynamicLifetimeTableChanged'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleTypeToggleChanged'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleColoringStyleChanged'
        QtMocHelpers::SlotData<void(int)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onParticleDirectionalColorClicked'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleAddIntensityStop'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleRemoveIntensityStop'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleIntensityTableChanged'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleAddLifetimeStop'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleRemoveLifetimeStop'
        QtMocHelpers::SlotData<void()>(32, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onParticleLifetimeTableChanged'
        QtMocHelpers::SlotData<void()>(33, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<FlowVisualizerMenu, qt_meta_tag_ZN18FlowVisualizerMenuE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject FlowVisualizerMenu::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18FlowVisualizerMenuE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18FlowVisualizerMenuE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18FlowVisualizerMenuE_t>.metaTypes,
    nullptr
} };

void FlowVisualizerMenu::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<FlowVisualizerMenu *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->settingsChanged(); break;
        case 1: _t->transformChanged(); break;
        case 2: _t->testViewportRequested(); break;
        case 3: _t->onSettingChanged(); break;
        case 4: _t->onMasterVisibilityChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->onResetVisualizerClicked(); break;
        case 6: _t->onVisualizationTypeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->onBoundaryTypeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->onStaticColoringStyleChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->onStaticDirectionalColorClicked(); break;
        case 10: _t->onStaticAddColorStop(); break;
        case 11: _t->onStaticRemoveColorStop(); break;
        case 12: _t->onStaticColorStopTableChanged(); break;
        case 13: _t->onDynamicColoringStyleChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 14: _t->onDynamicDirectionalColorClicked(); break;
        case 15: _t->onDynamicAddIntensityStop(); break;
        case 16: _t->onDynamicRemoveIntensityStop(); break;
        case 17: _t->onDynamicIntensityTableChanged(); break;
        case 18: _t->onDynamicAddLifetimeStop(); break;
        case 19: _t->onDynamicRemoveLifetimeStop(); break;
        case 20: _t->onDynamicLifetimeTableChanged(); break;
        case 21: _t->onParticleTypeToggleChanged(); break;
        case 22: _t->onParticleColoringStyleChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 23: _t->onParticleDirectionalColorClicked(); break;
        case 24: _t->onParticleAddIntensityStop(); break;
        case 25: _t->onParticleRemoveIntensityStop(); break;
        case 26: _t->onParticleIntensityTableChanged(); break;
        case 27: _t->onParticleAddLifetimeStop(); break;
        case 28: _t->onParticleRemoveLifetimeStop(); break;
        case 29: _t->onParticleLifetimeTableChanged(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (FlowVisualizerMenu::*)()>(_a, &FlowVisualizerMenu::settingsChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (FlowVisualizerMenu::*)()>(_a, &FlowVisualizerMenu::transformChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (FlowVisualizerMenu::*)()>(_a, &FlowVisualizerMenu::testViewportRequested, 2))
            return;
    }
}

const QMetaObject *FlowVisualizerMenu::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FlowVisualizerMenu::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18FlowVisualizerMenuE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int FlowVisualizerMenu::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 30)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 30;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 30)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 30;
    }
    return _id;
}

// SIGNAL 0
void FlowVisualizerMenu::settingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void FlowVisualizerMenu::transformChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void FlowVisualizerMenu::testViewportRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
