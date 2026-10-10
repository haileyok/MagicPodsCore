// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapSmartRoutingMac.h"

#include <algorithm>
#include <cstdlib>

namespace MagicPodsCore
{
    namespace AapSmartRoutingMac
    {
        std::optional<std::vector<unsigned char>> ParseMacAddress(const std::string &mac)
        {
            if (mac.size() != 17)
                return std::nullopt;

            std::vector<unsigned char> bytes{};
            bytes.reserve(6);
            for (size_t i = 0; i < 6; i++)
            {
                size_t pos = i * 3;
                std::string octet = mac.substr(pos, 2);
                if (i < 5 && mac[pos + 2] != ':')
                    return std::nullopt;
                char *end = nullptr;
                unsigned long value = std::strtoul(octet.c_str(), &end, 16);
                if (end != octet.c_str() + octet.size() || value > 0xFF)
                    return std::nullopt;
                bytes.push_back(static_cast<unsigned char>(value));
            }

            // Smart Routing packets store the target MAC reversed.
            std::reverse(bytes.begin(), bytes.end());
            return bytes;
        }
    }
}
