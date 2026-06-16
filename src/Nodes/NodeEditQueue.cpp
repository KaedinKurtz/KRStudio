#include "NodeEditQueue.hpp"
#include <cstdio>

namespace krs::nodes {

NodeEditQueue& NodeEditQueue::instance() { static NodeEditQueue q; return q; }

void NodeEditQueue::setDeferred(bool d) { m_deferred.store(d); }
bool NodeEditQueue::deferred() const { return m_deferred.load(); }

void NodeEditQueue::post(const void* obj, const std::string& name, std::function<void()> fn)
{
    if (!m_deferred.load()) { fn(); return; }                 // immediate (gates): apply + recompute now
    char buf[32]; std::snprintf(buf, sizeof(buf), "%p:", obj);
    std::lock_guard<std::mutex> lk(m_mtx);
    m_latest[std::string(buf) + name] = std::move(fn);        // deferred (app): coalesce by (obj,name)
}

std::size_t NodeEditQueue::drain()
{
    std::unordered_map<std::string, std::function<void()>> local;
    { std::lock_guard<std::mutex> lk(m_mtx); local.swap(m_latest); }
    for (auto& kv : local) kv.second();
    return local.size();
}

std::size_t NodeEditQueue::pending() const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_latest.size();
}

void NodeEditQueue::cancel(const void* obj)
{
    char buf[32]; std::snprintf(buf, sizeof(buf), "%p:", obj);
    const std::string prefix(buf);
    std::lock_guard<std::mutex> lk(m_mtx);
    for (auto it = m_latest.begin(); it != m_latest.end(); )
        if (it->first.rfind(prefix, 0) == 0) it = m_latest.erase(it); else ++it;
}

} // namespace krs::nodes
