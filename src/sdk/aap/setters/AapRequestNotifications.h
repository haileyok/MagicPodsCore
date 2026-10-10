// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "AapRequest.h"

#include <vector>

namespace MagicPodsCore
{
    // REQUEST_NOTIFICATIONS (AACP message_type 0x0F, payload FF FF FF FF):
    // from the LibrePods reference — subscribe to notification updates.
    class AapRequestNotifications : public AapRequest
    {
    public:
        AapRequestNotifications();
        std::vector<unsigned char> Request() const override;
    };
}
