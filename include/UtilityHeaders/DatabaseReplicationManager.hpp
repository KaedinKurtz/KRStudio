#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>
#include <QTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QMutex>
#include <memory>
#include <functional>

namespace db {

struct ReplicationConfig {
    QString masterUrl;
    bool enabled = false;
    int syncInterval = 60000; // 1 minute
    bool autoSync = true;
    bool conflictResolution = true;
    QString replicationMode = "async"; // async, sync
};

struct ReplicationStatus {
    bool enabled;
    QString lastSyncTime;
    QString lastError;
    QString mode;
    QVariantMap metadata;
};

class DatabaseReplicationManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseReplicationManager(QObject* parent = nullptr);
    ~DatabaseReplicationManager();

    // Replication operations
    bool enableReplication(const QString& masterUrl);
    bool disableReplication();
    bool isReplicationEnabled();
    bool syncWithMaster();
    ReplicationStatus getStatus();
    void setConfig(const ReplicationConfig& config);
    ReplicationConfig getConfig() const { return m_config; }

    // Async operations
    QFuture<bool> syncWithMasterAsync();

signals:
    void replicationEnabled(bool enabled);
    void replicationSyncCompleted(bool success);
    void replicationError(const QString& error);
    void replicationWarning(const QString& warning);

private:
    ReplicationConfig m_config;
    ReplicationStatus m_status;
    QMutex m_mutex;
    QTimer m_syncTimer;
};

} // namespace db 