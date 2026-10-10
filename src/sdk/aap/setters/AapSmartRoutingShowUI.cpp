// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapSmartRoutingShowUI.h"

namespace MagicPodsCore
{
    AapSmartRoutingShowUI::AapSmartRoutingShowUI(const std::string &targetMac) : AapRequest{"AapSmartRoutingShowUI"}, _targetMac{targetMac}
    {
    }

    std::vector<unsigned char> AapSmartRoutingShowUI::Request() const
    {
        std::vector<unsigned char> request{0x04, 0x00, 0x04, 0x00, 0x10, 0x00};
        auto targetMac = AapSmartRoutingMac::ParseMacAddress(_targetMac); // stored little-endian (reversed)
        if (!targetMac.has_value())
            return {};
        request.insert(request.end(), targetMac->begin(), targetMac->end());
        request.insert(request.end(), {
            0x7E, 0x00,                         // body length
            0x01, 0xE6, 0x5B,                   // unknown, constant
            'S', 'm', 'a', 'r', 't', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'K', 'e', 'y', 'S', 'h', 'o', 'w', 'N', 'e', 'a', 'r', 'b', 'y', 'U', 'I',
            0x01,                               // separator?
            0x4A,                               // localscore
            'l', 'o', 'c', 'a', 'l', 's', 'c', 'o', 'r', 'e',
            0x31, 0x2D,
            0x01,
            0x46,                               // reasonHhijackv2
            'r', 'e', 'a', 's', 'o', 'n', 'H', 'h', 'i', 'j', 'a', 'c', 'k', 'v', '2',
            0x51,                               // audioRoutingScore
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'c', 'o', 'r', 'e',
            0xA2,
            0x5F,                               // audioRoutingSetOwnershipToFalse
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'e', 't', 'O', 'w', 'n', 'e', 'r', 's', 'h', 'i', 'p', 'T', 'o', 'F', 'a', 'l', 's', 'e',
            0x01,
            0x4B,                               // remotescore
            'r', 'e', 'm', 'o', 't', 'e', 's', 'c', 'o', 'r', 'e',
            0xA2,
        });
        request.resize(140, 0x00); // LibrePods allocates a 134-byte body
        return request;
    }
}
