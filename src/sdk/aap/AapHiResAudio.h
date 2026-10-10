// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "Logger.h"

#include <cstdint>
#include <functional>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace MagicPodsCore
{
    // AACP framing helpers for the proprietary hi-res microphone stream
    // (AAC-ELD @ 64 kHz, message_type 0x58 over L2CAP PSM 0x1001).
    namespace AapHiResAudio
    {
        // Actual sample rate of the decoded stream. The AirPods advertise
        // 48 kHz in the AudioSpecificConfig (AOT 39 ELD, index 3) but send
        // 64000 Hz audio; the decoder is opened at 48000 and the output is
        // treated as 64 kHz (known quirk, matches iOS behavior).
        constexpr uint32_t SampleRate = 64000;
        constexpr size_t FrameSamples = 480;
        constexpr int Channels = 1;

        // Predicate for type-0x58 *audio* SDUs (subtype 0x0001).
        // Bytes: [0]=0x04 [2]=0x04 u16le@4=0x58 u16le@6=0x0001.
        bool IsAudio(const std::vector<unsigned char> &sdu);

        // Walk the sub-frames of one type-0x58 audio SDU (22-byte header,
        // then N x [ts:u32 LE][len:u8][AAC AU]) invoking emit per AAC-ELD
        // AU. Returns the number of AUs emitted.
        size_t DemuxType58(const std::vector<unsigned char> &sdu,
                           const std::function<void(const unsigned char *au, size_t len)> &emit);
    }

    // AAC-ELD decoder using libavcodec (LGPL) for the AirPods proprietary
    // hi-res uplink: AOT 39, mono, 64000 Hz, 480-sample frames (7.5 ms).
    class HiResMicDecoder
    {
    private:
        AVCodecContext *_ctx{nullptr};
        AVPacket *_pkt{nullptr};
        AVFrame *_frame{nullptr};
        std::vector<unsigned char> _inbuf{};

        void Free();

        static int16_t FloatToS16(float sample);

    public:
        HiResMicDecoder() = default;
        HiResMicDecoder(const HiResMicDecoder&) = delete;
        HiResMicDecoder& operator=(const HiResMicDecoder&) = delete;
        HiResMicDecoder(HiResMicDecoder &&other) noexcept;
        HiResMicDecoder& operator=(HiResMicDecoder &&other) noexcept;
        ~HiResMicDecoder();

        // Open the AAC-ELD decoder. Returns false on failure.
        bool Open();

        // Decode one access unit, appending interleaved s16 PCM to out.
        // Returns the number of samples appended, or -1 on a decode error.
        int Decode(const unsigned char *au, size_t len, std::vector<int16_t> &out);
    };
}
