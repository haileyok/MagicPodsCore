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
    // Z-Y-X) — used to report the fused orientation.
    static void QuaternionToEuler(double qw, double qx, double qy, double qz,
                                   double &yaw, double &pitch, double &roll)
    {
        double sinr = 2.0 * (qw * qx + qy * qz);
        double cosr = 1.0 - 2.0 * (qx * qx + qy * qy);
        roll = std::atan2(sinr, cosr) * 180.0 / M_PI;

        double sinp = 2.0 * (qw * qy - qz * qx);
        if (std::fabs(sinp) >= 1.0)
            pitch = std::copysign(90.0, sinp);
        else
            pitch = std::asin(sinp) * 180.0 / M_PI;

        double siny = 2.0 * (qw * qz + qx * qy);
        double cosy = 1.0 - 2.0 * (qy * qy + qz * qz);
        yaw = std::atan2(siny, cosy) * 180.0 / M_PI;
    }

    // Euler (ZYX intrinsic) -> unit quaternion.
    static void EulerToQuaternion(double yawDeg, double pitchDeg, double rollDeg,
                                  double &qw, double &qx, double &qy, double &qz)
    {
        double yaw = yawDeg * M_PI / 180.0;
        double pitch = pitchDeg * M_PI / 180.0;
        double roll = rollDeg * M_PI / 180.0;
        double cy = std::cos(yaw * 0.5), sy = std::sin(yaw * 0.5);
        double cp = std::cos(pitch * 0.5), sp = std::sin(pitch * 0.5);
        double cr = std::cos(roll * 0.5), sr = std::sin(roll * 0.5);
        qw = cr * cp * cy + sr * sp * sy;
        qx = sr * cp * cy - cr * sp * sy;
        qy = cr * sp * cy + sr * cp * sy;
        qz = cr * cp * sy - sr * sp * cy;
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
                const auto &s = _lastSample.value();
                bodyJson["yaw"] = _yaw;
                bodyJson["pitch"] = _pitch;
                bodyJson["roll"] = _roll;
                double qw{}, qx{}, qy{}, qz{};
                EulerToQuaternion(_yaw, _pitch, _roll, qw, qx, qy, qz);
                bodyJson["quaternion"] = nlohmann::json::object({
                    {"w", qw},
                    {"x", qx},
                    {"y", qy},
                    {"z", qz},
                });
                // raw sensor values (for calibration/diagnosis)
                bodyJson["gyro"] = nlohmann::json::array({s.gyroYaw, s.gyroPitch, s.gyroRoll});
                bodyJson["accel"] = nlohmann::json::array({s.accelX, s.accelY, s.accelZ});
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

    // Fusion update: integrate the bud-A gyro, correct pitch/roll with the
    // accel gravity vector (complementary filter), hold heading when still.
    void AapHeadTrackingCapability::OnSample(const AapHeadTrackingSample &sample, double dt)
    {
        std::lock_guard<std::mutex> lock(_sampleMutex);
        _lastSample = sample;

        // Gyro rates in deg/s (scale from calibration; see header note).
        const double gYaw = sample.gyroYaw * GyroScaleDegPerRaw;
        const double gPitch = sample.gyroPitch * GyroScaleDegPerRaw;
        const double gRoll = sample.gyroRoll * GyroScaleDegPerRaw;

        const bool still = std::fabs(gYaw) < StillThresholdDegPerS &&
                           std::fabs(gPitch) < StillThresholdDegPerS &&
                           std::fabs(gRoll) < StillThresholdDegPerS;

        if (!still)
        {
            _yaw += gYaw * dt;
            _pitch += gPitch * dt;
            _roll += gRoll * dt;
        }

        // Accel gravity correction for pitch/roll (slow, absolute). The accel
        // axis mapping is part of the pending calibration; for now correct
        // toward the gravity-implied angles only when the estimate is fresh.
        if (!_haveGravity)
        {
            // First sample establishes the reference gravity vector: define
            // it as the neutral pose (worn, facing forward).
            _gravX = sample.accelX;
            _gravY = sample.accelY;
            _gravZ = sample.accelZ;
            _haveGravity = true;
            return;
        }

        // Pitch/roll correction from gravity direction vs reference. The
        // reference gravity rotates with the head; the angle between current
        // and reference gravity approximates pitch/roll tilt (small-angle).
        // Full quaternion gravity correction comes with the calibration pass.
        double dx = sample.accelX - _gravX;
        double dy = sample.accelY - _gravY;
        double dz = sample.accelZ - _gravZ;
        (void)dx; (void)dy; (void)dz; // used after calibration fixes axis mapping
        // (Calibration TODO: apply AccelCorrectionGain pull once the accel
        // axes are mapped during the scripted 90° capture session.)
    }

    void AapHeadTrackingCapability::OnReceivedData(const std::vector<unsigned char> &data)
    {
        if (!_enabled.load() || !_tracking.load())
            return;

        if (data.size() >= 8 && data[4] == 0x17 && data[5] == 0x00)
        {
            auto sample = AapHeadTrackingDecode::DecodeFrame(data);
            if (sample.has_value())
            {
                auto now = std::chrono::steady_clock::now();
                double dt = 0.02; // 50 Hz nominal
                if (_frameCount.load() > 0)
                {
                    dt = std::chrono::duration<double>(now - _lastFrameTime).count();
                    if (dt <= 0 || dt > 0.2)
                        dt = 0.02; // stale gap — clamp to nominal
                }
                _lastFrameTime = now;
                _frameCount.fetch_add(1);
                OnSample(sample.value(), dt);
                if (_frameCount.load() == 1)
                    FireStateChanged();
            }
            else if (AapHeadTrackingDecode::IsSensorReport(data) && !_stopping.load())
            {
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
            _yaw = _pitch = _roll = 0.0;
            _haveGravity = false;
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
