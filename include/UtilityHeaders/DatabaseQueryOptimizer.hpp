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
#include <vector>

namespace db {

struct QueryOptimizerConfig {
    bool enableQueryAnalysis = true;
    bool enableQueryCaching = true;
    bool enableQueryRewriting = true;
    bool enableIndexHints = true;
    int maxQueryCacheSize = 1000;
    int queryCacheTimeout = 3600000; // 1 hour
    bool enableSlowQueryLogging = true;
    int slowQueryThreshold = 100; // milliseconds
    bool enableQueryPlans = true;
    bool enableStatistics = true;
    int statisticsUpdateInterval = 3600000; // 1 hour
    bool enableAutoOptimization = true;
    int autoOptimizationInterval = 86400000; // 24 hours
};

struct QueryAnalysis {
    QString originalQuery;
    QString optimizedQuery;
    QString queryPlan;
    qint64 originalExecutionTime;
    qint64 optimizedExecutionTime;
    double improvementPercentage;
    QStringList suggestions;
    QStringList warnings;
    QVariantMap statistics;
    QDateTime analyzedAt;
};

struct QueryCacheEntry {
    QString query;
    QString normalizedQuery;
    QVariantMap parameters;
    QByteArray result;
    QDateTime cachedAt;
    QDateTime lastAccessed;
    int accessCount;
    qint64 executionTime;
    bool isValid;
};

struct QueryStatistics {
    QString queryPattern;
    int executionCount;
    qint64 totalExecutionTime;
    qint64 avgExecutionTime;
    qint64 minExecutionTime;
    qint64 maxExecutionTime;
    QDateTime firstExecuted;
    QDateTime lastExecuted;
    QStringList tableNames;
    QStringList indexNames;
    bool usesIndex;
    int rowsReturned;
    int rowsAffected;
};

struct OptimizationSuggestion {
    QString type; // "index", "query_rewrite", "parameter_binding", "join_optimization"
    QString description;
    QString originalQuery;
    QString suggestedQuery;
    double expectedImprovement;
    QStringList affectedTables;
    bool isAutomatic;
    bool requiresSchemaChange;
};

class DatabaseQueryOptimizer : public QObject {
    Q_OBJECT

public:
    explicit DatabaseQueryOptimizer(QObject* parent = nullptr);
    ~DatabaseQueryOptimizer();

    // Configuration
    void setConfig(const QueryOptimizerConfig& config);
    QueryOptimizerConfig getConfig() const { return m_config; }

    // Query analysis and optimization
    QueryAnalysis analyzeQuery(const QString& query, const QVariantMap& params = {});
    QString optimizeQuery(const QString& query, const QVariantMap& params = {});
    QStringList getQuerySuggestions(const QString& query);
    QString getQueryPlan(const QString& query, const QVariantMap& params = {});

    // Query caching
    bool cacheQuery(const QString& query, const QVariantMap& params, const QByteArray& result);
    QByteArray getCachedQuery(const QString& query, const QVariantMap& params);
    bool invalidateCache(const QString& queryPattern = "");
    void clearCache();
    int getCacheSize() const;
    double getCacheHitRate() const;

    // Query statistics
    void recordQueryExecution(const QString& query, const QVariantMap& params, qint64 executionTime);
    QueryStatistics getQueryStatistics(const QString& queryPattern);
    QList<QueryStatistics> getTopQueries(int limit = 10);
    QList<QueryStatistics> getSlowQueries(int limit = 10);
    void resetStatistics();

    // Index optimization
    QStringList suggestIndexes(const QString& query);
    QStringList suggestIndexesForTable(const QString& tableName);
    bool analyzeIndexUsage();
    QStringList getUnusedIndexes();
    QStringList getMissingIndexes();

    // Automatic optimization
    void enableAutoOptimization(bool enable);
    void setAutoOptimizationInterval(int milliseconds);
    QList<OptimizationSuggestion> getOptimizationSuggestions();
    bool applyOptimizationSuggestion(const OptimizationSuggestion& suggestion);
    bool applyAllOptimizationSuggestions();

    // Performance monitoring
    void enablePerformanceMonitoring(bool enable);
    void setSlowQueryThreshold(int milliseconds);
    QList<QueryAnalysis> getSlowQueryAnalysis(int limit = 50);
    void exportPerformanceReport(const QString& filePath);

    // Asynchronous operations
    QFuture<QueryAnalysis> analyzeQueryAsync(const QString& query, const QVariantMap& params = {});
    QFuture<QString> optimizeQueryAsync(const QString& query, const QVariantMap& params = {});
    QFuture<QList<OptimizationSuggestion>> getOptimizationSuggestionsAsync();
    QFuture<bool> applyOptimizationSuggestionAsync(const OptimizationSuggestion& suggestion);

    // Utility functions
    QString normalizeQuery(const QString& query);
    QStringList extractTableNames(const QString& query);
    QStringList extractColumnNames(const QString& query);
    bool isQueryCacheable(const QString& query);
    QString generateQueryHash(const QString& query, const QVariantMap& params);
    void exportQueryHistory(const QString& filePath);

signals:
    void queryAnalyzed(const QueryAnalysis& analysis);
    void queryOptimized(const QString& original, const QString& optimized);
    void cacheHit(const QString& query);
    void cacheMiss(const QString& query);
    void slowQueryDetected(const QString& query, qint64 executionTime);
    void optimizationSuggestionGenerated(const OptimizationSuggestion& suggestion);
    void optimizationApplied(const OptimizationSuggestion& suggestion, bool success);
    void statisticsUpdated();
    void performanceReportGenerated(const QString& filePath);
    void optimizerError(const QString& error);
    void optimizerWarning(const QString& warning);

private slots:
    void onAutoOptimizationTimer();
    void onStatisticsUpdateTimer();
    void onCacheCleanupTimer();
    void onQueryAnalysisCompleted();
    void onQueryOptimizationCompleted();
    void onOptimizationSuggestionCompleted();

private:
    // Private implementation
    QueryAnalysis performQueryAnalysis(const QString& query, const QVariantMap& params);
    QString performQueryOptimization(const QString& query, const QVariantMap& params);
    QStringList generateQuerySuggestions(const QString& query, const QVariantMap& params);
    QString generateQueryPlan(const QString& query, const QVariantMap& params);

    // Cache management
    void cleanupExpiredCache();
    void evictLeastUsedCache();
    bool isCacheEntryValid(const QueryCacheEntry& entry);
    QString normalizeQueryForCache(const QString& query);

    // Statistics management
    void updateQueryStatistics(const QString& query, const QVariantMap& params, qint64 executionTime);
    void cleanupOldStatistics();
    void calculateQueryPatterns();

    // Index analysis
    QStringList analyzeIndexUsageForQuery(const QString& query);
    QStringList findMissingIndexes(const QString& query);
    QStringList findUnusedIndexes();
    double estimateIndexBenefit(const QString& indexName, const QString& query);

    // Query rewriting
    QString rewriteQueryForOptimization(const QString& query);
    QString addIndexHints(const QString& query, const QStringList& indexes);
    QString optimizeJoins(const QString& query);
    QString optimizeWhereClause(const QString& query);

    // Helper functions
    bool isQueryComplex(const QString& query);
    bool hasSubqueries(const QString& query);
    bool hasAggregations(const QString& query);
    bool hasJoins(const QString& query);
    int estimateQueryComplexity(const QString& query);
    void logOptimizationEvent(const QString& event, const QVariantMap& data = {});

    // Member variables
    QueryOptimizerConfig m_config;
    QTimer m_autoOptimizationTimer;
    QTimer m_statisticsUpdateTimer;
    QTimer m_cacheCleanupTimer;
    
    // Query cache
    std::unordered_map<QString, QueryCacheEntry> m_queryCache;
    int m_cacheHits = 0;
    int m_cacheMisses = 0;
    
    // Query statistics
    std::unordered_map<QString, QueryStatistics> m_queryStatistics;
    QQueue<QueryAnalysis> m_slowQueryAnalysis;
    
    // Optimization suggestions
    QList<OptimizationSuggestion> m_optimizationSuggestions;
    
    QMutex m_cacheMutex;
    QMutex m_statisticsMutex;
    QMutex m_suggestionsMutex;
    
    // Async operation watchers
    QList<QFutureWatcher<QueryAnalysis>*> m_analysisWatchers;
    QList<QFutureWatcher<QString>*> m_optimizationWatchers;
    QList<QFutureWatcher<QList<OptimizationSuggestion>>*> m_suggestionWatchers;
    QList<QFutureWatcher<bool>*> m_applicationWatchers;
    
    // Callback functions
    std::function<void(const QueryAnalysis&)> m_analysisCallback;
    std::function<void(const QString&, const QString&)> m_optimizationCallback;
    std::function<void(const OptimizationSuggestion&)> m_suggestionCallback;
    std::function<void(const QString&)> m_errorCallback;
    
    // Query patterns and templates
    std::unordered_map<QString, QString> m_queryPatterns;
    std::unordered_map<QString, QStringList> m_tableIndexes;
    std::unordered_map<QString, QVariantMap> m_tableStatistics;
};

} // namespace db 