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
#include <unordered_map>

namespace db {

struct MigrationConfig {
    QString migrationsDirectory = "migrations/";
    bool autoMigrate = true;
    bool backupBeforeMigration = true;
    bool validateAfterMigration = true;
    int maxMigrationRetries = 3;
    int migrationTimeout = 300000; // 5 minutes
    bool enableRollback = true;
    bool logMigrationDetails = true;
    QString migrationTableName = "schema_migrations";
};

struct MigrationInfo {
    QString version;
    QString name;
    QString description;
    QDateTime appliedAt;
    qint64 executionTime;
    bool success;
    QString errorMessage;
    QVariantMap metadata;
};

struct MigrationFile {
    QString path;
    QString version;
    QString name;
    QString description;
    QString upSql;
    QString downSql;
    QDateTime createdAt;
    bool isValid;
    QString validationError;
};

struct MigrationProgress {
    QString currentMigration;
    int currentStep;
    int totalSteps;
    int progress; // 0-100
    QString status;
    QDateTime startTime;
    QDateTime estimatedCompletion;
    QString currentOperation;
};

class DatabaseMigrationManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseMigrationManager(QObject* parent = nullptr);
    ~DatabaseMigrationManager();

    // Configuration
    void setConfig(const MigrationConfig& config);
    MigrationConfig getConfig() const { return m_config; }

    // Migration operations
    bool migrate();
    bool migrateToVersion(const QString& targetVersion);
    bool migrateToLatest();
    bool rollback();
    bool rollbackToVersion(const QString& targetVersion);
    bool rollbackLast();

    // Migration management
    QStringList getAvailableMigrations();
    QStringList getAppliedMigrations();
    QString getCurrentVersion() const;
    QString getLatestVersion() const;
    bool isUpToDate() const;
    bool hasPendingMigrations() const;

    // Migration file operations
    bool createMigration(const QString& name, const QString& description = "");
    bool validateMigration(const QString& version);
    bool deleteMigration(const QString& version);
    bool editMigration(const QString& version, const QString& upSql, const QString& downSql);

    // Migration status and history
    MigrationInfo getMigrationInfo(const QString& version);
    QList<MigrationInfo> getMigrationHistory(int limit = 50);
    MigrationProgress getCurrentProgress() const;
    bool isMigrationInProgress() const;

    // Validation and testing
    bool validateDatabaseSchema();
    bool testMigration(const QString& version);
    bool dryRunMigration(const QString& version);
    QStringList getSchemaValidationErrors();

    // Asynchronous operations
    QFuture<bool> migrateAsync();
    QFuture<bool> migrateToVersionAsync(const QString& targetVersion);
    QFuture<bool> rollbackAsync();
    QFuture<bool> validateSchemaAsync();
    QFuture<QList<MigrationInfo>> getMigrationHistoryAsync();

    // Utility functions
    QString generateMigrationVersion() const;
    QString generateMigrationName(const QString& description) const;
    bool backupBeforeMigration();
    bool restoreAfterFailedMigration();
    void exportMigrationHistory(const QString& filePath);
    void importMigrationHistory(const QString& filePath);

signals:
    void migrationStarted(const QString& version);
    void migrationCompleted(const QString& version, bool success);
    void migrationProgress(const MigrationProgress& progress);
    void rollbackStarted(const QString& version);
    void rollbackCompleted(const QString& version, bool success);
    void rollbackProgress(const MigrationProgress& progress);
    void validationStarted();
    void validationCompleted(bool success);
    void schemaChanged();
    void migrationError(const QString& error);
    void migrationWarning(const QString& warning);

private slots:
    void onMigrationCompleted();
    void onRollbackCompleted();
    void onValidationCompleted();
    void onSchemaChanged();

private:
    // Private implementation
    bool performMigration(const QString& version);
    bool performRollback(const QString& version);
    bool performValidation();
    bool executeMigrationSql(const QString& sql, const QString& version);
    bool executeRollbackSql(const QString& sql, const QString& version);

    // Helper functions
    bool createMigrationTable();
    bool recordMigration(const MigrationInfo& info);
    bool removeMigrationRecord(const QString& version);
    MigrationFile loadMigrationFile(const QString& version);
    bool saveMigrationFile(const MigrationFile& migration);
    bool validateMigrationFile(const MigrationFile& migration);
    QString parseMigrationVersion(const QString& filename);
    QString parseMigrationName(const QString& filename);
    bool updateMigrationProgress(const MigrationProgress& progress);
    void logMigrationEvent(const QString& event, const QVariantMap& data = {});

    // SQL parsing and validation
    QStringList parseSqlStatements(const QString& sql);
    bool validateSqlStatement(const QString& sql);
    QString extractTableNames(const QString& sql);
    bool checkTableExists(const QString& tableName);
    bool checkColumnExists(const QString& tableName, const QString& columnName);

    // Member variables
    MigrationConfig m_config;
    QString m_currentVersion;
    QString m_latestVersion;
    
    QQueue<MigrationInfo> m_migrationHistory;
    MigrationProgress m_currentProgress;
    bool m_migrationInProgress = false;
    bool m_rollbackInProgress = false;
    
    QMutex m_mutex;
    QMutex m_progressMutex;
    
    // Migration file cache
    std::unordered_map<QString, MigrationFile> m_migrationCache;
    bool m_cacheValid = false;
    
    // Async operation watchers
    QList<QFutureWatcher<bool>*> m_migrationWatchers;
    QList<QFutureWatcher<bool>*> m_rollbackWatchers;
    QList<QFutureWatcher<bool>*> m_validationWatchers;
    QList<QFutureWatcher<QList<MigrationInfo>>*> m_historyWatchers;
    
    // Callback functions
    std::function<void(const MigrationProgress&)> m_progressCallback;
    std::function<void(const QString&, bool)> m_completionCallback;
    std::function<void(const QString&)> m_errorCallback;
    
    // Migration templates
    QString m_upMigrationTemplate;
    QString m_downMigrationTemplate;
};

// Migration script templates
namespace MigrationTemplates {
    const QString UP_TEMPLATE = R"(
-- Migration: %1
-- Description: %2
-- Created: %3

-- Add your migration SQL here
-- Example:
-- CREATE TABLE IF NOT EXISTS new_table (
--     id INTEGER PRIMARY KEY AUTOINCREMENT,
--     name TEXT NOT NULL,
--     created_at DATETIME DEFAULT CURRENT_TIMESTAMP
-- );

-- ALTER TABLE existing_table ADD COLUMN new_column TEXT;

-- INSERT INTO table_name (column1, column2) VALUES ('value1', 'value2');
)";

    const QString DOWN_TEMPLATE = R"(
-- Rollback Migration: %1
-- Description: %2
-- Created: %3

-- Add your rollback SQL here
-- Example:
-- DROP TABLE IF EXISTS new_table;

-- ALTER TABLE existing_table DROP COLUMN new_column;

-- DELETE FROM table_name WHERE column1 = 'value1';
)";
}

} // namespace db 