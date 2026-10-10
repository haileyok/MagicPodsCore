// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapRequestNotifications.h"

namespace MagicPodsCore
{
    AapRequestNotifications::AapRequestNotifications() : AapRequest{"AapRequestNotifications"}
    {
    }

    std::vector<unsigned char> AapRequestNotifications::Request() const
    {
        return std::vector<unsigned char>{0x04, 0x00, 0x04, 0x00, 0x0F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    }
}
