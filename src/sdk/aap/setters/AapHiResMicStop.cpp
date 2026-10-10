// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHiResMicStop.h"

namespace MagicPodsCore
{
    AapHiResMicStop::AapHiResMicStop() : AapRequest{"AapHiResMicStop"}
    {
    }

    std::vector<unsigned char> AapHiResMicStop::Request() const
    {
        return std::vector<unsigned char>{
            0x04, 0x00, 0x04, 0x00, 0x58, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x01};
    }
}
