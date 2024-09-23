#include <Arduino.h>
#include "remote_scales_plugin_registry.h"
#include "scales/eclair.h"
#include "remote_scales.h"  // For DiscoveredDevice

// 全局变量
std::unique_ptr<RemoteScales> scales = nullptr;
RemoteScalesScanner scanner;

// 重量更新回调函数
void weightUpdatedCallback(float weight) {
    Serial.printf("Weight updated: %.2f g\n", weight);
}

// 日志回调函数
void scalesLogCallback(std::string message) {
    Serial.println(message.c_str());
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting EclairScales test...");

    // 初始化 BLE
    NimBLEDevice::init("");
    Serial.println("BLE initialized");
    
    // 启用主动扫描模式（在 scanner 初始化之前设置）
    NimBLEDevice::getScan()->setActiveScan(true); // 启用主动扫描

    // 应用 EclairScales 插件
    EclairScalesPlugin::apply();
    Serial.println("Applied EclairScales plugin");

    // 初始化并开始异步扫描
    scanner.initializeAsyncScan();
    Serial.println("Started scanning for devices.");

    // 设置扫描超时，防止无限等待
    unsigned long scanStartTime = millis();
    const unsigned long scanTimeout = 20000; // 20秒超时

    while (scanner.getDiscoveredScales().empty() && (millis() - scanStartTime < scanTimeout)) {
        delay(100);
    }

    if (scanner.getDiscoveredScales().empty()) {
        Serial.println("No Eclair Scales found within timeout.");
        return;
    }

    // 获取第一个发现的设备
    std::vector<DiscoveredDevice> discoveredDevices = scanner.getDiscoveredScales();
    DiscoveredDevice device = discoveredDevices.front();
    
    // 使用 device 对象获取设备地址
    Serial.printf("Discovered device: %s [%s]\n", device.getName().c_str(), device.getAddress().toString().c_str());

    // 初始化 EclairScales 并尝试连接
    scales = std::make_unique<EclairScales>(device);
    // 设置回调函数
    scales->setWeightUpdatedCallback(weightUpdatedCallback);
    scales->setLogCallback(scalesLogCallback);
    if (scales->connect()) {
        Serial.println("Connected to Eclair scale successfully.");
        // 发送去皮命令
        if (scales->tare()) {
            Serial.println("Tare command sent.");
        }
    } else {
        Serial.println("Failed to connect to Eclair scale.");
        scales.reset(); // 释放资源

        // 可选：重新开始扫描
        Serial.println("Restarting scan...");
        scanner.initializeAsyncScan();
    }
}

void loop() {
    if (scales != nullptr && scales->isConnected()) {
        // 更新 Eclair 电子秤状态（根据协议，此处无需执行特定操作）
        scales->update();
        delay(1000);
    } else {
        // 可选：尝试重新连接或处理未连接状态
        delay(1000);
    }
}
