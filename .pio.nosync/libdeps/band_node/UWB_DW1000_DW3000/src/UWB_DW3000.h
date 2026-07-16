#ifndef UWB_DW3000_H
#define UWB_DW3000_H

#include "UWB_Core.h"
#include <SPI.h>

class UWB_DW3000 : public UWB_Module {
public:
    UWB_DW3000(SPIClass& spi = SPI);
    
    bool begin(int csPin, int irqPin, int rstPin, SPIClass& spi = SPI) override;
    bool configure(uint8_t channel = 5, uint8_t preambleCode = 9, uint16_t preambleLength = 128, uint8_t dataRate = 1) override;
    
    bool transmit(const uint8_t* data, uint16_t length) override;
    uint64_t calculateDelayedTransmitTimestamp(uint64_t referenceTimestamp, uint32_t delayUwbTicks) override;
    bool transmitDelayedAt(const uint8_t* data, uint16_t length, uint64_t delayedTxTimestamp) override;
    void setAntennaDelay(uint16_t delay) override;
    uint16_t getTxAntennaDelay() override;
    bool startReceive() override;
    bool readReceivedData(uint8_t* buffer, uint16_t& length) override;

    float getReceivePower() override;
    uint64_t getTransmitTimestamp() override;
    uint64_t getReceiveTimestamp() override;
    uint64_t getSystemTimestamp() override;
    
    void onIRQ() override;
    void setCallbacks(UWB_Callback txDoneCb, UWB_Callback rxDoneCb) override;
    
    bool enterDeepSleep() override;
    bool wakeUp() override;

private:
    int _cs, _irq, _rst;
    UWB_Callback txDoneCallback;
    UWB_Callback rxDoneCallback;

    uint8_t internalRxBuffer[1024];
    uint16_t internalRxLength = 0;
    uint16_t antennaDelay = 16385;

    bool prepareTxFrame(const uint8_t* data, uint16_t length);

    static UWB_DW3000* _instance;
};

#endif // UWB_DW3000_H
