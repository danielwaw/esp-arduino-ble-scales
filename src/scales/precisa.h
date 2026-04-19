#pragma once

#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

class PrecisaScales : public RemoteScales {

public:
PrecisaScales(const DiscoveredDevice& device);
  virtual ~PrecisaScales(void);

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

class PrecisaScalesPlugin {
public:
  static void apply() {
    RemoteScalesPlugin plugin = RemoteScalesPlugin{
        .id = "plugin-precisa",
        .handles =
            [](const DiscoveredDevice& device) {
              return PrecisaScalesPlugin::handles(device);
            },
        .initialise = [](const DiscoveredDevice& device)
            -> std::unique_ptr<RemoteScales> {
          return std::make_unique<PrecisaScales>(device);
        },
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
  }

private:
  static bool handles(const DiscoveredDevice& device) {
    static constexpr const char* kNamePrefixes[] = { "CFS-9002", "LSJ-001" };
    static constexpr const char* kHexSignatures[] = { "a6bc", "042" };

    const std::string& name = device.getName();
    if (!name.empty()) {
      for (const char* p : kNamePrefixes) {
        if (name.rfind(p, 0) == 0) return true;
      }
      return false;
    }
    const std::string& mfg = device.getManufacturerData();
    if (mfg.empty()) return false;
    std::string hex = NimBLEUtils::dataToHexString((uint8_t*)(mfg.c_str()), mfg.length());
    for (const char* p : kHexSignatures) {
      if (hex.find(p) != std::string::npos) return true;
    }
    return false;
  }
};
