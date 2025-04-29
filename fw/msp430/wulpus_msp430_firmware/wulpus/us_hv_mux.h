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

#ifndef US_HV_MUX_H_
#define US_HV_MUX_H_

#include "driverlib.h"

// Delay in MCLK cycles (should be at least 12 ns according to HV2707 datasheet)
#define DELAY_CYCLES    1


#define HV_MUX_LE_PORT     GPIO_PORT_P7
#define HV_MUX_LE_PIN      GPIO_PIN0

#define HV_MUX_CLR_PORT    GPIO_PORT_P4
#define HV_MUX_CLR_PIN     GPIO_PIN3



void hvMuxInit(void);
void hvMuxConfTx(uint16_t tx_config);
void hvMuxConfRx(uint16_t rx_config);

void rxSpiSend(uint8_t byte);

// Latch outputs
// Transition from High to Low  transfers the contents of the
// shift registers into the latches and turn on switches
void hvMuxLatchOutput(void);

void hvMuxLatchHighToLow(void);
void hvMuxLatchLowToHigh(void);

// Instructs the driver to ignore the next latch event
void hvMuxIgnoreNxtLatchEvt(void);

#endif /* US_HV_MUX_H_ */
