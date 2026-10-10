// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapMediaInformationNewDevice.h"

namespace MagicPodsCore
{
    AapMediaInformationNewDevice::AapMediaInformationNewDevice(const std::string &selfMac, const std::string &targetMac, const std::string &btName)
        : AapRequest{"AapMediaInformationNewDevice"}, _selfMac{selfMac}, _targetMac{targetMac}, _btName{btName}
    {
    }

    std::vector<unsigned char> AapMediaInformationNewDevice::Request() const
    {
        std::vector<unsigned char> request{0x04, 0x00, 0x04, 0x00, 0x10, 0x00};
        auto targetMac = AapSmartRoutingMac::ParseMacAddress(_targetMac); // stored little-endian (reversed)
        if (!targetMac.has_value())
            return {};
        request.insert(request.end(), targetMac->begin(), targetMac->end());
        request.insert(request.end(), {
            0x6C, 0x00,                         // body length
            0x01, 0xE5, 0x4A,                   // unknown, constant
            'p', 'l', 'a', 'y', 'i', 'n', 'g', 'A', 'p', 'p',
            0x42,                               // 'B'
            'N', 'A',
            0x52,                               // 'R'
            'h', 'o', 's', 't', 'S', 't', 'r', 'e', 'a', 'm', 'i', 'n', 'g', 'S', 't', 'a', 't', 'e',
            0x42,                               // 'B'
            'N', 'O',
            0x49,                               // 'I'
            'b', 't', 'A', 'd', 'd', 'r', 'e', 's', 's',
            0x51,                               // 'Q'
        });
        request.insert(request.end(), _selfMac.begin(), _selfMac.end());
        request.insert(request.end(), {
            0x46,                               // 'F'
            'b', 't', 'N', 'a', 'm', 'e',
            0x47,                               // 'G'
        });
        request.insert(request.end(), _btName.begin(), _btName.end());
        request.insert(request.end(), {
            0x58,                               // 'X'
            'o', 't', 'h', 'e', 'r', 'D', 'e', 'v', 'i', 'c', 'e',
            'A', 'u', 'd', 'i', 'o', 'C', 'a', 't', 'e', 'g', 'o', 'r', 'y',
            0x30, 0x64,
        });
        request.resize(122, 0x00); // LibrePods allocates a 116-byte body (zero-padded)
        return request;
    }
}
