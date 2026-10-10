// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"
#include "AapSmartRoutingMac.h"

#include <string>

namespace MagicPodsCore
{
    // Smart Routing "hijack" request (AACP message_type 0x10): asks the buds
    // to move to this host, taking them from the host currently using them.
    // Ported verbatim from LibrePods AACPManager.createHijackRequestPacket.
    class AapHijackRequest : public AapRequest
    {
    private:
        std::string _targetMac;

    public:
        explicit AapHijackRequest(const std::string &targetMac);
        std::vector<unsigned char> Request() const override;
    };
}
