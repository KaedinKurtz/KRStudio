#pragma once

#include <librealsense2/rs.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// A simple struct to hold basic info about a connected device.
// This remains unchanged.
struct RealSenseDeviceInfo {
    std::string name;
    std::string serialNumber;
};

// A detailed struct to hold a specific, unique stream profile.
// This is used to populate the UI and configure the streams.
struct StreamProfile {
    int stream_width;
    int stream_height;
    int stream_fps;
    rs2_stream stream_type;
    rs2_format stream_format;
};

// Required for using StreamProfile in std::set
bool operator<(const StreamProfile& a, const StreamProfile& b);

class RealSenseManager {
public:
    RealSenseManager();
    ~RealSenseManager();

    // --- Device and Profile Discovery ---
    std::vector<RealSenseDeviceInfo> getAvailableDevices(); // Gets a list of connected devices.
    std::vector<StreamProfile> getSupportedProfiles(const std::string& serialNumber); // Gets all valid profiles for a device.

    // --- Streaming Control ---
    // This now takes the specific profiles you want to start. It's more explicit.
    bool startStreaming(const std::string& serialNumber, const std::vector<StreamProfile>& profiles);
    void stopStreaming(); // Stops all active sensors.

    // --- Data Retrieval ---
    // Polls the queues for the latest frames. This is thread-safe.
    bool pollFrames(rs2::frameset& out);

    // --- Error Handling ---
    std::string getLastError() const;
    bool setSensorOption(rs2_option option, float value);

private:
    // SDK objects
    rs2::context        m_context; // Manages device discovery.
    rs2::pipeline       m_pipeline;
    rs2::pipeline_profile    m_pipeProfile;
    rs2::device         m_activeDevice; // The currently streaming device.

    // Thread-safe queues for receiving frames from the SDK's internal threads.
    rs2::frame_queue    m_colorQueue;
    rs2::frame_queue    m_infraredQueue;
    bool                m_isStreaming{ false };
    // State management

    // Error message handling
    mutable std::mutex  m_errorMutex;
    std::string         m_lastError;
};
