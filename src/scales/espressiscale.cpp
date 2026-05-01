#include "EspressiScale.h"
#include <iostream>

EspressiScales::EspressiScales(const DiscoveredDevice& device)
  : RemoteScales(device) {}

EspressiScales::~EspressiScales() {}

bool EspressiScales::connect() {
  if (RemoteScales::clientIsConnected()) return true;

  bool result = RemoteScales::clientConnect();
  if (!result) {
    RemoteScales::clientCleanup();
    return false;
  }

  if (!performConnectionHandshake() || !subscribeToNotifications()) {
    return false;
  }

  RemoteScales::setWeight(0.f);
  return true;
}

void EspressiScales::disconnect() { RemoteScales::clientCleanup(); }

bool EspressiScales::isConnected() { return RemoteScales::clientIsConnected(); }

void EspressiScales::update() {
  if (markedForReconnection) {
    RemoteScales::clientCleanup();
    if (connect()) markedForReconnection = false;
  } else {
    verifyConnected();
  }
}

bool EspressiScales::tare() {
  if (!verifyConnected()) return false;
  // Tare-command from sourcecode: [0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, XOR]
  uint8_t payload[] = { 0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C };
  writeCharacteristic->writeValue(payload, sizeof(payload), false);
  return true;
}

bool EspressiScales::performConnectionHandshake() {
  service = RemoteScales::clientGetService(espressiServiceUUID);
  if (service == nullptr) {
    clientCleanup();
    return false;
  }

  readCharacteristic = service->getCharacteristic(espressiReadUUID);
  writeCharacteristic = service->getCharacteristic(espressiWriteUUID);
  
  if (readCharacteristic == nullptr || writeCharacteristic == nullptr) {
    clientCleanup();
    return false;
  }
  return true;
}

bool EspressiScales::subscribeToNotifications() {
  if (readCharacteristic->canNotify()) {
    auto callback = [this](NimBLERemoteCharacteristic* characteristic,
      uint8_t* data, size_t length, bool isNotify) {
        readCallback(characteristic, data, length, isNotify);
      };
    return readCharacteristic->subscribe(true, callback, false);
  }
  clientCleanup();
  return false;
}

void EspressiScales::readCallback(NimBLERemoteCharacteristic* pCharacteristic,
  uint8_t* pData, size_t length, bool isNotify) {
  // EspressiScale sends 7 bytes. Byte 0 is 0x03, Byte 1 is 0xCE for weight
  if (length == 7 && pData[0] == 0x03 && pData[1] == 0xCE) {
    handleWeightNotification(pData, length);
  }
}

void EspressiScales::handleWeightNotification(uint8_t* pData, size_t length) {
  // Decoding: Byte 2 (MSB) and Byte 3 (LSB). Weight saved as g * 10
  int16_t weight10 = static_cast<int16_t>((pData[2] << 8) | pData[3]);

  // Checksum validation based on sourcecode XOR-logic
  uint8_t expectedXor = pData[length - 1];
  uint8_t xorSum = 0x03; // Startvalue for XOR in EspressiScale
  for (int i = 1; i < length - 1; i++) {
    xorSum ^= pData[i];
  }

  if (xorSum != expectedXor) {
    RemoteScales::log("Checksum error\n");
    return;
  }

  // Converting from g*10 to g
  RemoteScales::setWeight(weight10 / 10.f);
}

bool EspressiScales::verifyConnected() {
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}