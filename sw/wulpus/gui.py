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

from __future__ import annotations

import time
from pathlib import Path
from threading import Thread
from typing import TYPE_CHECKING, Any

import ipywidgets as widgets
import numpy as np
from numpy.typing import NDArray
from scipy import signal as ss
from scipy.signal import hilbert

from imgui_bundle import imgui, immapp, implot

from .dongle import WulpusDongle

if TYPE_CHECKING:
    from .uss_conf_pro import WulpusProUssConfig

import nest_asyncio

nest_asyncio.apply()

# Constants
LINE_N_SAMPLES: int = 400
FILE_NAME_BASE: str = "data"
MAX_SAVE_FILES: int = 100
GAIN_PLOT_LOW_Y_MARGIN_DB: int = 20

# Default bandpass filter settings (relative to pulse frequency)
BANDPASS_LOW_RATIO: float = 0.55  # 55% of pulse_freq
BANDPASS_HIGH_RATIO: float = 1.45  # 145% of pulse_freq


class WulpusGuiSingleCh(widgets.VBox):
    """
    Single-channel GUI for WULPUS ultrasound data acquisition and visualization.

    This widget provides controls for:
    - Serial port connection management
    - Real-time A-mode data visualization using ImPlot
    - Bandpass filtering and envelope detection
    - Data acquisition and storage

    Attributes:
        com_link: Communication link to the WULPUS device.
        port_opened: Whether a serial port is currently open.
        acquisition_running: Whether data acquisition is in progress.
    """

    def __init__(
        self,
        com_link: WulpusDongle,
        uss_conf: WulpusProUssConfig,
        max_vis_fps: int = 60,
        dark_mode: bool = True,
    ) -> None:
        """
        Initialize the WULPUS GUI.

        Args:
            com_link: Communication link to the WULPUS device.
            uss_conf: Ultrasound subsystem configuration.
            max_vis_fps: Maximum visualization frames per second.
            dark_mode: Whether to use dark mode for the plot window.

        Raises:
            ValueError: If max_vis_fps is not positive.
        """
        super().__init__()

        if max_vis_fps <= 0:
            raise ValueError("max_vis_fps must be positive")

        # Store configuration
        self._com_link = com_link
        self._uss_conf = uss_conf
        self._vis_fps_period = 1.0 / max_vis_fps
        self._dark_mode = dark_mode

        # State variables
        self._port_opened = False
        self._acquisition_running = False
        self._visualize = False
        self._rx_tx_conf_to_display = 0
        self._bandpass_manually_changed = False
        self._data_cnt = 0

        # Thread references
        self._plot_thread: Thread | None = None
        self._acquisition_thread: Thread | None = None

        # Current data references
        self._current_data: tuple[NDArray[np.int16], int, int] | None = None
        self._current_amode_data: NDArray[np.int16] | None = None

        # Found devices cache
        self._found_devices: list[Any] = []

        # Initialize data arrays
        self._init_data_arrays()

        # Initialize signal processing
        self._init_signal_processing()

        # Initialize widgets
        self._init_widgets()

        # Build GUI layout
        self._build_layout()

        # Connect callbacks
        self._connect_callbacks()

        # Start initial port scan
        self._start_port_scan()

        # Open plot window
        self.open_plot_window()

    # region Properties

    @property
    def com_link(self) -> WulpusDongle:
        """Communication link to the WULPUS device."""
        return self._com_link

    @property
    def port_opened(self) -> bool:
        """Whether a serial port is currently open."""
        return self._port_opened

    @property
    def acquisition_running(self) -> bool:
        """Whether data acquisition is in progress."""
        return self._acquisition_running

    @property
    def data_arr(self) -> NDArray[np.int16]:
        """Acquired data array."""
        return self._data_arr

    @property
    def acq_num_arr(self) -> NDArray[np.uint16]:
        """Acquisition number array."""
        return self._acq_num_arr

    @property
    def tx_rx_id_arr(self) -> NDArray[np.uint8]:
        """TX/RX ID array."""
        return self._tx_rx_id_arr

    @property
    def save_location(self) -> str:
        """Get the save location prefix."""
        return self._save_location.value.strip() or "data"

    @save_location.setter
    def save_location(self, value: str) -> None:
        """Set the save location prefix."""
        self._save_location.value = value

    @property
    def save_file_base(self) -> str:
        """Get the save file base name."""
        return self._save_file_base.value.strip() or FILE_NAME_BASE

    @save_file_base.setter
    def save_file_base(self, value: str) -> None:
        """Set the save file base name."""
        self._save_file_base.value = value

    @property
    def do_save(self) -> bool:
        """Whether to save data after acquisition."""
        return self._save_data_check.value

    @do_save.setter
    def do_save(self, value: bool) -> None:
        """Set whether to save data after acquisition."""
        self._save_data_check.value = value

    # endregion

    # region Initialization

    def _init_data_arrays(self) -> None:
        """Initialize data storage arrays."""
        acq_length = self._com_link.acq_length
        num_acqs = self._uss_conf.num_acqs

        self._data_arr = np.zeros((acq_length, num_acqs), dtype=np.int16)
        self._acq_num_arr = np.zeros(num_acqs, dtype=np.uint16)
        self._tx_rx_id_arr = np.zeros(num_acqs, dtype=np.uint8)

        # Shared data for implot visualization
        self._implot_raw_data = np.zeros(LINE_N_SAMPLES, dtype=np.float64)
        self._implot_filt_data = np.zeros(LINE_N_SAMPLES, dtype=np.float64)
        self._implot_env_data = np.zeros(LINE_N_SAMPLES, dtype=np.float64)
        self._implot_x_data = np.arange(LINE_N_SAMPLES, dtype=np.float64)

        # Calculate and store gain curve
        self._uss_conf.calc_gain_curve()
        gain_curve = self._uss_conf.gain_curve_db
        self._implot_gain_data = np.zeros(LINE_N_SAMPLES, dtype=np.float64)
        gain_len = min(LINE_N_SAMPLES, len(gain_curve))
        self._implot_gain_data[:gain_len] = gain_curve[:gain_len]

        # Gain curve Y-axis limits
        self._gain_y_min = self._uss_conf.rx_gain - GAIN_PLOT_LOW_Y_MARGIN_DB
        self._gain_y_max = 80 + self._uss_conf.rx_gain

    def _init_signal_processing(self) -> None:
        """Initialize signal processing parameters and filter."""
        sampling_freq = self._uss_conf.sampling_freq
        self._f_low_cutoff = sampling_freq / 2 * 0.1
        self._f_high_cutoff = sampling_freq / 2 * 0.9

        self._design_filter(sampling_freq, self._f_low_cutoff, self._f_high_cutoff)

    def _init_widgets(self) -> None:
        """Initialize all GUI widgets."""
        # Serial port widgets
        self._ser_scan_button = widgets.Button(
            description="Scan ports",
            disabled=False,
            style={"description_width": "initial"},
        )

        self._ser_open_button = widgets.Button(
            description="Open port",
            disabled=True,
        )

        self._ports_dd = widgets.Dropdown(
            options=["Scanning..."],
            value="Scanning...",
            disabled=True,
        )

        # Plot window button
        self._plot_window_button = widgets.Button(
            description="Open Plot",
            disabled=False,
        )

        # TX/RX configuration dropdown
        num_configs = self._uss_conf.num_txrx_configs
        config_options = [str(x) for x in range(num_configs)]
        self._tx_rx_sel_dd = widgets.Dropdown(
            options=config_options,
            value=config_options[0],
            disabled=False,
            layout=widgets.Layout(width="100px"),
        )

        # Bandpass filter slider
        f_low_mhz = self._f_low_cutoff / 1e6
        f_high_mhz = self._f_high_cutoff / 1e6
        self._band_pass_frs = widgets.FloatRangeSlider(
            value=[f_low_mhz, f_high_mhz],
            min=f_low_mhz,
            max=f_high_mhz,
            step=0.1,
            description="Band pass (MHz):",
            disabled=False,
            continuous_update=False,
            orientation="horizontal",
            readout=True,
            readout_format=".1f",
            style={"description_width": "initial"},
        )

        # Progress bar
        self._frame_progr_bar = widgets.IntProgress(
            value=0,
            min=0,
            max=self._uss_conf.num_acqs,
            step=1,
            description="Progress:",
            bar_style="success",
            orientation="horizontal",
            style={"description_width": "initial"},
        )

        # Start/stop button
        self._start_stop_button = widgets.Button(
            description="Start measurement",
            disabled=True,
        )

        # Save data checkbox
        self._save_data_check = widgets.Checkbox(
            value=False,
            description="Save Data",
            disabled=False,
            layout=widgets.Layout(width="200px"),
        )

        # Save location text input
        self._save_location = widgets.Text(
            value="data",
            placeholder="Save location prefix",
            disabled=False,
            layout=widgets.Layout(width="150px"),
        )

        # Save file base name text input
        self._save_file_base = widgets.Text(
            value=FILE_NAME_BASE,
            placeholder="File base name",
            disabled=False,
            layout=widgets.Layout(width="150px"),
        )

        # Status label
        self._save_data_label = widgets.Label(value="")

    def _build_layout(self) -> None:
        """Build the GUI layout."""
        # Section labels
        connection_label = widgets.HTML("<b>Connection</b>")
        settings_label = widgets.HTML("<b>Settings</b>")
        acquisition_label = widgets.HTML("<b>Acquisition</b>")

        # Connection section
        connection_row = widgets.HBox(
            [
                widgets.HTML("Serial port:"),
                self._ports_dd,
                self._ser_open_button,
                self._ser_scan_button,
            ],
            layout=widgets.Layout(align_items="center", gap="8px"),
        )

        # Settings section
        settings_row = widgets.HBox(
            [
                widgets.HTML("Active RX config:"),
                self._tx_rx_sel_dd,
                self._band_pass_frs,
            ],
            layout=widgets.Layout(align_items="center", gap="8px"),
        )

        # Control section
        control_row = widgets.HBox(
            [
                self._start_stop_button,
                self._plot_window_button,
            ],
            layout=widgets.Layout(align_items="center", gap="8px"),
        )

        # Save section
        save_row = widgets.HBox(
            [
                self._save_data_check,
                widgets.HTML("Save location:"),
                self._save_location,
                widgets.HTML("File base:"),
                self._save_file_base,
            ],
            layout=widgets.Layout(align_items="center", gap="8px"),
        )

        # Progress section
        progress_row = widgets.HBox(
            [
                self._frame_progr_bar,
                self._save_data_label,
            ],
            layout=widgets.Layout(align_items="center", gap="8px"),
        )

        # Divider style
        divider = widgets.HTML(
            '<hr style="margin: 4px 0; border: none; border-top: 1px solid #ccc;">'
        )
        divider2 = widgets.HTML(
            '<hr style="margin: 4px 0; border: none; border-top: 1px solid #ccc;">'
        )

        # Main layout with sections
        main_box = widgets.VBox(
            [
                connection_label,
                connection_row,
                divider,
                settings_label,
                settings_row,
                divider2,
                acquisition_label,
                control_row,
                save_row,
                progress_row,
            ],
            layout=widgets.Layout(gap="4px", padding="8px"),
        )
        self.children = [main_box]

    def _connect_callbacks(self) -> None:
        """Connect widget callbacks."""
        self._ser_scan_button.on_click(self._on_scan_ports)
        self._ser_open_button.on_click(self._on_open_port)
        self._plot_window_button.on_click(self._on_open_plot_window)
        self._tx_rx_sel_dd.observe(self._on_rx_config_changed, "value")
        self._band_pass_frs.observe(self._on_bandpass_changed, "value")
        self._start_stop_button.on_click(self._on_start_stop)

    def _start_port_scan(self) -> None:
        """Start asynchronous port scanning."""
        scan_thread = Thread(
            target=self._on_scan_ports,
            args=(self._ser_scan_button,),
            daemon=True,
        )
        scan_thread.start()

    # endregion

    # region Plot Window

    def open_plot_window(self) -> None:
        """Open the ImPlot visualization window."""
        if self._plot_thread is not None and self._plot_thread.is_alive():
            return

        # Line width for traces
        line_width = 2.0

        # Colors for dark mode (brighter, more saturated)
        dark_colors = {
            "raw": imgui.ImVec4(0.3, 0.7, 1.0, 1.0),  # Bright cyan-blue
            "filtered": imgui.ImVec4(1.0, 0.6, 0.1, 1.0),  # Bright orange
            "envelope": imgui.ImVec4(0.3, 1.0, 0.4, 1.0),  # Bright green
        }

        # Colors for light mode (deeper, more contrast)
        light_colors = {
            "raw": imgui.ImVec4(0.0, 0.4, 0.8, 1.0),  # Deep blue
            "filtered": imgui.ImVec4(0.9, 0.3, 0.0, 1.0),  # Deep orange
            "envelope": imgui.ImVec4(0.0, 0.6, 0.2, 1.0),  # Deep green
        }

        def plot_func() -> None:
            def plot_build() -> None:
                if self._dark_mode:
                    imgui.style_colors_dark()
                    colors = dark_colors
                else:
                    imgui.style_colors_light()
                    colors = light_colors

                if implot.begin_plot("A-mode data", size=(-1, -1)):
                    # Setup primary Y-axis (ADC code)
                    implot.setup_axis(implot.ImAxis_.x1, "Sample")
                    implot.setup_axis(implot.ImAxis_.y1, "ADC code")
                    implot.setup_axis_limits(implot.ImAxis_.x1, 0, LINE_N_SAMPLES)
                    implot.setup_axis_limits(implot.ImAxis_.y1, -2500, 2500)

                    # Setup secondary Y-axis (Gain in dB)
                    implot.setup_axis(
                        implot.ImAxis_.y2,
                        "Gain [dB]",
                        implot.AxisFlags_.aux_default.value,
                    )
                    implot.setup_axis_limits(
                        implot.ImAxis_.y2, self._gain_y_min, self._gain_y_max
                    )

                    # Raw data (primary Y-axis)
                    implot.set_axes(implot.ImAxis_.x1, implot.ImAxis_.y1)
                    implot.set_next_line_style(colors["raw"], line_width)
                    implot.plot_line(
                        "Raw data",
                        self._implot_x_data,
                        self._implot_raw_data,
                    )
                    # Filtered data (primary Y-axis)
                    implot.set_next_line_style(colors["filtered"], line_width)
                    implot.plot_line(
                        "Filtered data",
                        self._implot_x_data,
                        self._implot_filt_data,
                    )
                    # Envelope data (primary Y-axis)
                    implot.set_next_line_style(colors["envelope"], line_width)
                    implot.plot_line(
                        "Envelope data",
                        self._implot_x_data,
                        self._implot_env_data,
                    )

                    # Gain curve (secondary Y-axis)
                    implot.set_axes(implot.ImAxis_.x1, implot.ImAxis_.y2)
                    implot.set_next_line_style(
                        imgui.ImVec4(0.5, 0.5, 0.5, 1.0), line_width
                    )  # Gray dashed
                    implot.plot_line(
                        "Gain curve",
                        self._implot_x_data,
                        self._implot_gain_data,
                    )

                    implot.end_plot()

            immapp.run_nb(
                gui_function=plot_build,
                window_title="Wulpus A-mode Plot",
                window_size=(800, 600),
            )

        self._plot_thread = Thread(target=plot_func, daemon=True)
        self._plot_thread.start()

    def close_plot_window(self) -> None:
        """Close the ImPlot visualization window."""
        if self._plot_thread is not None and self._plot_thread.is_alive():
            self._plot_thread.join(timeout=0.1)

    # endregion

    # region Callbacks

    def _on_open_plot_window(self, _: widgets.Button | None) -> None:
        """Handle plot window button click."""
        self.open_plot_window()

    def _on_scan_ports(self, button: widgets.Button) -> None:
        """Handle port scan button click."""
        button.disabled = True
        button.description = "Scanning..."

        try:
            self._found_devices = self._com_link.get_available()

            if not self._found_devices:
                self._ports_dd.options = ["No ports found"]
                self._ports_dd.value = "No ports found"
                self._ports_dd.disabled = True
                self._ser_open_button.disabled = True
            else:
                self._update_ports_dropdown()
                self._ports_dd.disabled = False
                self._ser_open_button.disabled = False
        finally:
            button.description = "Scan ports"
            button.disabled = False

    def _update_ports_dropdown(self) -> None:
        """Update the ports dropdown based on connection type."""
        self._ports_dd.options = [
            device.description
            if device.name in device.description
            else f"{device.description} ({device.name})"
            for device in self._found_devices
        ]

        if self._ports_dd.options:
            self._ports_dd.value = self._ports_dd.options[0]

    def _on_open_port(self, button: widgets.Button) -> None:
        """Handle port open/close button click."""
        button.disabled = True

        try:
            if not self._port_opened:
                self._open_port(button)
            else:
                self._close_port(button)
        finally:
            button.disabled = False

    def _open_port(self, button: widgets.Button) -> None:
        """Open the selected serial port."""
        if not self._ports_dd.options:
            return

        button.description = "Opening port..."

        device = self._found_devices[self._ports_dd.index]

        if not self._com_link.open(device):
            button.description = "Open port"
            self._port_opened = False
            self._start_stop_button.disabled = True
            return

        button.description = "Close port"
        self._port_opened = True
        self._start_stop_button.disabled = False

    def _close_port(self, button: widgets.Button) -> None:
        """Close the current serial port."""
        button.description = "Closing port..."

        self._com_link.close()
        button.description = "Open port"
        self._port_opened = False
        self._start_stop_button.disabled = True

    def _on_rx_config_changed(self, change: dict[str, Any]) -> None:
        """Handle RX configuration change."""
        self._rx_tx_conf_to_display = int(change["new"])

    def _on_bandpass_changed(self, change: dict[str, Any]) -> None:
        """Handle bandpass filter range change."""
        self._bandpass_manually_changed = True
        f_low, f_high = change["new"]
        self._design_filter(self._uss_conf.sampling_freq, f_low * 1e6, f_high * 1e6)

    def _on_start_stop(self, button: widgets.Button) -> None:
        """Handle start/stop measurement button click."""
        button.disabled = True

        try:
            if not self._acquisition_running:
                self._start_acquisition()
            else:
                self._stop_acquisition()
        finally:
            button.disabled = False

    def _start_acquisition(self) -> None:
        """Start data acquisition."""
        self._start_stop_button.description = "Starting..."

        # Set default bandpass if not manually changed
        if not self._bandpass_manually_changed:
            self._set_default_bandpass()

        # Disable port controls during acquisition
        self._ser_open_button.disabled = True
        self._save_data_label.value = ""

        # Start acquisition
        self._acquisition_running = True
        self._current_data = None

        self._acquisition_thread = Thread(
            target=self._run_acquisition_loop,
            daemon=True,
        )
        self._acquisition_thread.start()

    def _stop_acquisition(self) -> None:
        """Stop data acquisition."""
        self._start_stop_button.description = "Stopping..."
        self._acquisition_running = False
        self._ser_open_button.disabled = False
        self._start_stop_button.description = "Start measurement"

    def _set_default_bandpass(self) -> None:
        """Set default bandpass filter based on pulse frequency."""
        pulse_freq = self._uss_conf.pulse_freq
        f_low = pulse_freq * BANDPASS_LOW_RATIO / 1e6
        f_high = pulse_freq * BANDPASS_HIGH_RATIO / 1e6

        # Clamp to slider bounds
        f_low = max(f_low, self._band_pass_frs.min)
        f_high = min(f_high, self._band_pass_frs.max)

        self._band_pass_frs.value = [f_low, f_high]
        self._design_filter(self._uss_conf.sampling_freq, f_low * 1e6, f_high * 1e6)

    # endregion

    # region Acquisition

    def _run_acquisition_loop(self) -> None:
        """Main acquisition loop (runs in separate thread)."""
        # Reset data buffers
        acq_length = self._com_link.acq_length
        num_acqs = self._uss_conf.num_acqs

        self._data_arr = np.zeros((acq_length, num_acqs), dtype=np.int16)
        self._acq_num_arr = np.zeros(num_acqs, dtype=np.uint16)
        self._tx_rx_id_arr = np.zeros(num_acqs, dtype=np.uint8)
        self._data_cnt = 0

        # Disable RX during configuration
        self._com_link.toggle_rx(False)

        # Send restart command
        if not self._send_restart_command():
            return

        # Wait for system to reset
        time.sleep(2.5)

        # Send configuration
        if not self._send_configuration():
            return

        # Enable RX again
        self._com_link.toggle_rx(True)

        # Start visualization thread
        self._visualize = True
        self._current_data = None
        self._current_amode_data = None

        vis_thread = Thread(
            target=self._run_visualization,
            args=(num_acqs,),
            daemon=True,
        )
        vis_thread.start()

        # Update button state
        self._start_stop_button.description = "Stop measurement"

        # Acquisition loop
        self._acquire_data(num_acqs)

        # Disable RX after acquisition
        self._com_link.toggle_rx(False)

        # Stop visualization
        self._visualize = False
        vis_thread.join()

        # Send restart command
        self._com_link.send_config(self._uss_conf.get_restart_package())

        # Save data if requested
        if self._save_data_check.value:
            self._save_data_to_file()

        # Reset state
        if self._ser_open_button.disabled:
            self._stop_acquisition()

    def _send_restart_command(self) -> bool:
        """Send restart command to the device."""
        if not self._com_link.send_config(self._uss_conf.get_restart_package()):
            self._handle_error("Error sending restart command")
            return False
        return True

    def _send_configuration(self) -> bool:
        """Send configuration package to the device."""
        try:
            if not self._com_link.send_config(self._uss_conf.get_conf_package()):
                self._handle_error("Error sending configuration package")
                return False
        except ValueError as e:
            self._handle_error(str(e))
            return False
        return True

    def _handle_error(self, message: str) -> None:
        """Handle acquisition error."""
        print(message)
        self._save_data_label.value = message
        self._acquisition_running = False
        if self._ser_open_button.disabled:
            self._stop_acquisition()

    def _acquire_data(self, num_acqs: int) -> None:
        """Acquire data from the device."""
        while self._data_cnt < num_acqs and self._acquisition_running:
            data = self._com_link.receive_data()

            if data is None:
                continue

            self._current_data = data

            # Update A-mode data if this is the selected config
            if data[2] == self._rx_tx_conf_to_display:
                self._current_amode_data = data[0]

            # Store data
            self._data_arr[:, self._data_cnt] = data[0]
            self._acq_num_arr[self._data_cnt] = data[1]
            self._tx_rx_id_arr[self._data_cnt] = data[2]

            self._data_cnt += 1

            # Update progress bar
            self._frame_progr_bar.description = f"Progress: {self._data_cnt}/{num_acqs}"
            self._frame_progr_bar.value = self._data_cnt

    def _run_visualization(self, num_acqs: int) -> None:
        """Visualization loop (runs in separate thread)."""
        self._frame_progr_bar.max = num_acqs

        while self._visualize:
            begin_time = time.time()

            if self._current_amode_data is not None:
                self._update_plot_data()

            # Sleep to maintain target FPS
            elapsed = time.time() - begin_time
            sleep_time = self._vis_fps_period - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    def _update_plot_data(self) -> None:
        """Update the plot data arrays."""
        if self._current_amode_data is None:
            return

        data_len = min(LINE_N_SAMPLES, len(self._current_amode_data))

        # Raw data
        self._implot_raw_data[:data_len] = self._current_amode_data[:data_len]

        # Filtered data
        filt_data = self._filter_data(self._current_amode_data)
        self._implot_filt_data[:data_len] = filt_data[:data_len]

        # Envelope
        env_data = self._get_envelope(filt_data)
        self._implot_env_data[:data_len] = env_data[:data_len]

    # endregion

    # region Signal Processing

    def _design_filter(
        self,
        f_sampling: float,
        f_low_cutoff: float,
        f_high_cutoff: float,
        trans_width: float = 0.2e6,
        n_taps: int = 31,
    ) -> None:
        """
        Design a bandpass filter using the Remez algorithm.

        Args:
            f_sampling: Sampling frequency in Hz.
            f_low_cutoff: Low cutoff frequency in Hz.
            f_high_cutoff: High cutoff frequency in Hz.
            trans_width: Transition width in Hz.
            n_taps: Number of filter taps.
        """
        bands = [
            0,
            f_low_cutoff - trans_width,
            f_low_cutoff,
            f_high_cutoff,
            f_high_cutoff + trans_width,
            f_sampling / 2,
        ]

        self._filt_b = ss.remez(n_taps, bands, [0, 1, 0], fs=f_sampling, maxiter=2500)
        self._filt_a = 1.0

    def _filter_data(self, data: NDArray[np.int16]) -> NDArray[np.float64]:
        """Apply the bandpass filter to the data."""
        return ss.filtfilt(self._filt_b, self._filt_a, data)

    def _get_envelope(self, data: NDArray[np.float64]) -> NDArray[np.float64]:
        """Compute the envelope using the Hilbert transform."""
        return np.abs(hilbert(data))  # type: ignore

    # endregion

    # region Data Storage

    def _save_data_to_file(self) -> None:
        """Save acquired data to an NPZ file."""
        filename = self._get_unique_filename()

        np.savez(
            filename[:-4],  # Remove .npz extension (added by savez)
            data_arr=self._data_arr,
            acq_num_arr=self._acq_num_arr,
            tx_rx_id_arr=self._tx_rx_id_arr,
        )

        self._save_data_label.value = f"Data saved in {filename}"

    def _get_unique_filename(self) -> str:
        """Get a unique filename for saving data."""
        base = self._save_location.value.strip() or "data"
        base_file = self._save_file_base.value.strip() or FILE_NAME_BASE

        # Create directory if path contains a directory component
        base_path = Path(base).resolve()
        if base_path != Path.cwd():
            base_path.mkdir(parents=True, exist_ok=True)

        for i in range(MAX_SAVE_FILES):
            filename = base_path / f"{base_file}_{i}.npz"
            if not filename.is_file():
                return str(filename)

        # Fallback to last filename if all exist
        return str(base_path / f"{base_file}_{MAX_SAVE_FILES - 1}.npz")

    # endregion
