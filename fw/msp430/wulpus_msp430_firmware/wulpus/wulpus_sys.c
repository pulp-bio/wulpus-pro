/*
 * Copyright (C) 2025 ETH Zurich. All rights reserved.
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

#include "wulpus_sys.h"


void getDefaultUsConfig(msp_config_t * msp_config)
{

    msp_config->pllOutFreq = HSPLL_OUT_80_MHZ;
    msp_config->xtalFreq = HSPLL_XTAL_FREQ_8_MHZ;
    msp_config->xtalType = HSPLL_XTAL_CERAMIC_RESONATOR;
    // No buffered xtal clock output
    msp_config->outEnPllXtal = false;


    msp_config->biasImp = BIAS_IMP_2950_OHM;
    msp_config->chargePumpMode = CHARGE_PUMP_NORMAL_MODE;

    msp_config->uupsBiasDelay = UUPS_BIAS_NO_DELAY;

    // Six time marks events (default values)
    msp_config->startPpgCnt = 2500;
    msp_config->turnOnAdcCnt = 25;
    msp_config->startPgaInBiasCnt = 25;
    msp_config->startAdcSamplCnt = 2514;
    msp_config->restartCaptCnt = 937;
    msp_config->captTimeoutCnt = 3750;

    // Extra time events (SW-managed)
    msp_config->startHvMuxRxCnt = 4000;
    msp_config->dcDcTurnOnTime = 1000;


    // Acquisition settings
    msp_config->overSamplRate = SDHS_OVER_SAMPL_RATE_10;
    msp_config->sampleSize = 400;
    msp_config->rxGain = PGA_GAIN_9_0_DB;
    msp_config->measPeriod = 32768;

    // VGA settings
    msp_config->vgaRcPrechargeCycles = 0;
    msp_config->vgaRcGainSlopeWiperCode = 256;

    // TX/RX configurations
    msp_config->txRxConfLen = 0;
//    msp_config->txConfigs[TX_RX_CONF_LEN_MAX];
//    msp_config->rxConfigs[TX_RX_CONF_LEN_MAX];

    // Pulser settins
    msp_config->driveStrength = PPG_NORMAL_DRIVE;

    msp_config->transFreq = 2250000; // Reserved, not used
    msp_config->pulseFreq = 2250000;
    msp_config->pulsesDutyCycle = 50; // 50 %
    msp_config->numPulses = 2;
    msp_config->numStopPulses = 0;
    msp_config->pulserPolarity = PPG_POLARITY_START_WITH_HIGH;
    msp_config->pulserPauseState = PPG_PAUSE_STATE_LOW;

    return;
}

// Extract Uss config from spi RX buffer
// Return 1 if config is valid
bool extractUsConfig(uint8_t * spi_rx, msp_config_t * msp_config)
{
    // Check start byte
    if (spi_rx[0] != START_BYTE_CONF_PACK)
        return 0;

    // Note: The MSP430 cannot access 2-byte words at odd addresses, so the CPU just ignores the lowest bit of word addresses.
    //Therefore, we do here some magic

    msp_config->dcDcTurnOnTime = READ_uint16(spi_rx + 1);
    msp_config->measPeriod     = READ_uint16(spi_rx + 3);
    msp_config->transFreq      = READ_uint32(spi_rx + 5); // Reserved, not used
    msp_config->pulseFreq      = READ_uint32(spi_rx + 9);
    msp_config->numPulses      = READ_uint8(spi_rx + 13);
    msp_config->overSamplRate  = (sdhs_over_sampl_rate_t)READ_uint16(spi_rx + 14);
    msp_config->sampleSize     = READ_uint16(spi_rx + 16);
    msp_config->rxGain         = READ_uint8(spi_rx + 18);
    msp_config->txRxConfLen    = READ_uint8(spi_rx + 19);

    if (msp_config->txRxConfLen > TX_RX_CONF_LEN_MAX)
        return 0;

    // Copy the TX RX configs
    uint8_t i;
    for (i = 0; i < (msp_config->txRxConfLen); i++)
    {
        msp_config->txConfigs[i] = READ_uint16(spi_rx + 20 + 4*i);
        msp_config->rxConfigs[i] = READ_uint16(spi_rx + 22 + 4*i);
    }

    uint8_t offset = 20 + 4*(msp_config->txRxConfLen);

    // Copy the data from the Advanced settings section
    msp_config->startHvMuxRxCnt         = READ_uint16(spi_rx + offset);
    msp_config->startPpgCnt             = READ_uint16(spi_rx + offset + 2);
    msp_config->turnOnAdcCnt            = READ_uint16(spi_rx + offset + 4);
    msp_config->startPgaInBiasCnt       = READ_uint16(spi_rx + offset + 6);
    msp_config->startAdcSamplCnt        = READ_uint16(spi_rx + offset + 8);
    msp_config->restartCaptCnt          = READ_uint16(spi_rx + offset + 10);
    msp_config->captTimeoutCnt          = READ_uint16(spi_rx + offset + 12);
    msp_config->vgaRcPrechargeCycles    = READ_uint16(spi_rx + offset + 14);
    msp_config->vgaRcGainSlopeWiperCode = READ_uint16(spi_rx + offset + 16);

    return 1;
}

// Check the first byte and check if restart should be done.
bool isRestartCondition(uint8_t * spi_rx)
{
    // Check for restart byte
    if (spi_rx[0] != START_BYTE_RESTART)
    {
        return 0;
    }

    return 1;
}

// Initiate MSP430-controlled power switches
void initAllPowerSwitches(void)
{
    // Select Port 1
    // Set Pin 6 to output "RxPwr" (powers up input amplifier OPA836 using the load switch U6)
//    GPIO_setAsOutputPin(GPIO_PORT_P1, GPIO_PIN6);

    // Select Port 2
    // Set Pin 2 to output "EN HV" (power up HV PCB)
    // Power enable pin for DC-DC converters on HV PCB (powers TPS61222 and LT1945)
//    GPIO_setAsOutputPin(GPIO_PORT_P2, GPIO_PIN2);

    // Select Port 6
    // Set Pin 0 to output "RxEn" (powers up input amplifier OPA836)
    // Set Pin 4 to output "SW_EN" (Switch DC/DC: TPS61222)
    // Set Pin 5 to output "HV_EN" (HV DC/DC: LT1945)
//    GPIO_setAsOutputPin(GPIO_PORT_P6, GPIO_PIN0 + GPIO_PIN4 + GPIO_PIN5);


    // Low Pass filter
    GPIO_setAsOutputPin(GPIO_PORT_LP_FILT_EN, GPIO_PIN_LP_FILT_EN);
    GPIO_setOutputLowOnPin(GPIO_PORT_LP_FILT_EN, GPIO_PIN_LP_FILT_EN);

    // Envelope Detector Enable control (Disable by default)
    GPIO_setAsOutputPin(GPIO_PORT_ENV_DET_EN, GPIO_PIN_ENV_DET_EN);
    GPIO_setOutputLowOnPin(GPIO_PORT_ENV_DET_EN, GPIO_PIN_ENV_DET_EN);

    // VGA Power Enable
    GPIO_setAsOutputPin(GPIO_PORT_VGA_PWR_EN, GPIO_PIN_VGA_PWR_EN);
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_PWR_EN, GPIO_PIN_VGA_PWR_EN);

    // Preamp power switch (Off by default, but on dev board is always on in Hardware)
    GPIO_setAsOutputPin(GPIO_PORT_PREAMP_PWR_EN, GPIO_PIN_PREAMP_PWR_EN);
    GPIO_setOutputLowOnPin(GPIO_PORT_PREAMP_PWR_EN, GPIO_PIN_PREAMP_PWR_EN);

    // Preamp Shutdown (Enable shutdown by default)
    GPIO_setAsOutputPin(GPIO_PORT_PREAMP_EN, GPIO_PIN_PREAMP_EN);
    GPIO_setOutputHighOnPin(GPIO_PORT_PREAMP_EN, GPIO_PIN_PREAMP_EN);

    // Power switch for HV DC DC input
    GPIO_setAsOutputPin(GPIO_PORT_HV_DCDC_EN, GPIO_PIN_HV_DCDC_EN);
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_DCDC_EN, GPIO_PIN_HV_DCDC_EN);

    // Enable for + HV converter
    GPIO_setAsOutputPin(GPIO_PORT_HV_POS_EN, GPIO_PIN_HV_POS_EN);
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_POS_EN, GPIO_PIN_HV_POS_EN);

    // Enable for - HV converter
    GPIO_setAsOutputPin(GPIO_PORT_HV_NEG_EN, GPIO_PIN_HV_NEG_EN);
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_NEG_EN, GPIO_PIN_HV_NEG_EN);

    // Enable for +5V HV MUX supply
    GPIO_setAsOutputPin(GPIO_PORT_HV_MUX_PWR_EN, GPIO_PIN_HV_MUX_PWR_EN);
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_MUX_PWR_EN, GPIO_PIN_HV_MUX_PWR_EN);

    // HV pulser HiZ state enable
    GPIO_setAsOutputPin(GPIO_PORT_PULSER_HIZ_EN_N, GPIO_PIN_PULSER_HIZ_EN_N);
    GPIO_setOutputLowOnPin(GPIO_PORT_PULSER_HIZ_EN_N, GPIO_PIN_PULSER_HIZ_EN_N);

    // Digipot chip select is disabled by default
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);
    GPIO_setOutputHighOnPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);

    // Digipot RC Enable (disabled by default, set to 0)
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);

    // Digipot sink signal (output by default, set to 0)
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);



    return;
}

// Init other GPIOs
void initOtherGpios(void)
{
    // Set BLE ready pin as input
    GPIO_setAsInputPin(GPIO_PORT_BLE_READY, GPIO_PIN_BLE_READY);

    // Configure LED
    P1DIR |= GPIO_PIN_LED_MSP430;
    P1OUT &= ~GPIO_PIN_LED_MSP430;

    return;
}

bool isBleReady(void)
{
    return GPIO_getInputPinValue(GPIO_PORT_BLE_READY, GPIO_PIN_BLE_READY);
}

// Enable Rx Operational Amplifier power supply
void enableOpAmpSupply(void)
{
    // Enable Power for OPA836
    // Set Pin 6 "RxPwr" to high
//    GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN6);
}

// Disable Rx Operational Amplifier power supply
void disableOpAmpSupply(void)
{
    // Disable Power for OPA836
    // Set Pin 6 "RxPwr" to low
//    GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN6);
}

// Enable Rx Operational Amplifier
void enableOpAmp(void)
{
    // Enable RX OPA836
    // Set Pin 0 "RxEn" to high
//    GPIO_setOutputHighOnPin(GPIO_PORT_P6, GPIO_PIN0);

    return;
}

// Disable Rx Operational Amplifier
void disableOpAmp(void)
{
    // Disable RX OPA836
    // Set Pin 0 "RxEn" to low
//    GPIO_setOutputLowOnPin(GPIO_PORT_P6, GPIO_PIN0);

    return;
}

// Enable HV PCB power supply
void enableHvPcbSupply(void)
{
    // Power up HV PCB
    // Powering up this domain takes quite long, therefore it's powered on always and not
    // just for the US measurements
    // Set Pin 2 "EN HV" to high (power up HV PCB)
//    GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN2);

    return;
}

// Disable HV PCB power supply
void disableHvPcbSupply(void)
{
    // Power down HV PCB
    // Set Pin 2 "EN HV" to low (power down HV PCB)
//    GPIO_setOutputLowOnPin(GPIO_PORT_P2, GPIO_PIN2);
}

// Enable DC-DC converters on HV PCB
void enableHvPcbDcDc(void)
{
    // Enable HV and +5 V
    // Set Pin 4 "SW_EN" to high (enables the DC/DC TPS61222)
    // Set Pin 5 "HV1_EN" to high (enables the HV DC/DC LT1945)
//    GPIO_setOutputHighOnPin(GPIO_PORT_P6, GPIO_PIN4+GPIO_PIN5);

    return;
}

// Disable DC-DC converters on HV PCB
void disableHvPcbDcDc(void)
{
    // Disable HV and +5 V
    // Set Pin 4 "SW_EN" to low (disables the DC/DC TPS61222)
    // Set Pin 5 "HV1_EN" to low (disables the HV DC/DC LT1945)
//    GPIO_setOutputLowOnPin(GPIO_PORT_P6, GPIO_PIN4 + GPIO_PIN5);

    return;
}

// Disable only HV DC-DC converter on HV PCB
void disableHvDcDc(void)
{
    // Set Pin 5 "HV1_EN" to low (disables the HV DC/DC LT1945)
//    GPIO_setOutputLowOnPin(GPIO_PORT_P6, GPIO_PIN5);
}

void enableLowPassFilter(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_LP_FILT_EN, GPIO_PIN_LP_FILT_EN);
    return;
}

void disableLowPassFilter(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_LP_FILT_EN, GPIO_PIN_LP_FILT_EN);
    return;
}


void enableEnvDet(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_ENV_DET_EN, GPIO_PIN_ENV_DET_EN);
    return;
}

void disableEnvDet(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_ENV_DET_EN, GPIO_PIN_ENV_DET_EN);
    return;
}

void enableVgaPwr(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_VGA_PWR_EN, GPIO_PIN_VGA_PWR_EN);
    return;
}

void disableVgaPwr(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_PWR_EN, GPIO_PIN_VGA_PWR_EN);
    return;
}

void enablePreampPwr(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_PREAMP_PWR_EN, GPIO_PIN_PREAMP_PWR_EN);
    return;
}

void disablePreampPwr(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_PREAMP_PWR_EN, GPIO_PIN_PREAMP_PWR_EN);
    return;
}

void enablePreamp(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_PREAMP_EN, GPIO_PIN_PREAMP_EN);
    return;
}

void disablePreamp(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_PREAMP_EN, GPIO_PIN_PREAMP_EN);
    return;
}

void enableHvDcDcPwr(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_DCDC_EN, GPIO_PIN_HV_DCDC_EN);
    return;
}

void disableHvDcDcPwr(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_DCDC_EN, GPIO_PIN_HV_DCDC_EN);
    return;
}

void enableHvNeg(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_NEG_EN, GPIO_PIN_HV_NEG_EN);
    return;
}

void disableHvNeg(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_NEG_EN, GPIO_PIN_HV_NEG_EN);
    return;
}

void enableHvPos(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_POS_EN, GPIO_PIN_HV_POS_EN);
    return;
}

void disableHvPos(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_POS_EN, GPIO_PIN_HV_POS_EN);
    return;
}

void enableHvMuxPwr(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_HV_MUX_PWR_EN, GPIO_PIN_HV_MUX_PWR_EN);
    return;
}

void disableHvMuxPwr(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_HV_MUX_PWR_EN, GPIO_PIN_HV_MUX_PWR_EN);
    return;
}

void enableHvPulser(void)
{
    GPIO_setOutputLowOnPin(GPIO_PORT_PULSER_HIZ_EN_N, GPIO_PIN_PULSER_HIZ_EN_N);
    return;
}

void disableHvPulser(void)
{
    GPIO_setOutputHighOnPin(GPIO_PORT_PULSER_HIZ_EN_N, GPIO_PIN_PULSER_HIZ_EN_N);
    return;
}


void vgaDigipotSetWiperCode(uint8_t code)
{
    //Enable chip select
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);

    // Send code over RX SPI
    rxSpiSend(code);

    // Disable chip select
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);
    GPIO_setOutputHighOnPin(GPIO_PORT_VGA_GAIN_CS_N, GPIO_PIN_VGA_GAIN_CS_N);
}

void vgaDigipotRcEnable(void)
{
    // Put sink pin to high impedance
    GPIO_setAsInputPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);

    // Set RC Enable pin high
    GPIO_setOutputHighOnPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);
}


void vgaDigipotSinkEnable(void)
{
    // Set RC Enable pin to high impedance
    GPIO_setAsInputPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);

    // Put sink pin to output, set 0
    GPIO_setOutputLowOnPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);
    GPIO_setAsOutputPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);
}

void vgaDigipotFixGain(void)
{
    // Set RC gain enable pin to high impedance mode
    GPIO_setAsInputPin(GPIO_PORT_VGA_GAIN_RC_EN, GPIO_PIN_VGA_GAIN_RC_EN);
    GPIO_setAsInputPin(GPIO_PORT_VGA_GAIN_RC_SINK, GPIO_PIN_VGA_GAIN_RC_SINK);
}


void enableAll(void)
{
    // WULPUS PRO (17.04.25)
    // Enable Low Pass Filter
    enableLowPassFilter();

    // Enable VGA
    enableVgaPwr();

    // Enable Preamplifier
    enablePreamp();

    enableHvDcDcPwr();
    enableHvNeg();
    enableHvPos();

    return;
}


void disableAll(void)
{
    // Disable Low Pass Filter
    disableLowPassFilter();

    // Disable VGA
    disableVgaPwr();

    // Disable Preamplifier
    disablePreamp();

    disableHvDcDcPwr();
    disableHvNeg();
    disableHvPos();

    return;
}



