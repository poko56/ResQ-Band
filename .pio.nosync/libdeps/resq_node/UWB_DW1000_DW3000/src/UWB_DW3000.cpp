#include "UWB_DW3000.h"
#include <Arduino.h>
#include <string.h>
#include "DW3000Ng/DW3000Ng.hpp"
#include "qorvo/dw3000.h"
#include "qorvo/dw3000_device_api.h"
#include "qorvo/dw3000_vals.h"

static constexpr uint64_t UWB40_MASK = 0xFFFFFFFFFFULL;

extern HardwareSerial Serial;

UWB_DW3000* UWB_DW3000::_instance = nullptr;

UWB_DW3000::UWB_DW3000(SPIClass& spi) : _cs(-1), _irq(-1), _rst(-1), txDoneCallback(nullptr), rxDoneCallback(nullptr) {
    (void)spi;
    _instance = this;
}

bool UWB_DW3000::begin(int csPin, int irqPin, int rstPin, SPIClass& spi) {
    _cs = csPin;
    _irq = irqPin;
    _rst = rstPin;

    extern SPIClass* _dw3000_spi;
    _dw3000_spi = &spi;

    DW3000NgClass::_cs = csPin;
    DW3000NgClass::_rst = rstPin;

    pinMode(_irq, INPUT);
    
    if (_rst != -1) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(2);
        pinMode(_rst, INPUT);
        delay(5);
    }

    DW3000NgClass::spiSelect(_cs);
    DW3000NgClass::rstSelect(_rst);
    
    DW3000NgClass::begin();
    
    if (!DW3000NgClass::checkSPI()) {
        return false;
    }
    
    
    DW3000NgClass::init();
    return true;
}

bool UWB_DW3000::configure(uint8_t channel, uint8_t preambleCode, uint16_t preambleLength, uint8_t dataRate) {
    (void)preambleCode;
    (void)preambleLength;
    (void)dataRate;

    if (channel == 5) DW3000NgClass::setChannel(CHANNEL_5);
    else DW3000NgClass::setChannel(CHANNEL_9);
    
    // Set explicit TX Power
    DW3000NgClass::write32(0x01, 0x0C, 0xFDFDFDFD);

    // Baseline CH5 / 128 / 6.8M using official dwt_config_t
    static dwt_config_t config = {
        5,                // chan
        DWT_PLEN_128,    // txPreambLength
        DWT_PAC8,        // rxPAC
        9,               // txCode
        9,               // rxCode
        DWT_SFD_IEEE_4A, // sfdType
        DWT_BR_6M8,      // dataRate
        DWT_PHRMODE_STD, // phrMode
        DWT_PHRRATE_STD, // phrRate
        129,             // sfdTO
        DWT_STS_MODE_OFF,// stsMode
        DWT_STS_LEN_64,  // stsLength
        DWT_PDOA_M0      // pdoaMode
    };

    // Apply the official Qorvo-style configuration
    dwt_configure(&config);
    dwt_setdblrxbuffmode(DBL_BUF_STATE_DIS, DBL_BUF_MODE_MAN);

    // Antenna delay must be calibrated per board/antenna.
    setAntennaDelay(16385);

    // Enable EXTTXE (GPIO5) and EXTRXE (GPIO6) for hardware debugging
    uint32_t gpio_mode = DW3000NgClass::read32(0x05, 0x00);
    gpio_mode &= ~((7 << 15) | (7 << 18)); // Clear bits
    gpio_mode |= ((1 << 15) | (1 << 18));  // Set to mode 1
    DW3000NgClass::write32(0x05, 0x00, gpio_mode);

    // Setup Interrupts in SYS_ENABLE_REG. In DW3000, SYS_STATUS is at 0x00:0x44, SYS_ENABLE is at 0x00:0x3C.
    uint32_t mask =
        SYS_STATUS_TXFRS  |
        SYS_STATUS_RXFCG  |
        SYS_STATUS_RXFCE  |
        SYS_STATUS_RXPE   |
        SYS_STATUS_RXOVRR |
        SYS_STATUS_RXSFDTO|
        SYS_STATUS_RXPTO  |
        SYS_STATUS_CIAERR;
    DW3000NgClass::write32(0x00, 0x3C, mask);
    
    // Also clear any stuck flags that might have triggered before we set the mask
    DW3000NgClass::write32(0x00, 0x44, 0xFFFFFFFF);
    DW3000NgClass::write32(0x00, 0x48, 0xFFFFFFFF);

    return true;
}

bool UWB_DW3000::prepareTxFrame(const uint8_t* data, uint16_t length) {
    if (length == 0 || length > 1021) {
        return false;
    }

    DW3000NgClass::writeBytes(0x14, 0x00, data, length);

    // TX_FCTRL is 48 bit. TX frame length includes the 2-byte FCS.
    uint8_t tx_fctrl_bytes[6] = {0};
    uint16_t txflen = (length + 2) & 0x3FF;
    tx_fctrl_bytes[0] = txflen & 0xFF;

    uint8_t plen = DW3000NgClass::config[1];
    tx_fctrl_bytes[1] =
        ((txflen >> 8) & 0x03) |
        ((DW3000NgClass::config[4] & 0x01) << 2) |
        (0 << 3) |
        ((plen & 0x0F) << 4);
    tx_fctrl_bytes[2] = 0x00;
    tx_fctrl_bytes[3] = 0x00;
    tx_fctrl_bytes[4] = 0x00;
    tx_fctrl_bytes[5] = 0x00;

    DW3000NgClass::writeBytes(0x00, 0x24, tx_fctrl_bytes, 6);
    return true;
}

bool UWB_DW3000::transmit(const uint8_t* data, uint16_t length) {
    DW3000NgClass::writeFastCommand(0x00); // TXRXOFF
    delay(2); // Wait for transition to IDLE_PLL
    DW3000NgClass::writeFastCommand(0x12); // Clear all status bits via FAST COMMAND

    if (!prepareTxFrame(data, length)) {
        return false;
    }

    // Clear SYS_STATUS to make sure TXFRS is fresh
    DW3000NgClass::write32(0x00, 0x44, 0xFFFFFFFF);
    DW3000NgClass::write32(0x00, 0x48, 0xFFFFFFFF);
    while (DW3000NgClass::read32(0x00, 0x44) != 0) { delay(1); }

    bool tx_ok = false;

    DW3000NgClass::writeFastCommand(0x01); // CMD_TX

    uint32_t t0 = millis();
    while (millis() - t0 < 100) {
        uint32_t st_lo = DW3000NgClass::read32(0x00, 0x44);

        if (st_lo & SYS_STATUS_TXFRS) {
            tx_ok = true;
            break;
        }
    }

    if (!tx_ok) {
        DW3000NgClass::writeFastCommand(0x00); // TXRXOFF
        return false;
    }

    return true;
}

uint64_t UWB_DW3000::calculateDelayedTransmitTimestamp(uint64_t referenceTimestamp, uint32_t delayUwbTicks) {
    uint64_t target = (referenceTimestamp + (uint64_t)delayUwbTicks) & UWB40_MASK;
    uint32_t delayedReg = (uint32_t)(target >> 8);
    delayedReg &= 0xFFFFFFFEUL; // DW3000 ignores bit 0 of DX_TIME.
    return ((((uint64_t)delayedReg) << 8) + (uint64_t)antennaDelay) & UWB40_MASK;
}

bool UWB_DW3000::transmitDelayedAt(const uint8_t* data, uint16_t length, uint64_t delayedTxTimestamp) {
    uint64_t delayedStart = (delayedTxTimestamp - (uint64_t)antennaDelay) & UWB40_MASK;
    uint32_t delayedReg = (uint32_t)(delayedStart >> 8);
    delayedReg &= 0xFFFFFFFEUL;

    dwt_forcetrxoff();
    DW3000NgClass::write32(0x00, 0x44, 0xFFFFFFFF);
    DW3000NgClass::write32(0x00, 0x48, 0xFFFFFFFF);

    if (!prepareTxFrame(data, length)) {
        return false;
    }

    dwt_setdelayedtrxtime(delayedReg);
    if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
        dwt_forcetrxoff();
        return false;
    }

    uint32_t t0 = millis();
    while (millis() - t0 < 100) {
        uint32_t st_lo = DW3000NgClass::read32(0x00, 0x44);
        if (st_lo & SYS_STATUS_TXFRS) {
            DW3000NgClass::write32(0x00, 0x44, SYS_STATUS_TXFRS);
            return true;
        }
    }

    dwt_forcetrxoff();
    return false;
}

void UWB_DW3000::setAntennaDelay(uint16_t delay) {
    antennaDelay = delay;
    DW3000NgClass::setTXAntennaDelay(delay);
    dwt_settxantennadelay(delay);
    dwt_setrxantennadelay(delay);
}

uint16_t UWB_DW3000::getTxAntennaDelay() {
    return antennaDelay;
}

bool UWB_DW3000::startReceive() {
    dwt_forcetrxoff();

    DW3000NgClass::write32(0x00, 0x44, 0xFFFFFFFF);
    DW3000NgClass::write32(0x00, 0x48, 0xFFFFFFFF);

    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    return true;
}

bool UWB_DW3000::readReceivedData(uint8_t* buffer, uint16_t& length) {
    if (internalRxLength > 0) {
        memcpy(buffer, internalRxBuffer, internalRxLength);
        length = internalRxLength;
        internalRxLength = 0; // Mark as read
        return true;
    }
    return false;
}

float UWB_DW3000::getReceivePower() {
    return DW3000NgClass::getSignalStrength();
}

uint64_t UWB_DW3000::getTransmitTimestamp() {
    return DW3000NgClass::readTXTimestamp();
}

uint64_t UWB_DW3000::getReceiveTimestamp() {
    return DW3000NgClass::readRXTimestamp();
}

uint64_t UWB_DW3000::getSystemTimestamp() {
    uint32_t ts_low = DW3000NgClass::read32(0x04, 0x00);
    uint32_t ts_high = DW3000NgClass::read32(0x04, 0x04) & 0xFF;
    return ((uint64_t)ts_high << 32) | ts_low;
}

void UWB_DW3000::onIRQ() {
    uint32_t status = DW3000NgClass::read32(0x00, 0x44);

    if (status & SYS_STATUS_TXFRS) {
        DW3000NgClass::write32(0x00, 0x44, SYS_STATUS_TXFRS);
        if (txDoneCallback) txDoneCallback();
        return;
    }

    if (status & SYS_STATUS_RXFCG) {
        uint32_t rx_finfo = DW3000NgClass::read32(0x00, 0x4C);
        uint16_t rxLen = rx_finfo & 0x3FF;

        if (rxLen > 2 && rxLen <= sizeof(internalRxBuffer) + 2) {
            internalRxLength = rxLen - 2;
            dwt_readrxdata(internalRxBuffer, internalRxLength, 0);
        } else {
            internalRxLength = 0;
        }

        // Очистить RX-good bits ПОСЛЕ чтения буфера
        DW3000NgClass::write32(
            0x00,
            0x44,
            SYS_STATUS_RXFCG |
            SYS_STATUS_RXDFR  |
            SYS_STATUS_RXPRD  |
            SYS_STATUS_RXSFDD |
            SYS_STATUS_RXPHD  |
            SYS_STATUS_CIADONE
        );

        // Перезапуск RX только тут, один раз
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        if (rxDoneCallback) rxDoneCallback();
        return;
    }

    uint32_t rx_errors =
        SYS_STATUS_RXFCE |
        SYS_STATUS_RXPE |
        SYS_STATUS_RXOVRR |
        SYS_STATUS_RXSFDTO |
        SYS_STATUS_RXPTO |
        SYS_STATUS_CIAERR;

    if (status & rx_errors) {
        dwt_forcetrxoff();

        DW3000NgClass::write32(0x00, 0x44, 0xFFFFFFFF);
        DW3000NgClass::write32(0x00, 0x48, 0xFFFFFFFF);

        dwt_rxenable(DWT_START_RX_IMMEDIATE);
        return;
    }

    if (status != 0) {
        DW3000NgClass::write32(0x00, 0x44, status);
        return;
    }
}

void UWB_DW3000::setCallbacks(UWB_Callback txDoneCb, UWB_Callback rxDoneCb) {
    txDoneCallback = txDoneCb;
    rxDoneCallback = rxDoneCb;
}

bool UWB_DW3000::enterDeepSleep() {
    return true; // Not implemented yet
}

bool UWB_DW3000::wakeUp() {
    return true; // Not implemented yet
}
