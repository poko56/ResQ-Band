/*
 * Copyright (c) 2023 by Philipp Hafkemeyer
 * Qorvo DW3000 library for Arduino
 *
 * This project is licensed under the GNU GPL v3.0 License.
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include "Arduino.h"
#include "SPI.h"
//#include "stdlib.h"
#include "DW3000Ng.hpp"

extern HardwareSerial Serial;

DW3000NgClass DW3000Ng;
uint8_t DW3000NgClass::_cs = 10;
uint8_t DW3000NgClass::_rst = 9;

void DW3000NgClass::spiSelect(uint8_t cs) {
    _cs = cs;
}
void DW3000NgClass::rstSelect(uint8_t rst) {
    _rst = rst;
}

#define DEBUG_OUTPUT 0 // Turn to 1 to get all reads, writes, etc. as info in the console

int antenna_delay = 0x3FCA; // For calibration purposes; the smaller the number, the longer the ranging results

int led_status = 0;

int destination = 0x0;  // Default Values for Destination and Sender IDs
int sender = 0x0;

// Initial Radio Configuration
int DW3000NgClass::config[] = {
    CHANNEL_5,           // Channel
    PREAMBLE_128,        // Preamble Length
    9,                   // Preamble Code (Same for RX and TX!)
    PAC8,                // PAC
    DATARATE_6_8MB,      // Datarate
    PHR_MODE_STANDARD,   // PHR Mode
    PHR_RATE_850KB       // PHR Rate
};



/*
 #####  Chip Setup  #####
*/



/*
 Initializes the SPI Interface
*/
void DW3000NgClass::begin() {
    delay(5);
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH); // Ensure CS is HIGH before any SPI transaction
    SPI.begin();
    
    // Force chip to IDLE by issuing TXRXOFF fast command before init()
    writeFastCommand(0x00);
    delay(10);

    spiSelect(_cs);

    Serial.println("[INFO] SPI ready");
}

/*
 Initializes the chip, checks for a connection and sets up a initial configuration
*/
void DW3000NgClass::init() {
    Serial.println("\n+++ DecaWave DW3000 Test +++\n");

    if (!checkForDevID()) {
        Serial.println("[ERROR] Dev ID is wrong! Aborting!");
        return;
    }

    setBitHigh(GEN_CFG_AES_LOW_REG, 0x10, 4);


    while (!checkForIDLE()) {
        Serial.println("[WARNING] IDLE FAILED (stage 1)");
        delay(100);
    }

    softReset();

    delay(200);

    while (!checkForIDLE()) {
        Serial.println("[WARNING] IDLE FAILED (stage 2)");
        delay(100);
    }

    uint32_t ldo_low = readOTP(0x04);
    uint32_t ldo_high = readOTP(0x05);
    uint32_t bias_tune = readOTP(0xA);
    bias_tune = (bias_tune >> 16) & BIAS_CTRL_BIAS_MASK;

    if (ldo_low != 0 && ldo_high != 0 && bias_tune != 0) {
        write32(0x11, 0x1F, bias_tune);

        write32(0x0B, 0x08, 0x0100);
    }

    int xtrim_value = readOTP(0x1E);

    xtrim_value = xtrim_value == 0 ? 0x2E : xtrim_value; //if xtrim_value from OTP memory is 0, choose 0x2E as default value

    write8(FS_CTRL_REG, 0x14, xtrim_value);
    if (DEBUG_OUTPUT) Serial.print("xtrim: ");
    if (DEBUG_OUTPUT) Serial.println(xtrim_value);

    writeSysConfig();

    // disable all interrupts first
    write32(0x00, 0x3C, 0x00000000);
    write32(0x00, 0x40, 0x00000000);

    // clear all status bits low/high
    write32(0x00, 0x44, 0xFFFFFFFF);
    write32(0x00, 0x48, 0xFFFFFFFF);

    // enable only needed IRQ
    uint32_t irq_mask = SYS_STATUS_TXFRS | SYS_STATUS_RXFCG | SYS_STATUS_RXFCE | SYS_STATUS_RXPE | SYS_STATUS_RXSFDTO | SYS_STATUS_RXPTO | SYS_STATUS_CIAERR;
    write32(0x00, 0x3C, irq_mask);
    write32(0x00, 0x40, 0x00000000);

    write24(0x0A, 0x00, 0x000900); //AON_DIG_CFG register setup; sets up auto-rx calibration and on-wakeup GO2IDLE  //0xA

    /*
     * Set RX and TX config
     */
    
    read32(0x4, 0x20);

    //SET PAC TO 32 (0x00) reg:06:00 bits:1-0, bit 4 to 0 (00001100) (0xC)
    write32(0x6, 0x0, 0x81101C);

    write32(0x07, 0x34, 0x4); // Enable temp sensor readings


    // Cleaned up magic values: rely on writeSysConfig() for RF tuning or default POR.

    // write32(0x11, 0x04, 0xB40200); // DO NOT FORCE CLK_CTRL, let it be default or 0!

    // write32(0x11, 0x08, 0x80030738); // DO NOT FORCE SEQ_CTRL!
    Serial.println("[INFO] Initialization finished.");

    Serial.print("PLL_CFG 0x09:0x00 = 0x");
    Serial.println(DW3000NgClass::read32(0x09, 0x00), HEX);

    Serial.print("PLL_CAL 0x09:0x08 = 0x");
    Serial.println(DW3000NgClass::read8(0x09, 0x08), HEX);

    Serial.print("CHAN_CTRL 0x01:0x14 = 0x");
    Serial.println(DW3000NgClass::read32(0x01, 0x14), HEX);

    Serial.print("SYS_CFG 0x00:0x10 = 0x");
    Serial.println(DW3000NgClass::read32(0x00, 0x10), HEX);
}

/*
 Writes the initial configuration to the chip
*/
void DW3000NgClass::writeSysConfig() {


    if (config[2] > 24) {
        Serial.println("[ERROR] SCP ERROR! TX & RX Preamble Code higher than 24!");
    }

    int otp_write = 0x1400;

    bool longPreamble =
        config[1] == PREAMBLE_1024 ||
        config[1] == PREAMBLE_1536 ||
        config[1] == PREAMBLE_2048 ||
        config[1] == PREAMBLE_4096;
    if (longPreamble) {
        otp_write |= 0x04;
    }


    write32(OTP_IF_REG, 0x08, otp_write); //set OTP config
    

    //64 = STS length
    write32(STS_CFG_REG, 0x0, 64 / 8 - 1);

    write8(GEN_CFG_AES_LOW_REG, 0x29, 0x00);    // DW3000 SYS_CFG (0x00:0x10)
    // phr_mode (bit 4) = 0 (Standard 802.15.4-2015)
    // phr_6m8 (bit 5) = 1 (6.8 Mbps PHR)
    // cia_ipatov (bit 7) = 1
    // cia_sts (bit 8) = 1
    int usr_cfg = 0x0080;
    write32(GEN_CFG_AES_LOW_REG, 0x10, usr_cfg);

    

    // Dynamic CHAN_CTRL
    int chan_ctrl_val = 0;
    if (config[0] == CHANNEL_9) chan_ctrl_val |= 1;
    
    uint8_t pcode = config[2];
    chan_ctrl_val |= (pcode << 3);
    chan_ctrl_val |= (pcode << 8);

    uint8_t sfd_type = 0; // Standard IEEE 8-symbol
    if (config[1] == PREAMBLE_1024 || config[1] == PREAMBLE_2048) {
        sfd_type = 2; // Decawave 16-symbol
    }
    chan_ctrl_val |= (sfd_type << 1);
    
    write32(GEN_CFG_AES_HIGH_REG, 0x14, chan_ctrl_val);

    int tx_fctrl_val = read32(GEN_CFG_AES_LOW_REG, 0x24);
    
    tx_fctrl_val &= ~(0x0F << 12); // Clear txpsr (bits 12-15)
    tx_fctrl_val |= (config[1] << 12); // Add preamble length (MUST BE BIT 12 IN DW3000!)
    tx_fctrl_val &= ~(0x01 << 10); // Clear txbr (bit 10)
    tx_fctrl_val |= (config[4] << 10); // Add data rate
    write32(GEN_CFG_AES_LOW_REG, 0x24, tx_fctrl_val);
    
    if (config[0] == CHANNEL_5) {
        // Channel 5 RF/PLL profile
        write8 (RF_CONF_REG, 0x48, 0x14);        // LDO_RLOAD / RF analog support
        write8 (RF_CONF_REG, 0x1A, 0x0E);        // RF_TX_CTRL_1
        write32(RF_CONF_REG, 0x1C, 0x1C071134);  // RF_TX_CTRL_2 for CH5
        write32(FS_CTRL_REG, 0x00, 0x1F3C);      // PLL_CFG for CH5
        write8 (FS_CTRL_REG, 0x08, 0x81);        // PLL_CAL
    } else {
        // Channel 9
        write32(RF_CONF_REG, 0x1C, 0x1C010034);
        write8 (FS_CTRL_REG, 0x08, 0x81);
    }


    // Transition to IDLE_PLL
    write8(GEN_CFG_AES_LOW_REG, 0x44, 0x02);

    int otp_val = read32(OTP_IF_REG, 0x08);
    otp_val |= 0x40;
    if (config[0]) otp_val |= 0x2000;


    write32(OTP_IF_REG, 0x08, otp_val);

    

    write24(EXT_SYNC_REG, 0x0C, 0x020000); //Calibrate RX
    write8(EXT_SYNC_REG, 0x0C, 0x11);

    write8(0x0E, 0x02, 0x01); //Enable full CIA diagnostics to get signal strength information

    setTXAntennaDelay(antenna_delay); //set default antenna delay
}

/*
 Configures the chip for usage as a Transfer Device
*/
void DW3000NgClass::configureAsTX() {
    write8(RF_CONF_REG, 0x1C, 0x34); //write pg_delay
    write32(GEN_CFG_AES_HIGH_REG, 0x0C, 0xFDFDFDFD);
}

/*
 Sets the first 4 GPIO pins as output for external measurements and LED usage
*/
void DW3000NgClass::setupGPIO() {
    write32(0x05, 0x08, 0xF0); //Set GPIO0 - GPIO3 as OUTPUT on DW3000
}



/*
 #####  Double-Sided Ranging  #####
*/


/*
 Sends a Frame that is supposed to work in double sided ranging (See DW3000 User Manual 12.3 for more)
 @param stage Double-sided Ranging is more complicated than regular single-sided Ranging. Therefore, stages were introduced to make sure that the right frames get received at the right time. stage is a 3 bit int.
*/
void DW3000NgClass::ds_sendFrame(int stage) {
    setMode(1);
    write32(0x14, 0x01, sender & 0xFF);
    write32(0x14, 0x02, destination & 0xFF);
    write32(0x14, 0x03, stage & 0x7);
    setFrameLength(4);

    TXInstantRX(); //Await response

    bool error = true;
    for (int i = 0; i < 50; i++) {
        if (sentFrameSucc()) {
            error = false;
            break;
        }
    };
    if (error) {
        Serial.println("[ERROR] Could not send frame successfully!");
    }
}

/*
 Send the information that chip B collected to chip A for final time calculations
 @param t_roundB The time that it took between chip B (this chip) sending an answer and getting a response (rx2 - tx1)
 @param t_replyB The time that the chip took to process the received frame (tx1 - rx1)
*/
void DW3000NgClass::ds_sendRTInfo(int t_roundB, int t_replyB) {
    setMode(1);
    write32(0x14, 0x01, destination & 0xFF);
    write32(0x14, 0x02, sender & 0xFF);
    write32(0x14, 0x03, 4);
    write32(0x14, 0x04, t_roundB);
    write32(0x14, 0x08, t_replyB);

    setFrameLength(12);

    TXInstantRX();
}

/*
 Process all Round Trip Time info
 @param t_roundA The time it took between chip A sending a frame and getting a response
 @param t_replyA The time that chip A took to process the received frame
 @param t_roundB The time that it took between chip B sending an answer and getting a response
 @param t_replyB The time that chip B took to process the received frame
 @param clk_offset The calculated clock offset between both chips (See DW3000 User Manual 10.1 for more)
 @return returns the time in units of 15.65ps that the frames were in the air on average (only one direction)
*/
int DW3000NgClass::ds_processRTInfo(int t_roundA, int t_replyA, int t_roundB, int t_replyB, int clk_offset) { //returns ranging time in DW3000 ps units (~15.65ps per unit)
    if (DEBUG_OUTPUT) {
        Serial.println("\nProcessing Information:");
        Serial.print("t_roundA: ");
        Serial.println(t_roundA);
        Serial.print("t_replyA: ");
        Serial.println(t_replyA);
        Serial.print("t_roundB: ");
        Serial.println(t_roundB);
        Serial.print("t_replyB: ");
        Serial.println(t_replyB);
    }

    int reply_diff = t_replyA - t_replyB;

    long double clock_offset = t_replyA > t_replyB ? 1.0 + getClockOffset(clk_offset) : 1.0 - getClockOffset(clk_offset);

    int first_rt = t_roundA - t_replyB;
    int second_rt = t_roundB - t_replyA;

    int combined_rt = (first_rt + second_rt - (reply_diff - (reply_diff * clock_offset))) / 2;
    // int combined_rt_raw = (first_rt + second_rt) / 2;

    return combined_rt / 2; // divided by 2 to get just one range
}

/*
 Returns the stage that the frame was sent in
 @return The stage that the frame was sent in (read from the TX_Buffer)
*/
int DW3000NgClass::ds_getStage() {
    return read32(0x12, 0x03) & 0b111;
}

/*
 Checks if frame is error frame by checking its mode bits
 @return True if mode == 7; False if anything else
*/
bool DW3000NgClass::ds_isErrorFrame() {
    return ((read32(0x12, 0x00) & 0x7) == 7);
}

/*
 Sends a frame that has its mode set to 7 (Error Frame). Instantly switches to receive mode (RX)
*/
void DW3000NgClass::ds_sendErrorFrame() {
    Serial.println("[WARNING] Error Frame sent. Reverting back to stage 0.");
    setMode(7);
    setFrameLength(3);
    standardTX();
}



/*
 #####  Radio Settings  #####
*/


/*
 Set the channel that the chip should operate on
 @param data CHANNEL_5 or CHANNEL_9
*/
void DW3000NgClass::setChannel(uint8_t data) {
    if (data == CHANNEL_5 || data == CHANNEL_9) config[0] = data;
}

/*
 Set the preamble length for frame sending
 @param data See all options below or in DW3000Constants.h
*/
void DW3000NgClass::setPreambleLength(uint8_t data) {
    if (data == PREAMBLE_32 || data == PREAMBLE_64 || data == PREAMBLE_128 || data == PREAMBLE_256 || 
        data == PREAMBLE_512 || data == PREAMBLE_1024 || data == PREAMBLE_1536 || 
        data == PREAMBLE_2048 || data == PREAMBLE_4096) config[1] = data;
}

/*
 Set the preamble code
 @param data Should be between 9 and 12
*/
void DW3000NgClass::setPreambleCode(uint8_t data) {
    if (data <= 12 && data >= 9) config[2] = data;
}

/*
 Set the PAC size
 @param data PAC4, PAC8, PAC16 or PAC32
*/
void DW3000NgClass::setPACSize(uint8_t data) {
    if (data == PAC4 || data == PAC8 || data == PAC16 || data == PAC32) config[3] = data;
}

/*
 Set the datarate the chip sends and receives on
 @param data DATARATE_6_8_MB or DATARATE_850KB
*/
void DW3000NgClass::setDatarate(uint8_t data) {
    if (data == DATARATE_6_8MB || data == DATARATE_850KB) config[4] = data;
}

/*
 Set the PHR mode for the chip
 @param data PHR_MODE_STANDARD or PHR_MODE_LONG
*/
void DW3000NgClass::setPHRMode(uint8_t data) {
    if (data == PHR_MODE_STANDARD || data == PHR_MODE_LONG) config[5] = data;
}

/*
 Set the PHR rate for the chip
 @param data PHR_RATE_6_8MB or PHR_RATE_850KB
*/
void DW3000NgClass::setPHRRate(uint8_t data) {
    if (data == PHR_RATE_6_8MB || data == PHR_RATE_850KB) config[6] = data;
}



/*
 #####  Protocol Settings  #####
*/


/*
 Sets the frame type/mode to determine between double-sided and error frames.
 @param mode The mode that should be used:
    * 0 - Standard
    * 1 - Double-Sided Ranging
    * 2-6 - Reserved
    * 7 - Error
*/
void DW3000NgClass::setMode(int mode) {
    write32(0x14, 0x00, mode & 0x7);
}

/*
 Writes the given data to the chips TX Frame buffer
 @param frame_data The data that should be written onto the chip
*/
void DW3000NgClass::setTXFrame(unsigned long long frame_data) {  // deprecated! use write32(TX_BUFFER_REG, [...]);
    if (frame_data > ((pow(2, 8 * 8) - FCS_LEN))) {
        Serial.println("[ERROR] Frame is too long (> 1023 Bytes - FCS_LEN)!");
        return;
    }

    write32(TX_BUFFER_REG, 0x00, frame_data);
}

/*
 Sets the frames data length in bytes
 @param frameLen The length of the data in bytes
*/
void DW3000NgClass::setFrameLength(int frameLen) { // set Frame length in Bytes
    frameLen = frameLen + FCS_LEN;
    int curr_cfg = read32(0x00, 0x24);
    if (frameLen > 1023) {
        Serial.println("[ERROR] Frame length + FCS_LEN (2) is longer than 1023. Aborting!");
        return;
    }
    int tmp_cfg = (curr_cfg & 0xFFFFFC00) | frameLen;

    write32(GEN_CFG_AES_LOW_REG, 0x24, tmp_cfg);
}

/*
 Set the Antenna Delay for delayedTX operations
 @param data Can be anything between 0 and 0xFFFF
*/
void DW3000NgClass::setTXAntennaDelay(int delay) {
    antenna_delay = delay;
    write32(0x01, 0x04, delay);
}

/*
 Set the chips sender ID. As long as the value is not changed, it won't be changed by the program.
 @param senderID The ID that should be set. Can be between 0 and 255. Default is 0
*/
void DW3000NgClass::setSenderID(int senderID) {
    sender = senderID;
}

/*
 Set the destination ID. As long as the value is not changed, it won't be changed by the program. If you want to send a second frame, it will still hold the same destination ID!
 @param destID The ID that the frame should be sent to. Can be between 0 and 255. Default is 0
*/
void DW3000NgClass::setDestinationID(int destID) {
    destination = destID;
}



/*
 #####  Status Checks  #####
*/


/*
 Checks if a frame got received successfully
 @return 1 if successfully received; 2 if RX Status Error occured; 0 if no frame got received
*/
int DW3000NgClass::receivedFrameSucc() {
    int sys_stat = read32(GEN_CFG_AES_LOW_REG, 0x44);
    if ((sys_stat & SYS_STATUS_FRAME_RX_SUCC) > 0) {
        return 1;
    }
    else if ((sys_stat & SYS_STATUS_RX_ERR) > 0) {
        return 2;
    }
    return 0;
}

/*
 Checks if a frame got sent successfully
 @return 1 if successfully sent; 2 if TX Status Error occured; 0 if no frame got sent
*/
int DW3000NgClass::sentFrameSucc() { //No frame sent: 0; frame sent: 1; error while sending: 2
    int sys_stat = read32(GEN_CFG_AES_LOW_REG, 0x44);
    if ((sys_stat & SYS_STATUS_FRAME_TX_SUCC) == SYS_STATUS_FRAME_TX_SUCC) {
        return 1;
    }
    return 0;
}

/*
 Returns the senderID of the received frame.
 @return senderID of the received frame by reading out the frames data
*/
int DW3000NgClass::getSenderID() {
    return read32(0x12, 0x01) & 0xFF;
}

/*
 Returns the destinationID of the received frame.
 @return destinationID of the received frame by reading out the frames data
*/
int DW3000NgClass::getDestinationID() {
    return read32(0x12, 0x02) & 0xFF;
}

/*
 Checks if the chip has its Power Management System Control (PMSC) module in IDLE mode
 @return True if in IDLE, False if not
 */
bool DW3000NgClass::checkForIDLE() {
    return (read32(0x0F, 0x30) >> 16 & PMSC_STATE_IDLE) == PMSC_STATE_IDLE || (read32(0x00, 0x44) >> 16 & (SPIRDY_MASK | RCINIT_MASK)) == (SPIRDY_MASK | RCINIT_MASK) ? 1 : 0;
}

/*
 Checks if SPI can communicate with the chip
 @return 1 if True, 0 if False
*/
bool DW3000NgClass::checkSPI() {
    return checkForDevID();
}



/*
 #####  Radio Analytics  #####
*/


/*
 Calculates the Signal Strength of the received frame in dBm
 NOTE: If not using 64MHz PRF: See user manual capter 4.7.2 for an alternative calculation method
 @return The Signal Strength of the received frame in dBm
*/
double DW3000NgClass::getSignalStrength() {
    int CIRpower = read32(0x0C, 0x2C) & 0x1FF;
    int PAC_val = read32(0x0C, 0x58) & 0xFFF;
    unsigned int DGC_decision = (read32(0x03, 0x60) >> 28) & 0x7;
    double PRF_const = 121.7;

    /*Serial.println("Signal Strength Data:");
    Serial.print("CIR Power: ");
    Serial.println(CIRpower);
    Serial.print("PAC val: ");
    Serial.println(PAC_val);
    Serial.print("DGC decision: ");
    Serial.println(DGC_decision);*/

    return 10 * log10((CIRpower * (1 << 21)) / pow(PAC_val, 2)) + (6 * DGC_decision) - PRF_const;
}

/*
 Calculates the First Path Signal Strength of the received frame in dBm. Useful to check if a ranging was NLOS or LOS by comparing it to the overall Signal Strength.
 @return The First Path Signal Strength of the received frame in dBm
*/
double DW3000NgClass::getFirstPathSignalStrength() {
    float f1 = (read32(0x0C, 0x30) & 0x3FFFFF) >> 2;
    float f2 = (read32(0x0C, 0x34) & 0x3FFFFF) >> 2;
    float f3 = (read32(0x0C, 0x38) & 0x3FFFFF) >> 2;

    int PAC_val = read32(0x0C, 0x58) & 0xFFF;
    unsigned int DGC_decision = (read32(0x03, 0x60) >> 28) & 0x7;
    double PRF_const = 121.7;

    return 10 * log10((pow(f1, 2) + pow(f2, 2) + pow(f3, 2)) / pow(PAC_val, 2)) + (6 * DGC_decision) - PRF_const;
}

/*
 Get the currently set Antenna Delay for delayedTX operations
 .@return Antenna Delay
*/
int DW3000NgClass::getTXAntennaDelay() { //DEPRECATED use ANTENNA_DELAY variable instead!
    int delay = read32(0x01, 0x04) & 0xFFFF;
    return delay;
}

/*
 Get the calculated clock offset between this chip and the chip that sent a frame
 @return Calculated clock offset of the other chip
*/
long double DW3000NgClass::getClockOffset() {
    if (config[0] == CHANNEL_5) {
        return getRawClockOffset() * CLOCK_OFFSET_CHAN_5_CONSTANT / 1000000;
    }
    else {
        return getRawClockOffset() * CLOCK_OFFSET_CHAN_9_CONSTANT / 1000000;
    }
}

/*
 Get the calculated clock offset from the second chips perspective
 @return Calculated clock offset of this chip from the other chips perspective
*/
long double DW3000NgClass::getClockOffset(int32_t sec_clock_offset) {
    if (config[0] == CHANNEL_5) {
        return sec_clock_offset * CLOCK_OFFSET_CHAN_5_CONSTANT / 1000000;
    }
    else {
        return sec_clock_offset * CLOCK_OFFSET_CHAN_9_CONSTANT / 1000000;
    }
}

/*
 Get the raw clockset offset from the register of the chip
 @return Raw clock offset
*/
int DW3000NgClass::getRawClockOffset() {
    int raw_offset = read32(0x06, 0x29) & 0x1FFFFF;

    if (raw_offset & (1 << 20)) {
        raw_offset |= ~((1 << 21) - 1);
    }

    if (DEBUG_OUTPUT) {
        Serial.print("Raw offset: ");
        Serial.println(raw_offset);
    }
    return raw_offset;
}

/*
 Activates the chips internal temperature sensor and read its temperature
 @return the chips current temperature in °C
*/
float DW3000NgClass::getTempInC() {
    write32(0x07, 0x34, 0x04); //enable temp sensor readings

    write32(0x08, 0x00, 0x01); //enable poll

    while (!(read32(0x08, 0x04) & 0x01))
    {
    };

    int res = read32(0x08, 0x08);
    res = (res & 0xFF00) >> 8;
    int otp_temp = readOTP(0x09) & 0xFF;
    float tmp = (float)((res - otp_temp) * 1.05f) + 22.0f;

    write8(0x08, 0x00, 0x00); //Reset poll enable

    return tmp;
}

/*
 Reads the internal RX Timestamp. The timestamp is a relative timestamp to the chips internal clock. Units of ~15.65ps. (See DW3000 User Manual 4.1.7 for more)
 @return The RX Timestamp in units of ~15.65ps
*/
unsigned long long DW3000NgClass::readRXTimestamp() {
    uint32_t ts_low = read32(0x0C, 0x00);
    unsigned long long ts_high = read32(0x0C, 0x04) & 0xFF;

    unsigned long long rx_timestamp = (ts_high << 32) | ts_low;

    return rx_timestamp;
}

/*
 Reads the internal TX Timestamp. The timestamp is a relative timestamp to the chips internal clock. Units of ~15.65ps. (See DW3000 User Manual 3.2 for more)
 @return The TX Timestamp in units of ~15.65ps
*/
unsigned long long DW3000NgClass::readTXTimestamp() {
    unsigned long long ts_low = read32(0x00, 0x74);
    unsigned long long ts_high = read32(0x00, 0x78) & 0xFF;

    unsigned long long tx_timestamp = (ts_high << 32) + ts_low;

    return tx_timestamp;
}



/*
 #####  Chip Interaction  #####
*/


void DW3000NgClass::writeBytes(uint8_t base, uint8_t sub, const uint8_t* data, uint16_t length) {
    uint8_t header[2];
    uint8_t header_size;
    if (sub == 0) {
        header[0] = 0x80 | ((base & 0x1F) << 1);
        header_size = 1;
    } else {
        header[0] = 0x80 | 0x40 | ((base & 0x1F) << 1) | ((sub >> 6) & 0x01);
        header[1] = (uint8_t)(sub << 2);
        header_size = 2;
    }

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    for (int i = 0; i < header_size; i++) {
        SPI.transfer(header[i]);
    }
    for (uint16_t i = 0; i < length; i++) {
        SPI.transfer(data[i]);
    }
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
}

void DW3000NgClass::readBytes(uint8_t base, uint8_t sub, uint8_t* data, uint16_t length) {
    uint8_t header[2];
    uint8_t header_size;
    if (sub == 0) {
        header[0] = 0x00 | ((base & 0x1F) << 1);
        header_size = 1;
    } else {
        header[0] = 0x00 | 0x40 | ((base & 0x1F) << 1) | ((sub >> 6) & 0x01);
        header[1] = (uint8_t)(sub << 2);
        header_size = 2;
    }

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    for (int i = 0; i < header_size; i++) {
        SPI.transfer(header[i]);
    }
    for (uint16_t i = 0; i < length; i++) {
        data[i] = SPI.transfer(0x00);
    }
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
}

void DW3000NgClass::write32(uint8_t base, uint8_t sub, uint32_t data) {
    uint8_t buf[4];
    buf[0] = data & 0xFF;
    buf[1] = (data >> 8) & 0xFF;
    buf[2] = (data >> 16) & 0xFF;
    buf[3] = (data >> 24) & 0xFF;
    writeBytes(base, sub, buf, 4);
}

void DW3000NgClass::write24(uint8_t base, uint8_t sub, uint32_t data) {
    uint8_t buf[3];
    buf[0] = data & 0xFF;
    buf[1] = (data >> 8) & 0xFF;
    buf[2] = (data >> 16) & 0xFF;
    writeBytes(base, sub, buf, 3);
}

void DW3000NgClass::write16(uint8_t base, uint8_t sub, uint16_t data) {
    uint8_t buf[2];
    buf[0] = data & 0xFF;
    buf[1] = (data >> 8) & 0xFF;
    writeBytes(base, sub, buf, 2);
}

void DW3000NgClass::write8(uint8_t base, uint8_t sub, uint8_t data) {
    writeBytes(base, sub, &data, 1);
}

uint32_t DW3000NgClass::read32(uint8_t base, uint8_t sub) {
    uint8_t buf[4];
    readBytes(base, sub, buf, 4);
    return ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

uint16_t DW3000NgClass::read16(uint8_t base, uint8_t sub) {
    uint8_t buf[2];
    readBytes(base, sub, buf, 2);
    return ((uint16_t)buf[0]) | ((uint16_t)buf[1] << 8);
}

uint8_t DW3000NgClass::read8(uint8_t base, uint8_t sub) {
    uint8_t buf;
    readBytes(base, sub, &buf, 1);
    return buf;
}



/*
 Reads a specific OTP (One Time Programmable) Memory address inside the chips register
 @param addr The OTP Memory address
 @return The result of the read operation
 */
uint32_t DW3000NgClass::readOTP(uint8_t addr) {
    write32(OTP_IF_REG, 0x04, addr);
    write32(OTP_IF_REG, 0x08, 0x02);

    return read32(OTP_IF_REG, 0x10);
}



/*
 #####  Delayed Sending Settings  #####
*/


/*
 Sets a delay for a future TX operation
 @param delay The delay in units of ~4ns (see DW3000 User Manual 8.2.2.9 for more info)
*/
void DW3000NgClass::writeTXDelay(uint32_t delay) {
    write32(0x00, 0x2C, delay);
}

/*
 A delayed TX is typically performed to have a known delay between sender and receiver.
 As the chips delayedTX function has a lower timestamp resolution, the frame gets sent
 always a little bit earlier than its supposed to be. For example:
 Delay: 10ms. Receive Time of Frame: 2010us (2.01ms). It should send at 12.01ms, but will be
 sent at 12ms as the last digits get cut off. These last digits can be precalculated and
 sent inside the frame to be added to the RX Timestamp of the receiver. In the above case, 0.01ms
 The DX_Time resolution is ~4ns, the chips internal clock resolution is ~15.65ps.

 This function calculates the missing delay time, adds it to the frames payload and sets the fixed delay (TRANSMIT_DELAY).
*/
void DW3000NgClass::prepareDelayedTX() {
    long long rx_ts = readRXTimestamp();

    uint32_t exact_tx_timestamp = (long long)(rx_ts + TRANSMIT_DELAY) >> 8;

    long long calc_tx_timestamp = ((rx_ts + TRANSMIT_DELAY) & ~TRANSMIT_DIFF) + antenna_delay;

    uint32_t reply_delay = calc_tx_timestamp - rx_ts;

    /*
    * PAYLOAD DESIGN:
    +------+-----------------------------------------------------------------------+-------------------------------+-------------------------------+------+------+------+-----+
    | Byte |                                 1 (0x00)                              |           2 (0x01)            |           3 (0x02)            |     4 - 6 (0x03-0x05)    |
    +------+-----+-----+----+----------+----------+----------+----------+----------+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+------+------+------+-----+
    | Bits |  1  |  2  |  3 |     4    |     5    |     6    |     7    |     8    | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |                          |
    +------+-----+-----+----+----------+----------+----------+----------+----------+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+--------------------------+
    |      | Mode bits:     | Reserved | Reserved | Reserved | Reserved | Reserved |           Sender ID           |         Destination ID        | Internal Delay / Payload |
    |      | 0 - Standard   |          |          |          |          |          |                               |                               |                          |
    |      |1-7 - See below |          |          |          |          |          |                               |                               |                          |
    +------+----------------+----------+----------+----------+----------+----------+-------------------------------+-------------------------------+--------------------------+
    *
    * Mode bits:
    * 0 - Standard
    * 1 - Double Sided Ranging
    * 2-6 - Reserved
    * 7 - Error
    */

    write32(0x14, 0x01, sender & 0xFF);
    write32(0x14, 0x02, destination & 0xFF);
    write32(0x14, 0x03, reply_delay); //set frame content

    setFrameLength(7); // Control Byte (1 Byte) + Sender ID (1 Byte) + Dest. ID (1 Byte) + Reply Delay (4 Bytes) = 7 Bytes

    //Write delay to register 
    writeTXDelay(exact_tx_timestamp);
}



/*
 #####  Radio Stage Settings / Transfer and Receive Modes  #####
*/


/*
 Activates delayed message transfer and switches to receive mode after TX is finished
*/
void DW3000NgClass::delayedTXThenRX() {
    writeFastCommand(0x0F);
}

/*
 Activates delayed message transfer
*/
void DW3000NgClass::delayedTX() {
    writeFastCommand(0x3);
}

/*
 Performs a standard TX command
*/
void DW3000NgClass::standardTX() {
    DW3000NgClass::writeFastCommand(0x01);
}

/*
 Performs a standard RX command
*/
void DW3000NgClass::standardRX() {
    DW3000NgClass::writeFastCommand(0x02);
}

/*
 Performs a TX operation and instantly switches to Receiver mode
*/
void DW3000NgClass::TXInstantRX() {
    DW3000NgClass::writeFastCommand(0x0C);
}



/*
 #####  DW3000 Firmware Interaction  #####
*/


/*
 Soft resets the chip via software
*/
void DW3000NgClass::softReset() {
    clearAONConfig();

    write32(PMSC_REG, 0x04, 0x1); //force clock to FAST_RC/4 clock

    write16(PMSC_REG, 0x00, 0x00); //init reset

    delay(100);

    write32(PMSC_REG, 0x00, 0xFFFF); //return back

    write8(PMSC_REG, 0x04, 0x00); //set clock back to Auto mode
}

/*
 Resets the Chip by physically pulling the _rst to LOW
*/
void DW3000NgClass::hardReset() {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW); // set reset pin active low to hard-reset DW3000 chip
    delay(10);
    pinMode(_rst, INPUT); // get pin back in floating state
}

/*
 Clears all System Status flags
*/
void DW3000NgClass::clearSystemStatus() {
    write32(GEN_CFG_AES_LOW_REG, 0x44, 0x3F7FFFFF);
}



/*
 #####  Hardware Status Information  #####
*/


/*
 Pulls a specific external LED to HIGH (tested on Makerfabs DW3000 board)
 @param led the index of the LED (0 - 2 possible)
 */
void DW3000NgClass::pullLEDHigh(int led) {
    if (led > 2) return;
    led_status = led_status | (1 << led);
    write32(0x05, 0x0C, led_status);
}

/*
 Pulls a specific external LED to LOW (tested on Makerfabs DW3000 board)
 @param led the index of the LED (0 - 2 possible)
 */
void DW3000NgClass::pullLEDLow(int led) {
    if (led > 2) return;
    led_status = led_status & ~((int)1 << led); //https://stackoverflow.com/questions/47981/how-to-set-clear-and-toggle-a-single-bit
    write32(0x05, 0x0C, led_status);
}



/*
 #####  Calculation and Conversion  #####
*/


/*
 Convert DW3000 internal picosecond units (~15.65ps per unit) to cm
 @param dw3000_ps_units DW3000 internal picosecond units. Gets returned from timestamps for example.
 @return The distance in cm
*/
double DW3000NgClass::convertToCM(int dw3000_ps_units) {
    return (double)dw3000_ps_units * PS_UNIT * SPEED_OF_LIGHT;
}

/*
 Calculate the Round Trip Time (RTT) for a ping operation and print out the distance in cm
*/
void DW3000NgClass::calculateTXRXdiff() {
    unsigned long long ping_tx = readTXTimestamp();
    unsigned long long ping_rx = readRXTimestamp();

    long double clk_offset = getClockOffset();
    long double clock_offset = 1.0 + clk_offset;

    /*
       * PAYLOAD DESIGN:
       +------+-----------------------------------------------------------------------+-------------------------------+-------------------------------+------+------+------+-----+
       | Byte |                                 1 (0x00)                              |           2 (0x01)            |           3 (0x02)            |     4 - 6 (0x03-0x...)   |
       +------+-----+-----+----+----------+----------+----------+----------+----------+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+------+------+------+-----+
       | Bits |  1  |  2  |  3 |     4    |     5    |     6    |     7    |     8    | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |                          |
       +------+-----+-----+----+----------+----------+----------+----------+----------+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+--------------------------+
       |      | Mode bits:     | Reserved | Reserved | Reserved | Reserved | Reserved |           Sender ID           |         Destination ID        | Internal Delay / Payload |
       |      | 0 - Standard   |          |          |          |          |          |                               |                               |                          |
       |      |1-7 - See below |          |          |          |          |          |                               |                               |                          |
       +------+----------------+----------+----------+----------+----------+----------+-------------------------------+-------------------------------+--------------------------+
    *
    * Mode bits:
    * 0 - Standard
    * 1 - Double Sided Ranging
    * 2-6 - Reserved
    * 7 - Error
    */

    long long t_reply = read32(RX_BUFFER_0_REG, 0x03);

    /*
    * Calculate round trip time (see DW3000 User Manual page 248 for more)
    */

    if (t_reply == 0) { //t_reply is 0 when the calculation could not be done on the PONG side
        return;
    }

    long long t_round = ping_rx - ping_tx;
    long long t_prop = lround((t_round - lround(t_reply * clock_offset)) / 2);

    long double t_prop_ps = t_prop * PS_UNIT;

    long double t_prop_cm = t_prop_ps * SPEED_OF_LIGHT;
    if (t_prop_cm >= 0) {
        printDouble(t_prop_cm, 100, false); // second value sets the decimal places. 100 = 2 decimal places, 1000 = 3, 10000 = 4, ...
        Serial.println("cm");
    }
}



/*
 #####  Printing  #####
*/


/*
 Debug Output to print the Round Trip Time (RTT) and essential additional information
*/
void DW3000NgClass::printRoundTripInformation() {
    Serial.println("\nRound Trip Information:");
    long long tx_ts = readTXTimestamp();
    long long rx_ts = readRXTimestamp();

    Serial.print("TX Timestamp: ");
    Serial.println(tx_ts);
    Serial.print("RX Timestamp: ");
    Serial.println(rx_ts);
}

/*
 Helper function to print Doubles in a Arduino Console. Prints values with a specific number of decimal places.
 @param val The value that should be printed
 @param precision Precision is 1 followed by the number of zeros for the desired number of decimal places. Example: printDouble (3.14159, 1000); prints 3.141 (three decimal places).
 @param linebreak If True, a linebreak will be added after the print (equal to Serial.println()). If not, no linebreak (equal to Serial.print())
*/
void DW3000NgClass::printDouble(double val, unsigned int precision, bool linebreak) { //https://forum.arduino.cc/t/printing-a-double-variable/44327/2
    Serial.print(int(val));  // print the integer part
    Serial.print("."); // print the decimal point
    unsigned int frac;
    if (val >= 0) {
        frac = (val - int(val)) * precision;
    }
    else {
        frac = (int(val) - val) * precision;
    }
    if (linebreak) {
        Serial.println(frac, DEC); // print the fraction with linebreak
    }
    else {
        Serial.print(frac, DEC); // print the fraction without linebreak
    }
}





/*
 ==========  Private Functions  ==========
*/





/*
 #####  Single Bit Settings  #####
*/


/*
 Set bit in a defined register address
 @param reg_addr The registers base address
 @param sub_addr The registers sub address
 @param shift The bit that should be modified, relative to the base and sub address (0 for bit 0, 1 for bit 1, etc.)
 @param b The state that the bit should be set to. True if should be set to 1, False if 0
*/
void DW3000NgClass::setBit(int reg_addr, int sub_addr, int shift, bool b) {
    uint8_t tmpByte = read8(reg_addr, sub_addr);
    if (b) {
        bitSet(tmpByte, shift);
    }
    else {
        bitClear(tmpByte, shift);
    }
    write8(reg_addr, sub_addr, tmpByte);
}

/*
 Sets bit to High (1) in a defined register
 @param reg_addr The registers base address
 @param sub_addr The registers sub address
 @param shift The bit that should be modified, relative to the base and sub address (0 for bit 0, 1 for bit 1, etc.)
*/
void DW3000NgClass::setBitHigh(int reg_addr, int sub_addr, int shift) {
    setBit(reg_addr, sub_addr, shift, 1);
}

/*
 Sets bit to Low (0) in a defined register
 @param reg_addr The registers base address
 @param sub_addr The registers sub address
 @param shift The bit that should be modified, relative to the base and sub address (0 for bit 0, 1 for bit 1, etc.)
*/
void DW3000NgClass::setBitLow(int reg_addr, int sub_addr, int shift) {
    setBit(reg_addr, sub_addr, shift, 0);
}



/*
 #####  Fast Commands  #####
*/


/*
 Writes a Fast Command to the chip (See DW3000 User Manual chapter 9 for more)
 @param cmd The command that should be sent
*/
void DW3000NgClass::writeFastCommand(int cmd) {
    if (DEBUG_OUTPUT) Serial.print("[INFO] Executing short command: ");

    int header = 0;

    header = header | 0x1;
    header = header | (cmd & 0x1F) << 1;
    header = header | 0x80;

    if (DEBUG_OUTPUT) Serial.println(header, BIN);

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    SPI.transfer((uint8_t)header);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
}



/*
 #####  SPI Interaction  #####
*/






/*
 #####  Soft Reset Helper Method  #####
*/


/*
 Clears the Always On register. This register stores information as long as power is supplied to the chip.
*/
void DW3000NgClass::clearAONConfig() {
    write16(AON_REG, NO_OFFSET, 0x00);
    write8(AON_REG, 0x14, 0x00);

    write8(AON_REG, 0x04, 0x00); //clear control of aon reg

    write32(AON_REG, 0x04, 0x02);

    delay(1);
}



/*
 #####  Other Helper Methods  #####
*/


/*
 Helper function to count the bits of a number
 return length of a number in bits
*/

/*
 Checks if a DeviceID can be read from the device (if not, SPI can not connect to the chip). Acts as a sanity check.
 @return 1 if DeviceID could be read; 0 if not.
*/
int DW3000NgClass::checkForDevID() {
    int res = read32(GEN_CFG_AES_LOW_REG, NO_OFFSET);
    if (res != 0xDECA0302U && res != 0xDECA0312U) {
        Serial.println("[ERROR] DEV_ID IS WRONG!");
        return 0;
    }
    return 1;
}
