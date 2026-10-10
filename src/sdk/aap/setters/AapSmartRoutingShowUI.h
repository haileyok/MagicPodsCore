// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"
#include "AapSmartRoutingMac.h"

#include <string>

namespace MagicPodsCore
{
    // Smart Routing "show nearby UI" request (AACP message_type 0x10):
    // makes the host currently using the buds show its "Move to ..." banner.
    // Ported verbatim from LibrePods AACPManager.createSmartRoutingShowUIPacket.
    class AapSmartRoutingShowUI : public AapRequest
    {
    private:
        std::string _targetMac;

    public:
        explicit AapSmartRoutingShowUI(const std::string &targetMac);
        std::vector<unsigned char> Request() const override;
    };
}
