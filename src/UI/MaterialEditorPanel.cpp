#include "MaterialEditorPanel.hpp"

#include "Scene.hpp"
#include "components.hpp"
#include "SelectionService.hpp"
#include "FaceMaterial.hpp"
#include "KParts.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QRadioButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog>
#include <QFrame>
#include <QStandardPaths>
#include <QDir>

MaterialEditorPanel::MaterialEditorPanel(Scene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    setStyleSheet(QStringLiteral(
        "QWidget{background:#2c313a;color:#e6e6e6;}"
        "QGroupBox{border:1px solid #4a5260;margin-top:6px;}"
        "QPushButton{background:#262b33;border:1px solid #4a5260;padding:4px;}"
        "QPushButton:hover{background:#323844;}"
        "QPushButton:checked{background:#d4af34;color:#222;}"
        "QSlider::groove:horizontal{height:4px;background:#555;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:#d4af34;border-radius:6px;}"));
    initializeUI();
}

void MaterialEditorPanel::initializeUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel(QStringLiteral("Material Editor"), this);
    title->setStyleSheet(QStringLiteral("font-weight:bold;padding:2px;"));
    layout->addWidget(title);

    // --- Target mode ---
    auto* modeBox = new QGroupBox(QStringLiteral("Apply to"), this);
    auto* ml = new QVBoxLayout(modeBox);
    m_wholeBody = new QRadioButton(QStringLiteral("Whole body"), modeBox);
    m_wholeBody->setObjectName(QStringLiteral("meWholeBody"));
    m_wholeBody->setChecked(true);
    m_thisFace = new QRadioButton(QStringLiteral("This face"), modeBox);
    m_thisFace->setObjectName(QStringLiteral("meThisFace"));
    auto* grp = new QButtonGroup(this);
    grp->addButton(m_wholeBody); grp->addButton(m_thisFace);
    ml->addWidget(m_wholeBody); ml->addWidget(m_thisFace);
    m_pickFaceBtn = new QPushButton(QStringLiteral("Pick Face (click a face in the viewport)"), modeBox);
    m_pickFaceBtn->setObjectName(QStringLiteral("mePickFaceButton"));
    m_pickFaceBtn->setCheckable(true);
    m_pickFaceBtn->setEnabled(false);
    ml->addWidget(m_pickFaceBtn);
    layout->addWidget(modeBox);
    connect(m_wholeBody, &QRadioButton::toggled, this, &MaterialEditorPanel::onModeChanged);
    connect(m_pickFaceBtn, &QPushButton::toggled, this, [this](bool on) { if (on) armFacePick(); else disarmFacePick(); });

    // --- Color ---
    auto* colorBox = new QGroupBox(QStringLiteral("Color (albedo)"), this);
    auto* cl = new QHBoxLayout(colorBox);
    m_swatch = new QLabel(colorBox);
    m_swatch->setObjectName(QStringLiteral("meSwatch"));
    m_swatch->setFixedSize(40, 24);
    m_swatch->setFrameShape(QFrame::Box);
    auto* pickColor = new QPushButton(QStringLiteral("Choose..."), colorBox);
    pickColor->setObjectName(QStringLiteral("meColorButton"));
    cl->addWidget(m_swatch); cl->addWidget(pickColor, 1);
    layout->addWidget(colorBox);
    connect(pickColor, &QPushButton::clicked, this, &MaterialEditorPanel::onPickColor);

    // --- PBR sliders (metalness / roughness) ---
    auto* pbrBox = new QGroupBox(QStringLiteral("Surface"), this);
    auto* pf = new QFormLayout(pbrBox);
    auto mkSlider = [&](const char* obj, int def) {
        auto* s = new QSlider(Qt::Horizontal, pbrBox);
        s->setObjectName(QString::fromLatin1(obj));
        s->setRange(0, 100); s->setValue(def);
        return s;
    };
    m_metal = mkSlider("meMetalSlider", 0);
    m_rough = mkSlider("meRoughSlider", 50);
    m_metalVal = new QLabel(QStringLiteral("0.00"), pbrBox);
    m_roughVal = new QLabel(QStringLiteral("0.50"), pbrBox);
    auto row = [&](QSlider* s, QLabel* v) { auto* w = new QWidget(pbrBox); auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0,0,0,0); h->addWidget(s, 1); h->addWidget(v); return w; };
    pf->addRow(QStringLiteral("Metalness"), row(m_metal, m_metalVal));
    pf->addRow(QStringLiteral("Roughness"), row(m_rough, m_roughVal));
    layout->addWidget(pbrBox);
    connect(m_metal, &QSlider::valueChanged, this, [this](int v){ m_metalVal->setText(QString::number(v/100.0,'f',2)); });
    connect(m_rough, &QSlider::valueChanged, this, [this](int v){ m_roughVal->setText(QString::number(v/100.0,'f',2)); });

    m_applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    m_applyBtn->setObjectName(QStringLiteral("meApplyButton"));
    layout->addWidget(m_applyBtn);
    connect(m_applyBtn, &QPushButton::clicked, this, &MaterialEditorPanel::onApply);

    auto* saveBtn = new QPushButton(QStringLiteral("Save as .kmaterial (library part)"), this);
    saveBtn->setObjectName(QStringLiteral("meSavePartButton"));
    layout->addWidget(saveBtn);
    connect(saveBtn, &QPushButton::clicked, this, &MaterialEditorPanel::onSaveAsPart);

    m_status = new QLabel(QStringLiteral("Select a body, choose Whole body / This face, then Apply."), this);
    m_status->setObjectName(QStringLiteral("meStatusLabel"));
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#9fb4cc;padding:2px;"));
    layout->addWidget(m_status);
    layout->addStretch(1);

    updateSwatch();
}

FaceMaterialFwd MaterialEditorPanel::currentMaterial() const {
    return { m_albedo, float(m_metal->value()) / 100.0f, float(m_rough->value()) / 100.0f };
}

void MaterialEditorPanel::updateSwatch() {
    if (!m_swatch) return;
    m_swatch->setStyleSheet(QStringLiteral("background:rgb(%1,%2,%3);border:1px solid #888;")
        .arg(int(m_albedo.r*255)).arg(int(m_albedo.g*255)).arg(int(m_albedo.b*255)));
}

void MaterialEditorPanel::onPickColor() {
    const QColor init = QColor::fromRgbF(m_albedo.r, m_albedo.g, m_albedo.b);
    const QColor c = QColorDialog::getColor(init, this, QStringLiteral("Albedo color"));
    if (!c.isValid()) return;
    m_albedo = glm::vec3(float(c.redF()), float(c.greenF()), float(c.blueF()));
    updateSwatch();
}

void MaterialEditorPanel::onModeChanged() {
    const bool face = m_thisFace && m_thisFace->isChecked();
    if (m_pickFaceBtn) m_pickFaceBtn->setEnabled(face);
    if (!face) disarmFacePick();
}

void MaterialEditorPanel::armFacePick() {
    if (!m_scene) return;
    m_facePickArmed = true;
    // Face-paint selection: any face type, accumulate off (single most-recent). Reuses the feature
    // pick path; we read the most recent valid selection at Apply time.
    if (auto* st = m_scene->getRegistry().ctx().find<krs::sel::SelectionState>()) {
        st->enabled = true; st->fifoTwoBores = false;
    }
    if (m_status) m_status->setText(QStringLiteral("Face-pick armed: click a face in the viewport, then Apply."));
}

void MaterialEditorPanel::disarmFacePick() {
    m_facePickArmed = false;
    if (m_pickFaceBtn && m_pickFaceBtn->isChecked()) { const QSignalBlocker b(m_pickFaceBtn); m_pickFaceBtn->setChecked(false); }
    // Feature picking is a triggered mode now (default OFF) -- release it, unless another mode
    // (choose-bores / measure) armed it for its own workflow.
    if (m_scene)
        if (auto* st = m_scene->getRegistry().ctx().find<krs::sel::SelectionState>())
            if (!st->fifoTwoBores && !st->measureMode) st->enabled = false;
}

void MaterialEditorPanel::onApply() {
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    // The target body = the current SelectedComponent entity.
    entt::entity target = entt::null;
    for (auto e : reg.view<SelectedComponent>()) { target = e; break; }
    if (target == entt::null) { if (m_status) m_status->setText(QStringLiteral("Select a body first (click it in the viewport).")); return; }

    const FaceMaterialFwd mf = currentMaterial();
    const FaceMaterial fm{ mf.albedo, mf.metallic, mf.roughness };

    if (m_thisFace && m_thisFace->isChecked()) {
        // Read the most-recent valid feature selection -> (entity, faceId).
        auto* sel = reg.ctx().find<krs::sel::SelectionState>();
        const krs::sel::Selection* pick = nullptr;
        if (sel) for (auto it = sel->selected.rbegin(); it != sel->selected.rend(); ++it)
            if (it->valid && it->faceId >= 0) { pick = &*it; break; }
        if (!pick) { if (m_status) m_status->setText(QStringLiteral("No face picked. Arm Pick Face, click a face, then Apply.")); return; }
        if (!krs::facemat::applyToFace(reg, pick->entity, pick->faceId, fm)) {
            if (m_status) m_status->setText(QStringLiteral("That entity has no B-Rep faces to paint."));
            return;
        }
        const int n = krs::facemat::rebuildFaceOverlays(*m_scene, pick->entity);
        if (m_status) m_status->setText(QStringLiteral("Painted face %1 (%2 painted face(s) on this body).").arg(pick->faceId).arg(n));
    } else {
        krs::facemat::applyToBody(reg, target, fm);
        if (m_status) m_status->setText(QStringLiteral("Applied to the whole body."));
    }
}

void MaterialEditorPanel::onSaveAsPart() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save material"),
        QStringLiteral("Material name:"), QLineEdit::Normal, QStringLiteral("My Material"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("parts"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name + QStringLiteral(".kmaterial"));
    const FaceMaterialFwd mf = currentMaterial();
    QJsonObject fields;
    fields["albedo"] = QJsonArray{ mf.albedo.r, mf.albedo.g, mf.albedo.b };
    fields["metallic"] = mf.metallic; fields["roughness"] = mf.roughness;
    const QString id = krs::parts::writePartFile(path, krs::parts::PartType::Material, name, QString(), fields);
    if (m_status) m_status->setText(id.isEmpty()
        ? QStringLiteral("Failed to write the material part.")
        : QStringLiteral("Saved %1 to the user parts library. Rescan the Manufacturer Parts panel to see it.").arg(name));
}
