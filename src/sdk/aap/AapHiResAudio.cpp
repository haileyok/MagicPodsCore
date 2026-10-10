// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHiResAudio.h"

#include <cmath>
#include <cstring>
#include <functional>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/channel_layout.h>
}

namespace MagicPodsCore
{
    namespace
    {
        // Type-0x58 audio SDU layout: 22-byte header, then N x [ts:u32 LE][len:u8][au].
        constexpr size_t Type58HeaderLen = 22;

        // AudioSpecificConfig for the AirPods uplink: AOT 39 (ELD), 48 kHz
        // index, mono. The stream itself is 64 kHz (see AapHiResAudio::SampleRate).
        constexpr unsigned char EldAsc[4] = {0xF8, 0xE6, 0x30, 0x00};
        constexpr int EldCodingRate = 48000;
        constexpr size_t EldInbufMax = 512;

        inline uint16_t U16le(const std::vector<unsigned char> &b, size_t off)
        {
            return static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8);
        }
    }

    namespace AapHiResAudio
    {
        bool IsAudio(const std::vector<unsigned char> &sdu)
        {
            return sdu.size() >= 8
                && sdu[0] == 0x04
                && sdu[2] == 0x04
                && U16le(sdu, 4) == 0x58
                && U16le(sdu, 6) == 0x0001;
        }

        size_t DemuxType58(const std::vector<unsigned char> &sdu,
                           const std::function<void(const unsigned char *au, size_t len)> &emit)
        {
            size_t off = Type58HeaderLen;
            size_t n = 0;

            while (off + 5 <= sdu.size())
            {
                size_t auLen = sdu[off + 4];
                size_t start = off + 5;
                size_t end = start + auLen;

                if (end > sdu.size())
                    break;

                emit(sdu.data() + start, auLen);
                n++;
                off = end;
            }
            return n;
        }
    }

    HiResMicDecoder::~HiResMicDecoder()
    {
        Free();
    }

    HiResMicDecoder::HiResMicDecoder(HiResMicDecoder &&other) noexcept
        : _ctx{other._ctx}, _pkt{other._pkt}, _frame{other._frame}, _inbuf{std::move(other._inbuf)}
    {
        other._ctx = nullptr;
        other._pkt = nullptr;
        other._frame = nullptr;
    }

    HiResMicDecoder& HiResMicDecoder::operator=(HiResMicDecoder &&other) noexcept
    {
        if (this != &other)
        {
            Free();
            _ctx = other._ctx;
            _pkt = other._pkt;
            _frame = other._frame;
            _inbuf = std::move(other._inbuf);
            other._ctx = nullptr;
            other._pkt = nullptr;
            other._frame = nullptr;
        }
        return *this;
    }

    void HiResMicDecoder::Free()
    {
        if (_frame)
            av_frame_free(&_frame);
        if (_pkt)
            av_packet_free(&_pkt);
        if (_ctx)
            avcodec_free_context(&_ctx);
    }

    int16_t HiResMicDecoder::FloatToS16(float sample)
    {
        if (sample > 1.0f)
            sample = 1.0f;
        else if (sample < -1.0f)
            sample = -1.0f;
        return static_cast<int16_t>(std::lround(sample * 32767.0f));
    }

    bool HiResMicDecoder::Open()
    {
        // stop libavcodec from spamming stderr
        av_log_set_level(AV_LOG_FATAL);

        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
        if (!codec)
        {
            Logger::Error("HiResMicDecoder: AAC decoder not found");
            return false;
        }

        _ctx = avcodec_alloc_context3(codec);
        if (!_ctx)
            return false;

        // extradata = the AudioSpecificConfig bytes
        uint8_t *extradata = static_cast<uint8_t *>(av_mallocz(sizeof(EldAsc) + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!extradata)
        {
            Free();
            return false;
        }
        std::memcpy(extradata, EldAsc, sizeof(EldAsc));
        _ctx->extradata = extradata;
        _ctx->extradata_size = sizeof(EldAsc);
        _ctx->sample_rate = EldCodingRate;
        av_channel_layout_default(&_ctx->ch_layout, AapHiResAudio::Channels);

        if (avcodec_open2(_ctx, codec, nullptr) < 0)
        {
            Logger::Error("HiResMicDecoder: failed to open AAC-ELD decoder");
            Free();
            return false;
        }

        _pkt = av_packet_alloc();
        _frame = av_frame_alloc();
        if (!_pkt || !_frame)
        {
            Free();
            return false;
        }

        _inbuf.resize(EldInbufMax + AV_INPUT_BUFFER_PADDING_SIZE);
        return true;
    }

    int HiResMicDecoder::Decode(const unsigned char *au, size_t len, std::vector<int16_t> &out)
    {
        if (len == 0 || len > EldInbufMax)
            return -1;

        std::memcpy(_inbuf.data(), au, len);
        std::memset(_inbuf.data() + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        _pkt->data = _inbuf.data();
        _pkt->size = static_cast<int>(len);

        if (avcodec_send_packet(_ctx, _pkt) < 0)
            return -1;
        if (avcodec_receive_frame(_ctx, _frame) < 0)
            return -1;

        int nch = _frame->ch_layout.nb_channels;
        int ns = _frame->nb_samples;
        if (nch <= 0 || ns <= 0)
            return -1;

        size_t total = static_cast<size_t>(ns) * static_cast<size_t>(nch);
        out.reserve(out.size() + total);

        switch (_frame->format)
        {
        case AV_SAMPLE_FMT_FLTP: { // native aac: planar float
            for (int i = 0; i < ns; i++)
                for (int c = 0; c < nch; c++)
                    out.push_back(FloatToS16(reinterpret_cast<const float *>(_frame->data[c])[i]));
            break;
        }
        case AV_SAMPLE_FMT_FLT: { // packed float
            const float *p = reinterpret_cast<const float *>(_frame->data[0]);
            for (size_t i = 0; i < total; i++)
                out.push_back(FloatToS16(p[i]));
            break;
        }
        case AV_SAMPLE_FMT_S16P: { // planar s16
            for (int i = 0; i < ns; i++)
                for (int c = 0; c < nch; c++)
                    out.push_back(reinterpret_cast<const int16_t *>(_frame->data[c])[i]);
            break;
        }
        case AV_SAMPLE_FMT_S16: { // packed s16
            const int16_t *p = reinterpret_cast<const int16_t *>(_frame->data[0]);
            for (size_t i = 0; i < total; i++)
                out.push_back(p[i]);
            break;
        }
        default:
            return -1;
        }

        return static_cast<int>(total);
    }
}
