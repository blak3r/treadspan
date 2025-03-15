#pragma once

// Forward declare your base interface, in case it's in another file
class TreadmillDevice {
public:
    virtual ~TreadmillDevice() {}
    virtual void setupHandler() = 0;
    virtual void loopHandler() = 0;
    virtual bool isConnected() = 0;
};