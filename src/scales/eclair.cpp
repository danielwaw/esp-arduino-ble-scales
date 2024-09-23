#include "eclair.h"
#include "remote_scales_plugin_registry.h"

const NimBLEUUID ECLAIR_SERVICE_UUID("B905EAEA-2E63-0E04-7582-7913F10D8F81");
const NimBLEUUID ECLAIR_DATA_CHAR_UUID("AD736C5F-BBC9-1F96-D304-CB5D5F41E160");
const NimBLEUUID ECLAIR_CONFIG_CHAR_UUID("4F9A45BA-8E1B-4E07-E157-0814D393B968");

// -----------------------------------------------------------------------------------
// ---------------------------------   PUBLIC   --------------------------------------
// -----------------------------------------------------------------------------------

EclairScales::EclairScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool EclairScales::connect() {
    if (RemoteScales::clientIsConnected()) {
        RemoteScales::log("Already connected\n");
        return true;
    }

    RemoteScales::log("Connecting to %s [%s]\n", RemoteScales::getDeviceName().c_str(), RemoteScales::getDeviceAddress().c_str());
    bool result = RemoteScales::clientConnect();
    if (!result) {
        RemoteScales::log("Failed to connect to client\n");
        RemoteScales::clientCleanup();
        return false;
    }

    if (!performConnectionHandshake()) {
        RemoteScales::log("Handshake failed\n");
        return false;
    }

    subscribeToNotifications();
    RemoteScales::setWeight(0.f);
    lastHeartbeat = millis();  // 初始化心跳时间戳
    return true;
}

void EclairScales::disconnect() {
    RemoteScales::clientCleanup();
}

bool EclairScales::isConnected() {
    return RemoteScales::clientIsConnected();
}

void EclairScales::update() {
    sendHeartbeat();
}

bool EclairScales::tare() {
    if (!isConnected()) return false;
    uint8_t tareCommand[2] = { static_cast<uint8_t>(EclairMessageType::TARE_COMMAND), 0x01 };
    uint8_t checksum = calculateXOR(&tareCommand[1], 1);  // 计算校验
    uint8_t message[3] = { tareCommand[0], tareCommand[1], checksum };
    configCharacteristic->writeValue(message, sizeof(message), true);
    RemoteScales::log("Sent tare command\n");
    return true;
}

// -----------------------------------------------------------------------------------
// ---------------------------------  PRIVATE  ---------------------------------------
// -----------------------------------------------------------------------------------

bool EclairScales::performConnectionHandshake() {
    RemoteScales::log("Performing handshake\n");

    service = RemoteScales::clientGetService(ECLAIR_SERVICE_UUID);
    if (service == nullptr) {
        RemoteScales::log("Failed to get Eclair service\n");
        RemoteScales::clientCleanup();
        return false;
    }

    dataCharacteristic = service->getCharacteristic(ECLAIR_DATA_CHAR_UUID);
    configCharacteristic = service->getCharacteristic(ECLAIR_CONFIG_CHAR_UUID);
    if (dataCharacteristic == nullptr || configCharacteristic == nullptr) {
        RemoteScales::log("Failed to get characteristics\n");
        RemoteScales::clientCleanup();
        return false;
    }

    RemoteScales::log("Successfully obtained service and characteristics\n");
    return true;
}

void EclairScales::sendMessage(EclairMessageType msgType, const uint8_t* data, size_t dataLength, bool waitResponse) {
    size_t totalLength = 1 + dataLength + 1; // 头部 + 数据 + 校验
    auto bytes = std::make_unique<uint8_t[]>(totalLength);
    bytes[0] = static_cast<uint8_t>(msgType); // 消息类型
    memcpy(bytes.get() + 1, data, dataLength); // 数据部分
    uint8_t checksum = calculateXOR(bytes.get() + 1, dataLength); // 计算校验，仅对数据部分
    bytes[totalLength - 1] = checksum; // 添加校验字节

    RemoteScales::log("Sending message: %s\n", RemoteScales::byteArrayToHexString(bytes.get(), totalLength).c_str());

    configCharacteristic->writeValue(bytes.get(), totalLength, waitResponse);
}

void EclairScales::notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    RemoteScales::log("Received notification from characteristic %s: %s\n",
        characteristic->getUUID().toString().c_str(),
        RemoteScales::byteArrayToHexString(data, length).c_str());

    if (characteristic->getUUID() == ECLAIR_DATA_CHAR_UUID) {
        handleDataNotification(data, length);
    } else if (characteristic->getUUID() == ECLAIR_CONFIG_CHAR_UUID) {
        handleConfigNotification(data, length);
    }
}

void EclairScales::handleDataNotification(uint8_t* data, size_t length) {
    if (length < 10) { // 头部（1字节）+ 数据（8字节）+ 校验（1字节）
        RemoteScales::log("Data notification length too short\n");
        return;
    }

    uint8_t header = data[0];
    uint8_t checksum = data[length - 1];
    uint8_t calculatedChecksum = calculateXOR(&data[1], length - 2); // 排除头部和校验字节

    if (calculatedChecksum != checksum) {
        RemoteScales::log("Invalid checksum in data notification: calculated %02X, received %02X\n", calculatedChecksum, checksum);
        return;
    }

    if (header == static_cast<uint8_t>(EclairMessageType::WEIGHT)) {
        int32_t rawWeight;
        memcpy(&rawWeight, &data[1], 4); // 假设小端序
        float weight = rawWeight / 1000.0f; // 转换为克
        RemoteScales::setWeight(weight);
    } else if (header == static_cast<uint8_t>(EclairMessageType::FLOW_RATE)) {
        RemoteScales::log("Received flow rate data\n");
    } else {
        RemoteScales::log("Unknown data notification header: %02X\n", header);
    }
}

void EclairScales::handleConfigNotification(uint8_t* data, size_t length) {
    if (length < 3) { // 头部（1字节）+ 数据（1字节）+ 校验（1字节）
        RemoteScales::log("Config notification length too short\n");
        return;
    }

    uint8_t header = data[0];
    uint8_t value = data[1];
    uint8_t checksum = data[length - 1];
    uint8_t calculatedChecksum = calculateXOR(&data[1], length - 2); // 排除头部和校验字节

    if (calculatedChecksum != checksum) {
        RemoteScales::log("Invalid checksum in config notification: calculated %02X, received %02X\n", calculatedChecksum, checksum);
        return;
    }

    if (header == static_cast<uint8_t>(EclairMessageType::BATTERY_STATUS)) {
        battery = value;
        RemoteScales::log("Battery status updated: %d%%\n", battery);
    } else if (header == static_cast<uint8_t>(EclairMessageType::TIMER_STATUS)) {
        RemoteScales::log("Timer status updated: %d\n", value);
    } else {
        RemoteScales::log("Unknown config notification header: %02X\n", header);
    }
}

uint8_t EclairScales::calculateXOR(const uint8_t* data, size_t length) {
    uint8_t result = 0;
    for (size_t i = 0; i < length; i++) {
        result ^= data[i];
    }
    return result;
}

void EclairScales::subscribeToNotifications() {
    RemoteScales::log("Subscribing to notifications\n");

    auto callback = [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
        notifyCallback(characteristic, data, length, isNotify);
    };

    if (dataCharacteristic->canNotify()) {
        RemoteScales::log("Subscribing to data characteristic\n");
        dataCharacteristic->subscribe(true, callback);
    } else {
        RemoteScales::log("Data characteristic cannot notify\n");
    }

    if (configCharacteristic->canNotify()) {
        RemoteScales::log("Subscribing to config characteristic\n");
        configCharacteristic->subscribe(true, callback);
    } else {
        RemoteScales::log("Config characteristic cannot notify\n");
    }
}

void EclairScales::sendHeartbeat() {
    if (!isConnected()) {
        return;
    }

    uint32_t now = millis();
    if (now - lastHeartbeat < 2000) {
        return;
    }

    uint8_t heartbeatCommand[] = { 0x00 };  // 示例心跳命令
    sendMessage(EclairMessageType::TIMER_STATUS, heartbeatCommand, sizeof(heartbeatCommand));
    lastHeartbeat = now;
}
