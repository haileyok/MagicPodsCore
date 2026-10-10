// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHandoffCapability.h"
#include "sdk/aap/setters/AapHijackRequest.h"
#include "sdk/aap/setters/AapHijackReversed.h"
#include "sdk/aap/setters/AapMediaInformation.h"
#include "sdk/aap/setters/AapSmartRoutingShowUI.h"
#include "StringUtils.h"

#include <algorithm>
#include <array>

namespace MagicPodsCore
{
    namespace
    {
        // The name this host announces to the buds. The other host shows it
        // in its "Moved to <name>" banner; the LibrePods reference sends
        // "Android" (same-length strings map to different device names —
        // "iPad" shows "Moved to iPad"). "Linux" matches the reference's
        // semantics; the banner text is cosmetic.
        constexpr const char *SelfBtName = "Linux";

        std::string BytesToMacString(const unsigned char *bytes)
        {
            return StringUtils::Format("%02X:%02X:%02X:%02X:%02X:%02X",
                                       bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        }
    }

    AapHandoffCapability::AapHandoffCapability(AapDevice &device, const std::string &selfMac)
        : AapCapability("handoff", false, device), _selfMac{selfMac}
    {
        if (auto setting = device.LoadSettingInt("handoff_enabled"); setting.has_value())
            _enabled = setting.value() != 0;

        isAvailable = true;
        FireStateChanged();
    }

    nlohmann::json AapHandoffCapability::CreateJsonBody()
    {
        auto devicesJson = nlohmann::json::array();
        for (const auto &connected : _connectedDevices)
        {
            auto deviceJson = nlohmann::json::object();
            deviceJson["mac"] = connected.mac;
            deviceJson["type"] = connected.type.has_value() ? connected.type.value() : "";
            devicesJson.push_back(deviceJson);
        }

        bool otherHostPresent = false;
        for (const auto &connected : _connectedDevices)
        {
            if (connected.mac != _selfMac)
            {
                otherHostPresent = true;
                break;
            }
        }

        auto bodyJson = nlohmann::json::object();
        bodyJson["connected_devices"] = devicesJson;
        bodyJson["other_host_present"] = otherHostPresent;
        bodyJson["enabled"] = _enabled;
        if (_audioSource.has_value())
        {
            bodyJson["audio_source"] = nlohmann::json::object({
                {"mac", _audioSource->mac},
                {"type", _audioSource->type},
            });
        }
        return bodyJson;
    }

    void AapHandoffCapability::FireStateChanged()
    {
        if (!isAvailable)
            isAvailable = true;
        _onChanged.FireEvent(*this);
    }

    // ---------------------------------------------------------------------------
    // Incoming data
    // ---------------------------------------------------------------------------

    void AapHandoffCapability::OnReceivedData(const std::vector<unsigned char> &data)
    {
        // data: 04 00 04 00 | opcode u16 LE | payload
        if (data.size() < 6)
            return;

        unsigned short opcode = static_cast<unsigned short>(data[4]) | (static_cast<unsigned short>(data[5]) << 8);
        switch (opcode)
        {
        case 0x0E: // audio source
            ProcessAudioSource(data);
            break;
        case 0x11: // smart routing response (another host's announce reaches us here)
            ProcessSmartRoutingResponse(data);
            break;
        case 0x2E: // connected devices
            ProcessConnectedDevices(data);
            break;
        default:
            break;
        }
    }

    // Connected-device list (LibrePods AACPManager.parseConnectedDevicesResponse,
    // AACPManager.kt:1187): payload = reversed target MAC (6) | length header (2)
    // | device count (1) | per device [MAC 6, in order][info1][info2] (8).
    void AapHandoffCapability::ProcessConnectedDevices(const std::vector<unsigned char> &data)
    {
        if (data.size() < 9)
        {
            Logger::Warn("Handoff: connected devices packet too short: %s",
                         StringUtils::BytesToHexString(data).c_str());
            return;
        }

        const unsigned char *payload = data.data() + 6;
        size_t count = payload[2];
        if (data.size() < 9 + count * 8)
        {
            Logger::Warn("Handoff: connected devices packet length mismatch: %s",
                         StringUtils::BytesToHexString(data).c_str());
            return;
        }

        std::vector<AapHandoffConnectedDevice> devices{};
        devices.reserve(count);
        for (size_t i = 0; i < count; i++)
        {
            const unsigned char *base = payload + 3 + i * 8;
            AapHandoffConnectedDevice device{};
            device.mac = BytesToMacString(base); // stored in order, NOT reversed
            device.info1 = base[6];
            device.info2 = base[7];
            devices.push_back(device);
        }

        _connectedDevices = std::move(devices);
        Logger::Info("Handoff: connected devices (%zu):", _connectedDevices.size());
        for (const auto &connected : _connectedDevices)
            Logger::Info("Handoff:   %s info1=%02X info2=%02X", connected.mac.c_str(), connected.info1, connected.info2);
        FireStateChanged();
    }

    // Audio source (LibrePods AACPManager.parseAudioSourceResponse,
    // AACPManager.kt:1171): payload = source MAC (6, reversed) | type (1:
    // 0 none / 1 call / 2 media) | trailing bytes.
    void AapHandoffCapability::ProcessAudioSource(const std::vector<unsigned char> &data)
    {
        if (data.size() < 13)
        {
            Logger::Warn("Handoff: audio source packet too short: %s",
                         StringUtils::BytesToHexString(data).c_str());
            return;
        }

        const unsigned char *payload = data.data() + 6;
        std::array<unsigned char, 6> mac{};
        for (size_t i = 0; i < 6; i++)
            mac[i] = payload[5 - i]; // reversed
        unsigned char typeByte = payload[6];
        const char *type = "none";
        if (typeByte == 0x01)
            type = "call";
        else if (typeByte == 0x02)
            type = "media";

        _audioSource = AapHandoffAudioSource{BytesToMacString(mac.data()), type};
        Logger::Info("Handoff: audio source: %s (%s)", _audioSource->mac.c_str(), type);
        FireStateChanged();
    }

    // Smart Routing response (LibrePods dispatch, AACPManager.kt:539): the
    // sender is the reversed MAC at payload[0..6]; look for host names to
    // label the connected devices, like the reference does.
    void AapHandoffCapability::ProcessSmartRoutingResponse(const std::vector<unsigned char> &data)
    {
        if (data.size() < 12)
            return;

        const unsigned char *payload = data.data() + 6;
        std::array<unsigned char, 6> mac{};
        for (size_t i = 0; i < 6; i++)
            mac[i] = payload[5 - i]; // reversed
        std::string sender = BytesToMacString(mac.data());

        // The response payload is mostly printable TLV text.
        std::string text{};
        if (data.size() > 14)
            text.assign(reinterpret_cast<const char *>(payload + 8),
                        std::min(data.size() - 14, size_t{260}));

        static const std::array<std::pair<const char *, const char *>, 5> names{{
            {"iPad", "iPad"},
            {"Mac", "Mac"},
            {"iPhone", "iPhone"},
            {"Linux", "Linux"},
            {"Android", "Android"},
        }};
        for (const auto &name : names)
        {
            if (text.find(name.first) != std::string::npos)
            {
                for (auto &connected : _connectedDevices)
                {
                    if (connected.mac == sender)
                        connected.type = name.second;
                }
                break;
            }
        }

        Logger::Info("Handoff: smart routing response from %s", sender.c_str());
        FireStateChanged();
    }

    void AapHandoffCapability::Reset()
    {
        _connectedDevices.clear();
        _audioSource.reset();
        FireStateChanged();
        AapCapability::Reset();
    }

    // ---------------------------------------------------------------------------
    // Actions
    // ---------------------------------------------------------------------------

    void AapHandoffCapability::SendHijack()
    {
        bool sent = false;
        for (const auto &connected : _connectedDevices)
        {
            if (connected.mac == _selfMac)
                continue;

            AapHijackRequest hijack{connected.mac};
            SendData(hijack);
            Logger::Info("Handoff: hijack request to %s: %s", connected.mac.c_str(),
                         StringUtils::BytesToHexString(hijack.Request()).c_str());
            sent = true;
        }

        if (sent)
        {
            AapMediaInformation media{_selfMac, _selfMac, true, SelfBtName};
            SendData(media);
            Logger::Info("Handoff: media information (streaming) to hold the buds: %s",
                         StringUtils::BytesToHexString(media.Request()).c_str());
        }
        else
            Logger::Warn("Handoff: hijack requested but no other connected host is known");
    }

    void AapHandoffCapability::SendRelease()
    {
        bool sent = false;
        for (const auto &connected : _connectedDevices)
        {
            if (connected.mac == _selfMac)
                continue;

            AapHijackReversed reversed{connected.mac};
            SendData(reversed);
            Logger::Info("Handoff: hijack reversed to %s: %s", connected.mac.c_str(),
                         StringUtils::BytesToHexString(reversed.Request()).c_str());
            sent = true;
        }

        if (!sent)
            Logger::Warn("Handoff: release requested but no other connected host is known");
    }

    void AapHandoffCapability::SetFromJson(const nlohmann::json &json)
    {
        if (!json.contains(name))
            return;

        const auto &capability = json.at(name);

        if (capability.contains("enabled") && capability["enabled"].is_boolean())
        {
            bool enabled = capability["enabled"].get<bool>();
            if (enabled != _enabled)
            {
                _enabled = enabled;
                device.SaveSettingInt("handoff_enabled", enabled ? 1 : 0);
                Logger::Info("Handoff: %s", enabled ? "enabled" : "disabled");
                FireStateChanged();
            }
            return;
        }

        if (capability.contains("action") && capability["action"].is_string())
        {
            std::string action = capability["action"].get<std::string>();
            if (!_enabled)
            {
                Logger::Warn("Handoff: refusing '%s' action: handoff is disabled", action.c_str());
                return;
            }
            if (!device.GetConnected())
            {
                Logger::Warn("Handoff: refusing '%s' action: device is not connected", action.c_str());
                return;
            }

            if (action == "hijack")
            {
                Logger::Info("Handoff: hijack action");
                SendHijack();
            }
            else if (action == "release")
            {
                Logger::Info("Handoff: release action");
                SendRelease();
            }
            else
                Logger::Warn("Handoff: unknown action '%s'", action.c_str());
        }
    }
}
