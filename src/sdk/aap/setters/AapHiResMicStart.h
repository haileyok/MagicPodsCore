// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"

namespace MagicPodsCore
{
    // Starts the proprietary hi-res microphone stream (AACP message_type
    // 0x58). The mic rides the control channel like on iOS: A2DP keeps
    // playing, and the device answers with type-0x58 audio SDUs.
    class AapHiResMicStart : public AapRequest
    {
    public:
        AapHiResMicStart();
        std::vector<unsigned char> Request() const override;
    };
}
