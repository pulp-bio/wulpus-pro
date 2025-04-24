/*
 * Copyright (C) 2023 ETH Zurich. All rights reserved.
 *
 * Author: Sergei Vostrikov, ETH Zurich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WULPUS_SYS_H_
#define WULPUS_SYS_H_

#include "driverlib.h"

#include "us_spi.h"
#include "us_hv_mux.h"
#include "uslib.h"

// Defines for LED on Acquisition PCB
#define GPIO_PORT_LED_MSP430        GPIO_PORT_P4
#define GPIO_PIN_LED_MSP430         GPIO_PIN1

// Defines for BLE connection ready signal
#define GPIO_PORT_BLE_READY         GPIO_PORT_P5
#define GPIO_PIN_BLE_READY          GPIO_PIN7


// New for WULPUS PRO (Apr-May 2025)

// Preamp control
#define GPIO_PORT_PREAMP_PWR_EN     GPIO_PORT_P1
#define GPIO_PIN_PREAMP_PWR_EN      GPIO_PIN5

#define GPIO_PORT_PREAMP_EN         GPIO_PORT_P1
#define GPIO_PIN_PREAMP_EN          GPIO_PIN3

// VGA control
#define GPIO_PORT_VGA_PWR_EN        GPIO_PORT_P1
#define GPIO_PIN_VGA_PWR_EN         GPIO_PIN2

#define GPIO_PORT_VGA_RC_EN         GPIO_PORT_P1
#define GPIO_PIN_VGA_RC_EN          GPIO_PIN0

#define GPIO_PORT_VGA_RC_SINK       GPIO_PORT_P2
#define GPIO_PIN_VGA_RC_SINK        GPIO_PIN2

// Envelope Detector
#define GPIO_PORT_ENV_DET_EN        GPIO_PORT_P1
#define GPIO_PIN_ENV_DET_EN         GPIO_PIN4

// Low Pass filter
#define GPIO_PORT_LP_FILT_EN        GPIO_PORT_P3
#define GPIO_PIN_LP_FILT_EN         GPIO_PIN1

// HV DC DC
#define GPIO_PORT_HV_DCDC_EN        GPIO_PORT_P3
#define GPIO_PIN_HV_DCDC_EN         GPIO_PIN5

#define GPIO_PORT_HV_POS_EN         GPIO_PORT_P3
#define GPIO_PIN_HV_POS_EN          GPIO_PIN3

#define GPIO_PORT_HV_NEG_EN         GPIO_PORT_P3
#define GPIO_PIN_HV_NEG_EN          GPIO_PIN4

// HV MUX Power Enable
#define GPIO_PORT_HV_MUX_PWR_EN     GPIO_PORT_P6
#define GPIO_PIN_HV_MUX_PWR_EN      GPIO_PIN5

#define GPIO_PORT_PULSER_HIZ_EN_N   GPIO_PORT_P6
#define GPIO_PIN_PULSER_HIZ_EN_N    GPIO_PIN2

// VGA Gain control
#define GPIO_PORT_VGA_GAIN_RC_EN    GPIO_PORT_P1
#define GPIO_PIN_VGA_GAIN_RC_EN     GPIO_PIN0

#define GPIO_PORT_VGA_GAIN_CS_N     GPIO_PORT_P1
#define GPIO_PIN_VGA_GAIN_CS_N      GPIO_PIN1

#define GPIO_PORT_VGA_GAIN_RC_SINK  GPIO_PORT_P2
#define GPIO_PIN_VGA_GAIN_RC_SINK   GPIO_PIN2






// MACROS to help with memory access

#define READ_uint8(address)   (((uint8_t *)address)[0])

#define READ_uint16(address)  (((uint8_t *)address)[0] | (((uint16_t)((uint8_t *)address)[1])<<8))

#define READ_uint32(address)  (((uint8_t *)address)[0]|\
                              (((uint32_t)((uint8_t *)address)[1])<<8)|\
                              (((uint32_t)((uint8_t *)address)[2])<<16)|\
                              (((uint32_t)((uint8_t *)address)[3])<<24))


// Commands for indicating the configuration package or restart command
#define START_BYTE_CONF_PACK    (0xFA)
#define START_BYTE_RESTART      (0xFB)

void getDefaultUsConfig(msp_config_t * msp_config);

// Extract Uss config from the spi RX buffer
// Return 1 if config is valid
bool extractUsConfig(uint8_t * spi_rx, msp_config_t * msp_config);

//// Extra functions ////

// Check the first byte and check if restart should be performed
bool isRestartCondition(uint8_t * spi_rx);

// Initiate MSP430-controlled power switches
void initAllPowerSwitches(void);
// Init other GPIOs
void initOtherGpios(void);

// Function to check if BLE host is ready
bool isBleReady(void);


// Enable Rx Operational Amplifier power supply
void enableOpAmpSupply(void);
// Disable Rx Operational Amplifier power supply
void disableOpAmpSupply(void);

// Enable Rx Operational Amplifier
void enableOpAmp(void);
// Disable Rx Operational Amplifier
void disableOpAmp(void);

// Enable HV PCB power supply
void enableHvPcbSupply(void);
// Disable HV PCB power supply
void disableHvPcbSupply(void);

// Enable DC-DC converters on HV PCB
void enableHvPcbDcDc(void);
// Disable DC-DC converters on HV PCB
void disableHvPcbDcDc(void);
// Disable only HV DC-DC converter on HV PCB
void disableHvDcDc(void);

// New functions for WULPUS PRO

void enableLowPassFilter(void);
void disableLowPassFilter(void);

void enableEnvDet(void);
void disableEnvDet(void);

void enableVgaPwr(void);
void disableVgaPwr(void);

void enablePreampPwr(void);
void disablePreampPwr(void);

void enablePreamp(void);
void disablePreamp(void);

void enableHvDcDcPwr(void);
void disableHvDcDcPwr(void);

void enableHvNeg(void);
void disableHvNeg(void);

void enableHvPos(void);
void disableHvPos(void);

void enableHvMuxPwr(void);
void disableHvMuxPwr(void);

void enableHvPulser(void);
void disableHvPulser(void);

// Used before acquisitions and after acquisitions are completed
void enableAll(void);
void disableAll(void);

#endif /* WULPUS_SYS_H_ */
