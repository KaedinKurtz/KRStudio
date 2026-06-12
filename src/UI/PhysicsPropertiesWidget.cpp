#include "PhysicsPropertiesWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QFrame>
#include <QScrollArea>

namespace {
// Section header in the house style: centered label over a horizontal line.
QWidget* makeSectionHeader(const QString& text, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 6, 0, 0);
    l->setSpacing(2);
    auto* label = new QLabel(text, w);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-weight: bold;");
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::HLine);
    l->addWidget(label);
    l->addWidget(line);
    return w;
}

QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step, int decimals = 3)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    return s;
}
} // namespace

PhysicsPropertiesWidget::PhysicsPropertiesWidget(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    m_objectName = new QLabel(QStringLiteral("No selection"), content);
    m_objectName->setAlignment(Qt::AlignCenter);
    m_objectName->setStyleSheet("font-weight: bold; font-size: 10pt; padding: 4px;");
    layout->addWidget(m_objectName);

    layout->addWidget(makeSectionHeader(QStringLiteral("Rigid Body"), content));
    layout->addWidget(buildRigidBodySection());
    layout->addWidget(makeSectionHeader(QStringLiteral("Collision"), content));
    layout->addWidget(buildColliderSection());
    layout->addWidget(makeSectionHeader(QStringLiteral("Fluid"), content));
    layout->addWidget(buildFluidSection());

    scroll->setWidget(content);
    outer->addWidget(scroll);

    setEntity(entt::null);
}

QWidget* PhysicsPropertiesWidget::buildRigidBodySection()
{
    auto* box = new QGroupBox(this);
    auto* g = new QGridLayout(box);
    g->setContentsMargins(6, 6, 6, 6);
    g->setVerticalSpacing(3);

    m_rbEnabled = new QCheckBox(QStringLiteral("Rigid Body"), box);
    g->addWidget(m_rbEnabled, 0, 0, 1, 2);

    g->addWidget(new QLabel(QStringLiteral("Type"), box), 1, 0);
    m_rbType = new QComboBox(box);
    m_rbType->addItems({ QStringLiteral("Static"), QStringLiteral("Kinematic"), QStringLiteral("Dynamic") });
    g->addWidget(m_rbType, 1, 1);

    g->addWidget(new QLabel(QStringLiteral("Mass (kg)"), box), 2, 0);
    m_rbMass = makeSpin(box, 0.001, 100000.0, 0.1);
    m_rbMass->setValue(1.0);
    g->addWidget(m_rbMass, 2, 1);

    g->addWidget(new QLabel(QStringLiteral("Linear damping"), box), 3, 0);
    m_rbLinDamp = makeSpin(box, 0.0, 10.0, 0.01);
    g->addWidget(m_rbLinDamp, 3, 1);

    g->addWidget(new QLabel(QStringLiteral("Angular damping"), box), 4, 0);
    m_rbAngDamp = makeSpin(box, 0.0, 10.0, 0.01);
    g->addWidget(m_rbAngDamp, 4, 1);

    g->addWidget(new QLabel(QStringLiteral("Friction"), box), 5, 0);
    m_rbFriction = makeSpin(box, 0.0, 2.0, 0.05);
    m_rbFriction->setValue(0.6);
    g->addWidget(m_rbFriction, 5, 1);

    g->addWidget(new QLabel(QStringLiteral("Bounciness"), box), 6, 0);
    m_rbRestitution = makeSpin(box, 0.0, 1.0, 0.05);
    m_rbRestitution->setValue(0.1);
    g->addWidget(m_rbRestitution, 6, 1);

    connect(m_rbEnabled, &QCheckBox::toggled, this, &PhysicsPropertiesWidget::applyRigidBody);
    connect(m_rbType, &QComboBox::currentIndexChanged, this, &PhysicsPropertiesWidget::applyRigidBody);
    for (auto* s : { m_rbMass, m_rbLinDamp, m_rbAngDamp, m_rbFriction, m_rbRestitution })
        connect(s, &QDoubleSpinBox::valueChanged, this, &PhysicsPropertiesWidget::applyRigidBody);
    return box;
}

QWidget* PhysicsPropertiesWidget::buildColliderSection()
{
    auto* box = new QGroupBox(this);
    auto* g = new QGridLayout(box);
    g->setContentsMargins(6, 6, 6, 6);
    g->setVerticalSpacing(3);

    g->addWidget(new QLabel(QStringLiteral("Shape"), box), 0, 0);
    m_colShape = new QComboBox(box);
    m_colShape->addItems({ QStringLiteral("None (auto box)"), QStringLiteral("Box"),
                           QStringLiteral("Sphere"), QStringLiteral("Capsule"),
                           QStringLiteral("Convex Mesh") });
    g->addWidget(m_colShape, 0, 1);

    m_colStack = new QStackedWidget(box);

    // page 0: none / convex (no parameters)
    m_colStack->addWidget(new QWidget(box));

    // page 1: box half-extents
    {
        auto* page = new QWidget(box);
        auto* pg = new QGridLayout(page);
        pg->setContentsMargins(0, 0, 0, 0);
        pg->addWidget(new QLabel(QStringLiteral("Half X"), page), 0, 0);
        m_colBoxX = makeSpin(page, 0.001, 1000.0, 0.05); m_colBoxX->setValue(0.5);
        pg->addWidget(m_colBoxX, 0, 1);
        pg->addWidget(new QLabel(QStringLiteral("Half Y"), page), 1, 0);
        m_colBoxY = makeSpin(page, 0.001, 1000.0, 0.05); m_colBoxY->setValue(0.5);
        pg->addWidget(m_colBoxY, 1, 1);
        pg->addWidget(new QLabel(QStringLiteral("Half Z"), page), 2, 0);
        m_colBoxZ = makeSpin(page, 0.001, 1000.0, 0.05); m_colBoxZ->setValue(0.5);
        pg->addWidget(m_colBoxZ, 2, 1);
        m_colStack->addWidget(page);
    }
    // page 2: sphere radius
    {
        auto* page = new QWidget(box);
        auto* pg = new QGridLayout(page);
        pg->setContentsMargins(0, 0, 0, 0);
        pg->addWidget(new QLabel(QStringLiteral("Radius"), page), 0, 0);
        m_colSphereRadius = makeSpin(page, 0.001, 1000.0, 0.05); m_colSphereRadius->setValue(0.5);
        pg->addWidget(m_colSphereRadius, 0, 1);
        m_colStack->addWidget(page);
    }
    // page 3: capsule
    {
        auto* page = new QWidget(box);
        auto* pg = new QGridLayout(page);
        pg->setContentsMargins(0, 0, 0, 0);
        pg->addWidget(new QLabel(QStringLiteral("Radius"), page), 0, 0);
        m_colCapRadius = makeSpin(page, 0.001, 1000.0, 0.05); m_colCapRadius->setValue(0.5);
        pg->addWidget(m_colCapRadius, 0, 1);
        pg->addWidget(new QLabel(QStringLiteral("Height"), page), 1, 0);
        m_colCapHeight = makeSpin(page, 0.001, 1000.0, 0.05); m_colCapHeight->setValue(1.0);
        pg->addWidget(m_colCapHeight, 1, 1);
        m_colStack->addWidget(page);
    }
    g->addWidget(m_colStack, 1, 0, 1, 2);

    m_colAutoFit = new QPushButton(QStringLiteral("Fit to mesh bounds"), box);
    g->addWidget(m_colAutoFit, 2, 0, 1, 2);

    connect(m_colShape, &QComboBox::currentIndexChanged, this, [this](int idx) {
        // shape combo: 0 none, 1 box, 2 sphere, 3 capsule, 4 convex
        const int page = (idx == 1) ? 1 : (idx == 2) ? 2 : (idx == 3) ? 3 : 0;
        m_colStack->setCurrentIndex(page);
        applyCollider();
    });
    for (auto* s : { m_colBoxX, m_colBoxY, m_colBoxZ, m_colSphereRadius, m_colCapRadius, m_colCapHeight })
        connect(s, &QDoubleSpinBox::valueChanged, this, &PhysicsPropertiesWidget::applyCollider);
    connect(m_colAutoFit, &QPushButton::clicked, this, [this]() {
        if (m_entity == entt::null) return;
        auto& reg = m_scene->getRegistry();
        if (auto* mesh = reg.try_get<RenderableMeshComponent>(m_entity)) {
            const glm::vec3 he = glm::max((mesh->aabbMax - mesh->aabbMin) * 0.5f, glm::vec3(0.01f));
            m_updating = true;
            m_colBoxX->setValue(he.x); m_colBoxY->setValue(he.y); m_colBoxZ->setValue(he.z);
            m_colSphereRadius->setValue(std::max({ he.x, he.y, he.z }));
            m_colCapRadius->setValue(std::max(he.x, he.z));
            m_colCapHeight->setValue(he.y * 2.0f);
            m_updating = false;
            applyCollider();
        }
    });
    return box;
}

QWidget* PhysicsPropertiesWidget::buildFluidSection()
{
    auto* box = new QGroupBox(this);
    auto* g = new QGridLayout(box);
    g->setContentsMargins(6, 6, 6, 6);
    g->setVerticalSpacing(3);

    m_emEnabled = new QCheckBox(QStringLiteral("Fluid Emitter"), box);
    g->addWidget(m_emEnabled, 0, 0, 1, 2);
    g->addWidget(new QLabel(QStringLiteral("Rate (particles/s)"), box), 1, 0);
    m_emRate = makeSpin(box, 0.0, 50000.0, 50.0, 0); m_emRate->setValue(500.0);
    g->addWidget(m_emRate, 1, 1);
    g->addWidget(new QLabel(QStringLiteral("Speed (m/s)"), box), 2, 0);
    m_emSpeed = makeSpin(box, 0.0, 50.0, 0.1); m_emSpeed->setValue(1.0);
    g->addWidget(m_emSpeed, 2, 1);
    g->addWidget(new QLabel(QStringLiteral("Spread (deg)"), box), 3, 0);
    m_emSpread = makeSpin(box, 0.0, 90.0, 1.0, 1); m_emSpread->setValue(5.0);
    g->addWidget(m_emSpread, 3, 1);
    g->addWidget(new QLabel(QStringLiteral("Nozzle radius"), box), 4, 0);
    m_emRadius = makeSpin(box, 0.0, 5.0, 0.01); m_emRadius->setValue(0.03);
    g->addWidget(m_emRadius, 4, 1);
    g->addWidget(new QLabel(QStringLiteral("Lifetime (s, 0=inf)"), box), 5, 0);
    m_emLifetime = makeSpin(box, 0.0, 600.0, 1.0, 1);
    g->addWidget(m_emLifetime, 5, 1);

    auto* line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    g->addWidget(line, 6, 0, 1, 2);

    m_volEnabled = new QCheckBox(QStringLiteral("Fluid Volume (pre-filled box)"), box);
    g->addWidget(m_volEnabled, 7, 0, 1, 2);
    g->addWidget(new QLabel(QStringLiteral("Half X"), box), 8, 0);
    m_volX = makeSpin(box, 0.01, 50.0, 0.05); m_volX->setValue(0.25);
    g->addWidget(m_volX, 8, 1);
    g->addWidget(new QLabel(QStringLiteral("Half Y"), box), 9, 0);
    m_volY = makeSpin(box, 0.01, 50.0, 0.05); m_volY->setValue(0.25);
    g->addWidget(m_volY, 9, 1);
    g->addWidget(new QLabel(QStringLiteral("Half Z"), box), 10, 0);
    m_volZ = makeSpin(box, 0.01, 50.0, 0.05); m_volZ->setValue(0.25);
    g->addWidget(m_volZ, 10, 1);
    g->addWidget(new QLabel(QStringLiteral("Particle spacing"), box), 11, 0);
    m_volSpacing = makeSpin(box, 0.02, 0.5, 0.005); m_volSpacing->setValue(0.05);
    g->addWidget(m_volSpacing, 11, 1);

    connect(m_emEnabled, &QCheckBox::toggled, this, &PhysicsPropertiesWidget::applyEmitter);
    for (auto* s : { m_emRate, m_emSpeed, m_emSpread, m_emRadius, m_emLifetime })
        connect(s, &QDoubleSpinBox::valueChanged, this, &PhysicsPropertiesWidget::applyEmitter);
    connect(m_volEnabled, &QCheckBox::toggled, this, &PhysicsPropertiesWidget::applyVolume);
    for (auto* s : { m_volX, m_volY, m_volZ, m_volSpacing })
        connect(s, &QDoubleSpinBox::valueChanged, this, &PhysicsPropertiesWidget::applyVolume);
    return box;
}

// ===========================================================================
void PhysicsPropertiesWidget::setEntity(entt::entity entity)
{
    m_entity = entity;
    rebuildFromEntity();
}

void PhysicsPropertiesWidget::rebuildFromEntity()
{
    m_updating = true;
    auto& reg = m_scene->getRegistry();
    const bool valid = m_entity != entt::null && reg.valid(m_entity);
    setEnabled(valid);

    if (!valid) {
        m_objectName->setText(QStringLiteral("No selection"));
        m_updating = false;
        return;
    }

    const auto* tag = reg.try_get<TagComponent>(m_entity);
    m_objectName->setText(tag && !tag->tag.empty()
        ? QString::fromStdString(tag->tag)
        : QStringLiteral("Entity %1").arg(uint32_t(m_entity)));

    // rigid body
    if (const auto* rb = reg.try_get<RigidBodyComponent>(m_entity)) {
        m_rbEnabled->setChecked(true);
        m_rbType->setCurrentIndex(int(rb->bodyType));
        m_rbMass->setValue(rb->mass);
        m_rbLinDamp->setValue(rb->linearDamping);
        m_rbAngDamp->setValue(rb->angularDamping);
    }
    else {
        m_rbEnabled->setChecked(false);
    }

    // collider (priority order matches the simulation backend)
    if (const auto* box = reg.try_get<BoxCollider>(m_entity)) {
        m_colShape->setCurrentIndex(1);
        m_colStack->setCurrentIndex(1);
        m_colBoxX->setValue(box->halfExtents.x);
        m_colBoxY->setValue(box->halfExtents.y);
        m_colBoxZ->setValue(box->halfExtents.z);
        m_rbFriction->setValue(box->material.dynamicFriction);
        m_rbRestitution->setValue(box->material.restitution);
    }
    else if (const auto* sph = reg.try_get<SphereCollider>(m_entity)) {
        m_colShape->setCurrentIndex(2);
        m_colStack->setCurrentIndex(2);
        m_colSphereRadius->setValue(sph->radius);
        m_rbFriction->setValue(sph->material.dynamicFriction);
        m_rbRestitution->setValue(sph->material.restitution);
    }
    else if (const auto* cap = reg.try_get<CapsuleCollider>(m_entity)) {
        m_colShape->setCurrentIndex(3);
        m_colStack->setCurrentIndex(3);
        m_colCapRadius->setValue(cap->radius);
        m_colCapHeight->setValue(cap->height);
    }
    else if (reg.any_of<ConvexMeshCollider>(m_entity)) {
        m_colShape->setCurrentIndex(4);
        m_colStack->setCurrentIndex(0);
    }
    else {
        m_colShape->setCurrentIndex(0);
        m_colStack->setCurrentIndex(0);
    }

    // fluid
    if (const auto* em = reg.try_get<FluidEmitterComponent>(m_entity)) {
        m_emEnabled->setChecked(true);
        m_emRate->setValue(em->ratePerSecond);
        m_emSpeed->setValue(em->initialSpeed);
        m_emSpread->setValue(em->spreadDegrees);
        m_emRadius->setValue(em->emitterRadius);
        m_emLifetime->setValue(em->particleLifetime);
    }
    else {
        m_emEnabled->setChecked(false);
    }
    if (const auto* vol = reg.try_get<FluidVolumeComponent>(m_entity)) {
        m_volEnabled->setChecked(true);
        m_volX->setValue(vol->halfExtents.x);
        m_volY->setValue(vol->halfExtents.y);
        m_volZ->setValue(vol->halfExtents.z);
        m_volSpacing->setValue(vol->particleSpacing);
    }
    else {
        m_volEnabled->setChecked(false);
    }

    m_updating = false;
}

void PhysicsPropertiesWidget::applyRigidBody()
{
    if (m_updating || m_entity == entt::null) return;
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(m_entity)) return;

    if (m_rbEnabled->isChecked()) {
        auto& rb = reg.get_or_emplace<RigidBodyComponent>(m_entity);
        rb.bodyType = RigidBodyComponent::BodyType(m_rbType->currentIndex());
        rb.mass = float(m_rbMass->value());
        rb.linearDamping = float(m_rbLinDamp->value());
        rb.angularDamping = float(m_rbAngDamp->value());
    }
    else {
        reg.remove<RigidBodyComponent>(m_entity);
    }
    applyCollider(); // material values live on the collider
}

void PhysicsPropertiesWidget::applyCollider()
{
    if (m_updating || m_entity == entt::null) return;
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(m_entity)) return;

    PhysicsMaterial mat;
    mat.staticFriction = mat.dynamicFriction = float(m_rbFriction->value());
    mat.restitution = float(m_rbRestitution->value());

    reg.remove<BoxCollider>(m_entity);
    reg.remove<SphereCollider>(m_entity);
    reg.remove<CapsuleCollider>(m_entity);
    reg.remove<ConvexMeshCollider>(m_entity);

    switch (m_colShape->currentIndex()) {
    case 1: { // box
        auto& c = reg.emplace<BoxCollider>(m_entity);
        c.halfExtents = { float(m_colBoxX->value()), float(m_colBoxY->value()), float(m_colBoxZ->value()) };
        c.material = mat;
        break;
    }
    case 2: { // sphere
        auto& c = reg.emplace<SphereCollider>(m_entity);
        c.radius = float(m_colSphereRadius->value());
        c.material = mat;
        break;
    }
    case 3: { // capsule
        auto& c = reg.emplace<CapsuleCollider>(m_entity);
        c.radius = float(m_colCapRadius->value());
        c.height = float(m_colCapHeight->value());
        c.material = mat;
        break;
    }
    case 4: { // convex mesh
        auto& c = reg.emplace<ConvexMeshCollider>(m_entity);
        c.material = mat;
        break;
    }
    default: break; // none: simulation falls back to mesh AABB box
    }
}

void PhysicsPropertiesWidget::applyEmitter()
{
    if (m_updating || m_entity == entt::null) return;
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(m_entity)) return;

    if (m_emEnabled->isChecked()) {
        auto& em = reg.get_or_emplace<FluidEmitterComponent>(m_entity);
        em.enabled = true;
        em.ratePerSecond = float(m_emRate->value());
        em.initialSpeed = float(m_emSpeed->value());
        em.spreadDegrees = float(m_emSpread->value());
        em.emitterRadius = float(m_emRadius->value());
        em.particleLifetime = float(m_emLifetime->value());
    }
    else {
        reg.remove<FluidEmitterComponent>(m_entity);
    }
}

void PhysicsPropertiesWidget::applyVolume()
{
    if (m_updating || m_entity == entt::null) return;
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(m_entity)) return;

    if (m_volEnabled->isChecked()) {
        auto& vol = reg.get_or_emplace<FluidVolumeComponent>(m_entity);
        vol.halfExtents = { float(m_volX->value()), float(m_volY->value()), float(m_volZ->value()) };
        vol.particleSpacing = float(m_volSpacing->value());
    }
    else {
        reg.remove<FluidVolumeComponent>(m_entity);
    }
}
