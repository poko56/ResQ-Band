#ifndef UWB_DW1000_H
#define UWB_DW1000_H

#include "UWB_Core.h"
// This assumes you have the arduino-dw1000-ng library installed
// or included in your Arduino IDE environment.
// Since the folder `Arduino_DW1000/arduino-dw1000-ng-master` exists,
// it should be accessible.

class UWB_DW1000 : public UWB_Module {
public:
    UWB_DW1000();

    virtual bool begin(int csPin, int irqPin, int rstPin = -1, SPIClass& spi = SPI) override;
    virtual bool configure(uint8_t channel = 5, uint8_t preambleCode = 9, uint16_t preambleLength = 128, uint8_t dataRate = 1) override;
    
    virtual bool transmit(const uint8_t* data, uint16_t length) override;
    virtual uint64_t calculateDelayedTransmitTimestamp(uint64_t referenceTimestamp, uint32_t delayUwbTicks) override;
    virtual bool transmitDelayedAt(const uint8_t* data, uint16_t length, uint64_t delayedTxTimestamp) override;
    virtual void setAntennaDelay(uint16_t delay) override;
    virtual uint16_t getTxAntennaDelay() override;
    virtual bool startReceive() override;
    virtual bool readReceivedData(uint8_t* buffer, uint16_t& length) override;

    float getReceivePower() override;
    uint64_t getTransmitTimestamp() override;
    uint64_t getReceiveTimestamp() override;
    uint64_t getSystemTimestamp() override;
    
    virtual void onIRQ() override;
    virtual void setCallbacks(UWB_Callback txDoneCb, UWB_Callback rxDoneCb) override;
    
    virtual bool enterDeepSleep() override;
    virtual bool wakeUp() override;

private:
    int _cs;
    int _irq;
    int _rst;
    UWB_Callback txDoneCallback;
    UWB_Callback rxDoneCallback;

    // Helper static functions for the DW1000Ng library callbacks
    static void handleSent();
    static void handleReceived();
    static void handleError();
    static void handleReceiveFailed();
    static void handleReceiveTimeout();

    static UWB_DW1000* _instance;
};

#endif
