#pragma once

#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

class InsmartScales : public RemoteScales {
public:
  InsmartScales(const DiscoveredDevice& device);
  virtual ~InsmartScales();

  bool connect() override;
  void disconnect() override;
  bool isConnected() override;
  void update() override;
  bool tare() override;

private:
  NimBLERemoteService*         service              = nullptr;
  NimBLERemoteCharacteristic*  weightCharacteristic  = nullptr; // FFF3
  NimBLERemoteCharacteristic*  cmdCharacteristic     = nullptr; // FFF1

  bool markedForReconnection = false;

  bool performConnectionHandshake();
  bool subscribeToNotifications();
  bool verifyConnected();

  void notifyCallback(NimBLERemoteCharacteristic* characteristic,
                      uint8_t* data, size_t length, bool isNotify);

  float decodeWeight(const uint8_t* data, size_t length);
};

class InsmartScalesPlugin {
public:
  static void apply() {
    RemoteScalesPlugin plugin = RemoteScalesPlugin{
      .id = "plugin-insmart",
      .handles = [](const DiscoveredDevice& device) {
        return InsmartScalesPlugin::handles(device);
      },
      .initialise = [](const DiscoveredDevice& device)
          -> std::unique_ptr<RemoteScales> {
        return std::make_unique<InsmartScales>(device);
      },
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
  }

private:
  static bool handles(const DiscoveredDevice& device) {
    const std::string& name = device.getName();
    return !name.empty() && name.find("863A") != std::string::npos;
  }
};
