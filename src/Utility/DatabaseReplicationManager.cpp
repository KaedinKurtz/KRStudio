#include "DatabaseReplicationManager.hpp"
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>

using namespace db;

DatabaseReplicationManager::DatabaseReplicationManager(QObject* parent)
    : QObject(parent)
{
    m_status.enabled = false;
    m_status.lastSyncTime = "Never";
    m_status.lastError = "";
    m_status.mode = m_config.replicationMode;
}

DatabaseReplicationManager::~DatabaseReplicationManager() = default;

bool DatabaseReplicationManager::enableReplication(const QString& masterUrl) {
    QMutexLocker locker(&m_mutex);
    if (masterUrl.isEmpty()) {
        emit replicationError("Master URL is empty");
        return false;
    }
    m_config.masterUrl = masterUrl;
    m_config.enabled = true;
    m_status.enabled = true;
    emit replicationEnabled(true);
    return true;
}

bool DatabaseReplicationManager::disableReplication() {
    QMutexLocker locker(&m_mutex);
    m_config.enabled = false;
    m_status.enabled = false;
    emit replicationEnabled(false);
    return true;
}

bool DatabaseReplicationManager::isReplicationEnabled(){
    QMutexLocker locker(&m_mutex);
    return m_config.enabled;
}

bool DatabaseReplicationManager::syncWithMaster() {
    QMutexLocker locker(&m_mutex);
    if (!m_config.enabled || m_config.masterUrl.isEmpty()) {
        emit replicationError("Replication not enabled or master URL not set");
        return false;
    }
    // Assume masterUrl is a file path for file-based replication
    QString localDbPath = QSqlDatabase::database().databaseName();
    QString masterDbPath = m_config.masterUrl;
    if (!QFile::exists(masterDbPath)) {
        emit replicationError("Master database file does not exist: " + masterDbPath);
        m_status.lastError = "Master DB missing";
        return false;
    }
    // Close local DB before copying
    QSqlDatabase::database().close();
    if (!QFile::copy(masterDbPath, localDbPath)) {
        emit replicationError("Failed to copy master DB to local: " + localDbPath);
        m_status.lastError = "Copy failed";
        return false;
    }
    m_status.lastSyncTime = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_status.lastError = "";
    emit replicationSyncCompleted(true);
    return true;
}

ReplicationStatus DatabaseReplicationManager::getStatus(){
    QMutexLocker locker(&m_mutex);
    return m_status;
}

void DatabaseReplicationManager::setConfig(const ReplicationConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_config = config;
    m_status.mode = config.replicationMode;
}

QFuture<bool> DatabaseReplicationManager::syncWithMasterAsync() {
    return QtConcurrent::run([=]() { return syncWithMaster(); });
} 