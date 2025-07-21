#include "ObservableDataManager.hpp"

// --- Singleton Access ---
ObservableDataManager& ObservableDataManager::instance() {
    static ObservableDataManager instance; // Meyers' Singleton: thread-safe and lazy-initialized.
    return instance;
}

// --- Discovery & Analysis API ---

std::vector<std::string> ObservableDataManager::getAllDataSourceIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> ids;
    ids.reserve(m_dataSources.size());
    for (const auto& pair : m_dataSources) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::string ObservableDataManager::getDataSourceDescription(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_dataSources.find(id);
    if (it != m_dataSources.end()) {
        return it->second.description;
    }
    return "Unknown ID";
}

const std::type_info& ObservableDataManager::getDataSourceType(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_dataSources.find(id);
    if (it != m_dataSources.end()) {
        return it->second.typeInfo;
    }
    return typeid(void);
}

ObservableDataManager::HealthStatus ObservableDataManager::getDataSourceHealth(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_dataSources.find(id);
    if (it != m_dataSources.end()) {
        // ToDo: Implement actual health checking logic (e.g., based on last_update_timestamp)
        return it->second.health;
    }
    return HealthStatus::Faulted;
}

double ObservableDataManager::getDataSourceFrequency(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_dataSources.find(id);
    if (it != m_dataSources.end()) {
        // ToDo: Implement frequency calculation from timestamp_history
        return it->second.measured_frequency_hz;
    }
    return 0.0;
}

LatencyProfile ObservableDataManager::getLatencyProfile(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // ToDo: Implement actual latency profile reconstruction
    return LatencyProfile{ id, 0.0f, 0.0f, {} };
}

// --- Debugging API ---

void ObservableDataManager::dumpHistoryToFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // ToDo: Implement file writing logic
}
