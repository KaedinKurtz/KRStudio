#pragma once

#include "components.hpp"
#include <string>
#include <functional>
#include <any>
#include <optional>
#include <vector>
#include <map>
#include <mutex>
#include <typeinfo>
#include <chrono>
#include <deque>
#include <memory>

/**
 * @brief A recursive structure representing the latency profile tree for a data source.
 */
struct LatencyProfileNode {
    std::string id;
    float processing_time_ms = 0.0f;
    float total_time_ms = 0.0f; // Sum of this node and its critical path dependency.
    std::vector<LatencyProfileNode> children;
};
using LatencyProfile = LatencyProfileNode;


class ObservableDataManager {
public:
    enum class HealthStatus { Active, Stale, Faulted };

    // Singleton access method.
    static ObservableDataManager& instance();

    // --- Core API ---

    /**
     * @brief Publishes a new value with full profiling info. (Preferred Method)
     */
    template<typename T>
    void publish(const std::string& id, ProfiledData<T>&& data);

    /**
     * @brief Registers a simple, poll-able data source for legacy or uninstrumented code.
     * @param create_derivatives If true, the manager will auto-generate velocity/acceleration sources.
     */
    template<typename T>
    void registerDataSource(const std::string& id, std::function<T()> fetcher, const std::string& description, bool create_derivatives = false);

    /**
     * @brief Retrieves the latest fully-profiled data packet for a source.
     */
    template<typename T>
    std::optional<std::shared_ptr<const ProfiledData<T>>> getData(const std::string& id);

    // --- Discovery & Analysis API ---

    std::vector<std::string> getAllDataSourceIds() const;
    std::string getDataSourceDescription(const std::string& id) const;
    const std::type_info& getDataSourceType(const std::string& id) const;
    HealthStatus getDataSourceHealth(const std::string& id) const;
    double getDataSourceFrequency(const std::string& id) const;
    LatencyProfile getLatencyProfile(const std::string& id) const;

    // --- Debugging API ---

    /**
     * @brief Dumps the global "black box" buffer of recent events to a file.
     */
    void dumpHistoryToFile(const std::string& filepath);

private:
    ObservableDataManager() = default;

    struct DataSource {
        std::shared_ptr<const std::any> last_profiled_data; // Stores the last ProfiledData<T> packet
        std::function<std::any()> fetcher; // For simple pull-based sources
        std::string description;
        const std::type_info& typeInfo;

        // --- Analysis & Health ---
        HealthStatus health = HealthStatus::Stale;
        double last_update_timestamp = 0.0;
        std::deque<double> timestamp_history;
        double measured_frequency_hz = 0.0;
    };

    mutable std::mutex m_mutex;
    std::map<std::string, DataSource> m_dataSources;

    // For the "Black Box" feature
    std::deque<std::pair<std::string, std::shared_ptr<const std::any>>> m_global_event_buffer;
    const size_t MAX_EVENT_BUFFER_SIZE = 20000;
};


//==============================================================================
// TEMPLATE METHOD DEFINITIONS
//==============================================================================

template<typename T>
void ObservableDataManager::publish(const std::string& id, ProfiledData<T>&& data) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Create a shared_ptr to a const std::any holding the ProfiledData
    auto any_ptr = std::make_shared<const std::any>(std::move(data));

    auto it = m_dataSources.find(id);
    if (it != m_dataSources.end()) {
        // Update existing source
        it->second.last_profiled_data = any_ptr;
        it->second.health = HealthStatus::Active;
        // ToDo: Update timestamp and frequency data
    }
    else {
        // Create new source on-the-fly
        m_dataSources[id] = {
            any_ptr,
            nullptr, // No fetcher for published data
            "Published data source",
            typeid(T),
            HealthStatus::Active
        };
    }

    // ToDo: Add to global event buffer
}

template<typename T>
void ObservableDataManager::registerDataSource(const std::string& id, std::function<T()> fetcher, const std::string& description, bool create_derivatives) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_dataSources.count(id)) {
        // ToDo: Log a warning, source already exists.
        return;
    }

    // Wrap the specific fetcher<T> in a generic fetcher<std::any>
    std::function<std::any()> anyFetcher = [fetcher]() {
        // This is a simplified version. A real implementation would create a full ProfiledData packet.
        return std::any(fetcher());
        };

    m_dataSources[id] = {
        nullptr, // No data until first polled
        anyFetcher,
        description,
        typeid(T),
        HealthStatus::Stale
    };

    // ToDo: Implement derivative source creation if create_derivatives is true
}


template<typename T>
std::optional<std::shared_ptr<const ProfiledData<T>>> ObservableDataManager::getData(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_dataSources.find(id);
    if (it == m_dataSources.end() || !it->second.last_profiled_data) {
        return std::nullopt;
    }

    // Try to cast the std::any back to the specific ProfiledData<T> type
    try {
        // The stored object is a shared_ptr<const std::any>. We need to get the any, then cast it.
        const std::any& stored_any = *it->second.last_profiled_data;
        const auto& profiled_data = std::any_cast<const ProfiledData<T>&>(stored_any);

        // This is tricky. We can't directly return a shared_ptr to the casted result because
        // its lifetime is tied to the std::any. The safest way is to create a new shared_ptr
        // that shares ownership with the original std::any pointer.
        // This is an advanced use of the aliasing constructor of std::shared_ptr.
        return std::shared_ptr<const ProfiledData<T>>(it->second.last_profiled_data, &profiled_data);
    }
    catch (const std::bad_any_cast& e) {
        // Type mismatch, which indicates a programming error.
        // ToDo: Log an error.
        return std::nullopt;
    }
}
