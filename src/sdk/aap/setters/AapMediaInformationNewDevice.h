// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"
#include "AapSmartRoutingMac.h"

#include <string>

namespace MagicPodsCore
{
    // Smart Routing media information, new-device variant (AACP
    // message_type 0x10): announces this host as a fresh device that is not
    // streaming (PlayingApp "NA", HostStreamingState "NO"). Ported verbatim
    // from LibrePods AACPManager.createMediaInformationNewDevicePacket.
    class AapMediaInformationNewDevice : public AapRequest
    {
    private:
        std::string _selfMac;
        std::string _targetMac;
        std::string _btName;

    public:
        AapMediaInformationNewDevice(const std::string &selfMac, const std::string &targetMac, const std::string &btName);
        std::vector<unsigned char> Request() const override;
    };
}
