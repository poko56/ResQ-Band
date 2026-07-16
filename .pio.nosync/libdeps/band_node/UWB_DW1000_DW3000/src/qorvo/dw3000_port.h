#ifndef DW3000_PORT_H
#define DW3000_PORT_H

#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>

extern SPIClass* _dw3000_spi;

int writetospi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t bodyLength, uint8_t *bodyBuffer);
int readfromspi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength, uint8_t *readBuffer);



// The Makerfabs library uses UART_puts for debug
void UART_puts(const char* str);

#endif
