// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"
#include "AapSmartRoutingMac.h"

#include <string>

namespace MagicPodsCore
{
    // Smart Routing "hijack reversed" request (AACP message_type 0x10):
    // releases ownership of the buds back to the other host (that host shows
    // its reverse banner). Ported verbatim from LibrePods
    // AACPManager.createHijackReversedPacket.
    class AapHijackReversed : public AapRequest
    {
    private:
        std::string _targetMac;

    public:
        explicit AapHijackReversed(const std::string &targetMac);
        std::vector<unsigned char> Request() const override;
    };
}
