#include "RealSenseManager.hpp"
#include <librealsense2/rs.hpp>
#include <QDebug>
#include <set>

// Operator overload for sorting StreamProfile objects in a set.
bool operator<(const StreamProfile& a, const StreamProfile& b) {
    if (a.stream_type != b.stream_type) return a.stream_type < b.stream_type;
    if (a.stream_width != b.stream_width) return a.stream_width < b.stream_width;
    if (a.stream_height != b.stream_height) return a.stream_height < b.stream_height;
    if (a.stream_fps != b.stream_fps) return a.stream_fps < b.stream_fps;
    return a.stream_format < b.stream_format;
}

RealSenseManager::RealSenseManager() : m_isStreaming(false) {
    // --- ADD THIS LINE ---
    // This enables detailed logging from all SDK internal threads to the console.
    // It must be one of the first SDK calls in your application.
    rs2::log_to_console(RS2_LOG_SEVERITY_DEBUG);

    qDebug() << "[RS_MANAGER] Manager constructed.";
}

RealSenseManager::~RealSenseManager() {
    stopStreaming(); // Ensures clean shutdown.
    qDebug() << "[RS_MANAGER] Manager destroyed.";
}

std::vector<RealSenseDeviceInfo> RealSenseManager::getAvailableDevices() {
    std::vector<RealSenseDeviceInfo> devices;
    for (auto&& dev : m_context.query_devices()) {
        devices.push_back({ dev.get_info(RS2_CAMERA_INFO_NAME), dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) });
    }
    return devices;
}

std::vector<StreamProfile> RealSenseManager::getSupportedProfiles(const std::string& serialNumber) {
    std::set<StreamProfile> unique_profiles;
    rs2::device dev;
    for (auto&& d : m_context.query_devices()) {
        if (serialNumber == d.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
            dev = d;
            break;
        }
    }

    if (!dev) return {};

    for (const auto& sensor : dev.query_sensors()) {
        for (const auto& profile : sensor.get_stream_profiles()) {
            if (auto video_profile = profile.as<rs2::video_stream_profile>()) {
                unique_profiles.insert({
                    video_profile.width(),
                    video_profile.height(),
                    video_profile.fps(),
                    video_profile.stream_type(),
                    video_profile.format()
                    });
            }
        }
    }
    return std::vector<StreamProfile>(unique_profiles.begin(), unique_profiles.end());
}


bool RealSenseManager::startStreaming(const std::string& serial,
    const std::vector<StreamProfile>& profiles)
{
    if (m_isStreaming) {
        qDebug() << "[RS_MANAGER] startStreaming called while already streaming; ignoring.";
        return true;   // change is here
    }
    try {
        rs2::config cfg;
        cfg.enable_device(serial);
        for (auto const& p : profiles) {
            // note: this is the (stream, index, w, h, format, fps) overload
            cfg.enable_stream(p.stream_type,
                /*stream_index*/ 0,
                p.stream_width,
                p.stream_height,
                p.stream_format,
                p.stream_fps);
        }

        // Start without a callback so we can poll on the same thread
        m_pipeline = rs2::pipeline(m_context);
        m_pipeline.start(cfg);  // no callback here :contentReference[oaicite:0]{index=0}

        m_isStreaming = true;
        return true;
    }
    catch (const rs2::error& e) {
        qWarning() << "[RS_MANAGER] startStreaming failed:" << e.what();
        return false;
    }
}

void RealSenseManager::stopStreaming() {
    if (!m_isStreaming) return;
    m_pipeline.stop();
    m_isStreaming = false;
}

bool RealSenseManager::pollFrames(rs2::frameset& out)
{
    if (!m_isStreaming) return false;
    // returns immediately if no new frames
    return m_pipeline.poll_for_frames(&out);
    // you could also block with `wait_for_frames()` if you prefer
}

std::string RealSenseManager::getLastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

// Re-added this function to allow the UI to set sensor options.
bool RealSenseManager::setSensorOption(rs2_option option, float value) {
    if (!m_isStreaming || !m_activeDevice) return false;

    // Find the first active sensor that supports this option and set it.
    for (auto& sensor : m_activeDevice.query_sensors()) {
        if (sensor.supports(option) && !sensor.is_option_read_only(option)) {
            try {
                sensor.set_option(option, value);
                return true;
            }
            catch (const rs2::error& e) {
                qWarning() << "[RS_MANAGER] Failed to set option" << rs2_option_to_string(option) << ":" << e.what();
                return false;
            }
        }
    }
    return false; // Option not supported by any sensor on the active device.
}