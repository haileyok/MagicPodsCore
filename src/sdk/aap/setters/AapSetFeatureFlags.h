// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"

#include <vector>

namespace MagicPodsCore
{
    // SET_FEATURE_FLAGS (AACP message_type 0x4D, payload FF 00 00 00 00 00
    // 00 00): from the LibrePods reference — sent once on connect; among
    // other things it makes the buds report their connected-device list
    // (0x2E) and audio source (0x0E) updates.
    class AapSetFeatureFlags : public AapRequest
    {
    public:
        AapSetFeatureFlags();
        std::vector<unsigned char> Request() const override;
    };
}
