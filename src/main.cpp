#include <Arduino.h>
#include "remote_scales_plugin_registry.h"
#include "scales/eclair.h"
#include "remote_scales.h"

std::unique_ptr<RemoteScales> device = nullptr;
RemoteScalesScanner scanner;

void setup() {
    Serial.begin(115200);
    NimBLEDevice::init("");
    Serial.println("BLE initialized");

    // Apply the EclairScales plugin
    EclairScalesPlugin::apply();

    // Start scanning
    scanner.initializeAsyncScan();
    Serial.println("Started scanning for devices.");

    // Wait for devices
    while (scanner.getDiscoveredScales().empty()) {
        delay(100);
    }

    // Stop scanning
    scanner.stopAsyncScan();

    // Get the first device
    std::vector<DiscoveredDevice> discoveredDevices = scanner.getDiscoveredScales();
    DiscoveredDevice deviceInfo = discoveredDevices.front();

    // Create scales instance
    device = RemoteScalesFactory::getInstance()->create(deviceInfo);
    if (device) {
        device->setLogCallback([](std::string message) {
            Serial.print(message.c_str());
        });

        device->setWeightUpdatedCallback([](float weight) {
            Serial.print("Weight updated: ");
            Serial.println(weight);
        });

        if (device->connect()) {
            Serial.println("Scales connected successfully.");
            // 发送去皮命令
            if (device->tare()) {
                Serial.println("Tare command sent.");
        }

        } else {
            Serial.println("Failed to connect to scales.");
        }
    } else {
        Serial.println("Failed to create scales instance.");
    }
}

void loop() {
    if (device) {
        device->update();
    }
    delay(1000);
}

