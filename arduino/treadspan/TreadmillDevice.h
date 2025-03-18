#pragma once

class TreadmillDevice {
public:
    virtual ~TreadmillDevice() {}
    virtual void setupHandler() = 0;
    virtual void loopHandler() = 0;
    virtual bool isConnected() = 0;

    /**
     * Return true if it's a bluetooth low energy device.
     */
    virtual bool isBle() = 0;
    
    /**
     * Return true if it's a bluetooth low energy device.
     */
    virtual String getBleServiceUuid() = 0;

};