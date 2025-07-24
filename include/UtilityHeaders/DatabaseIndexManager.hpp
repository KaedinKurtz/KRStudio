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
#include <unordered_map>

namespace db {

struct IndexInfo {
    QString tableName;
    QString indexName;
    QStringList columns;
    bool unique;
    bool valid;
    QString type;
    QString sql;
    QVariantMap metadata;
};

class DatabaseIndexManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseIndexManager(QObject* parent = nullptr);
    ~DatabaseIndexManager();

    // Index operations
    bool createIndex(const QString& table, const QString& indexName, const QStringList& columns, bool unique = false);
    bool dropIndex(const QString& indexName);
    bool rebuildIndex(const QString& indexName);
    bool reindexAll();
    bool analyzeIndex(const QString& indexName);
    bool analyzeAllIndexes();
    bool validateIndex(const QString& indexName);
    bool validateAllIndexes();
    bool indexExists(const QString& indexName);
    QList<IndexInfo> listIndexes(const QString& table = QString());

    // Async operations
    QFuture<bool> createIndexAsync(const QString& table, const QString& indexName, const QStringList& columns, bool unique = false);
    QFuture<bool> dropIndexAsync(const QString& indexName);
    QFuture<QList<IndexInfo>> listIndexesAsync(const QString& table = QString());

signals:
    void indexCreated(const QString& indexName, bool success);
    void indexDropped(const QString& indexName, bool success);
    void indexRebuilt(const QString& indexName, bool success);
    void indexAnalyzed(const QString& indexName, bool success);
    void indexValidated(const QString& indexName, bool valid);
    void indexError(const QString& error);
    void indexWarning(const QString& warning);

private:
    QMutex m_mutex;
    QList<IndexInfo> m_indexCache;
    bool m_cacheValid = false;
};

} // namespace db 