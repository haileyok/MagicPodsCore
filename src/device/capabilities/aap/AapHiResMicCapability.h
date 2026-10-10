// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapCapability.h"
#include "sdk/aap/AapHiResAudio.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace MagicPodsCore
{
    // Hi-res microphone capability (AACP message_type 0x58, AAC-ELD @ 64 kHz).
    //
    // While enabled and the device is connected, a virtual PulseAudio source
    // named AirPodsHiRes is created (module-pipe-source fed from a FIFO). An
    // activity monitor watches that source for recording clients: on the
    // first one it sends the 0x58 START packet, and type-0x58 audio SDUs are
    // diverted from the normal opcode dispatch into a bounded queue. A decode
    // thread demuxes the AAC-ELD frames, decodes them with libavcodec and
    // writes s16le mono 64 kHz PCM into the FIFO. When the last recording
    // client goes away (or the stream stalls for 2 s), the 0x58 STOP packet
    // is sent and the capture is torn down. This mirrors iOS: A2DP keeps
    // playing, the mic rides the control channel.
    class AapHiResMicCapability : public AapCapability
    {
    private:
        static constexpr std::chrono::milliseconds MonitorPollInterval{400};
        static constexpr std::chrono::milliseconds StallTimeout{2000};
        static constexpr std::chrono::milliseconds A2dpResetDelay{800};
        static constexpr size_t AudioQueueCapacity = 256;
        static constexpr float LevelRelease = 0.85f;

        // ---- audio SDU queue (divert -> decoder, drop-oldest on overflow) ----
        std::mutex _queueMutex{};
        std::condition_variable _queueNotEmpty{};
        std::deque<std::vector<unsigned char>> _audioQueue{};
        bool _queueFlushed{false};

        void EnqueueAudioSdu(const std::vector<unsigned char> &sdu);
        std::optional<std::vector<unsigned char>> TakeAudioSdu();

        // ---- capture session state ----
        std::atomic<bool> _enabled{true};
        std::atomic<bool> _monitorRunning{false};
        std::atomic<bool> _capturing{false};
        std::atomic<float> _level{0.0f};
        std::mutex _lastSduMutex{};
        std::optional<std::chrono::steady_clock::time_point> _lastSdu{};
        std::thread _monitorThread{};
        std::thread _decodeThread{};
        std::mutex _monitorMutex{};
        std::mutex _captureMutex{};
        size_t _audioEventId{};
        size_t _connectedEventId{};
        uint32_t _pulseModuleIndex{std::numeric_limits<uint32_t>::max()};
        int _fifoFd{-1};

        void MarkSdu();
        std::optional<std::chrono::milliseconds> SinceLastSdu();

        void MonitorLoop();
        void DecodeLoop(HiResMicDecoder &decoder);
        void StartMonitor();
        bool StartCapture();
        void StopCapture();
        void ResetA2dp();

        bool OpenVirtualSource();
        void CloseVirtualSource();
        void FireStateChanged();

    protected:
        nlohmann::json CreateJsonBody() override;
        void OnReceivedData(const std::vector<unsigned char> &data) override;
        void Reset() override;

    public:
        explicit AapHiResMicCapability(AapDevice &device);
        ~AapHiResMicCapability() override;
        void SetFromJson(const nlohmann::json &json) override;
    };
}
