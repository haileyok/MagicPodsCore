// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"
#include "AapSmartRoutingMac.h"

#include <string>

namespace MagicPodsCore
{
    // Smart Routing media information announce (AACP message_type 0x10):
    // tells the buds "this host is (or is not) playing" so they stay with
    // (or yield from) this host. Ported verbatim from LibrePods
    // AACPManager.createMediaInformationPacket.
    class AapMediaInformation : public AapRequest
    {
    private:
        std::string _selfMac;
        std::string _targetMac;
        bool _streamingState;
        std::string _btName;

    public:
        AapMediaInformation(const std::string &selfMac, const std::string &targetMac, bool streamingState, const std::string &btName);
        std::vector<unsigned char> Request() const override;
    };
}
