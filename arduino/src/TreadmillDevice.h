#pragma once
#include <Arduino.h>
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