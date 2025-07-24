#include "DatabaseIndexManager.hpp"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>

using namespace db;

DatabaseIndexManager::DatabaseIndexManager(QObject* parent)
    : QObject(parent)
{
}

DatabaseIndexManager::~DatabaseIndexManager() = default;

bool DatabaseIndexManager::createIndex(const QString& table, const QString& indexName, const QStringList& columns, bool unique) {
    QMutexLocker locker(&m_mutex);
    if (table.isEmpty() || indexName.isEmpty() || columns.isEmpty()) {
        emit indexError("Invalid parameters for createIndex");
        return false;
    }
    QString sql = QString("CREATE %1 INDEX IF NOT EXISTS %2 ON %3 (%4)")
        .arg(unique ? "UNIQUE" : "")
        .arg(indexName)
        .arg(table)
        .arg(columns.join(", "));
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    bool ok = query.exec(sql);
    if (!ok) {
        emit indexError(query.lastError().text());
    } else {
        emit indexCreated(indexName, true);
        m_cacheValid = false;
    }
    return ok;
}

bool DatabaseIndexManager::dropIndex(const QString& indexName) {
    QMutexLocker locker(&m_mutex);
    if (indexName.isEmpty()) {
        emit indexError("Invalid index name for dropIndex");
        return false;
    }
    QString sql = QString("DROP INDEX IF EXISTS %1").arg(indexName);
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    bool ok = query.exec(sql);
    if (!ok) {
        emit indexError(query.lastError().text());
    } else {
        emit indexDropped(indexName, true);
        m_cacheValid = false;
    }
    return ok;
}

bool DatabaseIndexManager::rebuildIndex(const QString& indexName) {
    QMutexLocker locker(&m_mutex);
    if (indexName.isEmpty()) {
        emit indexError("Invalid index name for rebuildIndex");
        return false;
    }
    // SQLite does not support REBUILD INDEX, so we drop and recreate if possible
    // Find index info
    auto indexes = listIndexes();
    auto it = std::find_if(indexes.begin(), indexes.end(), [&](const IndexInfo& info) { return info.indexName == indexName; });
    if (it == indexes.end()) {
        emit indexError("Index not found for rebuild");
        return false;
    }
    IndexInfo info = *it;
    if (!dropIndex(indexName)) return false;
    bool ok = createIndex(info.tableName, info.indexName, info.columns, info.unique);
    emit indexRebuilt(indexName, ok);
    return ok;
}

bool DatabaseIndexManager::reindexAll() {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    bool ok = query.exec("REINDEX");
    if (!ok) {
        emit indexError(query.lastError().text());
    }
    return ok;
}

bool DatabaseIndexManager::analyzeIndex(const QString& indexName) {
    QMutexLocker locker(&m_mutex);
    if (indexName.isEmpty()) {
        emit indexError("Invalid index name for analyzeIndex");
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    bool ok = query.exec(QString("ANALYZE %1").arg(indexName));
    if (!ok) {
        emit indexError(query.lastError().text());
    } else {
        emit indexAnalyzed(indexName, true);
    }
    return ok;
}

bool DatabaseIndexManager::analyzeAllIndexes() {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    bool ok = query.exec("ANALYZE");
    if (!ok) {
        emit indexError(query.lastError().text());
    }
    return ok;
}

bool DatabaseIndexManager::validateIndex(const QString& indexName) {
    QMutexLocker locker(&m_mutex);
    if (indexName.isEmpty()) {
        emit indexError("Invalid index name for validateIndex");
        return false;
    }
    // Check if index exists in sqlite_master
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    query.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name=?");
    query.addBindValue(indexName);
    bool ok = query.exec();
    bool found = false;
    if (ok && query.next()) {
        found = true;
    }
    emit indexValidated(indexName, found);
    if (!found) emit indexWarning("Index not found: " + indexName);
    return found;
}

bool DatabaseIndexManager::validateAllIndexes() {
    QMutexLocker locker(&m_mutex);
    auto indexes = listIndexes();
    bool allValid = true;
    for (const auto& idx : indexes) {
        if (!validateIndex(idx.indexName)) allValid = false;
    }
    return allValid;
}

bool DatabaseIndexManager::indexExists(const QString& indexName){
    QMutexLocker locker(&m_mutex);
    if (indexName.isEmpty()) return false;
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    query.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name=?");
    query.addBindValue(indexName);
    if (!query.exec()) return false;
    return query.next();
}

QList<IndexInfo> DatabaseIndexManager::listIndexes(const QString& table) {
    QMutexLocker locker(&m_mutex);
    if (m_cacheValid && (table.isEmpty() || std::any_of(m_indexCache.begin(), m_indexCache.end(), [&](const IndexInfo& idx){ return idx.tableName == table; }))) {
        if (table.isEmpty()) return m_indexCache;
        QList<IndexInfo> filtered;
        for (const auto& idx : m_indexCache) {
            if (idx.tableName == table) filtered.append(idx);
        }
        return filtered;
    }
    QList<IndexInfo> result;
    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery query(db);
    QString sql = "SELECT name, tbl_name, sql FROM sqlite_master WHERE type='index' AND name NOT LIKE 'sqlite_autoindex%'";
    if (!table.isEmpty()) {
        sql += " AND tbl_name=?";
        query.prepare(sql);
        query.addBindValue(table);
    } else {
        query.prepare(sql);
    }
    if (!query.exec()) {
        emit indexError(query.lastError().text());
        return {};
    }
    while (query.next()) {
        IndexInfo info;
        info.indexName = query.value(0).toString();
        info.tableName = query.value(1).toString();
        info.sql = query.value(2).toString();
        info.unique = info.sql.contains("UNIQUE", Qt::CaseInsensitive);
        info.valid = true;
        // Extract columns from SQL
        int open = info.sql.indexOf('(');
        int close = info.sql.lastIndexOf(')');
        if (open != -1 && close != -1 && close > open) {
            QString cols = info.sql.mid(open + 1, close - open - 1);
            info.columns = cols.split(',', Qt::SkipEmptyParts);
            for (auto& c : info.columns) c = c.trimmed();
        }
        result.append(info);
    }
    m_indexCache = result;
    m_cacheValid = true;
    if (!table.isEmpty()) {
        QList<IndexInfo> filtered;
        for (const auto& idx : result) {
            if (idx.tableName == table) filtered.append(idx);
        }
        return filtered;
    }
    return result;
}

QFuture<bool> DatabaseIndexManager::createIndexAsync(const QString& table, const QString& indexName, const QStringList& columns, bool unique) {
    return QtConcurrent::run([=]() { return createIndex(table, indexName, columns, unique); });
}

QFuture<bool> DatabaseIndexManager::dropIndexAsync(const QString& indexName) {
    return QtConcurrent::run([=]() { return dropIndex(indexName); });
}

QFuture<QList<IndexInfo>> DatabaseIndexManager::listIndexesAsync(const QString& table) {
    return QtConcurrent::run([=]() { return listIndexes(table); });
} 