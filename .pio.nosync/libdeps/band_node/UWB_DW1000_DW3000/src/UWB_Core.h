#ifndef UWB_CORE_H
#define UWB_CORE_H

#include <Arduino.h>

#include <SPI.h>

// Forward declaration of callback types
typedef void (*UWB_Callback)();

class UWB_Module {
public:
    virtual ~UWB_Module() {}

    // Initialize module. rstPin can be -1 if not used.
    virtual bool begin(int csPin, int irqPin, int rstPin = -1, SPIClass& spi = SPI) = 0;
    
    // Configuration
    virtual bool configure(uint8_t channel = 5, uint8_t preambleCode = 9, uint16_t preambleLength = 128, uint8_t dataRate = 1) = 0;
    
    // Transmission
    virtual bool transmit(const uint8_t* data, uint16_t length) = 0;

    // Delayed TX is required for accurate TWR ranging.
    // calculateDelayedTransmitTimestamp() returns the expected RMARKER TX timestamp.
    // transmitDelayedAt() sends at that timestamp using the chip delayed-TX hardware.
    virtual uint64_t calculateDelayedTransmitTimestamp(uint64_t referenceTimestamp, uint32_t delayUwbTicks) = 0;
    virtual bool transmitDelayedAt(const uint8_t* data, uint16_t length, uint64_t delayedTxTimestamp) = 0;
    virtual void setAntennaDelay(uint16_t delay) = 0;
    virtual uint16_t getTxAntennaDelay() = 0;
    
    // Reception
    virtual bool startReceive() = 0;
    virtual bool readReceivedData(uint8_t* buffer, uint16_t& length) = 0;

    // RSSI and Timestamps for Ranging
    virtual float getReceivePower() = 0;
    virtual uint64_t getTransmitTimestamp() = 0;
    virtual uint64_t getReceiveTimestamp() = 0;
    virtual uint64_t getSystemTimestamp() = 0;
    
    // Interrupts & Sleep
    virtual void onIRQ() = 0;
    virtual void setCallbacks(UWB_Callback txDoneCb, UWB_Callback rxDoneCb) = 0;
    
    virtual bool enterDeepSleep() = 0;
    virtual bool wakeUp() = 0;
};

#endif // UWB_CORE_H
