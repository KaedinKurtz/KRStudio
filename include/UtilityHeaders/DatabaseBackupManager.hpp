#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QMutex>
#include <QQueue>
#include <memory>
#include <functional>

namespace db {

struct BackupConfig {
    QString backupDirectory = "backups/";
    bool enableCompression = true;
    bool enableEncryption = false;
    QString encryptionKey;
    int compressionLevel = 6; // 0-9, higher = better compression but slower
    bool incrementalBackup = true;
    int maxBackups = 10;
    qint64 maxBackupSize = 1024 * 1024 * 1024; // 1GB
    bool autoBackup = true;
    int autoBackupInterval = 3600000; // 1 hour in milliseconds
    bool verifyBackup = true;
    bool backupWAL = true;
    QString backupFormat = "sqlite"; // sqlite, sql, custom
};

struct BackupInfo {
    QString name;
    QString path;
    QDateTime createdAt;
    qint64 size;
    bool compressed;
    bool encrypted;
    bool verified;
    QString checksum;
    QString description;
    QVariantMap metadata;
};

struct BackupProgress {
    QString operation; // "backup", "restore", "verify"
    int progress; // 0-100
    QString currentFile;
    qint64 bytesProcessed;
    qint64 totalBytes;
    QDateTime startTime;
    QDateTime estimatedCompletion;
    QString status;
};

class DatabaseBackupManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseBackupManager(QObject* parent = nullptr);
    ~DatabaseBackupManager();

    // Configuration
    void setConfig(const BackupConfig& config);
    BackupConfig getConfig() const { return m_config; }

    // Backup operations
    bool createBackup(const QString& name = "", const QString& description = "");
    bool createIncrementalBackup(const QString& baseBackupName);
    bool createFullBackup();
    bool createHotBackup(); // Online backup without stopping database

    // Restore operations
    bool restoreBackup(const QString& backupName, bool verify = true);
    bool restoreBackupFromFile(const QString& backupPath, bool verify = true);
    bool restoreToPointInTime(const QDateTime& pointInTime);

    // Backup management
    QStringList listBackups();
    BackupInfo getBackupInfo(const QString& backupName);
    bool deleteBackup(const QString& backupName);
    bool verifyBackup(const QString& backupName);
    bool repairBackup(const QString& backupName);

    // Compression and encryption
    bool compressBackup(const QString& backupName);
    bool decompressBackup(const QString& backupName);
    bool encryptBackup(const QString& backupName, const QString& key);
    bool decryptBackup(const QString& backupName, const QString& key);

    // Scheduling and automation
    void enableAutoBackup(bool enable);
    void setAutoBackupInterval(int milliseconds);
    void scheduleBackup(const QDateTime& time, const QString& description = "");
    void cancelScheduledBackup();

    // Monitoring and statistics
    BackupProgress getCurrentProgress() const;
    QList<BackupInfo> getBackupHistory(int limit = 50);
    qint64 getTotalBackupSize() const;
    int getBackupCount() const;
    QDateTime getLastBackupTime() const;
    bool isBackupInProgress() const;

    // Asynchronous operations
    QFuture<bool> createBackupAsync(const QString& name = "", const QString& description = "");
    QFuture<bool> restoreBackupAsync(const QString& backupName, bool verify = true);
    QFuture<bool> verifyBackupAsync(const QString& backupName);
    QFuture<QList<BackupInfo>> listBackupsAsync();

    // Utility functions
    QString generateBackupName() const;
    QString calculateChecksum(const QString& filePath);
    bool validateBackupFile(const QString& filePath);
    void cleanupOldBackups();
    void exportBackupList(const QString& filePath);

signals:
    void backupStarted(const QString& backupName);
    void backupCompleted(const QString& backupName, bool success);
    void backupProgress(const BackupProgress& progress);
    void restoreStarted(const QString& backupName);
    void restoreCompleted(const QString& backupName, bool success);
    void restoreProgress(const BackupProgress& progress);
    void verificationStarted(const QString& backupName);
    void verificationCompleted(const QString& backupName, bool success);
    void autoBackupTriggered();
    void backupError(const QString& error);
    void backupWarning(const QString& warning);

private slots:
    void onAutoBackupTimer();
    void onBackupCompleted();
    void onRestoreCompleted();
    void onVerificationCompleted();
    void onCleanupTimer();

private:
    // Private implementation
    bool performBackup(const QString& backupPath, const QString& description);
    bool performRestore(const QString& backupPath);
    bool performVerification(const QString& backupPath);
    bool performCompression(const QString& sourcePath, const QString& destPath);
    bool performDecompression(const QString& sourcePath, const QString& destPath);
    bool performEncryption(const QString& sourcePath, const QString& destPath, const QString& key);
    bool performDecryption(const QString& sourcePath, const QString& destPath, const QString& key);

    // Helper functions
    QString getBackupPath(const QString& backupName) const;
    QString getBackupMetadataPath(const QString& backupName) const;
    bool saveBackupMetadata(const BackupInfo& info);
    bool loadBackupMetadata(const QString& backupName, BackupInfo& info);
    bool updateBackupProgress(const BackupProgress& progress);
    void logBackupEvent(const QString& event, const QVariantMap& data = {});

    // Member variables
    BackupConfig m_config;
    QTimer m_autoBackupTimer;
    QTimer m_cleanupTimer;
    QDateTime m_lastBackupTime;
    QDateTime m_nextScheduledBackup;
    
    QQueue<BackupInfo> m_backupHistory;
    BackupProgress m_currentProgress;
    bool m_backupInProgress = false;
    bool m_restoreInProgress = false;
    
    QMutex m_mutex;
    QMutex m_progressMutex;
    
    // Async operation watchers
    QList<QFutureWatcher<bool>*> m_backupWatchers;
    QList<QFutureWatcher<bool>*> m_restoreWatchers;
    QList<QFutureWatcher<bool>*> m_verifyWatchers;
    QList<QFutureWatcher<QList<BackupInfo>>*> m_listWatchers;
    
    // Callback functions
    std::function<void(const BackupProgress&)> m_progressCallback;
    std::function<void(const QString&, bool)> m_completionCallback;
    std::function<void(const QString&)> m_errorCallback;
};

} // namespace db 