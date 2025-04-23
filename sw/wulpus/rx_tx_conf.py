"""
   Copyright (C) 2023 ETH Zurich. All rights reserved.
   Author: Sergei Vostrikov, ETH Zurich
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   SPDX-License-Identifier: Apache-2.0
"""

import numpy as np

# TX RX Configs
TX_RX_MAX_NUM_OF_CONFIGS = 16
MAX_CH_ID = 7

# TX RX is configured by activating the 
# switches of HV multiplexer
# The arrays below maps transducer channels (0...7)
# to switches IDs (0..15) which we need to activate

RX_MAP = np.array([0, 2, 4, 6, 8, 10, 12, 14])
TX_MAP = np.array([1, 3, 5, 7, 9, 11, 13, 15])

class WulpusRxTxConfigGen():
    def __init__(self):
        self.rx_configs = np.zeros(TX_RX_MAX_NUM_OF_CONFIGS, dtype='<u2')
        self.tx_configs = np.zeros(TX_RX_MAX_NUM_OF_CONFIGS, dtype='<u2')
        self.tx_rx_len = 0
        
    def add_config(self, tx_channels, rx_channels, optimized_switching=False):
        """
        Add a new configuration to the list of configurations.
        
        Args:
            tx_channels: List of TX channel IDs (0...7)
            rx_channels: List of RX channel IDs (0...7)
            optimized_switching: Bool value to activate an algorithm for minimizing switching artifacts
        """
        if self.tx_rx_len >= TX_RX_MAX_NUM_OF_CONFIGS:
            raise ValueError('Maximum number of configs is ' + str(TX_RX_MAX_NUM_OF_CONFIGS))
            
        if (any(channel > MAX_CH_ID for channel in tx_channels) or any(channel > MAX_CH_ID for channel in rx_channels)):
            raise ValueError('RX and TX channel ID must be less than ' + str(MAX_CH_ID))
        
        if (any(channel < 0 for channel in tx_channels) or any(channel < 0 for channel in rx_channels)):
            raise ValueError('RX and TX channel ID must be positive.')
        
        if (len(tx_channels) == 0):
            self.tx_configs[self.tx_rx_len] = 0
        else:
            # Shift 1 left by the provided switch TX indices and then apply OR bitwise operation along the array
            self.tx_configs[self.tx_rx_len] = np.bitwise_or.reduce(np.left_shift(1, TX_MAP[tx_channels]))
        
        if (len(rx_channels) == 0):
            self.rx_configs[self.tx_rx_len] = 0
        else:
            # Shift 1 left by the provided switch RX indices and then apply OR bitwise operation along the array
            self.rx_configs[self.tx_rx_len] = np.bitwise_or.reduce(np.left_shift(1, RX_MAP[rx_channels]))


        if optimized_switching:
            # Optimize switching artifacts (less switching MUX activity after TX (pulsing) -> better SNR)
            # Find which channels are active both for TX and RX.
            rx_tx_intersect_ch = list(set(tx_channels) & set(rx_channels))
            # Find which channels are only active for RX but not in TX.
            rx_only_ch = list(set(rx_tx_intersect_ch) ^ set(rx_channels))
            # Find which channels are only active for TX but not in RX.
            tx_only_ch = list(set(rx_tx_intersect_ch) ^ set(tx_channels))

            # Compare the number of channels in the sets
            if len(rx_tx_intersect_ch) > len(rx_only_ch):
                # Activate the channels from the rx_tx_intersect_ch set for RX already before the TX event
                temp_switch_config = np.bitwise_or.reduce(np.left_shift(1, RX_MAP[rx_tx_intersect_ch]))
                self.tx_configs[self.tx_rx_len] = np.bitwise_or(self.tx_configs[self.tx_rx_len], temp_switch_config)
            elif len(rx_only_ch) > 0:
                    # Create a group of receive only channels and enable them for RX already before the TX event
                    temp_switch_config = np.bitwise_or.reduce(np.left_shift(1, RX_MAP[rx_only_ch]))
                    self.tx_configs[self.tx_rx_len] = np.bitwise_or(self.tx_configs[self.tx_rx_len], temp_switch_config)

            # # Leave TX only channels on after the HV MUX switches to RX
            # if len(tx_only_ch) > 0:
            #     self.rx_configs[self.tx_rx_len] = np.bitwise_or.reduce(np.left_shift(1, TX_MAP[tx_only_ch]))

        
        self.tx_rx_len += 1
        
    def get_tx_configs(self):
        
        return self.tx_configs[:self.tx_rx_len]

    def get_rx_configs(self):
        
        return self.rx_configs[:self.tx_rx_len]
        