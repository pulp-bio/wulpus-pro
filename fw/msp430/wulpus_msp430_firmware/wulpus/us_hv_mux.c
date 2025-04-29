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

#include "us_hv_mux.h"
#include "us_spi.h"

static uint8_t ignore_nxt_le_evt = 0;

void hvMuxInit(void)
{
    // HV MUX Initialization

    // Latch enable (SW_LE) signal will be manually controlled
    GPIO_setAsOutputPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);

    // Clear (CLR) signal will be manually controlled
    GPIO_setAsOutputPin(HV_MUX_CLR_PORT, HV_MUX_CLR_PIN);
    // Pull CLR Low (no action)
    GPIO_setOutputLowOnPin(HV_MUX_CLR_PORT, HV_MUX_CLR_PIN);


    // Configure SPI pins
    // Select Port 5
    // Set Pin 4 to output peripheral function, SPI CLK: SW_CLK
    // Set Pin 5 to output peripheral function, SPI MOSI (SIMO): SW_D_IN
    GPIO_setAsPeripheralModuleFunctionOutputPin(
        GPIO_PORT_P5,
        GPIO_PIN4 + GPIO_PIN5,
        GPIO_SECONDARY_MODULE_FUNCTION);

    // Configure MISO pin
    GPIO_setAsPeripheralModuleFunctionInputPin(
        GPIO_PORT_P5,
        GPIO_PIN6,
        GPIO_SECONDARY_MODULE_FUNCTION);


    // Initialize SPI master: MSB first, inactive high clock polarity and 4 wire SPI
    EUSCI_B_SPI_initMasterParam param = {0};
    param.selectClockSource = EUSCI_B_SPI_CLOCKSOURCE_SMCLK;
    param.clockSourceFrequency = 8000000;
    param.desiredSpiClock = 8000000;
    param.msbFirst = EUSCI_B_SPI_MSB_FIRST;
    param.clockPhase = EUSCI_B_SPI_PHASE_DATA_CAPTURED_ONFIRST_CHANGED_ON_NEXT;
    param.clockPolarity = EUSCI_B_SPI_CLOCKPOLARITY_INACTIVITY_LOW;
    param.spiMode = EUSCI_B_SPI_4PIN_UCxSTE_ACTIVE_LOW;
    EUSCI_B_SPI_initMaster(EUSCI_B1_BASE, &param);

    // Use software Slave Select (not used)
    EUSCI_B_SPI_select4PinFunctionality(EUSCI_B1_BASE, EUSCI_B_SPI_ENABLE_SIGNAL_FOR_4WIRE_SLAVE);

    // Enable SPI Module
    EUSCI_B_SPI_enable(EUSCI_B1_BASE);
}

void hvMuxConfTx(uint16_t tx_config)
{
    // Pull ~LE High
    GPIO_setOutputHighOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);

    __delay_cycles(DELAY_CYCLES);

    // Write first byte (MSB) (Channel 0...3)
    UCB1TXBUF = (uint8_t) (tx_config >> 8);
    // Wait until the byte is sent
    while(UCB1STAT & UCBBUSY);
    // Write second byte (LSB)(Channel 4...7)
    UCB1TXBUF = (uint8_t) (tx_config & 0xFF);
    // Wait until the byte is sent
    while(UCB1STAT & UCBBUSY);

    hvMuxLatchOutput();  // by pulling ~LE LOW for a while
}

void hvMuxConfRx(uint16_t rx_config)
{
    // Pull ~LE High
    GPIO_setOutputHighOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);

    __delay_cycles(DELAY_CYCLES);

    // Write first byte (MSB) (Channel 0...3)
    UCB1TXBUF = (uint8_t) (rx_config >> 8);
    // Wait until the byte is sent
    while(UCB1STAT & UCBBUSY);
    // Write second byte (LSB)(Channel 4...7)
    UCB1TXBUF = (uint8_t) (rx_config & 0xFF);
    // Wait until the byte is sent
    while(UCB1STAT & UCBBUSY);
}

// Latch outputs
// Transition from High to Low transfers the contents of the
// shift registers into the latches and turns on switches
void hvMuxLatchOutput(void)
{
    // Check the flag to ignore latch event
    if (!ignore_nxt_le_evt)
    {
        // Pull ~LE Low to latch the signal
        GPIO_setOutputLowOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);

        // Wait

        __delay_cycles(DELAY_CYCLES);

        // Pull ~LE High
        GPIO_setOutputHighOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);
    }
    else
    {
        // Clear the flag
        ignore_nxt_le_evt = 0;
    }

}

void hvMuxLatchHighToLow(void)
{
    // Check the flag to ignore latch event
    if (!ignore_nxt_le_evt)
    {
        // Pull ~LE Low to latch the signal
        GPIO_setOutputLowOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);
    }
    else
    {
        // Clear the flag
        ignore_nxt_le_evt = 0;
    }
}

void hvMuxLatchLowToHigh(void)
{
    // Pull ~LE High
    GPIO_setOutputHighOnPin(HV_MUX_LE_PORT, HV_MUX_LE_PIN);
}

void hvMuxIgnoreNxtLatchEvt(void)
{
    ignore_nxt_le_evt = 1;
}

void rxSpiSend(uint8_t byte)
{
    // Write a byte
    UCB1TXBUF = byte;
    // Wait until the byte is sent
    while(UCB1STAT & UCBBUSY);
}
