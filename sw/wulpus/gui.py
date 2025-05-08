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

from scipy import signal as ss
from scipy.signal import hilbert
import ipywidgets as widgets
import matplotlib.pyplot as plt
import numpy as np
import time
from threading import Thread
import os.path
import logging

from wulpus.dongle import WulpusDongle

# plt.ioff()

V_TISSUE = 1540  # m/s

LOWER_BOUNDS_MM = 7  # data below this depth will be discarded

LINE_N_SAMPLES = 400

FILE_NAME_BASE = "data_"

box_layout = widgets.Layout(
    display="flex", flex_flow="column", align_items="center", width="50%"
)

# Grab the logger you use in this file (e.g. “WiFi” in your __init__)
gui_logger = logging.getLogger("GUI ")
gui_logger.setLevel(logging.DEBUG)
# Prevent messages from bubbling up to the root logger
gui_logger.propagate = False

# Create and attach a FileHandler just for this logger
file_handler = logging.FileHandler("wulpus.log")
file_handler.setLevel(logging.DEBUG)
formatter = logging.Formatter(
    "%(asctime)s\t%(name)s\t%(funcName)s\t%(levelname)s\t%(message)s",
    "%Y-%m-%d %H:%M:%S",
)
file_handler.setFormatter(formatter)
gui_logger.addHandler(file_handler)


class WulpusGuiSingleCh(widgets.VBox):
    def __init__(self, com_link: WulpusDongle, uss_conf, max_vis_fps=20):
        super().__init__()

        self.log = gui_logger

        # Communication link
        self.com_link = com_link
        self.log.debug(f"Communication link: {self.com_link}")

        # Ultrasound Subsystem Configurator
        self.uss_conf = uss_conf

        # Allocate memory to store the data and other parameters
        self.data_arr = np.zeros(
            (self.com_link.acq_length, uss_conf.num_acqs), dtype="<i2"
        )
        self.data_arr_bmode = np.zeros((8, self.com_link.acq_length), dtype="<i2")
        self.acq_num_arr = np.zeros(uss_conf.num_acqs, dtype="<u2")
        self.tx_rx_id_arr = np.zeros(uss_conf.num_acqs, dtype=np.uint8)

        # For visualization FPS control
        self.vis_fps_period = 1 / max_vis_fps

        # Extra variables to control visualization
        self.rx_tx_conf_to_display = 0

        # For Signal Processing
        self.f_low_cutoff = self.uss_conf.sampling_freq / 2 * 0.1
        self.f_high_cutoff = self.uss_conf.sampling_freq / 2 * 0.9
        self.design_filter(
            self.uss_conf.sampling_freq, self.f_low_cutoff, self.f_high_cutoff
        )

        # Define widgets
        # Serial port related
        self.ser_scan_button = widgets.Button(
            description="Scan ports",
            disabled=False,
            style={"description_width": "initial"},
        )

        self.ser_open_button = widgets.Button(description="Open port", disabled=True)

        self.port_opened = False
        self.acquisition_running = False

        devices = self.com_link.get_available()

        if len(devices) == 0:
            self.ports_dd = widgets.Dropdown(
                options=["No ports found"],
                value="No ports found",
                description="Serial port:",
                disabled=True,
                style={"description_width": "initial"},
            )
        else:
            self.ports_dd = widgets.Dropdown(
                options=[device.description for device in devices],
                value=devices[0].description,
                description="Serial port:",
                disabled=True,
                style={"description_width": "initial"},
            )
            self.click_scan_ports(self.ser_scan_button)

        # Visualization-related
        self.raw_data_check = widgets.Checkbox(
            value=True, description="Show Raw Data", disabled=True
        )

        self.filt_data_check = widgets.Checkbox(
            value=False, description="Show Filtered Data", disabled=True
        )

        self.env_data_check = widgets.Checkbox(
            value=False, description="Show Envelope", disabled=True
        )

        self.bmode_check = widgets.Checkbox(
            value=False, description="Show B-Mode", disabled=True
        )

        opt = [str(x) for x in range(self.uss_conf.num_txrx_configs)]
        self.tx_rx_sel_dd = widgets.Dropdown(
            options=opt,
            value=opt[0],
            description="Active RX config:",
            disabled=True,
            style={"description_width": "initial"},
        )

        self.band_pass_frs = widgets.FloatRangeSlider(
            value=[self.f_low_cutoff / 10**6, self.f_high_cutoff / 10**6],
            min=self.f_low_cutoff / 10**6,
            max=self.f_high_cutoff / 10**6,
            step=0.1,
            description="Band pass (MHz):",
            disabled=True,
            continuous_update=False,
            orientation="horizontal",
            readout=True,
            readout_format=".1f",
            style={"description_width": "initial"},
        )

        # Progress bar, Start and Stop button, save data checkbox and label
        self.frame_progr_bar = widgets.IntProgress(
            value=0,
            min=0,
            max=self.uss_conf.num_acqs,
            step=1,
            description="Progress:",
            bar_style="success",  # 'success', 'info', 'warning', 'danger' or ''
            orientation="horizontal",
            style={"description_width": "initial"},
        )

        self.start_stop_button = widgets.Button(
            description="Start measurement", disabled=True
        )

        self.save_data_check = widgets.Checkbox(
            value=True, description="Save Data as .npz", disabled=True
        )

        self.save_data_label = widgets.Label(value="")

        # Setup Visualization
        self.output = widgets.Output()
        self.one_time_fig_config()

        # Construct GUI grid
        controls_1 = widgets.VBox(
            [
                self.raw_data_check,
                self.filt_data_check,
                self.env_data_check,
                self.bmode_check,
            ]
        )

        controls_2 = widgets.VBox(
            [
                widgets.HBox([self.ser_open_button, self.ser_scan_button]),
                self.ports_dd,
                self.tx_rx_sel_dd,
                self.band_pass_frs,
            ]
        )

        controls = widgets.HBox([controls_1, controls_2])

        out_box = widgets.Box([self.output])

        progr_ctl_box_1 = widgets.VBox([self.start_stop_button, self.frame_progr_bar])
        progr_ctl_box_2 = widgets.VBox([self.save_data_check, self.save_data_label])
        progr_ctl_box = widgets.HBox([progr_ctl_box_1, progr_ctl_box_2])

        main_box = widgets.VBox([controls, out_box, progr_ctl_box])

        # Connect callbacks

        # To serial port related buttons
        self.ser_scan_button.on_click(self.click_scan_ports)
        self.ser_open_button.on_click(self.click_open_port)

        # To checkboxes
        self.raw_data_check.observe(self.turn_on_off_raw_data_plot, "value")
        self.filt_data_check.observe(self.turn_on_off_filt_data_plot, "value")
        self.env_data_check.observe(self.turn_on_off_env_plot, "value")
        self.bmode_check.observe(self.toggle_bmode, "value")

        # To TX RX select dropdown
        self.tx_rx_sel_dd.observe(self.select_rx_conf_to_plot, "value")

        # To progress slider
        self.band_pass_frs.observe(self.update_band_pass_range, "value")

        # To start stop acqusition button
        self.start_stop_button.on_click(self.click_start_stop_acq)

        # add to children
        self.children = [main_box]

        self.log.debug("WulpusGUISingleCh initialized")

    def one_time_fig_config(self):
        with self.output:
            self.fig, self.ax = plt.subplots(
                constrained_layout=True, figsize=(8, 4), ncols=1, nrows=1
            )

        if self.bmode_check.value:
            self.setup_bmode_plot()
        else:
            self.setup_amode_plot()

        self.fig.canvas.toolbar_position = "bottom"

    def setup_amode_plot(self):
        self.ax.clear()

        (self.raw_data_line,) = self.ax.plot(
            np.zeros(LINE_N_SAMPLES),
            color="blue",
            marker="o",
            markersize=1,
            label="Raw data",
        )

        (self.filt_data_line,) = self.ax.plot(
            np.zeros(LINE_N_SAMPLES),
            color="green",
            marker="o",
            markersize=1,
            label="Filtered data",
        )

        (self.envelope_line,) = self.ax.plot(
            np.zeros(LINE_N_SAMPLES),
            color="red",
            marker="o",
            markersize=1,
            label="Envelope",
        )

        self.ax.legend(loc="upper right")

        self.ax.set_xlabel("Samples")
        self.ax.set_ylabel("ADC digital code")
        self.ax.set_title("A-mode data")

        self.filt_data_line.set_visible(self.filt_data_check.value)
        self.envelope_line.set_visible(self.env_data_check.value)

        self.ax.set_ylim(-3000, 3000)
        self.ax.grid(True)

    def setup_bmode_plot(self):
        self.ax.clear()

        self.bmode_image = self.ax.imshow(np.zeros((8, LINE_N_SAMPLES)), aspect="auto")

        self.ax.set_xlabel("Depth (mm)")
        self.ax.set_ylabel("Channel number")
        self.ax.set_title("B-mode data")
        # self.bmode_image.set_clim(0, 2)
        self.bmode_image.set_clim(0, 200)

        meas_time = LINE_N_SAMPLES / self.uss_conf.sampling_freq
        meas_depth = meas_time * V_TISSUE * 1000 / 2
        # self.bmode_image.set_extent((LOWER_BOUNDS_MM, meas_depth, 0.5, 7.5))
        self.bmode_image.set_extent((0, meas_depth, 0.5, 7.5))

    # Callbacks

    def click_scan_ports(self, b):
        self.log.info("Scanning ports...")

        # Update drop-down for ports and make it enabled
        self.found_devices = self.com_link.get_available()
        self.log.info(f"Found {len(self.found_devices)} devices")
        for device in self.found_devices:
            self.log.debug(f"Device: {device.description}")

        if len(self.found_devices) == 0:
            self.log.warning("No devices found")
            self.ports_dd.options = ["No ports found"]
            self.ports_dd.value = "No ports found"
            self.ports_dd.disabled = True
            self.ser_open_button.disabled = True
        else:
            self.ports_dd.options = [
                device.description for device in self.found_devices
            ]
            self.ports_dd.value = self.found_devices[0].description
            self.ports_dd.disabled = False
            self.ser_open_button.disabled = False

        self.log.debug("Done scanning ports")

    def click_open_port(self, b):
        self.log.info("Opening/Closing port...")

        if not self.port_opened and len(self.ports_dd.options) > 0:
            self.log.info("Opening port")

            device = self.found_devices[self.ports_dd.index]

            if not self.com_link.open(device):
                b.description = "Open port"
                self.port_opened = False
                self.start_stop_button.disabled = True
                self.log.error("Port not opened")
                return

            b.description = "Close port"
            self.port_opened = True
            self.start_stop_button.disabled = False

            self.log.debug("Port opened")

        else:
            self.log.info("Closing port")

            self.com_link.close()
            b.description = "Open port"
            self.port_opened = False
            self.start_stop_button.disabled = True

            self.log.debug("Port closed")

    def turn_on_off_raw_data_plot(self, change):
        self.raw_data_line.set_visible(change.new)

    def turn_on_off_filt_data_plot(self, change):
        self.filt_data_line.set_visible(change.new)

    def turn_on_off_env_plot(self, change):
        self.envelope_line.set_visible(change.new)

    def toggle_bmode(self, change):
        if change.new:
            self.setup_bmode_plot()
            self.tx_rx_sel_dd.disabled = True
        else:
            self.setup_amode_plot()
            self.tx_rx_sel_dd.disabled = False

        self.fig.canvas.draw()
        self.fig.canvas.flush_events()

    def select_rx_conf_to_plot(self, change):
        self.rx_tx_conf_to_display = int(change.new)

    def update_band_pass_range(self, change):
        self.design_filter(
            self.uss_conf.sampling_freq, change.new[0] * 10**6, change.new[1] * 10**6
        )

    def click_start_stop_acq(self, b):
        self.log.info("Start/Stop acquisition")

        if not self.acquisition_running:
            self.log.info("Starting acquisition")
            # Enable the widgets active during acquisition
            self.raw_data_check.disabled = False
            self.filt_data_check.disabled = False
            self.env_data_check.disabled = False
            self.bmode_check.disabled = False
            self.tx_rx_sel_dd.disabled = False
            self.band_pass_frs.disabled = False
            self.save_data_check.disabled = False

            # Disable serial port related widgets
            self.ser_open_button.disabled = True

            # Clean Save data label
            self.save_data_label.value = ""

            # Change state of the button
            b.description = "Stop measurement"

            # Declare that acquisition is running
            self.acquisition_running = True

            # Run data acquisition loop
            self.log.info("Starting acquisition thread")
            self.current_data = None
            self.acquisition_thread = Thread(target=self.run_acquisition_loop)
            self.acquisition_thread.start()

            self.log.debug("Acquisition started")
        else:
            self.log.info("Stopping acquisition")

            # Stop acquisition, thread will stop by itself
            self.acquisition_running = False

            # Change state of the button
            b.description = "Start measurement"

            # Disable the widgets when not acquiring
            self.raw_data_check.disabled = True
            self.filt_data_check.disabled = True
            self.env_data_check.disabled = True
            self.bmode_check.disabled = True
            self.tx_rx_sel_dd.disabled = True
            self.band_pass_frs.disabled = True
            self.save_data_check.disabled = True

            # Enable serial port related widgets again
            self.ser_open_button.disabled = False

            self.log.debug("Acquisition stopped")

    def run_acquisition_loop(self):
        #         self.fig.show()
        self.log.info("Acquisition thread started")

        # Clean data buffer
        acq_length = self.com_link.acq_length
        number_of_acq = self.uss_conf.num_acqs
        self.data_arr = np.zeros((acq_length, number_of_acq), dtype="<i2")
        self.acq_num_arr = np.zeros(number_of_acq, dtype="<u2")
        self.tx_rx_id_arr = np.zeros(number_of_acq, dtype=np.uint8)
        # Acquisition counter
        self.data_cnt = 0
        self.log.debug("Data buffer cleaned")

        # Send TX stop
        self.log.info("Sending RX stop command")
        self.com_link.toggle_rx(False)
        self.log.debug("RX stop command sent")

        # Send a restart command (if system is already running)
        self.log.info("Sending restart command")
        self.com_link.send_config(self.uss_conf.get_restart_package())
        self.log.debug("Restart command sent")

        # Wait 2.5 seconds (much larger than max measurement period = 2s)
        self.log.debug("Waiting for 2.5 seconds")
        time.sleep(2.5)

        # Generate and send a configuration package
        try:
            self.log.info("Sending configuration package")
            self.com_link.send_config(self.uss_conf.get_conf_package())
            self.log.debug("Configuration package sent")
        except ValueError as e:
            self.log.error(f"Error sending configuration package: {e}")
            self.save_data_label.value = str(e)
            self.acquisition_running = False
            if self.ser_open_button.disabled:
                self.click_start_stop_acq(self.start_stop_button)
            return

        self.log.info("Starting visualization thread")
        self.visualize = True
        self.current_data = None
        self.current_amode_data = None
        t2 = Thread(target=self.visualization, args=(number_of_acq,))
        t2.start()

        # Send RX start command
        self.log.info("Sending RX start command")
        self.com_link.toggle_rx(True)
        self.log.debug("RX start command sent")

        # Readout data in a loop
        self.log.info("Starting data acquisition loop")
        while self.data_cnt < number_of_acq and self.acquisition_running:
            # Receive the data
            rf_arr, acq_nr, tx_rx_id = self.com_link.receive_data()
            # self.save_data_label.value = (
            #     f"{np.array(rf_arr).shape}, {acq_nr}, {tx_rx_id}"
            # )
            self.log.debug(
                f"Received data: {np.array(rf_arr).shape}, {acq_nr}, {tx_rx_id}"
            )

            # For now, we just ignore invalid data
            if (
                rf_arr is not None
                and (acq_nr >= 0 and acq_nr < number_of_acq)
                and (tx_rx_id >= 0 and tx_rx_id < self.uss_conf.num_txrx_configs)
            ):
                # self.log.debug("Data received")
                self.current_data = rf_arr

                if (
                    tx_rx_id == self.rx_tx_conf_to_display
                    and not self.bmode_check.value
                ):
                    self.current_amode_data = rf_arr

                # Save data
                self.data_arr[:, self.data_cnt] = rf_arr

                # and other params
                self.acq_num_arr[self.data_cnt] = acq_nr
                self.tx_rx_id_arr[self.data_cnt] = tx_rx_id

                # Save data to specific z
                self.data_arr_bmode[self.tx_rx_id_arr[self.data_cnt]] = (
                    self.get_envelope(self.filter_data(rf_arr))
                )

                self.data_cnt = self.data_cnt + 1

                # Update progress bar
                self.frame_progr_bar.description = (
                    "Progress: " + str(self.data_cnt) + "/" + str(number_of_acq)
                )
                self.frame_progr_bar.value = self.data_cnt

                self.log.debug(f"Progress: {self.data_cnt}/{number_of_acq}")
            else:
                self.log.warning("No data received")

        self.log.info(
            f"Acquisition loop finished. data_cnt = {self.data_cnt}, acq_running = {self.acquisition_running}"
        )

        self.log.info("Stopping RX")
        self.com_link.toggle_rx(False)
        self.log.debug("RX stopped")

        self.log.debug("Stopping visualization thread")
        self.visualize = False
        t2.join()

        self.log.info("Sending restart command")
        try:
            self.com_link.send_config(self.uss_conf.get_restart_package())
        except ValueError as e:
            self.log.error(f"Error sending restart command: {e}")
            self.save_data_label.value = str(e)
        self.log.debug("Restart command sent")

        # Save data to file if needed
        if self.save_data_check.value:
            self.save_data_to_file()

        # Stop acquisition
        if self.ser_open_button.disabled:
            self.click_start_stop_acq(self.start_stop_button)

        # self.click_open_port(self.ser_open_button) # if you want to close the port after acquisition

    def visualization(self, number_of_acq):
        self.log.info("Visualization thread started")

        self.frame_progr_bar.max = number_of_acq

        while self.visualize:
            # Update the visualization

            begin_time = time.time()

            # B-mode
            if self.bmode_check.value:
                if self.current_data is None:
                    continue
                try:
                    # self.bmode_image.set_data(np.log10(np.add(self.data_arr_bmode, 0.1)))                                # log scale
                    # self.bmode_image.set_data(self.data_arr_bmode[:,10*LOWER_BOUNDS_MM:])                                # linear scale
                    self.bmode_image.set_data(
                        self.data_arr_bmode
                    )  # linear scale, all data
                except Exception as _:
                    # B-mode graph is not initialized yet
                    pass

            # Check the id of RX TX config
            else:
                if self.current_amode_data is None:
                    continue
                filt_data = None

                # Raw RF data
                if self.raw_data_check.value:
                    self.raw_data_line.set_ydata(self.current_amode_data)

                # Filtered data
                if self.filt_data_check.value:
                    filt_data = self.filter_data(self.current_amode_data)
                    self.filt_data_line.set_ydata(filt_data)

                # Envelope
                if self.env_data_check.value:
                    if filt_data is None:
                        filt_data = self.filter_data(self.current_amode_data)
                    self.envelope_line.set_ydata(self.get_envelope(filt_data))

            self.fig.canvas.draw()
            # This will run the GUI event
            # loop until all UI events
            # currently waiting have been processed
            self.fig.canvas.flush_events()

            # send thread to sleep for max_vis_fps_period
            sleep_time = self.vis_fps_period - (time.time() - begin_time)
            if sleep_time > 0:
                time.sleep(sleep_time)

        self.log.info("Visualization thread finished")

    # Design bandpass filter
    def design_filter(
        self,
        f_sampling,
        f_low_cutoff,
        f_high_cutoff,
        trans_width=0.2 * 10**6,
        n_taps=31,
    ):
        temp = [
            0,
            f_low_cutoff - trans_width,
            f_low_cutoff,
            f_high_cutoff,
            f_high_cutoff + trans_width,
            f_sampling / 2,
        ]

        self.filt_b = ss.remez(n_taps, temp, [0, 1, 0], Hz=f_sampling, maxiter=2500)
        self.filt_a = 1

    def filter_data(self, data_in):
        return ss.filtfilt(self.filt_b, self.filt_a, data_in)

    def get_envelope(self, data_in):
        return np.abs(hilbert(data_in))

    def save_data_to_file(self):
        self.log.info("Saving data to file")

        # Check filename
        for i in range(100):
            filename = FILE_NAME_BASE + str(i) + ".npz"
            if not os.path.isfile(filename):
                break

        self.log.info(f"Saving data to {filename}")

        # Save numpy data array to file
        np.savez(
            filename[:-4],
            data_arr=self.data_arr,
            acq_num_arr=self.acq_num_arr,
            tx_rx_id_arr=self.tx_rx_id_arr,
        )

        self.save_data_label.value = "Data saved in " + filename

        self.log.info("Data saved")
