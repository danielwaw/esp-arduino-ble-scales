#pragma once
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <NimBLEScan.h>
#include <vector>
#include <memory>

enum class EclairMessageType : uint8_t {
    WEIGHT = 0x57,   // 'W' - Weight data
    FLOW = 0x46,     // 'F' - Flow rate data
    CONFIG = 0x43,   // 'C' - Configuration messages (Timer Start/Stop)
    BATTERY = 0x42   // 'B' - Battery status
};

class EclairScales : public RemoteScales {
public:
    EclairScales(const DiscoveredDevice& device);

    bool connect() override;
    void disconnect() override;
    bool isConnected() override;
    void update() override;
    bool tare() override;

private:
    NimBLERemoteService* service = nullptr;
    NimBLERemoteCharacteristic* dataCharacteristic = nullptr;
    NimBLERemoteCharacteristic* configCharacteristic = nullptr;

    void subscribeToNotifications();
    void handleNotification(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify);
    uint8_t calculateXOR(const uint8_t* data, size_t length);
};

class EclairScalesPlugin {
public:
    static void apply();

private:
    static bool handles(const DiscoveredDevice& device);
};

