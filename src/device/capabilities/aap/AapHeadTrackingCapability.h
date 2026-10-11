// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapCapability.h"
#include "sdk/aap/setters/AapHeadTracking.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace MagicPodsCore
{
    // Head-tracking capability (AACP opcode 0x17) — AirPods IMU orientation
    // stream at ~50 Hz for spatial audio.
    //
    // While enabled and the device is connected, enabling "tracking" performs
    // the RTBuddy service-init dance (mandatory — without it the buds drop the
    // L2CAP channel on the start packet) followed by the 0x17 START
    // (DEVMOTION6 SensorDataWX ServiceSetting, 50 Hz). Sensor reports arrive
    // as 0x17 frames; each is decoded into a Q15 quaternion (w reconstructed)
    // and published via the capability JSON (yaw/pitch/roll + quaternion), so
    // a spatializer driver can anchor the sound stage to the world while the
    // head moves — the Apple spatial-audio behavior.
    //
    // Verified live on AirPods Pro 2 (2026-10-10, goal-002): 996 frames at
    // 49.9 Hz, decode validated by gravity-rotation stability.
    class AapHeadTrackingCapability : public AapCapability
    {
    private:
        static constexpr std::chrono::milliseconds RtBuddyPaceEven{180};
        static constexpr std::chrono::milliseconds RtBuddyPaceOdd{220};

        // Gyro raw -> deg/s scale (calibration pending a scripted 90° capture;
        // 45°-excursion assumption: 0.0723 deg/raw — see goal-002 notes).
        static constexpr double GyroScaleDegPerRaw = 0.0723;
        // Stationary-detection: |gyro| under this (deg/s) => hold heading.
        static constexpr double StillThresholdDegPerS = 1.0;
        // Complementary-filter accel correction gain (per update).
        static constexpr double AccelCorrectionGain = 0.02;

        std::atomic<bool> _enabled{false};
        std::atomic<bool> _tracking{false};
        std::atomic<bool> _stopping{false};
        std::atomic<size_t> _frameCount{0};
        std::chrono::steady_clock::time_point _lastFrameTime{};

        // Fusion state (guarded by _sampleMutex)
        mutable std::mutex _sampleMutex{};
        double _yaw{0.0};      // deg, fused
        double _pitch{0.0};    // deg, fused
        double _roll{0.0};     // deg, fused
        bool _haveGravity{false};
        double _gravX{0.0};
        double _gravY{0.0};
        double _gravZ{0.0};
        std::optional<AapHeadTrackingSample> _lastSample{};
        bool _legacyStart{false};

        void OnSample(const AapHeadTrackingSample &sample, double dt);
        void StartTracking();
        void StopTracking();
        void FireStateChanged();

    protected:
        void OnReceivedData(const std::vector<unsigned char> &data) override;
        void Reset() override;

    public:
        explicit AapHeadTrackingCapability(AapDevice &device);
        ~AapHeadTrackingCapability() override = default;

        nlohmann::json CreateJsonBody() override;
        void SetFromJson(const nlohmann::json &json) override;
    };
}
