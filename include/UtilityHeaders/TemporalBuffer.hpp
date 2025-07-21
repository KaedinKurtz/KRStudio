#include <deque>

// A self-maintaining, fixed-size FIFO buffer for time-series analysis.
template<typename T>
class TemporalBuffer {
public:
    explicit TemporalBuffer(size_t maxSize) : m_maxSize(maxSize) {}

    void add(const TimestampedData<T>& data) {
        m_buffer.push_back(data);
        if (m_buffer.size() > m_maxSize) {
            m_buffer.pop_front();
        }
    }

    const std::deque<TimestampedData<T>>& getData() const { return m_buffer; }
    size_t size() const { return m_buffer.size(); }
    bool isFull() const { return m_buffer.size() == m_maxSize; }

private:
    size_t m_maxSize;
    std::deque<TimestampedData<T>> m_buffer;
};