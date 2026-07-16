#include <SPI.h>
#include <UWB_Core.h>

// IMPORTANT: Set to 1000 for DW1000, or 3000 for DW3000
#define UWB_MODULE 1000

#if UWB_MODULE == 3000
#include <UWB_DW3000.h>
#include <DW3000Ng/DW3000Ng.hpp>
UWB_DW3000 uwb;
#else
#include <UWB_DW1000.h>
UWB_DW1000 uwb;
#endif

HardwareSerial Serial(PA10, PA9); // STM32 Serial mapping

// IMPORTANT: Set to 1 to flash the Transmitter node, 0 to flash the Receiver node.
#define NODE_MODE_TX 0

// Pins mapped for STM32F103C8T6 (stm32duino) based on CubeIDE project
const int PIN_CS = PA4;
const int PIN_IRQ = PB0;
const int PIN_RST = PB1;

// SPI Pins
const int PIN_SCK = PA5;
const int PIN_MISO = PA6;
const int PIN_MOSI = PA7;

uint8_t payload[] = "Hello UWB!";
uint32_t lastTx = 0;
volatile bool txComplete = false;
volatile bool rxComplete = false;
volatile bool irqPending = false;
uint16_t rxLength = 0;
uint8_t rxBuffer[128];

void onTxDone() {
    txComplete = true;
}

void onRxDone() {
    rxComplete = true;
}

// Interrupt Service Routine
void uwb_isr() {
    irqPending = true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("UWB Module Initialization...");
    
    // Configure SPI pins
    SPI.setMOSI(PIN_MOSI);
    SPI.setMISO(PIN_MISO);
    SPI.setSCLK(PIN_SCK);

    uwb.setCallbacks(onTxDone, onRxDone);

    if (uwb.begin(PIN_CS, PIN_IRQ, PIN_RST, SPI)) {
        Serial.println("UWB Init Success.");
        uwb.configure(5, 9, 128, 1); // Channel 5, Code 9, Preamble 128, 6.8Mbps
        
        attachInterrupt(digitalPinToInterrupt(PIN_IRQ), uwb_isr, RISING);

        if (NODE_MODE_TX) {
            Serial.println("Node configured as TRANSMITTER.");
            lastTx = millis();
        } else {
            Serial.println("Node configured as RECEIVER.");
            uwb.startReceive();
        }
    } else {
        Serial.println("UWB Init Failed.");
        while(1) { delay(100); }
    }
}

void loop() {
    if (irqPending || digitalRead(PIN_IRQ)) {
        irqPending = false;
        uwb.onIRQ();
    }
    
    if (NODE_MODE_TX) {
        // Transmitter logic: send once per second
        if (millis() - lastTx > 1000) {
            lastTx = millis();
            Serial.println("Transmitting...");
            uwb.transmit(payload, sizeof(payload)-1);
        }
        
        if (txComplete) {
            txComplete = false;
            Serial.println("TX Done.");
        }
    } else {
        // Receiver logic: constantly wait for RX
        if (rxComplete) {
            rxComplete = false;
            
            if (uwb.readReceivedData(rxBuffer, rxLength)) {
                rxBuffer[rxLength] = '\0';
                Serial.print("RX Success! Length: ");
                Serial.print(rxLength);
                Serial.print(" | Payload: ");
                Serial.println((char*)rxBuffer);
                
                Serial.print("Signal Power: ");
                Serial.print(uwb.getReceivePower());
                Serial.println(" dBm");
            } else {
                Serial.println("RX Event fired but read failed.");
            }
        }
    }
}
