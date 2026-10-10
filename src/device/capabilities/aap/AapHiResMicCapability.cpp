// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "AapHiResMicCapability.h"
#include "sdk/aap/setters/AapHiResMicStart.h"
#include "sdk/aap/setters/AapHiResMicStop.h"
#include "sdk/aap/setters/AapInitExt.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <pulse/pulseaudio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
    constexpr const char *SourceName = "AirPodsHiRes";

    std::string FifoPath()
    {
        const char *dir = std::getenv("XDG_RUNTIME_DIR");
        return std::string{dir ? dir : "/tmp"} + "/magicpods-hires.fifo";
    }
}

namespace MagicPodsCore
{
    // ---------------------------------------------------------------------------
    // libpulse helpers (own short-lived mainloop per call, like the Rust
    // reference's output.rs; PulseAudioClient's context is owned by its
    // dedicated thread and must not be driven from other threads).
    // ---------------------------------------------------------------------------

    namespace
    {
        struct PulseSession
        {
            pa_mainloop *ml{nullptr};
            pa_context *ctx{nullptr};
            bool ok{false};

            ~PulseSession()
            {
                if (ctx)
                {
                    pa_context_disconnect(ctx);
                    pa_context_unref(ctx);
                }
                if (ml)
                    pa_mainloop_free(ml);
            }

            bool Connect()
            {
                ml = pa_mainloop_new();
                if (!ml)
                    return false;
                ctx = pa_context_new(pa_mainloop_get_api(ml), "MagicPodsCore-HiResMic");
                if (!ctx)
                    return false;
                if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
                    return false;

                while (true)
                {
                    if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
                        return false;
                    auto state = pa_context_get_state(ctx);
                    if (state == PA_CONTEXT_READY)
                        break;
                    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
                        return false;
                }
                ok = true;
                return true;
            }

            // Iterate the mainloop while the operation is running.
            void Drive(pa_operation *op)
            {
                if (!op)
                    return;
                while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                    pa_mainloop_iterate(ml, 1, nullptr);
                pa_operation_unref(op);
            }
        };

        // Unload leftover module-pipe-source instances from a previous run.
        void UnloadStalePulseModules()
        {
            PulseSession session{};
            if (!session.Connect())
                return;

            std::vector<uint32_t> stale{};
            session.Drive(pa_context_get_module_info_list(session.ctx,
                [](pa_context *, const pa_module_info *info, int eol, void *userdata) {
                    if (eol || !info)
                        return;
                    auto *out = static_cast<std::vector<uint32_t> *>(userdata);
                    if (info->argument && std::strstr(info->argument, SourceName))
                        out->push_back(info->index);
                },
                &stale));

            for (uint32_t index : stale)
            {
                Logger::Warn("HiResMic: unloading stale pulse module %u", index);
                PulseSession unloadSession{};
                if (!unloadSession.Connect())
                    continue;
                unloadSession.Drive(pa_context_unload_module(unloadSession.ctx, index, [](pa_context *, int, void *) {}, nullptr));
            }
        }

        // Load module-pipe-source for the hi-res mic FIFO. Returns the module
        // index, or nullopt on failure.
        //
        // 2026-10-10 fix: the module is now loaded by spawning `pactl
        // load-module` instead of pa_context_load_module on our own
        // short-lived PA context. Both create the source node, but streams
        // refused to route to the source created via the in-process context
        // (every capture silently fell back to the default/EE source) while
        // the identical module loaded via pactl routed perfectly — verified
        // side by side with hand-loaded test modules on PipeWire. pactl
        // output "<index>\n" is parsed for the module index (needed for
        // unload).
        std::optional<uint32_t> LoadPipeSourceModule(const std::string &fifo)
        {
            std::string args = std::string{"source_name="} + SourceName
                + " file=" + fifo
                + " format=s16le rate=" + std::to_string(AapHiResAudio::SampleRate)
                + " channels=" + std::to_string(AapHiResAudio::Channels)
                + " channel_map=mono"
                + " source_properties=device.description=AirPods_HiRes_Mic node.driver=false priority.driver=0";

            std::string cmd = "pactl load-module module-pipe-source " + args + " 2>/dev/null";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return std::nullopt;

            std::string output;
            char buf[64];
            while (fgets(buf, sizeof(buf), pipe) != nullptr)
                output += buf;
            int status = pclose(pipe);
            if (status != 0)
            {
                Logger::Error("HiResMic: pactl load-module failed (status %d)", status);
                return std::nullopt;
            }

            try
            {
                uint32_t moduleIndex = static_cast<uint32_t>(std::stoul(output));
                return moduleIndex;
            }
            catch (const std::exception&)
            {
                Logger::Error("HiResMic: could not parse pactl module index from '%s'", output.c_str());
                return std::nullopt;
            }
        }

        void UnloadPulseModule(uint32_t index)
        {
            if (index == std::numeric_limits<uint32_t>::max())
                return;
            PulseSession session{};
            if (!session.Connect())
                return;
            session.Drive(pa_context_unload_module(session.ctx, index, [](pa_context *, int, void *) {}, nullptr));
        }

        // Name of an application currently recording from the virtual source,
        // or nullopt if no capture client is attached.
        std::optional<std::string> GetSourceConsumer(const std::string &sourceName)
        {
            PulseSession session{};
            if (!session.Connect())
                return std::nullopt;

            uint32_t sourceIndex = std::numeric_limits<uint32_t>::max();
            session.Drive(pa_context_get_source_info_by_name(session.ctx, sourceName.c_str(),
                [](pa_context *, const pa_source_info *info, int eol, void *userdata) {
                    if (eol || !info)
                        return;
                    *static_cast<uint32_t *>(userdata) = info->index;
                },
                &sourceIndex));

            if (sourceIndex == std::numeric_limits<uint32_t>::max())
                return std::nullopt;

            struct SearchContext
            {
                uint32_t sourceIndex;
                std::optional<std::string> app;
            };
            SearchContext search{sourceIndex, std::nullopt};
            session.Drive(pa_context_get_source_output_info_list(session.ctx,
                [](pa_context *, const pa_source_output_info *info, int eol, void *userdata) {
                    auto *search = static_cast<SearchContext *>(userdata);
                    if (eol || !info)
                        return;
                    if (info->source != search->sourceIndex || search->app.has_value())
                        return;

                    const char *name = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
                    search->app = name ? name : (info->name ? info->name : "");
                },
                &search));

            return search.app;
        }
    }

    // ---------------------------------------------------------------------------
    // Capability
    // ---------------------------------------------------------------------------

    void AapHiResMicCapability::StartMonitor()
    {
        std::lock_guard<std::mutex> lock{_monitorMutex};
        if (_monitorRunning.load())
            return;
        // A previous loop may have exited on its own (device disconnect):
        // reap it before spawning a replacement.
        if (_monitorThread.joinable())
            _monitorThread.join();
        _monitorRunning.store(true);
        _monitorThread = std::thread(&AapHiResMicCapability::MonitorLoop, this);
    }

    AapHiResMicCapability::AapHiResMicCapability(AapDevice &device) : AapCapability("hires_mic", false, device)
    {
        if (auto setting = device.LoadSettingInt("hires_mic"); setting.has_value())
            _enabled.store(setting.value() != 0);
        else
            _enabled.store(AapInitExt::IsSupported(device.GetProductId()));

        _audioEventId = device.GetHiResAudioReceived().Subscribe([this](size_t id, const std::vector<unsigned char> &data) {
            MarkSdu();
            EnqueueAudioSdu(data);
        });

        _connectedEventId = device.GetConnectedPropertyChangedEvent().Subscribe([this](size_t id, bool isConnected) {
            if (!isConnected)
                StopCapture();
            else if (_enabled.load())
                StartMonitor();
        });

        if (_enabled.load() && device.GetConnected())
            StartMonitor();

        isAvailable = true;
        FireStateChanged();
    }

    AapHiResMicCapability::~AapHiResMicCapability()
    {
        if (_monitorThread.joinable())
        {
            _monitorRunning.store(false);
            _monitorThread.join();
        }
        StopCapture();
        device.GetConnectedPropertyChangedEvent().Unsubscribe(_connectedEventId);
        device.GetHiResAudioReceived().Unsubscribe(_audioEventId);
    }

    void AapHiResMicCapability::FireStateChanged()
    {
        if (!isAvailable)
            isAvailable = true;
        _onChanged.FireEvent(*this);
    }

    nlohmann::json AapHiResMicCapability::CreateJsonBody()
    {
        auto bodyJson = nlohmann::json::object();
        bodyJson["active"] = _capturing.load();
        bodyJson["level"] = _level.load();
        return bodyJson;
    }

    void AapHiResMicCapability::OnReceivedData(const std::vector<unsigned char> &data)
    {
        // Type-0x58 SDUs are diverted before normal dispatch in
        // AapDevice::OnResponseDataReceived; nothing arrives here.
    }

    void AapHiResMicCapability::Reset()
    {
        StopCapture();
        // Deliberately not calling AapCapability::Reset(): the toggle is a
        // device-level setting, not something that disappears on disconnect.
    }

    void AapHiResMicCapability::SetFromJson(const nlohmann::json &json)
    {
        if (!json.contains(name))
            return;

        const auto &capability = json.at(name);
        if (!capability.contains("enabled") || !capability["enabled"].is_boolean())
        {
            Logger::Error("Error: AapHiResMicCapability::SetFromJson got no enabled value");
            return;
        }

        bool enabled = capability["enabled"].get<bool>();
        if (enabled == _enabled.load())
            return;

        _enabled.store(enabled);
        device.SaveSettingInt("hires_mic", enabled ? 1 : 0);
        Logger::Info("HiResMic: %s", enabled ? "enabled" : "disabled");
        FireStateChanged();

        if (enabled)
        {
            if (device.GetConnected())
                StartMonitor();
        }
        else
        {
            _monitorRunning.store(false);
            // The monitor loop tears any active capture down on its way out;
            // join it to keep the disable synchronous.
            if (_monitorThread.joinable())
                _monitorThread.join();
        }
    }

    // ---------------------------------------------------------------------------
    // Audio SDU queue
    // ---------------------------------------------------------------------------

    void AapHiResMicCapability::EnqueueAudioSdu(const std::vector<unsigned char> &sdu)
    {
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            // Realtime path: drop-oldest on overflow, never back-pressure the
            // L2CAP recv loop.
            if (_audioQueue.size() >= AudioQueueCapacity)
            {
                _audioQueue.pop_front();
                Logger::Debug("HiResMic: audio queue overflow, dropped oldest SDU");
            }
            _audioQueue.push_back(sdu);
        }
        _queueNotEmpty.notify_one();
    }

    std::optional<std::vector<unsigned char>> AapHiResMicCapability::TakeAudioSdu()
    {
        std::unique_lock<std::mutex> lock{_queueMutex};
        _queueNotEmpty.wait(lock, [this]() {
            return !_audioQueue.empty() || _queueFlushed;
        });
        if (_audioQueue.empty())
            return std::nullopt; // flushed: capture ended

        auto sdu = std::move(_audioQueue.front());
        _audioQueue.pop_front();
        return sdu;
    }

    void AapHiResMicCapability::MarkSdu()
    {
        std::lock_guard<std::mutex> lock{_lastSduMutex};
        _lastSdu = std::chrono::steady_clock::now();
    }

    std::optional<std::chrono::milliseconds> AapHiResMicCapability::SinceLastSdu()
    {
        std::lock_guard<std::mutex> lock{_lastSduMutex};
        if (!_lastSdu.has_value())
            return std::nullopt;
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _lastSdu.value());
    }

    // ---------------------------------------------------------------------------
    // Activity monitor
    // ---------------------------------------------------------------------------

    void AapHiResMicCapability::MonitorLoop()
    {
        Logger::Info("HiResMic: activity monitor started, watching '%s'", SourceName);

        // Create the virtual input device; the monitor owns it and unloads it
        // when it exits.
        if (!OpenVirtualSource())
        {
            Logger::Error("HiResMic: could not create the virtual source; monitor exiting");
            return;
        }

        while (_monitorRunning.load())
        {
            std::this_thread::sleep_for(MonitorPollInterval);
            if (!_monitorRunning.load())
                break;

            if (!device.GetConnected() || !_enabled.load())
                break;

            // Keep the source up when a module unload raced us (e.g. the
            // system restarted PipeWire): re-arm it instead of idling forever.
            if (!_capturing.load())
                OpenVirtualSource();

            auto app = GetSourceConsumer(SourceName);
            bool recording = app.has_value();

            if (!_capturing.load() && recording)
            {
                Logger::Info("HiResMic: recorder detected (%s), starting capture",
                             app.value().empty() ? "unknown" : app.value().c_str());
                if (!StartCapture())
                {
                    Logger::Warn("HiResMic: capture start failed; will retry on next poll");
                    CloseVirtualSource();
                }
            }
            else if (_capturing.load())
            {
                auto since = SinceLastSdu();
                bool stalled = recording && since.has_value() && since.value() > StallTimeout;
                if (!recording)
                {
                    Logger::Info("HiResMic: stopping capture (no recording clients)");
                    StopCapture();
                }
                else if (stalled)
                {
                    Logger::Warn("HiResMic: no audio from device for %ld ms, restarting capture",
                                 static_cast<long>(StallTimeout.count()));
                    StopCapture();
                    if (!StartCapture())
                    {
                        Logger::Warn("HiResMic: capture restart failed; will retry on next poll");
                        CloseVirtualSource();
                    }
                }
            }
        }

        StopCapture();
        CloseVirtualSource();
        // Let StartMonitor know a replacement may be spawned (e.g. the
        // device reconnected); the previous thread is reaped there.
        _monitorRunning.store(false);
        Logger::Info("HiResMic: activity monitor stopped");
    }

    // ---------------------------------------------------------------------------
    // Capture session
    // ---------------------------------------------------------------------------

    bool AapHiResMicCapability::StartCapture()
    {
        // Serialize with StopCapture: the connected-changed event can fire
        // from the DBus thread while the monitor is starting a capture.
        std::lock_guard<std::mutex> captureLock{_captureMutex};

        if (_capturing.exchange(true))
            return true;

        HiResMicDecoder decoder{};
        if (!decoder.Open())
        {
            Logger::Error("HiResMic: failed to open AAC-ELD decoder");
            _capturing.store(false);
            return false;
        }

        // O_RDWR on a FIFO never blocks and keeps the pipe from ever seeing
        // "all writers closed"; we only ever write to it.
        std::string fifo = FifoPath();
        _fifoFd = open(fifo.c_str(), O_RDWR | O_CLOEXEC);
        if (_fifoFd < 0)
        {
            Logger::Error("HiResMic: could not open fifo %s", fifo.c_str());
            _capturing.store(false);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            _audioQueue.clear();
            _queueFlushed = false;
        }

        // Start the stall grace period now; the device should deliver SDUs
        // (which refresh this) well within the timeout.
        MarkSdu();

        SendData(AapHiResMicStart{});
        Logger::Info("HiResMic: microphone stream started");

        _level.store(0.0f);
        _decodeThread = std::thread([this, decoder = std::move(decoder)]() mutable {
            DecodeLoop(decoder);
        });

        FireStateChanged();

        // A2DP has to be suspended and resumed shortly after a 0x58 mic
        // start to avoid a corrupted transport state of the AirPods.
        std::thread([this]() {
            std::this_thread::sleep_for(A2dpResetDelay);
            ResetA2dp();
        }).detach();

        return true;
    }

    void AapHiResMicCapability::StopCapture()
    {
        // Serialize with StartCapture (see there); recursive calls from the
        // monitor loop's own teardown are impossible because it calls this
        // exactly once per transition.
        std::lock_guard<std::mutex> captureLock{_captureMutex};

        if (!_capturing.exchange(false))
            return;

        SendData(AapHiResMicStop{});
        Logger::Info("HiResMic: microphone stream stopped");

        // Wake the decode thread and let it drain.
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            _queueFlushed = true;
        }
        _queueNotEmpty.notify_all();
        if (_decodeThread.joinable())
            _decodeThread.join();

        if (_fifoFd >= 0)
        {
            close(_fifoFd);
            _fifoFd = -1;
        }

        _level.store(0.0f);
        FireStateChanged();

        // The transport also needs a reset on the way out.
        ResetA2dp();
    }

    void AapHiResMicCapability::DecodeLoop(HiResMicDecoder &decoder)
    {
        uint64_t frames = 0;
        uint64_t errors = 0;
        float env = 0.0f;
        std::vector<int16_t> pcm{};
        pcm.reserve(4096);

        while (auto sdu = TakeAudioSdu())
        {
            pcm.clear();
            AapHiResAudio::DemuxType58(sdu.value(), [&](const unsigned char *au, size_t len) {
                if (decoder.Decode(au, len, pcm) >= 0)
                    frames++;
                else
                {
                    // insert a silent frame
                    errors++;
                    pcm.resize(pcm.size() + AapHiResAudio::FrameSamples * AapHiResAudio::Channels, 0);
                }
            });

            if (pcm.empty())
                continue;

            float peak = 0.0f;
            const size_t bytes = pcm.size() * sizeof(int16_t);
            if (::write(_fifoFd, pcm.data(), bytes) != static_cast<ssize_t>(bytes))
            {
                Logger::Warn("HiResMic: fifo write broke; stopping decode loop");
                break;
            }
            for (int16_t sample : pcm)
                peak = std::max(peak, std::abs(static_cast<float>(sample)) / 32768.0f);

            env = peak >= env ? peak : env * LevelRelease;
            _level.store(env);

            if (frames > 0 && frames % 400 == 0)
            {
                double secs = static_cast<double>(frames) * AapHiResAudio::FrameSamples / AapHiResAudio::SampleRate;
                Logger::Info("HiResMic: %llu frames (%.0fs), %llu errors, level %.2f",
                             static_cast<unsigned long long>(frames), secs,
                             static_cast<unsigned long long>(errors), env);
            }
        }

        _level.store(0.0f);
        Logger::Info("HiResMic: decode loop ended (%llu frames, %llu errors)",
                     static_cast<unsigned long long>(frames), static_cast<unsigned long long>(errors));
    }

    // The AirPods' A2DP transport can end up in a corrupted state after a
    // 0x58 mic start/stop; suspending and resuming the bluez card profile
    // fixes it. This is what the iOS app implicitly does.
    //
    // 2026-10-10 fix: only run the off->restore dance from a known-good A2DP
    // profile, and verify the restore landed. Previously a stale or
    // headset-head-unit activeProfile was captured and restored, which could
    // park the card in 8 kHz HFP indefinitely ("spotify sounds garbage")
    // and tear down the just-started capture ("meet doesn't pick up mic").
    void AapHiResMicCapability::ResetA2dp()
    {
        auto audioClient = device.GetAudioClient();
        if (!audioClient)
            return;

        std::string card = audioClient->GetNameFromMac(device.GetAddress());
        auto info = audioClient->GetCardInfoByName(card);
        if (!info.has_value() || info->activeProfile.empty())
        {
            Logger::Warn("HiResMic: no active profile on %s; skipping A2DP reset", card.c_str());
            return;
        }

        // Only ever restore an A2DP profile. If the card is currently in a
        // headset profile (or off), pick the best available A2DP profile from
        // the card's profile list instead of echoing a bad state back.
        std::string restore = info->activeProfile;
        if (restore.rfind("a2dp", 0) != 0)
        {
            restore.clear();
            for (const auto& p : info->profiles)
            {
                if (p.first.rfind("a2dp", 0) == 0)
                {
                    // first a2dp profile in list order (highest priority)
                    restore = p.first;
                    break;
                }
            }
            if (restore.empty())
            {
                Logger::Warn("HiResMic: no A2DP profile available on %s; skipping A2DP reset", card.c_str());
                return;
            }
            Logger::Info("HiResMic: active profile %s is not A2DP; restoring to %s instead",
                         info->activeProfile.c_str(), restore.c_str());
        }

        Logger::Info("HiResMic: reset A2DP transport: %s off -> %s", card.c_str(), restore.c_str());
        audioClient->SetCardProfile(card, "off");
        audioClient->SetCardProfile(card, restore);

        // Verify the restore landed; one retry if a race made it stick at
        // something else (never leave the card parked in a headset profile).
        auto after = audioClient->GetCardInfoByName(card);
        if (after.has_value() && after->activeProfile.rfind("a2dp", 0) != 0)
        {
            Logger::Warn("HiResMic: A2DP reset landed on %s; retrying once", after->activeProfile.c_str());
            audioClient->SetCardProfile(card, restore);
            after = audioClient->GetCardInfoByName(card);
            if (after.has_value() && after->activeProfile.rfind("a2dp", 0) != 0)
                Logger::Error("HiResMic: A2DP reset failed to restore %s (stuck at %s)",
                              restore.c_str(), after->activeProfile.c_str());
        }
    }

    // ---------------------------------------------------------------------------
    // Virtual source
    // ---------------------------------------------------------------------------

    bool AapHiResMicCapability::OpenVirtualSource()
    {
        if (_pulseModuleIndex != std::numeric_limits<uint32_t>::max())
            return true;

        UnloadStalePulseModules();

        std::string fifo = FifoPath();
        unlink(fifo.c_str()); // drop any stale FIFO from a prior run
        if (mkfifo(fifo.c_str(), 0600) != 0)
        {
            Logger::Error("HiResMic: could not create fifo %s", fifo.c_str());
            return false;
        }

        auto module = LoadPipeSourceModule(fifo);
        if (!module.has_value())
        {
            Logger::Warn("HiResMic: could not load module-pipe-source");
            unlink(fifo.c_str());
            return false;
        }
        _pulseModuleIndex = module.value();

        Logger::Info("HiResMic: hi-res mic ready: select '%s' as your microphone", SourceName);
        return true;
    }

    void AapHiResMicCapability::CloseVirtualSource()
    {
        if (_fifoFd >= 0)
        {
            close(_fifoFd);
            _fifoFd = -1;
        }
        if (_pulseModuleIndex != std::numeric_limits<uint32_t>::max())
        {
            UnloadPulseModule(_pulseModuleIndex);
            _pulseModuleIndex = std::numeric_limits<uint32_t>::max();
        }
        unlink(FifoPath().c_str());
    }
}
