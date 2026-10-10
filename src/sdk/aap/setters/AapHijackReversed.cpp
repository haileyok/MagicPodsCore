// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHijackReversed.h"

namespace MagicPodsCore
{
    AapHijackReversed::AapHijackReversed(const std::string &targetMac) : AapRequest{"AapHijackReversed"}, _targetMac{targetMac}
    {
    }

    std::vector<unsigned char> AapHijackReversed::Request() const
    {
        std::vector<unsigned char> request{0x04, 0x00, 0x04, 0x00, 0x10, 0x00};
        auto targetMac = AapSmartRoutingMac::ParseMacAddress(_targetMac); // stored little-endian (reversed)
        if (!targetMac.has_value())
            return {};
        request.insert(request.end(), targetMac->begin(), targetMac->end());
        request.insert(request.end(), {
            0x59, 0x00,                         // body length
            0x01, 0xE3,                         // unknown, constant
            0x5F,                               // audioRoutingSetOwnershipToFalse
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'e', 't', 'O', 'w', 'n', 'e', 'r', 's', 'h', 'i', 'p', 'T', 'o', 'F', 'a', 'l', 's', 'e',
            0x01,
            0x59,                               // audioRoutingShowReverseUI
            'a', 'u', 'd', 'i', 'o', 'R', 'o', 'u', 't', 'i', 'n', 'g', 'S', 'h', 'o', 'w', 'R', 'e', 'v', 'e', 'r', 's', 'e', 'U', 'I',
            0x01,
            0x46,                               // reason
            'r', 'e', 'a', 's', 'o', 'n',
            0x53,                               // ReverseBannerTapped
            'R', 'e', 'v', 'e', 'r', 's', 'e', 'B', 'a', 'n', 'n', 'e', 'r', 'T', 'a', 'p', 'p', 'e', 'd',
        });
        request.resize(103, 0x00); // LibrePods allocates a 97-byte body (zero-padded)
        return request;
    }
}
