#include "precisa.h"
#include "remote_scales_plugin_registry.h"
#include <iostream>

using namespace std;

const NimBLEUUID serviceUUID("FFF0");
const NimBLEUUID readCharacteristicUUID("FFF1");
const NimBLEUUID writeCharacteristicUUID("FFF2");

PrecisaScales::PrecisaScales(const DiscoveredDevice& device)
  : RemoteScales(device) {
}

PrecisaScales::~PrecisaScales() {}

bool PrecisaScales::connect() {
  if (RemoteScales::clientIsConnected()) {
    RemoteScales::log("Already connected\n");
    return true;
  }

  RemoteScales::log("Connecting to %s[%s]\n",
    RemoteScales::getDeviceName().c_str(),
    RemoteScales::getDeviceAddress().c_str());

  bool result = RemoteScales::clientConnect();
  if (!result) {
    RemoteScales::clientCleanup();
    return false;
  }

  if (!performConnectionHandshake()) {
    return false;
  }

  if (!subscribeToNotifications()) {
    return false;
  }

  RemoteScales::setWeight(0.f);
  return true;
}

void PrecisaScales::disconnect() { RemoteScales::clientCleanup(); }

bool PrecisaScales::isConnected() { return RemoteScales::clientIsConnected(); }

void PrecisaScales::update() {
  if (markedForReconnection) {
    RemoteScales::log("Marked for disconnection. Will attempt to reconnect.\n");
    RemoteScales::clientCleanup();
    if (connect()) {
      markedForReconnection = false;
    }
    else {
      RemoteScales::log("Failed to reconnect\n");
      return;
    }
  }
  else {
    verifyConnected();
  }
}

bool PrecisaScales::tare() {
  if (!verifyConnected())
    return false;
  uint8_t payload[] = { 0xAA, 0x02, 0x31, 0x31 };
  writeCharacteristic->writeValue(payload, sizeof(payload), false);
  return true;
};

bool PrecisaScales::performConnectionHandshake() {
  RemoteScales::log("Performing handshake\n");

  service = RemoteScales::clientGetService(serviceUUID);
  if (service != nullptr) {
  }
  else {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Got Service\n");

  readCharacteristic = service->getCharacteristic(readCharacteristicUUID);
  writeCharacteristic = service->getCharacteristic(writeCharacteristicUUID);
  if (readCharacteristic == nullptr || writeCharacteristic == nullptr) {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Got readCharacteristic and writeCharacteristic\n");
  return true;
}

bool PrecisaScales::subscribeToNotifications() {
  if (readCharacteristic->canNotify()) {
    auto callback = [this](NimBLERemoteCharacteristic* characteristic,
      uint8_t* data, size_t length, bool isNotify) {
        readCallback(characteristic, data, length, isNotify);
      };
    if (!readCharacteristic->subscribe(true, callback, false)) {
      clientCleanup();
      return false;
    }
  }
  else {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Registered for notify\n");
  return true;
}

void PrecisaScales::readCallback(NimBLERemoteCharacteristic* pCharacteristic,
  uint8_t* pData, size_t length, bool isNotify) {
  if (length == 11 && pData[0] == 0xAA && pData[1] == 0x09 && pData[2] == 0x41) {
    handleWeightNotification(pData, length);
  }
  else {
    RemoteScales::log("Wrong packet length\n");
  }
}

void PrecisaScales::handleWeightNotification(uint8_t* pData, size_t length) {
  uint16_t weight100 = ((uint16_t)((pData[8] << 8) | pData[7]));

  if (pData[6] > 0) {
    weight100 = -weight100;
  }

  RemoteScales::setWeight(weight100 / 10.f);
  RemoteScales::log("Weight received\n");
}

bool PrecisaScales::verifyConnected() {
  if (markedForReconnection) {
    return false;
  }
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}
