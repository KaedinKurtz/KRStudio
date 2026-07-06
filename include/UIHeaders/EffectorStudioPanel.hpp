#pragma once
// ===========================================================================
// EFFECTOR STUDIO -- the .kee authoring + attach workspace (EE stack v1).
//
// The authoring model: an end effector IS a small robot, so its bodies and
// joints are authored with the EXISTING gated pipeline (import STEP, define
// joints in the Robot Builder -- the ctx RobotGraph being edited). This studio
// layers the EFFECTOR SEMANTICS on top and packages the whole thing:
//   1. FACE ROLES  -- paint Grip / KeepOut / Interaction onto the current
//      face selections (durable faceKey keyed, FaceRoleComponent);
//   2. INTERFACE MATE -- designate one placed MateConnector on the base body
//      as the flange mate (the .kee's "native interfacing mate");
//   3. TCP -- capture tool-centre-point frames from the last snap click;
//   4. ACTUATION -- the jaw drive joint (by name), open/closed q, mimic;
//   5. VALIDATE + SAVE/LOAD .kee (krs::kee, gated codec);
//   6. ATTACH -- mate a loaded/current effector onto ANY robot connector by
//      its PERSISTENT id ("with end effector 2" resolves connector #2):
//      rigid-snap all effector bodies so the interface frame mates the target
//      (Z anti-aligned, X aligned), and register the attachment in the ctx
//      AttachedEffectors registry the skill layer reads.
// ===========================================================================
#include <QWidget>
#include <entt/entt.hpp>
#include "KEE.hpp"

class Scene;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QListWidget;
class QLabel;
class QTimer;

namespace krs::ee {
// ctx registry of live attachments (the "with end effector N" identity).
struct Attachment {
    int robotId = -1;
    entt::entity flangeBody = entt::null;    // the robot body carrying the connector
    std::uint32_t connectorId = 0;           // PERSISTENT MateConnector id on that body
    QString effectorName;
    std::vector<entt::entity> effectorBodies;
};
struct AttachedEffectors { std::vector<Attachment> list; };
} // namespace krs::ee

class EffectorStudioPanel : public QWidget
{
    Q_OBJECT
public:
    explicit EffectorStudioPanel(Scene* scene, QWidget* parent = nullptr);

private slots:
    void onPaintRole();
    void onClearRole();
    void onSetInterfaceMate();
    void onCaptureTcp();
    void onValidate();
    void onSaveKee();
    void onLoadKee();
    void onAttach();
    void onDetach();
    void onTick();               // combos refresh + status

private:
    // Assemble an EffectorDoc from the LIVE editing state: the ctx RobotGraph +
    // FaceRoleComponent/MateConnectorComponent of its body entities + the panel fields.
    bool assembleDoc(krs::kee::EffectorDoc& out, QString* err);
    void refreshCombos();

    Scene* m_scene = nullptr;
    krs::kee::EffectorDoc m_loaded;          // last loaded .kee (for Attach of a loaded doc)
    bool m_haveLoaded = false;

    QLineEdit*      m_name = nullptr;
    QComboBox*      m_roleCombo = nullptr;
    QComboBox*      m_ifaceCombo = nullptr;  // connectors on the edited graph's bodies
    QLineEdit*      m_tcpName = nullptr;
    QComboBox*      m_driveCombo = nullptr;  // graph joints by name
    QDoubleSpinBox* m_openQ = nullptr;
    QDoubleSpinBox* m_closedQ = nullptr;
    QComboBox*      m_mimicCombo = nullptr;
    QDoubleSpinBox* m_mimicRatio = nullptr;
    QDoubleSpinBox* m_gripForce = nullptr;
    QComboBox*      m_attachCombo = nullptr; // robot connectors (persistent ids) to attach onto
    QListWidget*    m_validateList = nullptr;
    QLabel*         m_status = nullptr;
    QLabel*         m_tcpLabel = nullptr;
    QTimer*         m_tick = nullptr;
    std::vector<krs::kee::TcpFrame> m_tcps;  // captured this session (saved into the doc)
    QString m_combosSig;
};
