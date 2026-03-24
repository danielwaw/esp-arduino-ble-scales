#include "insmart.h"
#include "remote_scales_plugin_registry.h"

static const NimBLEUUID INSMART_SERVICE_UUID("FFF0");
static const NimBLEUUID INSMART_WEIGHT_CHAR_UUID("FFF3");
static const NimBLEUUID INSMART_CMD_CHAR_UUID("FFF1");

InsmartScales::InsmartScales(const DiscoveredDevice& device) : RemoteScales(device) {}

InsmartScales::~InsmartScales() {}

bool InsmartScales::connect() {
  if (RemoteScales::clientIsConnected()) {
    return true;
  }

  log("Connecting to %s[%s]\n",
    getDeviceName().c_str(), getDeviceAddress().c_str());

  if (!clientConnect()) {
    clientCleanup();
    return false;
  }

  if (!performConnectionHandshake()) {
    return false;
  }

  if (!subscribeToNotifications()) {
    return false;
  }

  setWeight(0.f);
  return true;
}

void InsmartScales::disconnect() {
  clientCleanup();
}

bool InsmartScales::isConnected() {
  return clientIsConnected();
}

bool InsmartScales::tare() {
  if (!verifyConnected()) return false;
  uint8_t payload[] = { 0x50, 0x00 };
  cmdCharacteristic->writeValue(payload, sizeof(payload), true);
  return true;
}

void InsmartScales::update() {
  if (markedForReconnection) {
    log("Reconnecting\n");
    clientCleanup();
    if (connect()) {
      markedForReconnection = false;
    }
  } else {
    verifyConnected();
  }
}

// ── Private ──────────────────────────────────────────────────────────────────

bool InsmartScales::performConnectionHandshake() {
  service = clientGetService(INSMART_SERVICE_UUID);
  if (service == nullptr) {
    log("FFF0 service not found\n");
    clientCleanup();
    return false;
  }

  // Force full characteristic + descriptor discovery so that subscribe()
  // can find and write the CCCD (0x2902) descriptors.
  service->getCharacteristics(true);

  weightCharacteristic = service->getCharacteristic(INSMART_WEIGHT_CHAR_UUID);
  cmdCharacteristic    = service->getCharacteristic(INSMART_CMD_CHAR_UUID);

  if (weightCharacteristic == nullptr || cmdCharacteristic == nullptr) {
    log("Required characteristics not found\n");
    clientCleanup();
    return false;
  }

  weightCharacteristic->getDescriptors(true);
  cmdCharacteristic->getDescriptors(true);

  return true;
}

bool InsmartScales::subscribeToNotifications() {
  auto callback = [this](NimBLERemoteCharacteristic* characteristic,
                         uint8_t* data, size_t length, bool isNotify) {
    notifyCallback(characteristic, data, length, isNotify);
  };

  if (!weightCharacteristic->canNotify()) {
    log("FFF3 does not support notify\n");
    clientCleanup();
    return false;
  }

  if (!weightCharacteristic->subscribe(true, callback, true)) {
    log("Failed to subscribe to FFF3\n");
    clientCleanup();
    return false;
  }

  if (cmdCharacteristic->canNotify()) {
    cmdCharacteristic->subscribe(true, callback, true);
  }

  return true;
}

void InsmartScales::notifyCallback(
  NimBLERemoteCharacteristic* characteristic,
  uint8_t* data, size_t length, bool isNotify)
{
  if (characteristic->getUUID() == INSMART_WEIGHT_CHAR_UUID) {
    float w = decodeWeight(data, length);
    if (w >= 0.f) {
      setWeight(w);
    }
  }
}

/*
 * INSMART FFF3 weight packet (8 bytes):
 *   [0] 0x12  [1] 0x06  [2] status  [3] 0x00
 *   [4] weight_lo  [5] weight_hi  [6] 0x08  [7] 0x00
 *
 * weight_g = uint16_LE(bytes[4:6]) / 10.0
 * status: 0x05 = stable, 0x01 = changing
 */
float InsmartScales::decodeWeight(const uint8_t* data, size_t length) {
  if (length < 6 || data[0] != 0x12) {
    return -1.f;
  }
  uint16_t raw = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  return raw / 10.0f;
}

bool InsmartScales::verifyConnected() {
  if (markedForReconnection) {
    return false;
  }
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}
