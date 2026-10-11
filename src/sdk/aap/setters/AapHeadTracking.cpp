// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHeadTracking.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace MagicPodsCore
{
    // ---------------------------------------------------------------------------
    // RTBuddy service-init dance
    // ---------------------------------------------------------------------------

    AapRtBuddyConnectService0::AapRtBuddyConnectService0() : AapRequest{"AapRtBuddyConnectService0"}
    {
    }

    std::vector<unsigned char> AapRtBuddyConnectService0::Request() const
    {
        // 00 00 00 00 | 01 00 | 03 00 | 00*8  (connect, service 0)
        return std::vector<unsigned char>{
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    AapRtBuddyCapabilitiesService0::AapRtBuddyCapabilitiesService0() : AapRequest{"AapRtBuddyCapabilitiesService0"}
    {
    }

    std::vector<unsigned char> AapRtBuddyCapabilitiesService0::Request() const
    {
        return std::vector<unsigned char>{0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    }

    AapRtBuddyConnectService4::AapRtBuddyConnectService4() : AapRequest{"AapRtBuddyConnectService4"}
    {
    }

    std::vector<unsigned char> AapRtBuddyConnectService4::Request() const
    {
        // 00 00 04 00 | 01 00 | 03 00 | 00*8  (connect, service 4)
        return std::vector<unsigned char>{
            0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    AapRtBuddyCapabilitiesService4::AapRtBuddyCapabilitiesService4() : AapRequest{"AapRtBuddyCapabilitiesService4"}
    {
    }

    std::vector<unsigned char> AapRtBuddyCapabilitiesService4::Request() const
    {
        return std::vector<unsigned char>{0x04, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00};
    }

    // ---------------------------------------------------------------------------
    // 0x17 start / stop
    // ---------------------------------------------------------------------------

    AapHeadTrackingStart::AapHeadTrackingStart(bool legacy) : AapRequest{"AapHeadTrackingStart"}, _legacy{legacy}
    {
    }

    std::vector<unsigned char> AapHeadTrackingStart::Request() const
    {
        if (_legacy)
        {
            // LibrePods/Android createStartHeadTrackingPacket (service-14 framing).
            // On AirPods Pro 2 this variant alone did not start the stream (the
            // RTBuddy dance is the decisive part); kept as a fallback.
            return std::vector<unsigned char>{
                0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
                0x10, 0x00, 0x10, 0x00, 0x08, 0xA1, 0x02, 0x42,
                0x0B, 0x08, 0x0E, 0x10, 0x02, 0x1A, 0x05, 0x01,
                0x40, 0x9C, 0x00, 0x00};
        }
        // RTBuddy SensorDataWX(seq=0x93) ServiceSetting(DEVMOTION6=16, mode=2,
        // interval=20,000 us = 50 Hz). Verified live on AirPods Pro 2.
        return std::vector<unsigned char>{
            0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x10, 0x00, 0x08, 0x93, 0x01, 0x42,
            0x0B, 0x08, 0x10, 0x10, 0x02, 0x1A, 0x05, 0x01,
            0x20, 0x4E, 0x00, 0x00};
    }

    AapHeadTrackingStop::AapHeadTrackingStop(bool legacy) : AapRequest{"AapHeadTrackingStop"}, _legacy{legacy}
    {
    }

    std::vector<unsigned char> AapHeadTrackingStop::Request() const
    {
        if (_legacy)
        {
            return std::vector<unsigned char>{
                0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
                0x10, 0x00, 0x11, 0x00, 0x08, 0x7E, 0x10, 0x02,
                0x42, 0x0B, 0x08, 0x4E, 0x10, 0x02, 0x1A, 0x05,
                0x01, 0x00, 0x00, 0x00, 0x00};
        }
        // RTBuddy stop: next unused local request id (0x95); service fields
        // mirror the start; mode/interval zeroed.
        return std::vector<unsigned char>{
            0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x10, 0x00, 0x08, 0x95, 0x01, 0x42,
            0x0B, 0x08, 0x10, 0x10, 0x02, 0x1A, 0x05, 0x01,
            0x00, 0x00, 0x00, 0x00};
    }

    // ---------------------------------------------------------------------------
    // Frame decode (protobuf SensorDataWX, hand-rolled walk — the payload is
    // small and stable; a full protobuf dependency is not warranted)
    // ---------------------------------------------------------------------------

    namespace
    {
        // Minimal protobuf wire reader for the few fields we need.
        struct WireField
        {
            unsigned number{};
            unsigned wireType{};
            // For wire type 2: offset/length into the buffer.
            size_t offset{};
            size_t length{};
            uint64_t varint{};
        };

        bool ReadVarint(const std::vector<unsigned char> &buf, size_t &pos, uint64_t &out)
        {
            uint64_t value = 0;
            unsigned shift = 0;
            while (pos < buf.size() && shift <= 63)
            {
                unsigned char b = buf[pos++];
                value |= static_cast<uint64_t>(b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                {
                    out = value;
                    return true;
                }
                shift += 7;
            }
            return false;
        }

        // Walk one level of protobuf fields in buf[base..base+size).
        bool NextField(const std::vector<unsigned char> &buf, size_t &pos, size_t end, WireField &out)
        {
            if (pos >= end)
                return false;
            uint64_t tag = 0;
            size_t tagStart = pos;
            if (!ReadVarint(buf, pos, tag))
                return false;
            out.number = static_cast<unsigned>(tag >> 3);
            out.wireType = static_cast<unsigned>(tag & 7);
            switch (out.wireType)
            {
            case 0:
                if (!ReadVarint(buf, pos, out.varint))
                    return false;
                return true;
            case 2: {
                uint64_t len = 0;
                if (!ReadVarint(buf, pos, len))
                    return false;
                out.offset = pos;
                out.length = static_cast<size_t>(len);
                if (pos + out.length > end)
                    return false;
                pos += out.length;
                return true;
            }
            default:
                // Fixed32/64 and groups not expected in these messages.
                return false;
            }
        }
    }

    bool AapHeadTrackingDecode::IsSensorReport(const std::vector<unsigned char> &frame)
    {
        // Frame layout (verified against live captures):
        //   04 00 04 00 17 00 00 00 | 10 00 | <plen u16 LE> | protobuf...
        //   byte[8] = 0x10 marker, byte[10..11] = plen (varies!), byte[12] =
        //   0x08 (protobuf field-1 tag), byte[13] = seq (varies!).
        if (frame.size() < 14)
            return false;
        if (frame[0] != 0x04 || frame[2] != 0x04)
            return false;
        if (frame[4] != 0x17 || frame[5] != 0x00)
            return false;
        if (frame[8] != 0x10 || frame[9] != 0x00)
            return false;
        if (frame[12] != 0x08)
            return false;
        return true;
    }

    std::optional<AapHeadTrackingSample> AapHeadTrackingDecode::DecodeFrame(const std::vector<unsigned char> &frame)
    {
        if (!IsSensorReport(frame))
            return std::nullopt;

        // protobuf body starts at 12 (after plen u16 at 10)
        size_t pos = 12;
        size_t end = frame.size();
        WireField top{};
        while (NextField(frame, pos, end, top))
        {
            if (top.number == 7 && top.wireType == 2)
            {
                // Command { service = 1, payload = 3 }
                size_t cpos = top.offset;
                size_t cend = top.offset + top.length;
                WireField cmd{};
                while (NextField(frame, cpos, cend, cmd))
                {
                    if (cmd.number == 3 && cmd.wireType == 2 && cmd.length >= 48)
                    {
                        const unsigned char *p = frame.data() + cmd.offset;
                        const size_t plen = cmd.length;

                        auto readI16 = [&](size_t off) -> int16_t {
                            int16_t v{};
                            std::memcpy(&v, p + off, 2);
                            return v;
                        };

                        AapHeadTrackingSample sample{};
                        // bud-A gyro triple (yaw, pitch, roll)
                        sample.gyroYaw = readI16(26);
                        sample.gyroPitch = readI16(28);
                        sample.gyroRoll = readI16(30);
                        // accel gravity vector (non-contiguous cluster)
                        sample.accelX = readI16(33);
                        sample.accelY = readI16(44);
                        sample.accelZ = readI16(46);
                        // stored neutral-pose reference quaternion (Q15)
                        {
                            double qx = readI16(34) / 32768.0;
                            double qy = readI16(36) / 32768.0;
                            double qz = readI16(38) / 32768.0;
                            double s = qx * qx + qy * qy + qz * qz;
                            sample.neutralW = s <= 1.0 ? std::sqrt(1.0 - s) : 0.0;
                            sample.neutralX = qx;
                            sample.neutralY = qy;
                            sample.neutralZ = qz;
                        }
                        (void)plen;
                        return sample;
                    }
                }
            }
        }
        return std::nullopt;
    }
}
