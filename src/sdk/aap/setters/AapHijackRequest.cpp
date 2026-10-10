// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHijackRequest.h"

namespace MagicPodsCore
{
    AapHijackRequest::AapHijackRequest(const std::string &targetMac) : AapRequest{"AapHijackRequest"}, _targetMac{targetMac}
    {
    }

    std::vector<unsigned char> AapHijackRequest::Request() const
    {
        // Total size 112 = AACP framing (6) + the 106-byte body the LibrePods
        // reference allocates (content fills it exactly).
        std::vector<unsigned char> request{0x04, 0x00, 0x04, 0x00, 0x10, 0x00};
        auto targetMac = AapSmartRoutingMac::ParseMacAddress(_targetMac); // stored little-endian (reversed)
        if (!targetMac.has_value())
            return {};
        request.insert(request.end(), targetMac->begin(), targetMac->end());
        request.insert(request.end(), {
            0x62, 0x00,                         // body length
            0x01, 0xE5,                         // unknown, constant
            0x4A,                               // localscore
            'l', 'o', 'c', 'a', 'l', 's', 'c', 'o', 'r', 'e',
            0x30, 0x64,
            0x46,                               // reason
            'r', 'e', 'a', 's', 'o', 'n',
            0x48,                               // Hijackv2
            'H', 'i', 'j', 'a', 'c', 'k', 'v', '2',
            0x51,                               // audioRoutingScore
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'c', 'o', 'r', 'e',
            0x31, 0x2D, 0x01,
            0x5F,                               // audioRoutingSetOwnershipToFalse
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'e', 't', 'O', 'w', 'n', 'e', 'r', 's', 'h', 'i', 'p', 'T', 'o', 'F', 'a', 'l', 's', 'e',
            0x01,
            0x4B,                               // remotescore
            'r', 'e', 'm', 'o', 't', 'e', 's', 'c', 'o', 'r', 'e',
            0xA5,
        });
        request.resize(112, 0x00); // LibrePods allocates a 106-byte body
        return request;
    }
}
