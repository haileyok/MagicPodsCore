// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHeadTrackingCapability.h"

#include "Logger.h"
#include "StringUtils.h"

#include <cmath>
#include <thread>

namespace MagicPodsCore
{
    // Yaw/pitch/roll (degrees) from a unit quaternion (intrinsic Tait-Bryan
    // Z-Y-X, matching the spatializer's Azimuth/Elevation semantics).
    static void QuaternionToEuler(const AapHeadTrackingSample &q, double &yaw, double &pitch, double &roll)
    {
        double sinr = 2.0 * (q.w * q.x + q.y * q.z);
        double cosr = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
        roll = std::atan2(sinr, cosr) * 180.0 / M_PI;

        double sinp = 2.0 * (q.w * q.y - q.z * q.x);
        if (std::fabs(sinp) >= 1.0)
            pitch = std::copysign(90.0, sinp);
        else
            pitch = std::asin(sinp) * 180.0 / M_PI;

        double siny = 2.0 * (q.w * q.z + q.x * q.y);
        double cosy = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
        yaw = std::atan2(siny, cosy) * 180.0 / M_PI;
    }

    AapHeadTrackingCapability::AapHeadTrackingCapability(AapDevice &device) : AapCapability("head_tracking", false, device)
    {
        if (auto setting = device.LoadSettingInt("head_tracking_enabled"); setting.has_value())
            _enabled.store(setting.value() != 0);

        isAvailable = true;
        FireStateChanged();
    }

    nlohmann::json AapHeadTrackingCapability::CreateJsonBody()
    {
        auto bodyJson = nlohmann::json::object();
        bodyJson["enabled"] = _enabled.load();
        bodyJson["tracking"] = _tracking.load();
        bodyJson["frames"] = _frameCount.load();
        {
            std::lock_guard<std::mutex> lock(_sampleMutex);
            if (_lastSample.has_value())
            {
                const auto &q = _lastSample.value();
                double yaw{}, pitch{}, roll{};
                QuaternionToEuler(q, yaw, pitch, roll);
                bodyJson["quaternion"] = nlohmann::json::object({
                    {"w", q.w},
                    {"x", q.x},
                    {"y", q.y},
                    {"z", q.z},
                });
                bodyJson["yaw"] = yaw;
                bodyJson["pitch"] = pitch;
                bodyJson["roll"] = roll;
            }
        }
        return bodyJson;
    }

    void AapHeadTrackingCapability::FireStateChanged()
    {
        if (!isAvailable)
            isAvailable = true;
        _onChanged.FireEvent(*this);
    }

    void AapHeadTrackingCapability::StartTracking()
    {
        if (_tracking.exchange(true))
            return;

        _frameCount.store(0);
        _lastFrame = std::chrono::steady_clock::now();

        // RTBuddy service-init dance (mandatory: without it the buds drop the
        // channel on the 0x17 start; pacing 180/220 ms per FIocker).
        const std::pair<const char *, AapRequest *> dance[4] = {
            {"rtbuddy connect svc0", nullptr},
            {"rtbuddy caps svc0", nullptr},
            {"rtbuddy connect svc4", nullptr},
            {"rtbuddy caps svc4", nullptr},
        };
        (void)dance; // silence unused when refactored

        AapRtBuddyConnectService0 c0{};
        SendData(c0);
        std::this_thread::sleep_for(RtBuddyPaceEven);
        AapRtBuddyCapabilitiesService0 p0{};
        SendData(p0);
        std::this_thread::sleep_for(RtBuddyPaceOdd);
        AapRtBuddyConnectService4 c4{};
        SendData(c4);
        std::this_thread::sleep_for(RtBuddyPaceEven);
        AapRtBuddyCapabilitiesService4 p4{};
        SendData(p4);
        std::this_thread::sleep_for(RtBuddyPaceOdd);

        AapHeadTrackingStart start{_legacyStart};
        SendData(start);
        Logger::Info("HeadTracking: START sent (%s variant): %s",
                     _legacyStart ? "legacy" : "devmotion6",
                     StringUtils::BytesToHexString(start.Request()).c_str());
    }

    void AapHeadTrackingCapability::StopTracking()
    {
        if (!_tracking.exchange(false))
            return;
        _stopping.store(true);
        AapHeadTrackingStop stop{_legacyStart};
        SendData(stop);
        Logger::Info("HeadTracking: STOP sent: %s",
                     StringUtils::BytesToHexString(stop.Request()).c_str());
        _stopping.store(false);
    }

    void AapHeadTrackingCapability::OnReceivedData(const std::vector<unsigned char> &data)
    {
        if (!_enabled.load() || !_tracking.load())
            return;

        // Only sensor-report frames decode; other 0x17 traffic is ignored.
        if (data.size() >= 8 && data[4] == 0x17 && data[5] == 0x00)
        {
            auto sample = AapHeadTrackingDecode::DecodeFrame(data);
            if (sample.has_value())
            {
                _frameCount.fetch_add(1);
                _lastFrame = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> lock(_sampleMutex);
                    _lastSample = sample.value();
                }
                // Fire on every frame is too chatty over the websocket for the
                // frontend; the driver polls the JSON directly. Fire only on
                // state transitions (first frame / stall recovery).
                if (_frameCount.load() == 1)
                    FireStateChanged();
            }
            else if (AapHeadTrackingDecode::IsSensorReport(data) && !_stopping.load())
            {
                // Sensor report that failed to decode: only warn when it is
                // not the known 20-byte ACK-shaped frames right after start.
                static size_t warned = 0;
                if (warned++ < 5)
                    Logger::Warn("HeadTracking: undecodable sensor report: %s",
                                 StringUtils::BytesToHexString(data).c_str());
            }
        }
    }

    void AapHeadTrackingCapability::Reset()
    {
        if (_tracking.load())
        {
            _tracking.store(false);
            _stopping.store(true);
            AapHeadTrackingStop stop{_legacyStart};
            SendData(stop);
            _stopping.store(false);
        }
        {
            std::lock_guard<std::mutex> lock(_sampleMutex);
            _lastSample.reset();
        }
        _frameCount.store(0);
        FireStateChanged();
        // Deliberately not calling AapCapability::Reset(): the enabled toggle
        // is a device-level setting that survives disconnects.
    }

    void AapHeadTrackingCapability::SetFromJson(const nlohmann::json &json)
    {
        if (!json.contains(name))
            return;

        const auto &capability = json.at(name);

        if (capability.contains("enabled") && capability["enabled"].is_boolean())
        {
            bool enabled = capability["enabled"].get<bool>();
            if (enabled != _enabled.load())
            {
                _enabled.store(enabled);
                device.SaveSettingInt("head_tracking_enabled", enabled ? 1 : 0);
                Logger::Info("HeadTracking: %s", enabled ? "enabled" : "disabled");
                if (!enabled && _tracking.load())
                    StopTracking();
                FireStateChanged();
            }
            return;
        }

        if (capability.contains("tracking") && capability["tracking"].is_boolean())
        {
            bool tracking = capability["tracking"].get<bool>();
            if (!_enabled.load())
            {
                Logger::Warn("HeadTracking: refusing tracking request: capability disabled");
                return;
            }
            if (!device.GetConnected())
            {
                Logger::Warn("HeadTracking: refusing tracking request: device not connected");
                return;
            }
            if (tracking && !_tracking.load())
                StartTracking();
            else if (!tracking && _tracking.load())
                StopTracking();
            FireStateChanged();
            return;
        }
    }
}
