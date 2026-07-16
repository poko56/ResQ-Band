/*
 * MIT License
 * 
 * Copyright (c) 2018 Michele Biondi, Andrea Salvatori
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#pragma once

#include <Arduino.h>
#include "DW1000NgConstants.hpp"

typedef struct device_configuration_t {
    bool extendedFrameLength;
    bool receiverAutoReenable;
    bool smartPower;
    bool frameCheck;
    bool nlos;
    SFDMode sfd;
    Channel channel;
    DataRate dataRate;
    PulseFrequency pulseFreq;
    PreambleLength preambleLen;
    PreambleCode preaCode;
} device_configuration_t;

typedef struct interrupt_configuration_t {
    bool interruptOnSent;
    bool interruptOnReceived;
    bool interruptOnReceiveFailed;
    bool interruptOnReceiveTimeout;
    bool interruptOnReceiveTimestampAvailable;
    bool interruptOnAutomaticAcknowledgeTrigger;
} interrupt_configuration_t;

typedef struct frame_filtering_configuration_t {
    bool behaveAsCoordinator;
    bool allowBeacon;
    bool allowData;
    bool allowAcknowledgement;
    bool allowMacCommand;
    bool allowAllReserved;
    bool allowReservedFour;
    bool allowReservedFive;
} frame_filtering_configuration_t;

typedef struct sleep_configuration_t {
    bool onWakeUpRunADC;
    bool onWakeUpReceive;
    bool onWakeUpLoadEUI;
    bool onWakeUpLoadL64Param;
    bool preserveSleep;
    bool enableSLP;
    bool enableWakePIN;
    bool enableWakeSPI;
} sleep_configuration_t;
