#pragma once
// ===========================================================================
// MATERIAL EDITOR PANEL -- custom PBR material authoring with a WHOLE-BODY vs THIS-FACE toggle.
// Color (albedo) + metalness + roughness sliders; "Whole Body" writes the selected entity's
// MaterialComponent, "This Face" arms a face-pick mode and paints just the clicked B-Rep face via
// krs::facemat (overlay sub-mesh). Save the current settings as a reusable .kmaterial library part.
// ===========================================================================
#include <QWidget>
#include <glm/glm.hpp>
#include "IMenu.hpp"

class Scene;
class QLabel;
class QPushButton;
class QSlider;
class QRadioButton;

// A plain PBR triple, so the header need not include components.hpp for krs::FaceMaterial.
struct FaceMaterialFwd { glm::vec3 albedo = glm::vec3(0.8f); float metallic = 0.0f; float roughness = 0.5f; };

class MaterialEditorPanel : public QWidget, public IMenu
{
    Q_OBJECT
public:
    explicit MaterialEditorPanel(Scene* scene, QWidget* parent = nullptr);
    ~MaterialEditorPanel() override = default;

    void initializeFresh() override        {}
    void initializeFromDatabase() override {}
    void shutdownAndSave() override        { disarmFacePick(); }
    QWidget* widget() override             { return this; }

private slots:
    void onPickColor();
    void onModeChanged();
    void onApply();
    void onSaveAsPart();

private:
    void initializeUI();
    void updateSwatch();
    void armFacePick();
    void disarmFacePick();
    FaceMaterialFwd currentMaterial() const;   // (fwd typedef below)

    Scene* m_scene = nullptr;
    glm::vec3 m_albedo = glm::vec3(0.8f);
    QLabel*       m_swatch = nullptr;
    QSlider*      m_metal = nullptr;
    QSlider*      m_rough = nullptr;
    QLabel*       m_metalVal = nullptr;
    QLabel*       m_roughVal = nullptr;
    QRadioButton* m_wholeBody = nullptr;
    QRadioButton* m_thisFace = nullptr;
    QPushButton*  m_pickFaceBtn = nullptr;
    QPushButton*  m_applyBtn = nullptr;
    QLabel*       m_status = nullptr;
    bool m_facePickArmed = false;
};
