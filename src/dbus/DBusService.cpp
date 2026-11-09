// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#include "./dbus/DBusService.h"
#include "Logger.h"

#include <limits>
#include <stdexcept>

namespace MagicPodsCore {

    namespace {
        constexpr auto BLUEZ_ADAPTER_INTERFACE = "org.bluez.Adapter1";
        constexpr auto BLUEZ_DEVICE_INTERFACE = "org.bluez.Device1";
        constexpr auto OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
        constexpr auto PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";

        // BlueZ derives adapter object paths ("hciN") from the kernel's hci
        // index, and the kernel hands out the lowest free index. A machine can
        // therefore end up with hci1 as its only adapter (e.g. while the first
        // controller is still initialising or has been unregistered), or with
        // hci0/hci1 in a different order than on the previous boot, so the
        // default adapter must never be assumed to be hci0.
        const std::regex ADAPTER_OBJECT_PATH_RE{"^/org/bluez/hci([0-9]+)$"};

        int AdapterIndex(const std::string& objectPath) {
            std::smatch match;
            if (!std::regex_match(objectPath, match, ADAPTER_OBJECT_PATH_RE)) {
                return -1;
            }
            try {
                return std::stoi(match[1].str());
            }
            catch (const std::exception&) {
                return -1;
            }
        }

        // Lowest index wins, which prefers hci0 (BlueZ's conventional default)
        // whenever it exists, and otherwise picks the adapter that does.
        std::string PickDefaultAdapter(const std::vector<std::string>& adapterPaths) {
            std::string defaultAdapterPath{};
            int defaultAdapterIndex = std::numeric_limits<int>::max();

            for (const auto& adapterPath : adapterPaths) {
                const int adapterIndex = AdapterIndex(adapterPath);
                if (adapterIndex >= 0 && adapterIndex < defaultAdapterIndex) {
                    defaultAdapterIndex = adapterIndex;
                    defaultAdapterPath = adapterPath;
                }
            }

            return defaultAdapterPath;
        }
    }

    DBusService::DBusService() : _rootProxy{sdbus::createProxy("org.bluez", "/")} {
        //TODO: See comment DeviceFetcher.cpp
        _rootProxy->uponSignal("InterfacesAdded").onInterface(OBJECT_MANAGER_INTERFACE).call([this](sdbus::ObjectPath objectPath, std::map<std::string, std::map<std::string, sdbus::Variant>> interfaces) {
            if (interfaces.contains(BLUEZ_ADAPTER_INTERFACE)) {
                HandleAdapterListChanged();
            }
            TryCreateDevice(objectPath, interfaces);
            TryUpdateInterfaceAddedForDevice(objectPath, interfaces);
        });
        _rootProxy->uponSignal("InterfacesRemoved").onInterface(OBJECT_MANAGER_INTERFACE).call([this](sdbus::ObjectPath objectPath, std::vector<std::string> array) {
            if (std::find(array.begin(), array.end(), BLUEZ_DEVICE_INTERFACE) != array.end()) {
                TryRemoveDevice(objectPath);
            }
            if (std::find(array.begin(), array.end(), BLUEZ_ADAPTER_INTERFACE) != array.end()) {
                HandleAdapterListChanged();
            }
        });
        _rootProxy->finishRegistration();

        // Adapter first, so device work has a target as soon as it starts.
        std::string adapterPath{};
        if (ResolveDefaultAdapterPath(adapterPath)) {
            AttachDefaultAdapter(adapterPath);
        }
        else {
            Logger::Warn("Could not query BlueZ for Bluetooth adapters");
        }

        FetchDevices();
    }

    bool DBusService::ResolveDefaultAdapterPath(std::string& adapterPath) {
        std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects{};

        try {
            _rootProxy->callMethod("GetManagedObjects").onInterface(OBJECT_MANAGER_INTERFACE).storeResultsTo<std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>>(managedObjects);
        }
        catch (const sdbus::Error& e) {
            Logger::Warn("Failed to enumerate BlueZ objects: %s", e.what());
            return false;
        }

        std::vector<std::string> adapterPaths{};
        for (const auto& [objectPath, interfaces] : managedObjects) {
            if (interfaces.contains(BLUEZ_ADAPTER_INTERFACE)) {
                adapterPaths.emplace_back(objectPath);
            }
        }

        adapterPath = PickDefaultAdapter(adapterPaths);
        return true;
    }

    void DBusService::HandleAdapterListChanged() {
        try {
            std::string adapterPath{};
            if (ResolveDefaultAdapterPath(adapterPath)) {
                AttachDefaultAdapter(adapterPath);
            }
        }
        catch (const std::exception& e) {
            Logger::Error("Failed to update the Bluetooth adapter: %s", e.what());
        }
    }

    void DBusService::AttachDefaultAdapter(const std::string& adapterPath) {
        {
            std::lock_guard<std::mutex> lock{_defaultAdapterMutex};
            if (adapterPath == _defaultAdapterPath) {
                return;
            }
        }

        // Build the new proxy before taking the lock: getProperty() blocks, and
        // a signal handler on the sdbus process thread may be waiting for it.
        std::shared_ptr<sdbus::IProxy> newAdapterProxy{};
        bool isPowered = false;

        if (!adapterPath.empty()) {
            newAdapterProxy = std::shared_ptr<sdbus::IProxy>{sdbus::createProxy("org.bluez", adapterPath)};
            newAdapterProxy->uponSignal("PropertiesChanged").onInterface(PROPERTIES_INTERFACE).call([this](std::string interfaceName, std::map<std::string, sdbus::Variant> values, std::vector<std::string> stringArray) {
                if (values.contains("Powered")) {
                    _isBluetoothAdapterPowered.SetValue(values["Powered"].get<bool>());
                }
            });
            newAdapterProxy->finishRegistration();

            try {
                isPowered = newAdapterProxy->getProperty("Powered").onInterface(BLUEZ_ADAPTER_INTERFACE).get<bool>();
            }
            catch (const sdbus::Error& e) {
                Logger::Warn("Failed to read Powered on %s: %s", adapterPath.c_str(), e.what());
            }
        }

        std::shared_ptr<sdbus::IProxy> oldAdapterProxy{};
        {
            std::lock_guard<std::mutex> lock{_defaultAdapterMutex};
            oldAdapterProxy = std::move(_defaultBluetoothAdapterProxy);
            _defaultBluetoothAdapterProxy = std::move(newAdapterProxy);
            _defaultAdapterPath = adapterPath;
        }

        // Released outside the lock: a proxy destructor unregisters callbacks.
        oldAdapterProxy.reset();

        if (adapterPath.empty()) {
            Logger::Warn("No Bluetooth adapter available");
        }
        else {
            Logger::Info("Using Bluetooth adapter %s (powered: %s)", adapterPath.c_str(), isPowered ? "yes" : "no");
        }

        _isBluetoothAdapterPowered.SetValue(isPowered);
    }

    std::shared_ptr<sdbus::IProxy> DBusService::CurrentAdapterProxy() {
        std::lock_guard<std::mutex> lock{_defaultAdapterMutex};
        return _defaultBluetoothAdapterProxy;
    }

    std::string DBusService::GetDefaultBluetoothAdapterAddress() {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            return "";
        }

        try {
            return adapterProxy->getProperty("Address").onInterface(BLUEZ_ADAPTER_INTERFACE).get<std::string>();
        } catch (const sdbus::Error& e) {
            return "";
        }
    }

    std::set<std::shared_ptr<DBusDeviceInfo>> DBusService::GetAllDevices() {
        std::set<std::shared_ptr<DBusDeviceInfo>> devices;
        for (const auto& [path, device] : _knownDevices) {
            devices.emplace(device);
        }
        return devices;
    }

    std::set<std::shared_ptr<DBusDeviceInfo>> DBusService::GetPairedDevices() {
        return _pairedDevices;
    }

    void DBusService::EnableBluetoothAdapter() {
        EnableBluetoothAdapterAsync([](const sdbus::Error* err) {});
    }

    void DBusService::EnableBluetoothAdapterAsync(std::function<void(const sdbus::Error*)>&& callback) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            Logger::Warn("Cannot enable Bluetooth: no adapter");
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        adapterProxy->setPropertyAsync("Powered").onInterface(BLUEZ_ADAPTER_INTERFACE).toValue(true).uponReplyInvoke(callback);
    }

    void DBusService::DisableBluetoothAdapter() {
        DisableBluetoothAdapterAsync([](const sdbus::Error* err) {});
    }

    void DBusService::DisableBluetoothAdapterAsync(std::function<void(const sdbus::Error*)>&& callback) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            Logger::Warn("Cannot disable Bluetooth: no adapter");
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        adapterProxy->setPropertyAsync("Powered").onInterface(BLUEZ_ADAPTER_INTERFACE).toValue(false).uponReplyInvoke(callback);
    }

    void DBusService::SetDiscoveryFilter(const std::map<std::string, sdbus::Variant> &filter) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            return;
        }
        adapterProxy->callMethod("SetDiscoveryFilter").onInterface(BLUEZ_ADAPTER_INTERFACE).withArguments(filter);
    }

    void DBusService::SetDiscoveryFilterAsync(const std::map<std::string, sdbus::Variant> &filter, std::function<void(const sdbus::Error *)> &&callback) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        adapterProxy->callMethodAsync("SetDiscoveryFilter").onInterface(BLUEZ_ADAPTER_INTERFACE).withArguments(filter).uponReplyInvoke(callback);
    }

    void DBusService::StartDiscovery() {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            return;
        }
        // Errors are deliberately propagated: callers distinguish NotReady
        // (adapter powered off) from InProgress to reschedule the scan.
        adapterProxy->callMethod("StartDiscovery").onInterface(BLUEZ_ADAPTER_INTERFACE);
    }

    void DBusService::StartDiscoveryAsync(std::function<void(const sdbus::Error*)>&& callback) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        adapterProxy->callMethodAsync("StartDiscovery").onInterface(BLUEZ_ADAPTER_INTERFACE).uponReplyInvoke(callback);
    }

    void DBusService::StopDiscovery() {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            return;
        }
        adapterProxy->callMethod("StopDiscovery").onInterface(BLUEZ_ADAPTER_INTERFACE).dontExpectReply();
    }

    void DBusService::StopDiscoveryAsync(std::function<void(const sdbus::Error*)>&& callback) {
        auto adapterProxy = CurrentAdapterProxy();
        if (!adapterProxy) {
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        adapterProxy->callMethodAsync("StopDiscovery").onInterface(BLUEZ_ADAPTER_INTERFACE).uponReplyInvoke(callback);
    }

    void DBusService::FetchDevices() {
        std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> managedObjects{};
        try {
            _rootProxy->callMethod("GetManagedObjects").onInterface(OBJECT_MANAGER_INTERFACE).storeResultsTo<std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>>>(managedObjects);
        }
        catch (const sdbus::Error& e) {
            // BlueZ may not be up yet (e.g. starting alongside the system
            // bus); device enumeration is retried on InterfacesAdded.
            Logger::Error("Failed to enumerate BlueZ devices: %s", e.what());
            return;
        }

        _knownDevices.clear();

        for (const auto& [objectPath, interfaces] : managedObjects) {
            TryCreateDevice(objectPath, interfaces);
        }
    }

    std::shared_ptr<DBusDeviceInfo> DBusService::TryCreateDevice(sdbus::ObjectPath objectPath, std::map<std::string, std::map<std::string, sdbus::Variant>> interfaces) {
        const std::regex DEVICE_INSTANCE_RE{"^/org/bluez/hci[0-9]+/dev(_[0-9A-F]{2}){6}$"};
        std::smatch match;
        
        if (std::regex_match(objectPath, match, DEVICE_INSTANCE_RE)) {
            if (interfaces.contains("org.bluez.Device1")) {
                auto deviceInfo = std::make_shared<DBusDeviceInfo>(objectPath, interfaces);
                    
                if (_knownDevices.contains(objectPath)) {
                    _knownDevices.erase(objectPath);
                }
                _knownDevices.emplace(objectPath, deviceInfo);
                _onAnyDeviceAddedEvent.FireEvent(deviceInfo);

                if (deviceInfo->GetPairedStatus().GetValue()) {
                    _pairedDevices.emplace(deviceInfo);
                    _onDeviceAddedEvent.FireEvent(deviceInfo);
                }
                else {
                    deviceInfo->GetPairedStatus().GetEvent().Subscribe([this, objectPath](size_t listenerId, bool newValue) {
                        if (newValue && _knownDevices.contains(objectPath)) {
                            auto device = _knownDevices.at(objectPath);
                            if (!_pairedDevices.contains(device)) {
                                _pairedDevices.emplace(device);
                                _onDeviceAddedEvent.FireEvent(device);
                            }
                        }
                    });
                }

                return deviceInfo;
            }
        }
        return nullptr;
    }

    bool DBusService::TryRemoveDevice(sdbus::ObjectPath objectPath) {
        if (_knownDevices.contains(objectPath)) {
            const auto& deviceInfo = _knownDevices.at(objectPath);

            _onDeviceRemovedEvent.FireEvent(deviceInfo);

            if (_pairedDevices.contains(deviceInfo)) {
                _pairedDevices.erase(deviceInfo);
            }
            _knownDevices.erase(objectPath);

            return true;
        }
        return false;
    }

    bool DBusService::TryUpdateInterfaceAddedForDevice(sdbus::ObjectPath objectPath, std::map<std::string, std::map<std::string, sdbus::Variant>> interfaces)
    {
        const std::regex DEVICE_INSTANCE_RE{"^/org/bluez/hci[0-9]+/dev(_[0-9A-F]{2}){6}$"};
        std::smatch match;
        
        if (std::regex_match(objectPath, match, DEVICE_INSTANCE_RE)) {
            if (_knownDevices.contains(objectPath)) {
                auto device = _knownDevices.at(objectPath);
                device->InterfaceAdded(interfaces);
                return true;
            }
        }
        return false;
    }
    
    bool DBusService::TryUpdateInterfaceRemovedForDevice(sdbus::ObjectPath objectPath)
    {
        return false;
    }
}
