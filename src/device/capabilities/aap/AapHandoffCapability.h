// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0
//
// Seamless handoff (AACP Smart Routing, message_type 0x10) — port of the
// LibrePods (Android/Rust) implementation.

#pragma once

#include "AapCapability.h"

#include <optional>
#include <string>
#include <vector>

namespace MagicPodsCore
{
    // A host (other than this one) the buds are connected to, as reported by
    // the buds in the connected-devices list (AACP 0x2E).
    struct AapHandoffConnectedDevice
    {
        std::string mac{};   // "XX:XX:XX:XX:XX:XX"
        unsigned char info1{};
        unsigned char info2{};
        std::optional<std::string> type{}; // "Mac"/"iPad"/... from Smart Routing responses
    };

    // Which host the buds are currently routing audio to (AACP 0x0E).
    struct AapHandoffAudioSource
    {
        std::string mac{};
        std::string type{}; // "none" / "call" / "media"
    };

    // AirPods seamless-handoff capability ("handoff").
    //
    // Subscribes to the buds' connected-device list (AACP 0x2E) and audio
    // source updates (AACP 0x0E) — both arrive unsolicited once feature
    // flags are enabled — and exposes them to the frontend together with
    // the "other host present" flag. Two explicit actions are available via
    // SetFromJson (never automatic — hijacking takes the buds from whatever
    // host is using them):
    //   {"handoff": {"action": "hijack"}}  — take the buds from every other
    //       connected host, then announce "I'm playing" to hold them;
    //   {"handoff": {"action": "release"}} — send the reverse-hijack to every
    //       other connected host, giving ownership back to them.
    //   {"handoff": {"enabled": bool}}     — user toggle (persisted, default
    //       off; gates the actions only, updates still flow through).
    class AapHandoffCapability : public AapCapability
    {
    private:
        bool _enabled{false};
        std::string _selfMac{};
        std::vector<AapHandoffConnectedDevice> _connectedDevices{};
        std::optional<AapHandoffAudioSource> _audioSource{};

        void ProcessConnectedDevices(const std::vector<unsigned char> &data);
        void ProcessAudioSource(const std::vector<unsigned char> &data);
        void ProcessSmartRoutingResponse(const std::vector<unsigned char> &data);
        void SendHijack();
        void SendRelease();
        void FireStateChanged();

    protected:
        nlohmann::json CreateJsonBody() override;
        void OnReceivedData(const std::vector<unsigned char> &data) override;
        void Reset() override;

    public:
        AapHandoffCapability(AapDevice &device, const std::string &selfMac);
        void SetFromJson(const nlohmann::json &json) override;
    };
}
