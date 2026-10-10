// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapSetFeatureFlags.h"

namespace MagicPodsCore
{
    AapSetFeatureFlags::AapSetFeatureFlags() : AapRequest{"AapSetFeatureFlags"}
    {
    }

    std::vector<unsigned char> AapSetFeatureFlags::Request() const
    {
        return std::vector<unsigned char>{0x04, 0x00, 0x04, 0x00, 0x4D, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}
