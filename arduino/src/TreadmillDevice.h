#pragma once
#include <Arduino.h>
#include <string>
#include <NimBLEDevice.h>

class TreadmillDevice {
public:
    virtual ~TreadmillDevice() {}
    virtual void setupHandler() = 0;
    virtual void loopHandler() = 0;
    virtual bool isConnected() = 0;

    virtual void sendReset() { }

    /**
     * Return true if it's a bluetooth low energy device.
     */
    virtual bool isBle() = 0;
    
    /**
     * Return true if it's a bluetooth low energy device.
     */
    virtual String getBleServiceUuid() = 0;

};


void printCharacteristicAndHandleMap(NimBLEClient* pClient) {
    Serial.println("Discovering services...");
    const std::vector<NimBLERemoteService*>& services = pClient->getServices(true);
  
    if (services.empty()) {
      Serial.println("No services found.");
      return;
    }
  
    for (size_t i = 0; i < services.size(); ++i) {
      NimBLERemoteService* service = services[i];
      Serial.print("Service: ");
      Serial.println(service->getUUID().toString().c_str());
  
      Serial.println("  Discovering characteristics...");
      const std::vector<NimBLERemoteCharacteristic*>& characteristics = service->getCharacteristics(true);
  
      if (characteristics.empty()) {
        Serial.println("  No characteristics found.");
      }
  
      for (size_t j = 0; j < characteristics.size(); ++j) {
        NimBLERemoteCharacteristic* characteristic = characteristics[j];
        Serial.print("    Characteristic: ");
        Serial.print(characteristic->getUUID().toString().c_str());
        Serial.printf("  Handle: 0x%04X", characteristic->getHandle());
  
        if (characteristic->canNotify()) {
          Serial.println("  [NOTIFY]");
        } else {
          Serial.println();
        }
      }
    }
  
    Serial.println("Done listing notifiable characteristics.");
  }


// 0x180a
#include <NimBLEClient.h>

struct DeviceInfo {
  std::string manufacturer;
  std::string modelNumber;
  std::string serialNumber;
  std::string hardwareRevision;
  std::string firmwareRevision;
  std::string softwareRevision;
  std::string systemId;

  void print() const {
    Serial.println("180A Device Info:");
    if (!manufacturer.empty())       Serial.printf("  Manufacturer: %s\n", manufacturer.c_str());
    if (!modelNumber.empty())        Serial.printf("  Model Number: %s\n", modelNumber.c_str());
    if (!serialNumber.empty())       Serial.printf("  Serial Number: %s\n", serialNumber.c_str());
    if (!hardwareRevision.empty())   Serial.printf("  Hardware Revision: %s\n", hardwareRevision.c_str());
    if (!firmwareRevision.empty())   Serial.printf("  Firmware Revision: %s\n", firmwareRevision.c_str());
    if (!softwareRevision.empty())   Serial.printf("  Software Revision: %s\n", softwareRevision.c_str());
    if (!systemId.empty())           Serial.printf("  System ID: %s\n", systemId.c_str());
  }
};

bool isProbablyText(const std::string& s) {
  for (char c : s) {
    if (c != '\n' && 
      ((uint8_t)c < 0x20 || (uint8_t) c>0x7E)) return false;
  }
  return true;
}

std::string parseSystemId(const std::string& val) {
  if( isProbablyText(val) ) {
    Serial.println("isProbText already");
    return val;
  }

  if (val.length() != 8) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)val[0], (uint8_t)val[1], (uint8_t)val[2], (uint8_t)val[3],
           (uint8_t)val[4], (uint8_t)val[5], (uint8_t)val[6], (uint8_t)val[7]);
  return std::string(buf);
}

DeviceInfo readDeviceInfoFrom180A(NimBLEClient* pClient) {
  DeviceInfo info;

  NimBLERemoteService* service = pClient->getService("180a");
  if (!service) {
    Serial.println("Device Information Service (0x180A) not found.");
    return info;
  }

  auto readChar = [&](const char* uuid) -> std::string {
    NimBLERemoteCharacteristic* c = service->getCharacteristic(uuid);
    if (c && c->canRead()) {
      std::string val = c->readValue();
      return isProbablyText(val) ? val : "";
    }
    return "";
  };

  info.manufacturer       = readChar("2a29");
  info.modelNumber        = readChar("2a24");
  info.serialNumber       = readChar("2a25");
  info.hardwareRevision   = readChar("2a27");
  info.firmwareRevision   = readChar("2a26");
  info.softwareRevision   = readChar("2a28");
  std::string rawSystemId = readChar("2a23");
  info.systemId           = parseSystemId( rawSystemId );
  return info;
}
