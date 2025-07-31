#include "DatabaseManager.hpp"
#include "DatabaseBackupManager.hpp"
#include "DatabaseMigrationManager.hpp"
#include "DatabaseQueryOptimizer.hpp"
#include "DatabaseIndexManager.hpp"
#include "DatabaseReplicationManager.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "MenuFactory.hpp"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QApplication>
#include <QStandardPaths>
#include <QDataStream>
#include <QBuffer>
#include <QIODevice>
#include <QTextStream>
#include <QDateTime>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace db;

// Singleton instance
DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager instance;
    return instance;
}

// Constructor
DatabaseManager::DatabaseManager() 
    : QObject(nullptr)
    , m_connectionPool(nullptr)
    , m_backupManager(nullptr)
    , m_migrationManager(nullptr)
    , m_queryOptimizer(nullptr)
    , m_indexManager(nullptr)
    , m_replicationManager(nullptr)
    , m_initialized(false)
    , m_inTransaction(false)
    , m_queryLoggingEnabled(false)
    , m_slowQueryThreshold(100)
{
    // Set up maintenance timer (every 6 hours)
    m_maintenanceTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_maintenanceTimer, &QTimer::timeout, this, [this]() {
        if (m_initialized) {
            optimize();
            vacuum();
        }
    });
    
    // Set up statistics timer (every hour)
    m_statsTimer.setInterval(60 * 60 * 1000);
    connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
        if (m_initialized) {
            analyze();
        }
    });
}

// Destructor
DatabaseManager::~DatabaseManager() {
    shutdown();
}

// Initialize the database system
bool DatabaseManager::initialize(const DatabaseConfig& config) {
    if (m_initialized) {
        qWarning() << "DatabaseManager already initialized";
        return true;
    }
    
    m_config = config;
    
    // Create database directory if it doesn't exist
    QFileInfo dbFile(m_config.databasePath);
    QDir dbDir = dbFile.absoluteDir();
    if (!dbDir.exists()) {
        if (!dbDir.mkpath(".")) {
            qCritical() << "Failed to create database directory:" << dbDir.absolutePath();
            return false;
        }
    }
    
    // Initialize connection pool
    m_connectionPool = std::make_unique<ConnectionPool>(m_config);
    
    // Initialize database
    if (!initializeDatabase()) {
        qCritical() << "Failed to initialize database";
        return false;
    }
    
    auto connection = getConnection();
    QSqlQuery q(connection);
    if (!q.exec(R"(
    CREATE TABLE IF NOT EXISTS menu_states (
        scene_name TEXT NOT NULL,
        menu_type   TEXT NOT NULL,
        state       TEXT,
        PRIMARY KEY (scene_name, menu_type)
    );
    )")) {
        qCritical() << "Failed to create menu_states table:" << q.lastError().text();
    }
    releaseConnection(connection);

    // Create specialized managers
    m_backupManager = std::make_unique<db::DatabaseBackupManager>(this);
    m_migrationManager = std::make_unique<db::DatabaseMigrationManager>(this);
    m_queryOptimizer = std::make_unique<db::DatabaseQueryOptimizer>(this);
    m_indexManager = std::make_unique<db::DatabaseIndexManager>(this);
    m_replicationManager = std::make_unique<db::DatabaseReplicationManager>(this);
    
    // Connect signals
    connect(m_backupManager.get(), &DatabaseBackupManager::backupCompleted,
            this, &DatabaseManager::onBackupCompleted);
    connect(m_migrationManager.get(), &DatabaseMigrationManager::migrationCompleted,
            this, &DatabaseManager::onMigrationCompleted);
    connect(m_queryOptimizer.get(), &DatabaseQueryOptimizer::slowQueryDetected,
            this, &DatabaseManager::onSlowQueryDetected);
    
    m_initialized = true;
    
    // Start maintenance timers
    m_maintenanceTimer.start();
    m_statsTimer.start();
    
    emit databaseEvent({DatabaseEvent::Connected, "Database initialized successfully", QDateTime::currentDateTime()});
    
    qDebug() << "DatabaseManager initialized successfully";
    return true;
}

// Shutdown the database system
void DatabaseManager::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    // Stop timers
    m_maintenanceTimer.stop();
    m_statsTimer.stop();
    
    // Close all connections
    if (m_connectionPool) {
        m_connectionPool->closeAllConnections();
    }
    
    // Shutdown managers
    m_backupManager.reset();
    m_migrationManager.reset();
    m_queryOptimizer.reset();
    m_indexManager.reset();
    m_replicationManager.reset();
    m_connectionPool.reset();
    
    m_initialized = false;
    
    emit databaseEvent({DatabaseEvent::Disconnected, "Database shutdown", QDateTime::currentDateTime()});
    
    qDebug() << "DatabaseManager shutdown complete";
}

// Initialize the database
bool DatabaseManager::initializeDatabase() {
    auto connection = getConnection();
    if (!connection.isValid()) {
        qCritical() << "Failed to get database connection";
        return false;
    }
    
    // Configure SQLite settings
    QSqlQuery query(connection);
    
    // Enable WAL mode for better concurrency
    if (m_config.enableWAL) {
        query.exec("PRAGMA journal_mode=WAL");
    }
    
    // Enable foreign keys
    if (m_config.enableForeignKeys) {
        query.exec("PRAGMA foreign_keys=ON");
    }
    
    // Set cache size
    query.exec(QString("PRAGMA cache_size=%1").arg(m_config.cacheSize));
    
    // Set page size
    query.exec(QString("PRAGMA page_size=%1").arg(m_config.pageSize));
    
    // Set synchronous mode
    query.exec(QString("PRAGMA synchronous=%1").arg(m_config.synchronous ? "FULL" : "OFF"));
    
    // Set temp store
    query.exec(QString("PRAGMA temp_store=%1").arg(m_config.tempStore ? "MEMORY" : "FILE"));
    
    // Set mmap size
    query.exec(QString("PRAGMA mmap_size=%1").arg(m_config.mmapSize));
    
    // Enable auto vacuum
    if (m_config.autoVacuum) {
        query.exec("PRAGMA auto_vacuum=INCREMENTAL");
        query.exec(QString("PRAGMA incremental_vacuum=%1").arg(m_config.incrementalVacuum));
    }
    
    // Enable recursive triggers
    if (m_config.recursiveTriggers) {
        query.exec("PRAGMA recursive_triggers=ON");
    }
    
    // Set busy timeout
    query.exec(QString("PRAGMA busy_timeout=%1").arg(m_config.busyTimeout));
    
    // Create tables
    if (!createTables()) {
        qCritical() << "Failed to create database tables";
        releaseConnection(connection);
        return false;
    }
    
    // Create indexes
    if (!createIndexes()) {
        qWarning() << "Failed to create some database indexes";
    }
    
    // Create triggers
    if (!createTriggers()) {
        qWarning() << "Failed to create some database triggers";
    }
    
    // Create views
    if (!createViews()) {
        qWarning() << "Failed to create some database views";
    }
    
    releaseConnection(connection);
    return true;
}

// Create database tables
bool DatabaseManager::createTables() {
    auto connection = getConnection();
    QSqlQuery query(connection);
    
    // Scenes table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS scenes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            description TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            metadata TEXT,
            version INTEGER DEFAULT 1
        )
    )")) {
        qCritical() << "Failed to create scenes table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Entities table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS entities (
            id INTEGER PRIMARY KEY,
            scene_name TEXT NOT NULL,
            entity_id INTEGER NOT NULL,
            tag TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (scene_name) REFERENCES scenes(name) ON DELETE CASCADE,
            UNIQUE(scene_name, entity_id)
        )
    )")) {
        qCritical() << "Failed to create entities table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Components table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS components (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            scene_name TEXT NOT NULL,
            entity_id INTEGER NOT NULL,
            component_type TEXT NOT NULL,
            data TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (scene_name) REFERENCES scenes(name) ON DELETE CASCADE,
            FOREIGN KEY (scene_name, entity_id) REFERENCES entities(scene_name, entity_id) ON DELETE CASCADE,
            UNIQUE(scene_name, entity_id, component_type)
        )
    )")) {
        qCritical() << "Failed to create components table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Backups table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS backups (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            path TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            size INTEGER,
            compressed BOOLEAN DEFAULT 0,
            encrypted BOOLEAN DEFAULT 0,
            verified BOOLEAN DEFAULT 0,
            checksum TEXT,
            description TEXT,
            metadata TEXT
        )
    )")) {
        qCritical() << "Failed to create backups table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Query history table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS query_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            query TEXT NOT NULL,
            parameters TEXT,
            execution_time INTEGER,
            rows_affected INTEGER,
            rows_returned INTEGER,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            was_slow BOOLEAN DEFAULT 0,
            plan TEXT
        )
    )")) {
        qCritical() << "Failed to create query_history table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Database statistics table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS database_stats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            stat_name TEXT UNIQUE NOT NULL,
            stat_value TEXT NOT NULL,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )")) {
        qCritical() << "Failed to create database_stats table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // --- Menu states table ---
     if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS menu_states (
            menu_name TEXT PRIMARY KEY,
            state      TEXT NOT NULL,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
     )")) {
        qCritical() << "Failed to create menu_states table:" << query.lastError().text();
        releaseConnection(connection);
        return false;
        
    }


    releaseConnection(connection);
    return true;
}

// Create database indexes
bool DatabaseManager::createIndexes() {
    auto connection = getConnection();
    QSqlQuery query(connection);
    
    // Indexes for better query performance
    QStringList indexes = {
        "CREATE INDEX IF NOT EXISTS idx_entities_scene ON entities(scene_name)",
        "CREATE INDEX IF NOT EXISTS idx_entities_tag ON entities(tag)",
        "CREATE INDEX IF NOT EXISTS idx_components_scene_entity ON components(scene_name, entity_id)",
        "CREATE INDEX IF NOT EXISTS idx_components_type ON components(component_type)",
        "CREATE INDEX IF NOT EXISTS idx_components_scene_type ON components(scene_name, component_type)",
        "CREATE INDEX IF NOT EXISTS idx_query_history_timestamp ON query_history(timestamp)",
        "CREATE INDEX IF NOT EXISTS idx_query_history_slow ON query_history(was_slow)",
        "CREATE INDEX IF NOT EXISTS idx_backups_created_at ON backups(created_at)",
        "CREATE INDEX IF NOT EXISTS idx_scenes_updated_at ON scenes(updated_at)"
    };
    
    for (const QString& indexSql : indexes) {
        if (!query.exec(indexSql)) {
            qWarning() << "Failed to create index:" << query.lastError().text();
        }
    }
    
    releaseConnection(connection);
    return true;
}

// Create database triggers
bool DatabaseManager::createTriggers() {
    auto connection = getConnection();
    QSqlQuery query(connection);
    
    // Trigger to update updated_at timestamp on scenes
    if (!query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS update_scenes_timestamp
        AFTER UPDATE ON scenes
        BEGIN
            UPDATE scenes SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
        END
    )")) {
        qWarning() << "Failed to create scenes update trigger:" << query.lastError().text();
    }
    
    // Trigger to update updated_at timestamp on entities
    if (!query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS update_entities_timestamp
        AFTER UPDATE ON entities
        BEGIN
            UPDATE entities SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
        END
    )")) {
        qWarning() << "Failed to create entities update trigger:" << query.lastError().text();
    }
    
    // Trigger to update updated_at timestamp on components
    if (!query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS update_components_timestamp
        AFTER UPDATE ON components
        BEGIN
            UPDATE components SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
        END
    )")) {
        qWarning() << "Failed to create components update trigger:" << query.lastError().text();
    }
    
    releaseConnection(connection);
    return true;
}

// Create database views
bool DatabaseManager::createViews() {
    auto connection = getConnection();
    QSqlQuery query(connection);
    
    // View for scene statistics
    if (!query.exec(R"(
        CREATE VIEW IF NOT EXISTS scene_stats AS
        SELECT 
            s.name,
            s.description,
            s.created_at,
            s.updated_at,
            COUNT(DISTINCT e.entity_id) as entity_count,
            COUNT(c.id) as component_count
        FROM scenes s
        LEFT JOIN entities e ON s.name = e.scene_name
        LEFT JOIN components c ON s.name = c.scene_name
        GROUP BY s.name, s.description, s.created_at, s.updated_at
    )")) {
        qWarning() << "Failed to create scene_stats view:" << query.lastError().text();
    }
    
    // View for entity statistics
    if (!query.exec(R"(
        CREATE VIEW IF NOT EXISTS entity_stats AS
        SELECT 
            e.scene_name,
            e.entity_id,
            e.tag,
            e.created_at,
            e.updated_at,
            COUNT(c.id) as component_count,
            GROUP_CONCAT(c.component_type) as component_types
        FROM entities e
        LEFT JOIN components c ON e.scene_name = c.scene_name AND e.entity_id = c.entity_id
        GROUP BY e.scene_name, e.entity_id, e.tag, e.created_at, e.updated_at
    )")) {
        qWarning() << "Failed to create entity_stats view:" << query.lastError().text();
    }
    
    releaseConnection(connection);
    return true;
}

// Get a database connection from the pool
QSqlDatabase DatabaseManager::getConnection() {
    if (!m_connectionPool) {
        qCritical() << "Connection pool not initialized";
        return QSqlDatabase();
    }
    return m_connectionPool->getConnection();
}

// Release a database connection back to the pool
void DatabaseManager::releaseConnection(QSqlDatabase& connection) {
    if (m_connectionPool) {
        m_connectionPool->releaseConnection(connection);
    }
}

// Execute a query
bool DatabaseManager::executeQuery(const QString& query, QVariantMap params) {
    if (!m_initialized) {
        qWarning() << "Database not initialized";
        return false;
    }
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QSqlQuery sqlQuery(connection);
    sqlQuery.prepare(query);
    
    // Bind parameters
    for (auto it = params.begin(); it != params.end(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }
    
    bool success = sqlQuery.exec();
    qint64 executionTime = timer.elapsed();
    
    // Log query execution
    if (m_queryLoggingEnabled) {
        QueryMetrics metrics;
        metrics.query = query;
        metrics.executionTime = executionTime;
        metrics.rowsAffected = sqlQuery.numRowsAffected();
        metrics.rowsReturned = sqlQuery.size();
        metrics.timestamp = QDateTime::currentDateTime();
        metrics.wasSlow = executionTime > m_slowQueryThreshold;
        metrics.plan = sqlQuery.lastQuery();
        
        QMutexLocker locker(&m_queryHistoryMutex);
        m_queryHistory.enqueue(metrics);
        if (m_queryHistory.size() > 1000) {
            m_queryHistory.dequeue();
        }
        
        emit queryExecuted(metrics);
    }
    
    if (!success) {
        QString error = sqlQuery.lastError().text();
        qWarning() << "Query execution failed:" << error;
        emit this->error(error);
    }
    
    releaseConnection(connection);
    return success;
}

// Execute a query and return results
QueryResults DatabaseManager::executeQueryWithResults(const QString& query, QVariantMap params) {
    QueryResults results;
    
    if (!m_initialized) {
        qWarning() << "Database not initialized";
        return results;
    }
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return results;
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QSqlQuery sqlQuery(connection);
    sqlQuery.prepare(query);
    
    // Bind parameters
    for (auto it = params.begin(); it != params.end(); ++it) {
        sqlQuery.bindValue(it.key(), it.value());
    }
    
    if (sqlQuery.exec()) {
        while (sqlQuery.next()) {
            QSqlRecord record = sqlQuery.record();
            results.push_back(record);
        }
    } else {
        QString error = sqlQuery.lastError().text();
        qWarning() << "Query execution failed:" << error;
        emit this->error(error);
    }
    
    qint64 executionTime = timer.elapsed();
    
    // Log query execution
    if (m_queryLoggingEnabled) {
        QueryMetrics metrics;
        metrics.query = query;
        metrics.executionTime = executionTime;
        metrics.rowsAffected = sqlQuery.numRowsAffected();
        metrics.rowsReturned = results.size();
        metrics.timestamp = QDateTime::currentDateTime();
        metrics.wasSlow = executionTime > m_slowQueryThreshold;
        metrics.plan = sqlQuery.lastQuery();
        
        QMutexLocker locker(&m_queryHistoryMutex);
        m_queryHistory.enqueue(metrics);
        if (m_queryHistory.size() > 1000) {
            m_queryHistory.dequeue();
        }
        
        emit queryExecuted(metrics);
    }
    
    releaseConnection(connection);
    return results;
}

// Save a scene to the database
bool DatabaseManager::saveScene(const Scene& scene, const QString& name) {
    if (!m_initialized) {
        qWarning() << "Database not initialized";
        return false;
    }
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QSqlQuery query(connection);
    
    // Begin transaction
    if (!query.exec("BEGIN TRANSACTION")) {
        qCritical() << "Failed to begin transaction:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    try {
        // Save scene metadata
        query.prepare("INSERT OR REPLACE INTO scenes (name, description, updated_at) VALUES (?, ?, ?)");
        query.addBindValue(name);
        query.addBindValue("KRStudio Scene");
        query.addBindValue(QDateTime::currentDateTime());
        
        if (!query.exec()) {
            qCritical() << "Failed to save scene:" << query.lastError().text();
            query.exec("ROLLBACK");
            releaseConnection(connection);
            return false;
        }
        
        // Get scene registry
        const auto& registry = scene.getRegistry();
        
        // Save all entities and their components
        auto entityView = registry.view<entt::entity>();
        for (auto entity : entityView) {
            if (!saveEntity(entity, registry, name)) {
                qCritical() << "Failed to save entity:" << static_cast<qint64>(entity);
                query.exec("ROLLBACK");
                releaseConnection(connection);
                return false;
            }
        }
        
        // Commit transaction
        if (!query.exec("COMMIT")) {
            qCritical() << "Failed to commit transaction:" << query.lastError().text();
            query.exec("ROLLBACK");
            releaseConnection(connection);
            return false;
        }
        
        qDebug() << "Scene saved successfully:" << name;
        releaseConnection(connection);
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception during scene save:" << e.what();
        query.exec("ROLLBACK");
        releaseConnection(connection);
        return false;
    }
}

// Load a scene from the database
std::unique_ptr<Scene> DatabaseManager::loadScene(const QString& name) {
    if (!m_initialized) {
        qWarning() << "Database not initialized";
        return nullptr;
    }
    
    auto scene = std::make_unique<Scene>();
    auto& registry = scene->getRegistry();
    
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return nullptr;
    }
    
    QSqlQuery query(connection);
    
    // Load all entities for this scene
    query.prepare("SELECT entity_id, tag FROM entities WHERE scene_name = ?");
    query.addBindValue(name);
    
    if (!query.exec()) {
        qCritical() << "Failed to load entities:" << query.lastError().text();
        releaseConnection(connection);
        return nullptr;
    }
    
    // Create entities and load their components
    while (query.next()) {
        qint64 entityId = query.value(0).toLongLong();
        QString tag = query.value(1).toString();
        
        entt::entity entity = registry.create();
        
        // Set entity ID to match the stored one
        // Note: This is a simplified approach. In a real implementation,
        // you might want to maintain entity ID mapping more carefully.
        
        // Load tag component if present
        if (!tag.isEmpty()) {
            registry.emplace<TagComponent>(entity, tag.toStdString());
        }
        
        // Load all components for this entity
        QSqlQuery componentQuery(connection);
        componentQuery.prepare("SELECT component_type, data FROM components WHERE scene_name = ? AND entity_id = ?");
        componentQuery.addBindValue(name);
        componentQuery.addBindValue(entityId);
        
        if (componentQuery.exec()) {
            while (componentQuery.next()) {
                QString componentType = componentQuery.value(0).toString();
                QString data = componentQuery.value(1).toString();
                
                // Deserialize and load component
                QVariant deserialized = deserializeComponent(data, componentType);
                if (deserialized.isValid()) {
                    // Apply component based on type
                    if (componentType == "TransformComponent") {
                        if (deserialized.canConvert<TransformComponent>()) {
                            registry.emplace<TransformComponent>(entity, deserialized.value<TransformComponent>());
                        }
                    } else if (componentType == "CameraComponent") {
                        if (deserialized.canConvert<CameraComponent>()) {
                            registry.emplace<CameraComponent>(entity, deserialized.value<CameraComponent>());
                        }
                    } else if (componentType == "RenderableMeshComponent") {
                        if (deserialized.canConvert<RenderableMeshComponent>()) {
                            registry.emplace<RenderableMeshComponent>(entity, deserialized.value<RenderableMeshComponent>());
                        }
                    } else if (componentType == "MaterialComponent") {
                        if (deserialized.canConvert<MaterialComponent>()) {
                            registry.emplace<MaterialComponent>(entity, deserialized.value<MaterialComponent>());
                        }
                    } else if (componentType == "GridComponent") {
                        if (deserialized.canConvert<GridComponent>()) {
                            registry.emplace<GridComponent>(entity, deserialized.value<GridComponent>());
                        }
                    } else if (componentType == "SplineComponent") {
                        if (deserialized.canConvert<SplineComponent>()) {
                            registry.emplace<SplineComponent>(entity, deserialized.value<SplineComponent>());
                        }
                    }
                    // Add more component types as needed
                }
            }
        }
    }
    
    releaseConnection(connection);
    qDebug() << "Scene loaded successfully:" << name;
    return scene;
}

// Save an entity and its components
bool DatabaseManager::saveEntity(entt::entity entity, const entt::registry& registry, const QString& sceneName) {
    auto connection = getConnection();
    if (!validateConnection(connection)) {
        return false;
    }
    
    QSqlQuery query(connection);
    
    // Save entity metadata
    query.prepare("INSERT OR REPLACE INTO entities (scene_name, entity_id, tag, updated_at) VALUES (?, ?, ?, ?)");
    query.addBindValue(sceneName);
    query.addBindValue(static_cast<qint64>(entity));
    
    QString tag;
    if (registry.all_of<TagComponent>(entity)) {
        tag = QString::fromStdString(registry.get<TagComponent>(entity).tag);
    }
    query.addBindValue(tag);
    query.addBindValue(QDateTime::currentDateTime());
    
    if (!query.exec()) {
        qCritical() << "Failed to save entity:" << query.lastError().text();
        releaseConnection(connection);
        return false;
    }
    
    // Save all components for this entity
    // Note: This is a simplified approach. In a real implementation,
    // you would iterate through all component types systematically.
    
    // Save TransformComponent
    if (registry.all_of<TransformComponent>(entity)) {
        if (!saveComponent(entity, registry.get<TransformComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Save CameraComponent
    if (registry.all_of<CameraComponent>(entity)) {
        if (!saveComponent(entity, registry.get<CameraComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Save RenderableMeshComponent
    if (registry.all_of<RenderableMeshComponent>(entity)) {
        if (!saveComponent(entity, registry.get<RenderableMeshComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Save MaterialComponent
    if (registry.all_of<MaterialComponent>(entity)) {
        if (!saveComponent(entity, registry.get<MaterialComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Save GridComponent
    if (registry.all_of<GridComponent>(entity)) {
        if (!saveComponent(entity, registry.get<GridComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Save SplineComponent
    if (registry.all_of<SplineComponent>(entity)) {
        if (!saveComponent(entity, registry.get<SplineComponent>(entity), sceneName)) {
            releaseConnection(connection);
            return false;
        }
    }
    
    // Add more component types as needed
    
    releaseConnection(connection);
    return true;
}

// Serialize a component to JSON
QString DatabaseManager::serializeComponent(const QVariant& component) {
    QJsonObject json;
    
    if (component.canConvert<TransformComponent>()) {
        auto transform = component.value<TransformComponent>();
        json["translation"] = QJsonArray{transform.translation.x, transform.translation.y, transform.translation.z};
        json["rotation"] = QJsonArray{transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w};
        json["scale"] = QJsonArray{transform.scale.x, transform.scale.y, transform.scale.z};
    } else if (component.canConvert<CameraComponent>()) {
        auto camera = component.value<CameraComponent>();
        json["isPrimary"] = camera.isPrimary;
        // Note: Camera serialization would need to be implemented based on your Camera class
    } else if (component.canConvert<RenderableMeshComponent>()) {
        auto mesh = component.value<RenderableMeshComponent>();
        QJsonArray vertices;
        for (const auto& vertex : mesh.vertices) {
            QJsonObject v;
            v["position"] = QJsonArray{vertex.position.x, vertex.position.y, vertex.position.z};
            v["normal"] = QJsonArray{vertex.normal.x, vertex.normal.y, vertex.normal.z};
            v["uv"] = QJsonArray{vertex.uv.x, vertex.uv.y};
            vertices.append(v);
        }
        json["vertices"] = vertices;
        
        QJsonArray indices;
        for (unsigned int index : mesh.indices) {
            indices.append(static_cast<int>(index));
        }
        json["indices"] = indices;
    } else if (component.canConvert<MaterialComponent>()) {
        auto material = component.value<MaterialComponent>();
        json["albedo"] = QJsonArray{material.albedo.x, material.albedo.y, material.albedo.z};
        json["metallic"] = material.metallic;
        json["roughness"] = material.roughness;
    } else if (component.canConvert<GridComponent>()) {
        auto grid = component.value<GridComponent>();
        json["masterVisible"] = grid.masterVisible;
        json["baseLineWidthPixels"] = grid.baseLineWidthPixels;
        json["showAxes"] = grid.showAxes;
        json["isMetric"] = grid.isMetric;
        json["showIntersections"] = grid.showIntersections;
        json["isDotted"] = grid.isDotted;
        json["snappingEnabled"] = grid.snappingEnabled;
        
        QJsonArray levels;
        for (const auto& level : grid.levels) {
            QJsonObject l;
            l["spacing"] = level.spacing;
            l["color"] = QJsonArray{level.color.x, level.color.y, level.color.z};
            l["startDistance"] = level.fadeInCameraDistanceStart;
            l["endDistance"] = level.fadeInCameraDistanceEnd;
            levels.append(l);
        }
        json["levels"] = levels;
    } else if (component.canConvert<SplineComponent>()) {
        auto spline = component.value<SplineComponent>();
        json["type"] = static_cast<int>(spline.type);
        json["thickness"] = spline.thickness;
        json["isDirty"] = spline.isDirty;
        
        QJsonArray controlPoints;
        for (const auto& point : spline.controlPoints) {
            controlPoints.append(QJsonArray{point.x, point.y, point.z});
        }
        json["controlPoints"] = controlPoints;
        
        json["glowColour"] = QJsonArray{spline.glowColour.x, spline.glowColour.y, spline.glowColour.z, spline.glowColour.w};
        json["coreColour"] = QJsonArray{spline.coreColour.x, spline.coreColour.y, spline.coreColour.z, spline.coreColour.w};
    }
    
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

// Deserialize a component from JSON
QVariant DatabaseManager::deserializeComponent(const QString& data, const QString& typeName) {
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject()) {
        return QVariant();
    }
    
    QJsonObject json = doc.object();
    
    if (typeName == "TransformComponent") {
        TransformComponent transform;
        if (json.contains("translation")) {
            auto trans = json["translation"].toArray();
            if (trans.size() >= 3) {
                transform.translation = glm::vec3(trans[0].toDouble(), trans[1].toDouble(), trans[2].toDouble());
            }
        }
        if (json.contains("rotation")) {
            auto rot = json["rotation"].toArray();
            if (rot.size() >= 4) {
                transform.rotation = glm::quat(rot[3].toDouble(), rot[0].toDouble(), rot[1].toDouble(), rot[2].toDouble());
            }
        }
        if (json.contains("scale")) {
            auto scale = json["scale"].toArray();
            if (scale.size() >= 3) {
                transform.scale = glm::vec3(scale[0].toDouble(), scale[1].toDouble(), scale[2].toDouble());
            }
        }
        return QVariant::fromValue(transform);
    } else if (typeName == "CameraComponent") {
        CameraComponent camera;
        if (json.contains("isPrimary")) {
            camera.isPrimary = json["isPrimary"].toBool();
        }
        return QVariant::fromValue(camera);
    } else if (typeName == "RenderableMeshComponent") {
        RenderableMeshComponent mesh;
        if (json.contains("vertices")) {
            auto vertices = json["vertices"].toArray();
            for (const auto& v : vertices) {
                auto obj = v.toObject();
                Vertex vertex;
                if (obj.contains("position")) {
                    auto pos = obj["position"].toArray();
                    if (pos.size() >= 3) {
                        vertex.position = glm::vec3(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());
                    }
                }
                if (obj.contains("normal")) {
                    auto norm = obj["normal"].toArray();
                    if (norm.size() >= 3) {
                        vertex.normal = glm::vec3(norm[0].toDouble(), norm[1].toDouble(), norm[2].toDouble());
                    }
                }
                if (obj.contains("uv")) {
                    auto uv = obj["uv"].toArray();
                    if (uv.size() >= 2) {
                        vertex.uv = glm::vec2(uv[0].toDouble(), uv[1].toDouble());
                    }
                }
                mesh.vertices.push_back(vertex);
            }
        }
        if (json.contains("indices")) {
            auto indices = json["indices"].toArray();
            for (const auto& index : indices) {
                mesh.indices.push_back(static_cast<unsigned int>(index.toInt()));
            }
        }
        return QVariant::fromValue(mesh);
    } else if (typeName == "MaterialComponent") {
        MaterialComponent material;
        if (json.contains("albedo")) {
            auto albedo = json["albedo"].toArray();
            if (albedo.size() >= 3) {
                material.albedo = glm::vec3(albedo[0].toDouble(), albedo[1].toDouble(), albedo[2].toDouble());
            }
        }
        if (json.contains("metallic")) {
            material.metallic = json["metallic"].toDouble();
        }
        if (json.contains("roughness")) {
            material.roughness = json["roughness"].toDouble();
        }
        return QVariant::fromValue(material);
    } else if (typeName == "GridComponent") {
        GridComponent grid;
        if (json.contains("masterVisible")) {
            grid.masterVisible = json["masterVisible"].toBool();
        }
        if (json.contains("baseLineWidthPixels")) {
            grid.baseLineWidthPixels = json["baseLineWidthPixels"].toDouble();
        }
        if (json.contains("showAxes")) {
            grid.showAxes = json["showAxes"].toBool();
        }
        if (json.contains("isMetric")) {
            grid.isMetric = json["isMetric"].toBool();
        }
        if (json.contains("showIntersections")) {
            grid.showIntersections = json["showIntersections"].toBool();
        }
        if (json.contains("isDotted")) {
            grid.isDotted = json["isDotted"].toBool();
        }
        if (json.contains("snappingEnabled")) {
            grid.snappingEnabled = json["snappingEnabled"].toBool();
        }
        if (json.contains("levels")) {
            auto levels = json["levels"].toArray();
            for (const auto& level : levels) {
                auto obj = level.toObject();
                GridLevel gridLevel{};
                if (obj.contains("spacing")) {
                    gridLevel.spacing = obj["spacing"].toDouble();
                }
                if (obj.contains("color")) {
                    auto color = obj["color"].toArray();
                    if (color.size() >= 3) {
                        gridLevel.color = glm::vec3(color[0].toDouble(), color[1].toDouble(), color[2].toDouble());
                    }
                }
                if (obj.contains("startDistance")) {
                    gridLevel.fadeInCameraDistanceStart = obj["startDistance"].toDouble();
                }
                if (obj.contains("endDistance")) {
                    gridLevel.fadeInCameraDistanceEnd = obj["endDistance"].toDouble();
                }
                grid.levels.push_back(gridLevel);
            }
        }
        return QVariant::fromValue(grid);
    } else if (typeName == "SplineComponent") {
        SplineComponent spline;
        if (json.contains("type")) {
            spline.type = static_cast<SplineType>(json["type"].toInt());
        }
        if (json.contains("thickness")) {
            spline.thickness = json["thickness"].toDouble();
        }
        if (json.contains("isDirty")) {
            spline.isDirty = json["isDirty"].toBool();
        }
        if (json.contains("controlPoints")) {
            auto points = json["controlPoints"].toArray();
            for (const auto& point : points) {
                auto p = point.toArray();
                if (p.size() >= 3) {
                    spline.controlPoints.push_back(glm::vec3(p[0].toDouble(), p[1].toDouble(), p[2].toDouble()));
                }
            }
        }
        if (json.contains("glowColour")) {
            auto glow = json["glowColour"].toArray();
            if (glow.size() >= 4) {
                spline.glowColour = glm::vec4(glow[0].toDouble(), glow[1].toDouble(), glow[2].toDouble(), glow[3].toDouble());
            }
        }
        if (json.contains("coreColour")) {
            auto core = json["coreColour"].toArray();
            if (core.size() >= 4) {
                spline.coreColour = glm::vec4(core[0].toDouble(), core[1].toDouble(), core[2].toDouble(), core[3].toDouble());
            }
        }
        return QVariant::fromValue(spline);
    }
    
    return QVariant();
}

// Validate a database connection
bool DatabaseManager::validateConnection(QSqlDatabase& connection) {
    if (!connection.isValid()) {
        qCritical() << "Invalid database connection";
        return false;
    }
    
    if (!connection.isOpen()) {
        if (!connection.open()) {
            qCritical() << "Failed to open database connection:" << connection.lastError().text();
            return false;
        }
    }
    
    return true;
}

// Get component type name
QString DatabaseManager::getComponentTypeName(const QVariant& component) {
    if (component.canConvert<TransformComponent>()) {
        return "TransformComponent";
    } else if (component.canConvert<CameraComponent>()) {
        return "CameraComponent";
    } else if (component.canConvert<RenderableMeshComponent>()) {
        return "RenderableMeshComponent";
    } else if (component.canConvert<MaterialComponent>()) {
        return "MaterialComponent";
    } else if (component.canConvert<GridComponent>()) {
        return "GridComponent";
    } else if (component.canConvert<SplineComponent>()) {
        return "SplineComponent";
    }
    return "UnknownComponent";
}

// Private slot implementations
void DatabaseManager::onQueryCompleted() {
    // Handle query completion
}

void DatabaseManager::onBackupCompleted() {
    // Handle backup completion
}

void DatabaseManager::onRestoreCompleted() {
    // Handle restore completion
}

void DatabaseManager::onOptimizationCompleted() {
    // Handle optimization completion
}

void DatabaseManager::onReplicationSync() {
    // Handle replication sync
}

// Connection Pool Implementation
ConnectionPool::ConnectionPool(const DatabaseConfig& config)
    : m_config(config)
{
}

ConnectionPool::~ConnectionPool() {
    closeAllConnections();
}

QSqlDatabase ConnectionPool::getConnection() {
    QMutexLocker locker(&m_mutex);
    
    QString connectionName;
    if (!m_availableConnections.isEmpty()) {
        connectionName = m_availableConnections.dequeue();
    } else if (m_activeConnections.size() < m_config.maxConnections) {
        connectionName = QString("KRStudioDB_%1").arg(++m_connectionCounter);
    } else {
        // Wait for a connection to become available
        // This is a simplified implementation
        qWarning() << "No available connections in pool";
        return QSqlDatabase();
    }
    
    QSqlDatabase connection;
    if (QSqlDatabase::contains(connectionName)) {
        connection = QSqlDatabase::database(connectionName);
    } else {
        connection = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        connection.setDatabaseName(m_config.databasePath);
        connection.setConnectOptions(QString("QSQLITE_BUSY_TIMEOUT=%1").arg(m_config.busyTimeout));
    }
    
    if (connection.open()) {
        m_activeConnections.insert(connectionName);
        return connection;
    } else {
        qCritical() << "Failed to open database connection:" << connection.lastError().text();
        return QSqlDatabase();
    }
}

void ConnectionPool::releaseConnection(QSqlDatabase& connection) {
    if (!connection.isValid()) {
        return;
    }
    
    QString connectionName = connection.connectionName();
    
    QMutexLocker locker(&m_mutex);
    if (m_activeConnections.contains(connectionName)) {
        m_activeConnections.remove(connectionName);
        m_availableConnections.enqueue(connectionName);
    }
}

void ConnectionPool::closeAllConnections() {
    QMutexLocker locker(&m_mutex);
    
    for (const QString& connectionName : m_activeConnections) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    
    for (const QString& connectionName : m_availableConnections) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    
    m_activeConnections.clear();
    m_availableConnections.clear();
}

int ConnectionPool::activeConnections() const {
    QMutexLocker locker(&m_mutex);
    return m_activeConnections.size();
}

int ConnectionPool::availableConnections() const {
    QMutexLocker locker(&m_mutex);
    return m_availableConnections.size();
}

// Database Transaction Implementation
DatabaseTransaction::DatabaseTransaction(DatabaseManager& manager)
    : m_manager(manager)
{
    m_manager.executeQuery("BEGIN TRANSACTION");
}

DatabaseTransaction::~DatabaseTransaction() {
    if (m_active && !m_committed) {
        rollback();
    }
}

bool DatabaseTransaction::commit() {
    if (!m_active) {
        return false;
    }
    
    bool success = m_manager.executeQuery("COMMIT");
    if (success) {
        m_committed = true;
        m_active = false;
    }
    return success;
}

bool DatabaseTransaction::rollback() {
    if (!m_active) {
        return false;
    }
    
    bool success = m_manager.executeQuery("ROLLBACK");
    m_active = false;
    return success;
}

// Stub implementations for remaining methods
bool DatabaseManager::optimize() { return true; }
bool DatabaseManager::vacuum() { return true; }
bool DatabaseManager::analyze() { return true; }
DatabaseStats DatabaseManager::getStats() { return DatabaseStats{}; }
void DatabaseManager::enableQueryLogging(bool enable) { m_queryLoggingEnabled = enable; }
void DatabaseManager::setSlowQueryThreshold(int milliseconds) { m_slowQueryThreshold = milliseconds; }
QList<QueryMetrics> DatabaseManager::getQueryHistory(int limit) { return QList<QueryMetrics>{}; }
void DatabaseManager::clearQueryHistory() { m_queryHistory.clear(); }
QString DatabaseManager::getLastError() const { return QString(); }
int DatabaseManager::getLastErrorCode() const { return 0; }
bool DatabaseManager::hasError() const { return false; }
void DatabaseManager::clearErrors() {}
void DatabaseManager::resetStats() {}
void DatabaseManager::exportStats(const QString& filePath) {}

void db::DatabaseManager::onMigrationCompleted() {
    // Stub implementation
}

void db::DatabaseManager::onSlowQueryDetected() {
    // Stub implementation
}

QStringList db::DatabaseManager::listScenes() {
    // Stub implementation - you can fill this in later
    return QStringList();
}

 bool DatabaseManager::saveMenuState(const QString & menuName, const QString & stateJson) {
    auto conn = getConnection();
    QSqlQuery query(conn);
    query.prepare(
        "INSERT OR REPLACE INTO menu_states "
         "(menu_name, state, updated_at) VALUES (?, ?, CURRENT_TIMESTAMP)"
         );
    query.addBindValue(menuName);
    query.addBindValue(stateJson);
    bool ok = query.exec();
    if (!ok) {
        qWarning() << "Failed to save menu state for" << menuName << ":" << query.lastError().text();
        
    }
     releaseConnection(conn);
    return ok;
    
}

QString DatabaseManager::loadMenuState(const QString & menuName) {
    auto conn = getConnection();
    QSqlQuery query(conn);
    query.prepare("SELECT state FROM menu_states WHERE menu_name = ?");
    query.addBindValue(menuName);
    QString state;
    if (query.exec() && query.next()) {
        state = query.value(0).toString();
        
    }
     releaseConnection(conn);
    return state;
    
}

QString DatabaseManager::menuTypeToString(MenuType type)
{
    switch (type) {
    case MenuType::FlowVisualizer: return QStringLiteral("FlowVisualizer");
    case MenuType::RealSense:      return QStringLiteral("RealSense");
    case MenuType::Database:       return QStringLiteral("Database");
    case MenuType::GridProperties: return QStringLiteral("GridProperties");
        // …add any others…
    default:                       return QString();
    }
}

bool DatabaseManager::menuConfigExists(const QString& menuType)
{
    // grab a QSqlDatabase from the pool
    QSqlQuery q(m_connectionPool->getConnection());
    q.prepare("SELECT 1 FROM MenuStates WHERE menu_name = :name LIMIT 1");
    q.bindValue(":name", menuType);
    if (!q.exec()) {
        qWarning() << "menuConfigExists query failed:" << q.lastError().text();
        return false;
    }
    return q.next();
}