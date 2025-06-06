"""
Copyright (C) 2023 ETH Zurich. All rights reserved.
Author: Sergei Vostrikov, ETH Zurich
        Cedric Hirschi, ETH Zurich
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
from wulpus.config_package import (
    configuration_package,
    us_to_ticks,
    PGA_GAIN_REG,
    PGA_GAIN,
    USS_CAPTURE_ACQ_RATES,
    USS_CAPT_OVER_SAMPLE_RATES_REG,
)

# CONSTANTS

# Protocol related
START_BYTE_CONF_PACK = 250
START_BYTE_RESTART = 251
# Maximum length of the configuration package
PACKAGE_LEN = 68


class WulpusUssConfig:
    """
    Represents the configuration of the WULPUS ultrasound subsystem.

    Attributes:
        num_acqs (int): Number of acquisitions to perform.
        dcdc_turnon (int): DC-DC turn on time in microseconds.
        meas_period (int): Measurement period in microseconds.
        trans_freq (int): Transducer frequency in Hertz.
        pulse_freq (int): Pulse frequency in Hertz.
        num_pulses (int): Number of pulses to excite the transducer.
        sampling_freq (int): Sampling frequency in Hertz.
        oversampling_rate (int): Oversampling rate.
        num_samples (int): Number of samples to acquire.
        rx_gain (int): RX gain in dB. (must be one of PGA_GAIN)
        num_txrx_configs (int): Number of TX/RX configurations.
        tx_configs (int[]): TX configurations. (Generated by WulpusRxTxConfigGen)
        rx_configs (int[]): RX configurations. (Generated by WulpusRxTxConfigGen)
        start_hvmuxrx (int): HV-MUX RX start time in microseconds.
        start_ppg (int): PPG start time in microseconds.
        turnon_adc (int): ADC turn on time in microseconds.
        start_pgainbias (int): PGA in bias start time in microseconds.
        start_adcsampl (int): ADC sampling start time in microseconds.
        restart_capt (int): Capture restart time in microseconds.
        capt_timeout (int): Capture timeout time in microseconds.
    """

    def __init__(
        self,
        num_acqs=100,
        dcdc_turnon=195300,
        meas_period=321965,
        trans_freq=225e4,
        pulse_freq=225e4,
        num_pulses=2,
        sampling_freq=USS_CAPTURE_ACQ_RATES[0],
        num_samples=400,
        rx_gain=PGA_GAIN[-10],
        num_txrx_configs=1,
        tx_configs=[0],
        rx_configs=[0],
        start_hvmuxrx=500,
        start_ppg=500,
        turnon_adc=5,
        start_pgainbias=5,
        start_adcsampl=503,
        restart_capt=3000,
        capt_timeout=3000,
    ):
        # check if sampling frequency is valid
        if sampling_freq not in USS_CAPTURE_ACQ_RATES:
            raise ValueError(
                "Sampling frequency of "
                + str(sampling_freq)
                + " is not allowed.\nAllowed values are: "
                + str(USS_CAPTURE_ACQ_RATES)
            )
        # check if rx gain is valid
        if rx_gain not in PGA_GAIN:
            raise ValueError(
                "RX gain of "
                + str(rx_gain)
                + " is not allowed.\nAllowed values are: "
                + str(PGA_GAIN)
            )

        # Parse basic settings
        self.num_acqs = int(num_acqs)
        self.dcdc_turnon = int(dcdc_turnon)
        self.meas_period = int(meas_period)
        self.trans_freq = int(trans_freq)
        self.pulse_freq = int(pulse_freq)
        self.num_pulses = int(num_pulses)
        self.sampling_freq = int(sampling_freq)
        self.num_samples = int(num_samples)
        self.rx_gain = float(rx_gain)
        self.num_txrx_configs = int(num_txrx_configs)
        self.tx_configs = np.array(tx_configs).astype("<u2")
        self.rx_configs = np.array(rx_configs).astype("<u2")

        # Parse advanced settings
        self.start_hvmuxrx = int(start_hvmuxrx)
        self.start_ppg = int(start_ppg)
        self.turnon_adc = int(turnon_adc)
        self.start_pgainbias = int(start_pgainbias)
        self.start_adcsampl = int(start_adcsampl)
        self.restart_capt = int(restart_capt)
        self.capt_timeout = int(capt_timeout)

        # check if configuration is valid
        self.convert_to_registers()  # convert to register saveable values
        _ = self.get_conf_package()  # use this to check if the configuration is valid

    def convert_to_registers(self):
        # convert to register saveable values

        self.dcdc_turnon_reg = int(self.dcdc_turnon * us_to_ticks["dcdc_turnon"])
        self.meas_period_reg = int(self.meas_period * us_to_ticks["meas_period"])
        self.trans_freq_reg = int(self.trans_freq)
        self.pulse_freq_reg = int(self.pulse_freq)
        self.num_pulses_reg = int(self.num_pulses)
        self.sampling_freq_reg = int(
            USS_CAPT_OVER_SAMPLE_RATES_REG[
                USS_CAPTURE_ACQ_RATES.index(self.sampling_freq)
            ]
        )  # converted to oversampling rate, thus name is misleading
        self.num_samples_reg = int(self.num_samples * 2)
        self.rx_gain_reg = int(PGA_GAIN_REG[PGA_GAIN.index(self.rx_gain)])
        self.num_txrx_configs_reg = int(self.num_txrx_configs)
        self.start_hvmuxrx_reg = int(self.start_hvmuxrx * us_to_ticks["start_hvmuxrx"])
        self.start_ppg_reg = int(self.start_ppg * us_to_ticks["start_ppg"])
        self.turnon_adc_reg = int(self.turnon_adc * us_to_ticks["turnon_adc"])
        self.start_pgainbias_reg = int(
            self.start_pgainbias * us_to_ticks["start_pgainbias"]
        )
        self.start_adcsampl_reg = int(
            self.start_adcsampl * us_to_ticks["start_adcsampl"]
        )
        self.restart_capt_reg = int(self.restart_capt * us_to_ticks["restart_capt"])
        self.capt_timeout_reg = int(self.capt_timeout * us_to_ticks["capt_timeout"])

    def get_conf_package(self):
        # Start byte fixed
        bytes_arr = np.array([START_BYTE_CONF_PACK]).astype("<u1").tobytes()

        # Make sure the values are converted to register saveable values
        self.convert_to_registers()

        # Write basic settings
        for param in configuration_package[0]:
            value = getattr(self, param.config_name + "_reg")
            bytes_arr += param.get_as_bytes(value)

        # Write TX and RX configurations
        for i in range(self.num_txrx_configs):
            bytes_arr += self.tx_configs[i].astype("<u2").tobytes()
            bytes_arr += self.rx_configs[i].astype("<u2").tobytes()

        # Write advanced settings
        for param in configuration_package[1]:
            value = getattr(self, param.config_name + "_reg")
            bytes_arr += param.get_as_bytes(value)

        # Add zeros to match the expected package legth if needed
        if len(bytes_arr) < PACKAGE_LEN:
            bytes_arr += np.zeros(PACKAGE_LEN - len(bytes_arr)).astype("<u1").tobytes()

        # Debug print the package
        # for byte in bytes_arr:
        #     # print hex value
        #     print(f'{byte:02x}', end=' ')
        # print()

        return bytes_arr

    def get_restart_package(self):
        # Start byte fixed
        bytes_arr = np.array([START_BYTE_RESTART]).astype("<u1").tobytes()

        # Add zeros to match the expected package legth if needed
        if len(bytes_arr) < PACKAGE_LEN:
            bytes_arr += np.zeros(PACKAGE_LEN - len(bytes_arr)).astype("<u1").tobytes()

        return bytes_arr
