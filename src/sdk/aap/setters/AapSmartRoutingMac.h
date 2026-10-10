// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "StringUtils.h"

#include <optional>
#include <string>
#include <vector>

namespace MagicPodsCore
{
    // Helpers shared by the Smart Routing (AACP 0x10) handoff packets: the
    // packet body always starts with the 6-byte target MAC stored reversed
    // (little-endian), followed by a 2-byte body length.
    namespace AapSmartRoutingMac
    {
        // Parses "XX:XX:XX:XX:XX:XX" into 6 bytes stored little-endian
        // (i.e. the MAC of the target device, reversed) — the form every
        // Smart Routing packet body starts with.
        // Returns std::nullopt on malformed input.
        std::optional<std::vector<unsigned char>> ParseMacAddress(const std::string &mac);
    }
}
