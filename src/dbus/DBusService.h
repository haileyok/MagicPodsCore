// MagicPodsCore: https://github.com/steam3d/MagicPodsCore
// Copyright: 2020-2026 Aleksandr Maslov <https://magicpods.app> & Andrei Litvintsev <a.a.litvintsev@gmail.com>
// License: GPL-3.0

#pragma once

#include "DBusDeviceInfo.h"
#include "ObservableVariable.h"

#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <regex>
#include <sdbus-c++/sdbus-c++.h>
#include <iostream>

namespace MagicPodsCore {

    class DBusService {
    private:
        std::unique_ptr<sdbus::IProxy> _rootProxy{};

        // The default adapter is discovered at runtime: BlueZ derives adapter
        // object paths from the kernel's hci index, which is not stable across
        // boots (see ResolveDefaultAdapterPath). Shared so an in-flight call
        // keeps its proxy alive while a hotplug swaps in another one.
        std::shared_ptr<sdbus::IProxy> _defaultBluetoothAdapterProxy{};
        std::string _defaultAdapterPath{};
        mutable std::mutex _defaultAdapterMutex{};

        std::map<sdbus::ObjectPath, std::shared_ptr<DBusDeviceInfo>> _knownDevices{};
        std::set<std::shared_ptr<DBusDeviceInfo>> _pairedDevices{};

        Event<std::shared_ptr<DBusDeviceInfo>> _onDeviceAddedEvent{};
        Event<std::shared_ptr<DBusDeviceInfo>> _onAnyDeviceAddedEvent{};
        Event<std::shared_ptr<DBusDeviceInfo>> _onDeviceRemovedEvent{};

        ObservableVariable<bool> _isBluetoothAdapterPowered{false};

    public:
        explicit DBusService();

        std::set<std::shared_ptr<DBusDeviceInfo>> GetAllDevices();
        std::set<std::shared_ptr<DBusDeviceInfo>> GetPairedDevices();

        ObservableVariable<bool>& IsBluetoothAdapterPowered() {
            return _isBluetoothAdapterPowered;
        }

        // MAC address ("XX:XX:XX:XX:XX:XX") of the default bluetooth adapter,
        // or an empty string if there is no adapter / it could not be read.
        std::string GetDefaultBluetoothAdapterAddress();

        void EnableBluetoothAdapter();
        void EnableBluetoothAdapterAsync(std::function<void(const sdbus::Error*)>&& callback);
        void DisableBluetoothAdapter();
        void DisableBluetoothAdapterAsync(std::function<void(const sdbus::Error*)>&& callback);

        void SetDiscoveryFilter(const std::map<std::string, sdbus::Variant>& filter);
        void SetDiscoveryFilterAsync(const std::map<std::string, sdbus::Variant>& filter, std::function<void(const sdbus::Error*)>&& callback);

        void StartDiscovery();
        void StartDiscoveryAsync(std::function<void(const sdbus::Error*)>&& callback);
        void StopDiscovery();
        void StopDiscoveryAsync(std::function<void(const sdbus::Error*)>&& callback);

        Event<std::shared_ptr<DBusDeviceInfo>>& GetOnDeviceAddedEvent()
        {
            return _onDeviceAddedEvent;
        }

        Event<std::shared_ptr<DBusDeviceInfo>>& GetOnAnyDeviceAddedEvent()
        {
            return _onAnyDeviceAddedEvent;
        }

        Event<std::shared_ptr<DBusDeviceInfo>>& GetOnDeviceRemovedEvent()
        {
            return _onDeviceRemovedEvent;
        }

    private:
        void FetchDevices();

        // Asks BlueZ which adapters exist and reports the one to use. Returns
        // false when BlueZ could not be queried, in which case the caller must
        // keep its current adapter instead of detaching it.
        bool ResolveDefaultAdapterPath(std::string& adapterPath);
        void AttachDefaultAdapter(const std::string& adapterPath);
        void HandleAdapterListChanged();
        std::shared_ptr<sdbus::IProxy> CurrentAdapterProxy();

        std::shared_ptr<DBusDeviceInfo> TryCreateDevice(sdbus::ObjectPath objectPath, std::map<std::string, std::map<std::string, sdbus::Variant>> interfaces);
        bool TryRemoveDevice(sdbus::ObjectPath objectPath);

        bool TryUpdateInterfaceAddedForDevice(sdbus::ObjectPath objectPath, std::map<std::string, std::map<std::string, sdbus::Variant>> interfaces);
        bool TryUpdateInterfaceRemovedForDevice(sdbus::ObjectPath objectPath);
    };

}