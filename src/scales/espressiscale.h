#pragma once

#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

// UUIDs from EspressiScale sourcecode
const NimBLEUUID espressiServiceUUID("0000FFF0-0000-1000-8000-00805F9B34FB");
const NimBLEUUID espressiReadUUID("0000FFF4-0000-1000-8000-00805F9B34FB");
const NimBLEUUID espressiWriteUUID("000036F5-0000-1000-8000-00805F9B34FB");

class EspressiScales : public RemoteScales {
public:
  EspressiScales(const DiscoveredDevice& device);
  virtual ~EspressiScales(void);

  bool connect(void) override;
  void disconnect(void) override;
  bool isConnected(void) override;
  void update(void) override;
  bool tare(void) override;

private:
  NimBLERemoteService* service;
  NimBLERemoteCharacteristic* readCharacteristic;
  NimBLERemoteCharacteristic* writeCharacteristic;

  bool markedForReconnection = false;

  void readCallback(NimBLERemoteCharacteristic* pCharacteristic, uint8_t* pData,
    size_t length, bool isNotify);

  bool performConnectionHandshake(void);
  bool subscribeToNotifications(void);
  void handleWeightNotification(uint8_t* pData, size_t length);
  bool verifyConnected(void);
};

class EspressiScalesPlugin {
public:
  static void apply() {
    RemoteScalesPlugin plugin = RemoteScalesPlugin{
        .id = "plugin-espressi",
        .handles = [](const DiscoveredDevice& device) {
          // Check for EspressiScale advertising name
          return device.getName() == "EspressiScale";
        },
        .initialise = [](const DiscoveredDevice& device) -> std::unique_ptr<RemoteScales> {
          return std::make_unique<EspressiScales>(device);
        },
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
  }
};