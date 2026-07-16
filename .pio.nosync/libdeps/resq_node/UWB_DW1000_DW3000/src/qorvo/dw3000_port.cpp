#include "dw3000_port.h"
#include <SPI.h>
#include "../DW3000Ng/DW3000Ng.hpp"

extern HardwareSerial Serial;
SPIClass* _dw3000_spi = &SPI;

int writetospi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t bodyLength, uint8_t *bodyBuffer) {
    _dw3000_spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(DW3000NgClass::_cs, LOW);
    for (int i = 0; i < headerLength; i++) {
        _dw3000_spi->transfer(headerBuffer[i]);
    }
    for (int i = 0; i < bodyLength; i++) {
        _dw3000_spi->transfer(bodyBuffer[i]);
    }
    digitalWrite(DW3000NgClass::_cs, HIGH);
    _dw3000_spi->endTransaction();
    return 0;
}

int readfromspi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength, uint8_t *readBuffer) {
    _dw3000_spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(DW3000NgClass::_cs, LOW);
    for (int i = 0; i < headerLength; i++) {
        _dw3000_spi->transfer(headerBuffer[i]);
    }
    for (int i = 0; i < readLength; i++) {
        readBuffer[i] = _dw3000_spi->transfer(0x00);
    }
    digitalWrite(DW3000NgClass::_cs, HIGH);
    _dw3000_spi->endTransaction();
    return 0;
}

void deca_sleep(uint8_t time_ms) {
    delay(time_ms);
}

void deca_usleep(uint8_t time_us) {
    delayMicroseconds(time_us);
}

void UART_puts(const char* str) {
    Serial.print(str);
}
