#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QQueue>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>
#include <variant>
#include <entt/entt.hpp>
#include "Scene.hpp" 
#include "UIHeaders/MenuFactory.hpp"

namespace db {

// Forward declarations
class DatabaseTransaction;
class DatabaseBackupManager;
class DatabaseMigrationManager;
class DatabaseQueryOptimizer;
class DatabaseIndexManager;
class DatabaseReplicationManager;

// Database configuration
struct DatabaseConfig {
    QString databasePath = "krstudio.db";
    int maxConnections = 10;
    int connectionTimeout = 30000; // 30 seconds
    bool enableWAL = true;
    bool enableForeignKeys = true;
    int cacheSize = -64000; // 64MB cache
    int pageSize = 4096;
    bool synchronous = true;
    int journalMode = 1; // WAL mode
    bool tempStore = false;
    int mmapSize = 268435456; // 256MB
    bool autoVacuum = true;
    int incrementalVacuum = 1000;
    bool recursiveTriggers = true;
    int busyTimeout = 30000;
    bool enableExtensions = true;
};

// Database statistics
struct DatabaseStats {
    qint64 totalSize = 0;
    qint64 dataSize = 0;
    qint64 indexSize = 0;
    qint64 walSize = 0;
    int pageCount = 0;
    int pageSize = 0;
    int cacheSize = 0;
    int cacheUsed = 0;
    int cacheHit = 0;
    int cacheMiss = 0;
    int totalQueries = 0;
    int slowQueries = 0;
    double avgQueryTime = 0.0;
    QDateTime lastOptimize;
    QDateTime lastVacuum;
    QDateTime lastBackup;
};

// Query result types
using QueryResult = std::variant<QSqlRecord, QVariantList, QByteArray, QString, int, double, bool>;
using QueryResults = std::vector<QueryResult>;

// Database operation types
enum class OperationType {
    Insert,
    Update,
    Delete,
    Select,
    Create,
    Drop,
    Alter,
    Transaction,
    Backup,
    Restore,
    Optimize,
    Vacuum
};

// Database event types
enum class DatabaseEvent {
    Connected,
    Disconnected,
    TransactionBegin,
    TransactionCommit,
    TransactionRollback,
    BackupStarted,
    BackupCompleted,
    BackupFailed,
    RestoreStarted,
    RestoreCompleted,
    RestoreFailed,
    OptimizeStarted,
    OptimizeCompleted,
    VacuumStarted,
    VacuumCompleted,
    Error,
    Warning,
    SlowQuery
};

// Database event data
struct DatabaseEventData {
    DatabaseEvent type;
    QString message;
    QDateTime timestamp;
    QVariantMap metadata;
    std::optional<QSqlError> error;
};

// Query performance metrics
struct QueryMetrics {
    QString query;
    qint64 executionTime;
    int rowsAffected;
    int rowsReturned;
    QDateTime timestamp;
    bool wasSlow;
    QString plan;
};

// Database connection pool
class ConnectionPool {
public:
    explicit ConnectionPool(const DatabaseConfig& config);
    ~ConnectionPool();

    QSqlDatabase getConnection();
    void releaseConnection(QSqlDatabase& connection);
    void closeAllConnections();
    int activeConnections() const;
    int availableConnections() const;

private:
    DatabaseConfig m_config;
    QQueue<QString> m_availableConnections;
    QSet<QString> m_activeConnections;
    mutable QMutex m_mutex;
    int m_connectionCounter = 0;
};

// Main database manager class
class DatabaseManager : public QObject {
    Q_OBJECT

public:
    static DatabaseManager& instance();
    
    // Initialization and configuration
    bool initialize(const DatabaseConfig& config = DatabaseConfig{});
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Configuration
    void setConfig(const DatabaseConfig& config);
    DatabaseConfig getConfig() const { return m_config; }
    
    // Connection management
    QSqlDatabase getConnection();
    void releaseConnection(QSqlDatabase& connection);
    
    // Basic database operations
    bool executeQuery(const QString& query, QVariantMap params = {});
    QueryResults executeQueryWithResults(const QString& query, QVariantMap params = {});
    QSqlRecord executeSingleQuery(const QString& query, QVariantMap params = {});
    int executeBatchQuery(const QStringList& queries);
    
    // Transaction management
    std::unique_ptr<DatabaseTransaction> beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    bool isInTransaction() const;
    
    // Scene persistence
    bool saveScene(const Scene& scene, const QString& name = "current");
    std::unique_ptr<Scene> loadScene(const QString& name = "current");
    bool deleteScene(const QString& name);
    QStringList listScenes();
    
    // Entity-Component persistence
    bool saveEntity(entt::entity entity, const entt::registry& registry, const QString& sceneName = "current");
    bool loadEntity(entt::entity entity, entt::registry& registry, const QString& sceneName = "current");
    bool deleteEntity(entt::entity entity, const QString& sceneName = "current");
    
    // Component persistence
    template<typename Component>
    bool saveComponent(entt::entity entity, const Component& component, const QString& sceneName = "current");
    
    template<typename Component>
    bool loadComponent(entt::entity entity, Component& component, const QString& sceneName = "current");
    
    template<typename Component>
    bool deleteComponent(entt::entity entity, const QString& sceneName = "current");
    
    // Backup and recovery
    bool createBackup(const QString& backupPath);
    bool restoreBackup(const QString& backupPath);
    QStringList listBackups();
    bool deleteBackup(const QString& backupName);
    
    // Maintenance
    bool optimize();
    bool vacuum();
    bool reindex();
    bool analyze();
    DatabaseStats getStats();
    
    // Monitoring and logging
    void enableQueryLogging(bool enable);
    void setSlowQueryThreshold(int milliseconds);
    QList<QueryMetrics> getQueryHistory(int limit = 100);
    void clearQueryHistory();
    
    // Asynchronous operations
    QFuture<bool> executeQueryAsync(const QString& query, QVariantMap params = {});
    QFuture<QueryResults> executeQueryWithResultsAsync(const QString& query, QVariantMap params = {});
    QFuture<bool> saveSceneAsync(const Scene& scene, const QString& name = "current");
    QFuture<std::unique_ptr<Scene>> loadSceneAsync(const QString& name = "current");
    
    // Replication and synchronization
    bool enableReplication(const QString& masterUrl);
    bool disableReplication();
    bool isReplicationEnabled() const;
    bool syncWithMaster();
    
    // Utility functions
    QString getLastError() const;
    int getLastErrorCode() const;
    bool hasError() const;
    void clearErrors();
    
    // Statistics and monitoring
    void resetStats();
    void exportStats(const QString& filePath);
    
    /// Returns true if we already have a saved state for this menu name.
    bool menuConfigExists(const QString& menuName);
    QString loadMenuState(const QString& menuName);
    bool saveMenuState(const QString& menuName, const QString& blob);

    static QString menuTypeToString(MenuType type);

signals:
    void databaseEvent(const DatabaseEventData& event);
    void queryExecuted(const QueryMetrics& metrics);
    void backupCompleted(bool success, const QString& path);
    void restoreCompleted(bool success, const QString& path);
    void optimizationCompleted(bool success);
    void replicationStatusChanged(bool enabled);
    void connectionStatusChanged(bool connected);
    void error(const QString& error);
    void warning(const QString& warning);

private slots:
    void onQueryCompleted();
    void onBackupCompleted();
    void onRestoreCompleted();
    void onOptimizationCompleted();
    void onReplicationSync();
    void onMigrationCompleted();
    void onSlowQueryDetected();

private:
    DatabaseManager();
    ~DatabaseManager();
    
    // Private implementation
    bool initializeDatabase();
    bool createTables();
    bool createIndexes();
    bool createTriggers();
    bool createViews();
    bool setupReplication();
    
    // Helper functions
    QString serializeComponent(const QVariant& component);
    QVariant deserializeComponent(const QString& data, const QString& typeName);
    QString getComponentTypeName(const QVariant& component);
    bool validateConnection(QSqlDatabase& connection);
    
    // Member variables
    DatabaseConfig m_config;
    std::unique_ptr<ConnectionPool> m_connectionPool;
    std::unique_ptr<DatabaseBackupManager> m_backupManager;
    std::unique_ptr<DatabaseMigrationManager> m_migrationManager;
    std::unique_ptr<DatabaseQueryOptimizer> m_queryOptimizer;
    std::unique_ptr<DatabaseIndexManager> m_indexManager;
    std::unique_ptr<DatabaseReplicationManager> m_replicationManager;
    
    bool m_initialized = false;
    bool m_inTransaction = false;
    bool m_queryLoggingEnabled = false;
    int m_slowQueryThreshold = 100; // milliseconds
    
    QQueue<QueryMetrics> m_queryHistory;
    QMutex m_queryHistoryMutex;
    
    QTimer m_maintenanceTimer;
    QTimer m_statsTimer;
    
    // Async operation watchers
    QList<QFutureWatcher<bool>*> m_asyncWatchers;
    QList<QFutureWatcher<QueryResults>*> m_asyncResultWatchers;
    
    mutable QMutex m_mutex;
};

// Database transaction class
class DatabaseTransaction {
public:
    explicit DatabaseTransaction(DatabaseManager& manager);
    ~DatabaseTransaction();
    
    bool commit();
    bool rollback();
    bool isActive() const { return m_active; }
    
private:
    DatabaseManager& m_manager;
    bool m_active = true;
    bool m_committed = false;
};

// Template implementations
template<typename Component>
bool DatabaseManager::saveComponent(entt::entity entity, const Component& component, const QString& sceneName) {
    QMutexLocker locker(&m_mutex);
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QSqlQuery query(connection);
    query.prepare("INSERT OR REPLACE INTO components (scene_name, entity_id, component_type, data, created_at, updated_at) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
    
    query.addBindValue(sceneName);
    query.addBindValue(static_cast<qint64>(entity));
    query.addBindValue(QString::fromStdString(std::string(entt::type_name<Component>::value())));
    query.addBindValue(serializeComponent(QVariant::fromValue(component)));
    query.addBindValue(QDateTime::currentDateTime());
    query.addBindValue(QDateTime::currentDateTime());
    
    bool success = query.exec();
    if (!success) {
        emit error(QString("Failed to save component: %1").arg(query.lastError().text()));
    }
    
    releaseConnection(connection);
    return success;
}

template<typename Component>
bool DatabaseManager::loadComponent(entt::entity entity, Component& component, const QString& sceneName) {
    QMutexLocker locker(&m_mutex);
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QSqlQuery query(connection);
    query.prepare("SELECT data FROM components WHERE scene_name = ? AND entity_id = ? AND component_type = ?");
    query.addBindValue(sceneName);
    query.addBindValue(static_cast<qint64>(entity));
    query.addBindValue(QString::fromStdString(entt::type_name<Component>().name()));
    
    if (!query.exec() || !query.next()) {
        releaseConnection(connection);
        return false;
    }
    
    QString data = query.value(0).toString();
    QVariant deserialized = deserializeComponent(data, QString::fromStdString(entt::type_name<Component>().name()));
    
    if (deserialized.canConvert<Component>()) {
        component = deserialized.value<Component>();
        releaseConnection(connection);
        return true;
    }
    
    releaseConnection(connection);
    return false;
}

template<typename Component>
bool DatabaseManager::deleteComponent(entt::entity entity, const QString& sceneName) {
    QMutexLocker locker(&m_mutex);
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QSqlQuery query(connection);
    query.prepare("DELETE FROM components WHERE scene_name = ? AND entity_id = ? AND component_type = ?");
    query.addBindValue(sceneName);
    query.addBindValue(static_cast<qint64>(entity));
    query.addBindValue(QString::fromStdString(entt::type_name<Component>().name()));
    
    bool success = query.exec();
    if (!success) {
        emit error(QString("Failed to delete component: %1").arg(query.lastError().text()));
    }
    
    releaseConnection(connection);
    return success;
}

} // namespace db 