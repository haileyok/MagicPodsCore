// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"

#include <array>
#include <optional>
#include <vector>

namespace MagicPodsCore
{
    // ---------------------------------------------------------------------------
    // Head tracking (AACP opcode 0x17) — verified live on AirPods Pro 2
    // (2026-10-10, goal-002).
    //
    // The buds only honor a 0x17 START after the RTBuddy service-init dance
    // (connect service 0, capabilities, connect service 4, capabilities) has
    // established the sensor-service context — sent with 180/220 ms pacing;
    // without it the buds TERMINATE the L2CAP channel on the start packet.
    // Source of the working sequence: FIocker/aap-head-tracker (confirmed on
    // AirPods Max 2 / fw 8E258) cross-checked against the apple-wireshark
    // RTBuddy grammar (SensorDataWX / DEVMOTION6 = service 16).
    //
    // Sensor reports arrive as opcode 0x17 frames: AACP header
    // (04 00 04 00 17 00 00 00) + plen u16 + protobuf SensorDataWX
    // { f1: seq, f2: log_type, f7: Command{ f1: service (16), f3: payload } }.
    // The 58-byte payload carries a signed Q15 quaternion (x, y, z int16 LE)
    // at bytes 34..39; w = sqrt(1 - x^2 - y^2 - z^2). Validated by rotating
    // the gravity vector with the quaternion across ~1000 frames (constant
    // world gravity with the buds stationary), the same method FIocker used.
    // ---------------------------------------------------------------------------

    // RTBuddy service-init dance (four packets, sent with pacing).
    class AapRtBuddyConnectService0 : public AapRequest
    {
    public:
        AapRtBuddyConnectService0();
        std::vector<unsigned char> Request() const override;
    };

    class AapRtBuddyCapabilitiesService0 : public AapRequest
    {
    public:
        AapRtBuddyCapabilitiesService0();
        std::vector<unsigned char> Request() const override;
    };

    class AapRtBuddyConnectService4 : public AapRequest
    {
    public:
        AapRtBuddyConnectService4();
        std::vector<unsigned char> Request() const override;
    };

    class AapRtBuddyCapabilitiesService4 : public AapRequest
    {
    public:
        AapRtBuddyCapabilitiesService4();
        std::vector<unsigned char> Request() const override;
    };

    // 0x17 start/stop packets. The body encodes an RTBuddy SensorDataWX
    // ServiceSetting: service DEVMOTION6 (16), mode 2, interval 20,000 us
    // (50 Hz). The "legacy" variant is the LibrePods/Android form (service
    // 0x0E framing); both are tried in order at runtime.
    class AapHeadTrackingStart : public AapRequest
    {
    public:
        explicit AapHeadTrackingStart(bool legacy = false);
        std::vector<unsigned char> Request() const override;

    private:
        bool _legacy{false};
    };

    class AapHeadTrackingStop : public AapRequest
    {
    public:
        explicit AapHeadTrackingStop(bool legacy = false);
        std::vector<unsigned char> Request() const override;

    private:
        bool _legacy{false};
    };

    // Parsed sensor sample from a DEVMOTION6 report (AirPods Pro 2 payload
    // layout, identified via phase-correlated motion captures 2026-10-10):
    //   bud-A gyro (yaw, pitch, roll) at payload offsets 26/28/30,
    //   accel gravity vector at payload offsets 33/44/46 (non-contiguous),
    //   offsets 34..39 carry the stored neutral-pose reference quaternion
    //   (constant per pose, not live).
    struct AapHeadTrackingSample
    {
        double gyroYaw{};    // raw units, bud A
        double gyroPitch{};  // raw units, bud A
        double gyroRoll{};   // raw units, bud A
        double accelX{};
        double accelY{};
        double accelZ{};
        double neutralW{};
        double neutralX{};
        double neutralY{};
        double neutralZ{};
    };

    // Decode helpers for the 0x17 sensor-report frames (payload-level).
    namespace AapHeadTrackingDecode
    {
        // True when the frame looks like a sensor report (protobuf
        // SensorDataWX with a DEVMOTION6 command payload).
        bool IsSensorReport(const std::vector<unsigned char> &frame);

        // Extract the bud-A gyro triple + accel vector from a sensor-report
        // frame. Returns nullopt when the frame is not decodable.
        std::optional<AapHeadTrackingSample> DecodeFrame(const std::vector<unsigned char> &frame);
    }
}
