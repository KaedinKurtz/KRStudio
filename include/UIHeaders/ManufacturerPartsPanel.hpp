#pragma once
// ===========================================================================
// MANUFACTURER PARTS PANEL -- the docked browser over krs::parts::library(). Lists indexed
// components (.kmotor/.kactuator/.kcamera/.kimu/...) grouped by type; a drag emits the part's PATH
// as "application/x-krstudio-asset" (the existing viewport/joint drop mime) so a part can be
// dropped onto a joint (actuator) or the scene. A Repository section shows the local store status
// and the cloud-contribution stub (Publish / Sync), honestly reporting offline.
// ===========================================================================
#include <QWidget>
#include <memory>
#include "IMenu.hpp"

class Scene;
class QTreeWidget;
class QLabel;
class QPushButton;
class QLineEdit;

namespace krs::parts { class PartRepository; }

class ManufacturerPartsPanel : public QWidget, public IMenu
{
    Q_OBJECT
public:
    explicit ManufacturerPartsPanel(Scene* scene, QWidget* parent = nullptr);
    ~ManufacturerPartsPanel() override;

    void initializeFresh() override        { rescan(); }
    void initializeFromDatabase() override { rescan(); }
    void shutdownAndSave() override        {}
    QWidget* widget() override             { return this; }

    // Point the library at a scene folder's search path (scene/parts -> project -> user) + rescan.
    void setSceneDir(const QString& sceneDir);

public slots:
    void rescan();          // re-index the library + repopulate the tree
    void onPublishSelected(); // contribute the selected part to the repository
    void onSync();          // sync local <-> remote (stub reports offline)

private:
    void initializeUI();
    void refreshRepoStatus();

    Scene* m_scene = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel*      m_status = nullptr;
    QLabel*      m_repoStatus = nullptr;
    QLineEdit*   m_remoteUrl = nullptr;
    QLineEdit*   m_remoteKey = nullptr;
    QPushButton* m_publishBtn = nullptr;
    QPushButton* m_syncBtn = nullptr;
    QPushButton* m_rescanBtn = nullptr;
    std::unique_ptr<krs::parts::PartRepository> m_repo;   // local store
    QString m_sceneDir;
};
