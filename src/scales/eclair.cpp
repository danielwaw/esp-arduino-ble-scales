#include "eclair.h"
#include "remote_scales_plugin_registry.h"

const NimBLEUUID eclairServiceUUID("B905EAEA-2E63-0E04-7582-7913F10D8F81");
const NimBLEUUID configCharacteristicUUID("4F9A45BA-8E1B-4E07-E157-0814D393B968");
const NimBLEUUID dataCharacteristicUUID("AD736C5F-BBC9-1F96-D304-CB5D5F41E160");

// EclairScales 构造函数实现
EclairScales::EclairScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool EclairScales::connect() {
    if (clientIsConnected()) {
        log("Already connected\n");
        return true;
    }

    log("Connecting to %s[%s]\n", getDeviceName().c_str(), getDeviceAddress().c_str());
    if (!clientConnect()) {
        clientCleanup();
        return false;
    }

    service = clientGetService(eclairServiceUUID);
    if (!service) {
        log("Failed to find Eclair service\n");
        clientCleanup();
        return false;
    }

    dataCharacteristic = service->getCharacteristic(dataCharacteristicUUID);
    configCharacteristic = service->getCharacteristic(configCharacteristicUUID);
    if (!dataCharacteristic || !configCharacteristic) {
        log("Failed to find Eclair characteristics\n");
        clientCleanup();
        return false;
    }

    subscribeToNotifications();
    return true;
}

void EclairScales::disconnect() {
    clientCleanup();
}

bool EclairScales::isConnected() {
    return clientIsConnected();
}

void EclairScales::update() {
    // Eclair scale does not require periodic updates, it pushes data via notifications.
}

bool EclairScales::tare() {
    if (!isConnected()) return false;
    uint8_t tareCommand[3] = {0x54, 0x01, calculateXOR(&tareCommand[1], 1)};
    configCharacteristic->writeValue(tareCommand, sizeof(tareCommand), true);
    log("Tare command sent");
    return true;
}

void EclairScales::subscribeToNotifications() {
    auto callback = [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
        handleNotification(characteristic, data, length, isNotify);
    };

    if (dataCharacteristic->canNotify()) {
        dataCharacteristic->subscribe(true, callback);
        log("Subscribed to data notifications");
    }
}

void EclairScales::handleNotification(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    if (characteristic == dataCharacteristic) {
        if (data[0] == 0x57) { // 'W' - Weight data
            int32_t weight = *reinterpret_cast<int32_t*>(&data[1]);
            uint32_t timer = *reinterpret_cast<uint32_t*>(&data[5]);
            uint8_t receivedXOR = data[9];
            uint8_t calculatedXOR = calculateXOR(&data[1], 8);
            if (receivedXOR == calculatedXOR) {
                setWeight(static_cast<float>(weight) / 1000.0f); // Convert to grams
                log("Weight: %.2f g, Timer: %u ms", getWeight(), timer);
            } else {
                log("XOR validation failed for weight data");
            }
        }
    }
}

uint8_t EclairScales::calculateXOR(const uint8_t* data, size_t length) {
    uint8_t xorResult = 0;
    for (size_t i = 0; i < length; i++) {
        xorResult ^= data[i];
    }
    return xorResult;
}

// 插件注册
void EclairScalesPlugin::apply() {
    RemoteScalesPlugin plugin = {
        .id = "plugin-eclair",
        .handles = [](const DiscoveredDevice& device) {
            return EclairScalesPlugin::handles(device);
        },
        .initialise = [](const DiscoveredDevice& device) -> std::unique_ptr<RemoteScales> {
            return std::make_unique<EclairScales>(device);
        }
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
}

bool EclairScalesPlugin::handles(const DiscoveredDevice& device) {
    return device.getName().rfind("Eclair-", 0) == 0; // Check if the device name starts with "Eclair-"
}
