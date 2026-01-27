#include "tencent.h"
#include <cstring>

// Initialize UUID constants
const NimBLEUUID TencentScales::DATA_SERVICE_UUID("FFE0");
const NimBLEUUID TencentScales::DATA_CHARACTERISTIC_UUID("FFE1");

TencentScales::TencentScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool TencentScales::connect() {
  if (isConnected()) {
    log("Already connected.\n");
    return true;
  }

  if (!clientConnect()) {
    clientCleanup();
    return false;
  }

  if (!performConnectionHandshake()) {
    clientCleanup();
    return false;
  }
  setWeight(0.f);
  return true;
}

void TencentScales::disconnect() {
  clientCleanup();
}

bool TencentScales::isConnected() {
  return clientIsConnected();
}

void TencentScales::update() {
  if (markedForReconnection) {
    log("Reconnecting...\n");
    clientCleanup();
    connect();
    markedForReconnection = false;
  }
  else {
    verifyConnected();
  }
}

bool TencentScales::tare() {
  if (!verifyConnected()) return false;
  log("Tare command sent.\n");
  uint8_t tareCommand[] = { CMD_TARE };
  dataCharacteristic->writeValue(tareCommand, sizeof(tareCommand), true);
  return true;
}

bool TencentScales::performConnectionHandshake() {
  log("Performing handshake...\n");

  service = clientGetService(DATA_SERVICE_UUID);
  if (!service) {
    log("Service not found.\n");
    return false;
  }

  dataCharacteristic = service->getCharacteristic(DATA_CHARACTERISTIC_UUID);
  if (!dataCharacteristic) {
    log("Characteristic not found.\n");
    return false;
  }

  if (dataCharacteristic->canNotify()) {
    dataCharacteristic->subscribe(true, [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
      notifyCallback(characteristic, data, length, isNotify);
      });
  }
  else {
    log("Notifications not supported.\n");
    return false;
  }

  return true;
}
bool TencentScales::verifyConnected() {
  if (markedForReconnection) {
    return false;
  }
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}

void TencentScales::notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
  log("Notification received.\n");
  if (length < 11) {
    log("Malformed data.\n");
    return;
  }
  parseStatusUpdate(data, length);
}

void TencentScales::parseStatusUpdate(const uint8_t* data, size_t length) {

  if (data[0] == 0xA7 && data[1] == 0x00 && data[2] == 0x24) {
    if (data[3] == 0x0C && data[4] == 0x13 && length >= 18) {
      float weight = static_cast<float>(parseWeight(data));
      setWeight(weight);
      log("Weight updated: %.1f g\n", weight);
    }
  }
}

float TencentScales::parseWeight(const uint8_t* data) {

  bool isNegative = (data[7] >> 4) & 0x01;

  uint32_t value = data[8] << 16 | data[9] << 8 | data[10];

  if (isNegative) {
    return -(value / 10.0f);
  }

  return value / 10.f;
}

uint8_t TencentScales::calculateChecksum(const uint8_t* data, size_t length) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < length - 1; ++i) {
    checksum += data[i];
  }
  return checksum;
}
