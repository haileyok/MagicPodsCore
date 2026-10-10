// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapMediaInformation.h"

namespace MagicPodsCore
{
    AapMediaInformation::AapMediaInformation(const std::string &selfMac, const std::string &targetMac, bool streamingState, const std::string &btName)
        : AapRequest{"AapMediaInformation"}, _selfMac{selfMac}, _targetMac{targetMac}, _streamingState{streamingState}, _btName{btName}
    {
    }

    std::vector<unsigned char> AapMediaInformation::Request() const
    {
        std::vector<unsigned char> request{0x04, 0x00, 0x04, 0x00, 0x10, 0x00};
        auto targetMac = AapSmartRoutingMac::ParseMacAddress(_targetMac); // stored little-endian (reversed)
        if (!targetMac.has_value())
            return {};
        request.insert(request.end(), targetMac->begin(), targetMac->end());
        request.insert(request.end(), {
            0x82, 0x00,                         // body length
            0x01, 0xE5, 0x4A,                   // unknown, constant
            'P', 'l', 'a', 'y', 'i', 'n', 'g', 'A', 'p', 'p',
            0x56,                               // 'V', identifier/separator
            'c', 'o', 'm', '.', 'g', 'o', 'o', 'g', 'l', 'e', '.', 'i', 'o', 's', '.', 'y', 'o', 'u', 't', 'u', 'b', 'e',
            0x52,                               // 'R'
            'H', 'o', 's', 't', 'S', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g', 'S', 't', 'a', 't', 'e',
            0x42,                               // 'B'
        });
        const std::string streaming = _streamingState ? "YES" : "NO";
        request.insert(request.end(), streaming.begin(), streaming.end());
        request.insert(request.end(), {
            0x49,                               // 'I'
            'b', 't', 'A', 'd', 'd', 'r', 'e', 's', 's',
            0x51,                               // 'Q'
        });
        request.insert(request.end(), _selfMac.begin(), _selfMac.end());
        request.insert(request.end(), {
            'b', 't', 'N', 'a', 'm', 'e',
            0x47,                               // 'G'
        });
        request.insert(request.end(), _btName.begin(), _btName.end());
        request.insert(request.end(), {
            0x58,                               // 'X'
            'o', 't', 'h', 'e', 'r', 'D', 'e', 'v', 'i', 'c', 'e',
            'A', 'u', 'd', 'i', 'o', 'C', 'a', 't', 'e', 'g', 'o', 'r', 'y',
            0x31, 0x2D, 0x01,
        });
        request.resize(144, 0x00); // LibrePods allocates a 138-byte body (zero-padded)
        return request;
    }
}
