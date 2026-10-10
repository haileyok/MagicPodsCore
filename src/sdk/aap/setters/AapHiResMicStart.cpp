// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHiResMicStart.h"

namespace MagicPodsCore
{
    AapHiResMicStart::AapHiResMicStart() : AapRequest{"AapHiResMicStart"}
    {
    }

    std::vector<unsigned char> AapHiResMicStart::Request() const
    {
        return std::vector<unsigned char>{
            0x04, 0x00, 0x04, 0x00, 0x58, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x01, 0x82, 0x00, 0x00, 0x00, 0x04, 0x96, 0x00};
    }
}
