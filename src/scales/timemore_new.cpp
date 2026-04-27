#include "timemore_new.h"
#include "remote_scales_plugin_registry.h"

// 使用完整的 128-bit UUID 以确保最高兼容性
const NimBLEUUID TIMEMORE_NEW_SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
const NimBLEUUID TIMEMORE_NEW_NOTIFY_CHAR_UUID("0000fff1-0000-1000-8000-00805f9b34fb");
const NimBLEUUID TIMEMORE_NEW_COMMAND_CHAR_UUID("0000fff2-0000-1000-8000-00805f9b34fb");

// -----------------------------------------------------------------------------------
// ---------------------------------   PUBLIC   --------------------------------------
// -----------------------------------------------------------------------------------

TimemoreNewScales::TimemoreNewScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool TimemoreNewScales::connect() {
    if (RemoteScales::clientIsConnected()) {
        return true;
    }

    RemoteScales::log("Connecting to %s [%s]\n", RemoteScales::getDeviceName().c_str(), RemoteScales::getDeviceAddress().c_str());

    // 第一次连接尝试
    bool result = RemoteScales::clientConnect();
    
    if (!result) {
        // 【智能容错】：连接失败极有可能是电子秤被重置，导致旧的配对密钥(Bond)失效
        RemoteScales::log("Connection failed. Attempting to delete old bond and retry...\n");
        
        // 精准删除这个特定 MAC 地址的本地配对记录
        NimBLEDevice::deleteBond(getDevice().getAddress());
        
        // 缓冲一下，进行第二次尝试，此时将重新进行安全配对
        delay(500); 
        result = RemoteScales::clientConnect();
    }

    if (!result) {
        RemoteScales::log("Final connection attempt failed.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    // 执行协议握手
    if (!performConnectionHandshake()) {
        RemoteScales::log("Handshake failed. Cleaning up.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    subscribeToNotifications();
    RemoteScales::setWeight(0.f);
    return true;
}

void TimemoreNewScales::disconnect() {
    RemoteScales::clientCleanup();
}

bool TimemoreNewScales::isConnected() {
    return RemoteScales::clientIsConnected();
}

void TimemoreNewScales::update() {
    // 状态维护逻辑：如果在此周期发现断线，则触发重新连接
    if (!isConnected()) {
        // 使用静态变量记录上一次尝试重连的时间
        static uint32_t lastReconnectAttempt = 0;
        
        // 智能节流：断线后，每隔 5 秒才进行一次重连尝试，而不是每毫秒都在死等
        if (millis() - lastReconnectAttempt > 5000) {
            RemoteScales::log("Device disconnected. Attempting to reconnect...\n");
            
            if (connect()) {
                RemoteScales::log("Reconnected to Timemore Dot successfully.\n");
            } else {
                RemoteScales::log("Reconnect failed. Will retry in 5 seconds.\n");
            }
            
            // 更新最后尝试的时间
            lastReconnectAttempt = millis();
        }
    }
}

bool TimemoreNewScales::tare() {
    if (!isConnected() || commandCharacteristic == nullptr) {
        RemoteScales::log("Cannot tare, not connected or no command characteristic.\n");
        return false;
    }

    // 根据官方规范构建去皮包: A5 5A 03 0D 00 00 + CRC
    uint8_t packet[8] = { 
        0xA5, 0x5A, 
        0x03, // CTRL_CMD
        0x0D, // TARE_ACTION
        0x00, 0x00, 0x00, 0x00 
    };
    
    uint16_t crc = calculateCRC16(packet, 6);
    packet[6] = crc & 0xFF;         // 小端序存储 CRC 低字节
    packet[7] = (crc >> 8) & 0xFF;  // 小端序存储 CRC 高字节

    RemoteScales::log("Sending tare command...\n");
    commandCharacteristic->writeValue(packet, sizeof(packet), false);
    return true;
}

// -----------------------------------------------------------------------------------
// ---------------------------------  PRIVATE  ---------------------------------------
// -----------------------------------------------------------------------------------

bool TimemoreNewScales::performConnectionHandshake() {
    RemoteScales::log("Performing handshake...\n");

    service = RemoteScales::clientGetService(TIMEMORE_NEW_SERVICE_UUID);
    if (service == nullptr) {
        RemoteScales::log("Failed to get Timemore Dot service.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    notifyCharacteristic = service->getCharacteristic(TIMEMORE_NEW_NOTIFY_CHAR_UUID);
    commandCharacteristic = service->getCharacteristic(TIMEMORE_NEW_COMMAND_CHAR_UUID);
    
    if (notifyCharacteristic == nullptr || commandCharacteristic == nullptr) {
        RemoteScales::log("Failed to get notify or command characteristics.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    // 开启设备端的数据通知描述符 (0x2902)
    NimBLERemoteDescriptor* notifyDesc = notifyCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (notifyDesc != nullptr) {
        uint8_t value[2] = { 0x01, 0x00 };
        notifyDesc->writeValue(value, 2, true);
    } else {
        RemoteScales::log("Failed to find notification descriptor.\n");
        return false;
    }

    // 发送官方 App 在连接成功后的 6 个初始化查询指令，以唤醒数据流
    uint8_t initQueries[] = { 19, 8, 5, 2, 6, 12 };
    for (int i = 0; i < 6; i++) {
        sendQueryCommand(initQueries[i]);
        delay(160); // 严格遵循官方规定的发包间隔
    }
    
    RemoteScales::log("Handshake completed successfully.\n");
    return true;
}

void TimemoreNewScales::sendQueryCommand(uint8_t queryType) {
    if (commandCharacteristic == nullptr) return;
    
    // 查询帧格式: A5 5A 02 [type] 00 00 00 00
    uint8_t payload[8] = { 
        0xA5, 0x5A, 
        0x02, // QUERY_CMD
        queryType, 
        0x00, 0x00, 0x00, 0x00 
    };
    commandCharacteristic->writeValue(payload, sizeof(payload), true);
}

void TimemoreNewScales::notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    // 将底层接收到的字节数据压入滑动缓冲区
    dataBuffer.insert(dataBuffer.end(), data, data + length);
    
    // 循环解析，直到缓冲区内没有完整的有效帧 (防止粘包)
    while (decodeAndHandleNotification());
}

bool TimemoreNewScales::decodeAndHandleNotification() {
    if (dataBuffer.size() < 2) return false;

    // 寻找有效帧头: A5 5A
    int headerIndex = -1;
    for (size_t i = 0; i < dataBuffer.size() - 1; ++i) {
        if (dataBuffer[i] == 0xA5 && dataBuffer[i+1] == 0x5A) {
            headerIndex = i;
            break;
        }
    }

    // 没有找到帧头，清空垃圾数据并退出
    if (headerIndex == -1) {
        dataBuffer.clear(); 
        return false;
    }
    
    // 擦除帧头之前的所有无效字节
    if (headerIndex > 0) {
        dataBuffer.erase(dataBuffer.begin(), dataBuffer.begin() + headerIndex); 
    }

    // 此时 dataBuffer 必定以 A5 5A 开头。检查是否包含完整的长度字段 (偏移量 4~5)
    if (dataBuffer.size() < 6) return false;

    // 读取 Data Payload 的长度 (大端序)
    uint16_t payloadLen = (dataBuffer[4] << 8) | dataBuffer[5];
    size_t fullLen = 6 + payloadLen + 2; // 帧头(2) + 类型(2) + 长度字段(2) + 载荷(N) + CRC(2)
    
    // 缓冲区数据尚不够一个完整的包，继续等待
    if (dataBuffer.size() < fullLen) return false;

    // 校验包类型 (0x01 0x01 = WEIGHT_DATA)
    if (dataBuffer[2] == 0x01 && dataBuffer[3] == 0x01 && payloadLen >= 9) {
        // 读取 32-bit 有符号大端序重量原始值 (位于偏移量 6)
        int32_t rawWeight = (dataBuffer[6] << 24) | 
                            (dataBuffer[7] << 16) | 
                            (dataBuffer[8] <<  8) | 
                            dataBuffer[9];
        
        // 转换为浮点数重量并推送到 Gaggiuino 框架
        float weight = rawWeight / 10.0f;
        RemoteScales::setWeight(weight);
    }

    // 核心逻辑：当前完整帧已处理完毕，将其从缓冲区彻底擦除
    dataBuffer.erase(dataBuffer.begin(), dataBuffer.begin() + fullLen);
    
    // 返回 true 以指示外部循环继续检查缓冲区是否还有下一个包
    return dataBuffer.size() >= 6;
}

uint16_t TimemoreNewScales::calculateCRC16(const uint8_t* data, size_t length) {
    // 官方算法：初始值 0，多项式 0xA001，LSB-first
    uint16_t crc = 0x0000; 
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x0001) != 0) {
                crc = (crc >> 1) ^ 0xA001; 
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void TimemoreNewScales::subscribeToNotifications() {
    RemoteScales::log("Subscribing to notifications...\n");

    auto callback = [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
        notifyCallback(characteristic, data, length, isNotify);
    };

    if (notifyCharacteristic->canNotify() || notifyCharacteristic->canIndicate()) {
        notifyCharacteristic->subscribe(true, callback);
        RemoteScales::log("Successfully subscribed to notify characteristic.\n");
    } else {
        RemoteScales::log("Notify characteristic cannot notify.\n");
    }
}
