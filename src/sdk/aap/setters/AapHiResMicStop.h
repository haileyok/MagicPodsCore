// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"

namespace MagicPodsCore
{
    // Stops the proprietary hi-res microphone stream (AACP message_type 0x58).
    class AapHiResMicStop : public AapRequest
    {
    public:
        AapHiResMicStop();
        std::vector<unsigned char> Request() const override;
    };
}
