#pragma once
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <NimBLEDevice.h>
#include <vector>
#include <memory>

// 定义 Timemore Dot (Ble2025) 的消息类型
enum class TimemoreNewMessageType : uint8_t {
    WEIGHT_DATA = 0x01,
    QUERY_CMD   = 0x02,
    CTRL_CMD    = 0x03,
    TARE_ACTION = 0x0D
};

class TimemoreNewScales : public RemoteScales {
public:
    TimemoreNewScales(const DiscoveredDevice& device);

    bool connect() override;
    void disconnect() override;
    bool isConnected() override;
    void update() override;
    bool tare() override;

private:
    NimBLERemoteService* service = nullptr;
    NimBLERemoteCharacteristic* notifyCharacteristic = nullptr;
    NimBLERemoteCharacteristic* commandCharacteristic = nullptr;

    std::vector<uint8_t> dataBuffer;
    bool markedForReconnection = false;

    bool performConnectionHandshake();
    void sendQueryCommand(uint8_t queryType);
    void notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify);
    bool decodeAndHandleNotification();
    uint16_t calculateCRC16(const uint8_t* data, size_t length);
    void subscribeToNotifications();
};

class TimemoreNewScalesPlugin {
public:
    static void apply() {
        RemoteScalesPlugin plugin = RemoteScalesPlugin{
            .id = "plugin-timemore-dot",
            .handles = [](const DiscoveredDevice& device) { return TimemoreNewScalesPlugin::handles(device); },
            .initialise = [](const DiscoveredDevice& device) -> std::unique_ptr<RemoteScales> {
                return std::make_unique<TimemoreNewScales>(device);
            },
        };
        RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
    }

private:
    static bool handles(const DiscoveredDevice& device) {
    const std::string& deviceName = device.getName();
    const std::string& mfgData = device.getManufacturerData();
    
    std::string mfgHex = "";
    for (size_t i = 0; i < mfgData.length(); i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X ", (uint8_t)mfgData[i]);
        mfgHex += hex;
    }

    Serial.printf("[SCAN] Name: '%s', MAC: %s, MfgData: %s\n", 
                  deviceName.empty() ? "UNKNOWN" : deviceName.c_str(), 
                  device.getAddress().toString().c_str(),
                  mfgHex.empty() ? "NONE" : mfgHex.c_str());

    // 通用适配条件 1：名字能匹配上 (防止有前导空格，改用 find)
    if (!deviceName.empty() && deviceName.find("TIMEMORE_Dot") != std::string::npos) {
        return true;
    }

    return false;
}
};