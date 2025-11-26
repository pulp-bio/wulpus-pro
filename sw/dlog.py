import argparse
import csv
import json
import matplotlib.pyplot as plt
from multiprocessing import Pool
import numpy as np
import os
import struct
import sys
import xml.etree.ElementTree as ET


def running_mean(x: np.ndarray, N: int) -> np.ndarray:
    """
    Compute `N` elements wide running average over `x`.

    :param x: 1-Dimensional NumPy array
    :param N: how many items to average. Should be even for optimal results.
    """

    # to ensure that output.shape == input.shape, we need to insert data
    # at the boundaries
    boundary_array = np.insert(x, 0, np.full((N // 2), x[0]))
    boundary_array = np.append(boundary_array, np.full((N // 2 + N % 2 - 1), x[-1]))

    return np.convolve(boundary_array, np.ones((N,)) / N, mode="valid")


def downsample(x: np.ndarray, N: int) -> np.ndarray:
    """Downsample `x` by factor `N`."""
    fill = x.shape[0] % N
    if fill:
        x = np.array(list(x) + [x[-1] for i in range(N - fill)])
    return x.reshape(-1, N).mean(axis=1)


class NpEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, np.integer):
            return int(obj)
        elif isinstance(obj, np.floating):
            return float(obj)
        elif isinstance(obj, np.ndarray):
            return obj.tolist()
        else:
            return super(NpEncoder, self).default(obj)


def PELT_get_changepoints(algo, penalty):
    res = (penalty, algo.predict(pen=penalty))
    return res


class PELT:
    def __init__(self, signal, num_samples=None):
        self.signal = signal
        self.model = "l1"
        self.jump = 1
        self.min_dist = 500

        if num_samples is not None:
            self.ds_factor = len(signal) // num_samples
        else:
            self.ds_factor = 1

        self.jump = self.ds_factor
        # if self.ds_factor > 1:
        #    self.signal = downsample(self.signal, self.ds_factor)
        # print(f"ds from {len(signal)} to {len(self.signal)}")

    def norm_signal(self, signal, scaler=25):
        max_val = max(np.abs(signal))
        normed_signal = np.zeros(shape=len(signal))
        for i, signal_i in enumerate(signal):
            normed_signal[i] = signal_i / max_val
            normed_signal[i] = normed_signal[i] * scaler
        return normed_signal

    def get_changepoints(self):
        # imported here as ruptures is only used for changepoint detection
        import ruptures

        algo = ruptures.Pelt(
            model=self.model, jump=self.jump, min_size=self.min_dist
        ).fit(self.norm_signal(self.signal))
        queue = list()
        for i in range(0, 100):
            queue.append((algo, i))
        with Pool() as pool:
            changepoints = pool.starmap(PELT_get_changepoints, queue)
        changepoints_by_penalty = dict()
        for res in changepoints:
            changepoints_by_penalty[res[0]] = res[1]
        num_changepoints = list()
        for i in range(0, 100):
            num_changepoints.append(len(changepoints_by_penalty[i]))

        # Find plateau
        start_index = -1
        end_index = -1
        longest_start = -1
        longest_end = -1
        prev_val = -1
        for i, num_bkpts in enumerate(num_changepoints):
            if num_bkpts != prev_val:
                end_index = i - 1
                if end_index - start_index > longest_end - longest_start:
                    # currently found sequence is the longest found yet
                    longest_start = start_index
                    longest_end = end_index
                start_index = i
            if i == len(num_changepoints) - 1:
                # end sequence with last value
                end_index = i
                # # since it is not guaranteed that this is the end of the plateau, assume the mid
                # # of the plateau was hit.
                # size = end_index - start_index
                # end_index = end_index + size
                # However this is not the clean solution. Better if search interval is widened
                # with range_min and range_max
                if end_index - start_index > longest_end - longest_start:
                    # last found sequence is the longest found yet
                    longest_start = start_index
                    longest_end = end_index
                start_index = i
            prev_val = num_bkpts
        middle_of_plateau = longest_start + (longest_start - longest_start) // 2
        changepoints = np.array(changepoints_by_penalty[middle_of_plateau])
        return changepoints


class DLogChannel:
    def __init__(self, desc_tuple):
        self.slot = desc_tuple[0]
        self.smu = desc_tuple[1]
        self.unit = desc_tuple[2]
        self.data = None

    def __repr__(self):
        return f"""<DLogChannel(slot={self.slot}, smu="{self.smu}", unit="{self.unit}", data={self.data})>"""


class DLog:
    def __init__(self, filename, skip=None, limit=None):
        self.skip_duration = skip
        self.limit_duration = limit
        self.filename = filename
        self.load_dlog(filename)

    def load_dlog(self, filename):
        lines = []
        line = ""

        with open(filename, "rb") as f:
            if ".xz" in filename:
                import lzma

                f = lzma.open(f)

            while line != "</dlog>\n":
                line = f.readline().decode()
                lines.append(line)
            xml_header = "".join(lines)
            raw_header = f.read(8)
            data_offset = f.tell()
            raw_data = f.read()

        xml_header = xml_header.replace("1ua>", "X1ua>")
        xml_header = xml_header.replace("2ua>", "X2ua>")
        dlog = ET.fromstring(xml_header)
        channels = []
        for channel in dlog.findall("channel"):
            channel_id = int(channel.get("id"))
            sense_curr = channel.find("sense_curr").text
            sense_volt = channel.find("sense_volt").text
            model = channel.find("ident").find("model").text
            if sense_volt == "1":
                channels.append((channel_id, model, "V"))
            if sense_curr == "1":
                channels.append((channel_id, model, "A"))

        num_channels = len(channels)

        self.channels = list(map(DLogChannel, channels))
        self.interval = float(dlog.find("frame").find("tint").text)
        self.sense_minmax = int(dlog.find("frame").find("sense_minmax").text)

        if self.sense_minmax:
            # there's a min, current, and max reading for each channel.
            num_channels *= 3

        self.planned_duration = int(dlog.find("frame").find("time").text)
        self.observed_duration = self.interval * int(len(raw_data) / (4 * num_channels))

        self.timestamps = np.linspace(
            0, self.observed_duration, num=int(len(raw_data) / (4 * num_channels))
        )

        if (
            self.skip_duration is not None
            and self.observed_duration >= self.skip_duration
        ):
            start_offset = 0
            for i, ts in enumerate(self.timestamps):
                if ts >= self.skip_duration:
                    start_offset = i
                    break
            self.timestamps = self.timestamps[start_offset:]
            raw_data = raw_data[start_offset * 4 * num_channels :]

        if (
            self.limit_duration is not None
            and self.observed_duration > self.limit_duration
        ):
            stop_offset = len(self.timestamps) - 1
            for i, ts in enumerate(self.timestamps):
                if ts > self.limit_duration:
                    stop_offset = i
                    break
            self.timestamps = self.timestamps[:stop_offset]
            self.observed_duration = self.timestamps[-1]
            raw_data = raw_data[: stop_offset * 4 * num_channels]

        self.data = np.ndarray(
            shape=(num_channels, int(len(raw_data) / (4 * num_channels))),
            dtype=np.float32,
        )

        iterator = struct.iter_unpack(">f", raw_data)
        channel_offset = 0
        measurement_offset = 0
        for value in iterator:
            if value[0] < -1e6 or value[0] > 1e6:
                print(
                    f"Invalid data value {value[0]} at channel {channel_offset}, measurement {measurement_offset}. Replacing with 0."
                )
                self.data[channel_offset, measurement_offset] = 0
            else:
                self.data[channel_offset, measurement_offset] = value[0]
            if channel_offset + 1 == num_channels:
                channel_offset = 0
                measurement_offset += 1
            else:
                channel_offset += 1

        # An SMU has four slots
        self.slots = [dict(), dict(), dict(), dict()]

        for i, channel in enumerate(self.channels):
            if self.sense_minmax:
                # [i*3] == current/avg(?), [i*3 + 1] == min, [i*3 + 2] == max
                channel.data = self.data[i * 3]
            else:
                channel.data = self.data[i]
            self.slots[channel.slot - 1][channel.unit] = channel

    def slot_has_data(self, slot):
        return len(self.slots[slot]) > 0

    def slot_has_power(self, slot):
        slot_data = self.slots[slot]
        if "W" in slot_data:
            return True
        if "V" in slot_data and "A" in slot_data:
            return True
        return False

    def count_data_slots(self):
        return sum(map(int, map(self.slot_has_data, range(4))))

    def count_power_slots(self):
        return sum(map(int, map(self.slot_has_power, range(4))))

    def observed_duration_equals_expectation(self):
        return int(self.observed_duration) == self.planned_duration

    def all_data_slots_have_power(self):
        for slot in range(4):
            if self.slot_has_data(slot) and not self.slot_has_power(slot):
                return False
        return True


def detect_changepoints(dlog, num_samples):
    ret_slots = list()
    for slot in dlog.slots:
        ret_slot = dict()
        for unit in slot:
            pelt = PELT(running_mean(slot[unit].data, 10), num_samples=num_samples)
            changepoints = pelt.get_changepoints()
            prev = 0
            ret_slot[unit] = list()
            for cp in changepoints:
                cp = cp - 1
                ret_slot[unit].append(
                    {
                        "interval": [dlog.timestamps[prev], dlog.timestamps[cp]],
                        "mean": np.mean(slot[unit].data[prev:cp]),
                    }
                )
                prev = cp
        ret_slots.append(ret_slot)
    return ret_slots


def print_stats(dlog):
    if dlog.observed_duration_equals_expectation():
        print(
            "Measurement duration: {:d} seconds at {:f} µs per sample".format(
                dlog.planned_duration, dlog.interval * 1_000_000
            )
        )
    else:
        print(
            "Measurement duration: {:f} of {:d} seconds at {:f} µs per sample".format(
                dlog.observed_duration, dlog.planned_duration, dlog.interval * 1_000_000
            )
        )

    for channel in dlog.channels:
        min_data = np.min(channel.data)
        max_data = np.max(channel.data)
        mean_data = np.mean(channel.data)
        std_data = np.std(channel.data)
        if channel.unit == "V":
            precision = 3
        else:
            precision = 6
        print(f"Slot {channel.slot} ({channel.smu}):")
        print(f"    Min  {min_data:.{precision}f} {channel.unit}")
        print(f"    Mean {mean_data:.{precision}f} {channel.unit}")
        print(f"    σ    {std_data:.{precision}f} {channel.unit}")
        print(f"    Max  {max_data:.{precision}f} {channel.unit}")
        print()


def plot_changepoints_vlines(changepoints):
    X = list()
    for cp in changepoints:
        X.append(cp["interval"][1])
    return X


def show_efficiency_plot(dlog, plot_x, changepoints=None):

    handles = list()
    traces = list()
    slots = list()

    for i, slot in enumerate(dlog.slots):
        if "W" in slot:
            slots.append(i)
            traces.append(slot["W"].data)
        elif "V" in slot and "A" in slot:
            slots.append(i)
            traces.append(slot["V"].data * slot["A"].data)

    assert len(traces) == 2

    if min(traces[0]) < min(traces[1]):
        in_trace = traces[1]
        out_trace = traces[0] * -1
        in_slot = slots[1]
        out_slot = slots[0]
    else:
        in_trace = traces[0]
        out_trace = traces[1] * -1
        in_slot = slots[0]
        out_slot = slots[1]

    eta = out_trace * 100 / in_trace

    eta[eta > 100] = np.nan
    eta[eta < 0] = np.nan

    marker = "."
    linestyle = ""

    if plot_x == "T":
        xaxis = dlog.timestamps
        xlabel = "Time [s]"
        marker = ""
        linestyle = "-"
    elif plot_x == "U":
        xaxis = dlog.slots[in_slot]["V"].data
        xlabel = "Input Voltage [V]"
    elif plot_x == "I":
        xaxis = dlog.slots[out_slot]["A"].data * -1
        xlabel = "Output Current [A]"
    elif plot_x == "P":
        xaxis = out_trace
        xlabel = "Output Power [W]"

    (handle,) = plt.plot(
        xaxis,
        eta,
        marker=marker,
        color="b",
        linestyle=linestyle,
        label="η",
        markersize=1,
    )
    handles.append(handle)
    (handle,) = plt.plot(
        xaxis,
        running_mean(eta, 10),
        marker=marker,
        color="r",
        linestyle=linestyle,
        label="mean(η, 10)",
        markersize=1,
    )
    handles.append(handle)

    plt.legend(handles=handles)
    plt.title(dlog.filename)
    plt.xlabel(xlabel)
    plt.ylabel("Conversion Efficiency [%]")
    plt.grid(True)
    plt.show()


def show_power_plot(dlog, plot_x, changepoints=None):

    handles = list()

    for i, slot in enumerate(dlog.slots):
        if "W" in slot:
            (handle,) = plt.plot(
                dlog.timestamps, slot["W"].data, "b-", label="P", markersize=1
            )
            handles.append(handle)
            (handle,) = plt.plot(
                dlog.timestamps,
                running_mean(slot["W"].data, 10),
                "r-",
                label="mean(P, 10)",
                markersize=1,
            )
            handles.append(handle)
            if changepoints is not None:
                plt.vlines(
                    plot_changepoints_vlines(changepoints[i]["W"]),
                    np.min(slot["W"].data),
                    np.max(slot["W"].data),
                    "g",
                    label="changepoints(P)",
                )
        elif "V" in slot and "A" in slot:
            (handle,) = plt.plot(
                dlog.timestamps,
                slot["V"].data * slot["A"].data,
                "b-",
                label="P = U * I",
                markersize=1,
            )
            handles.append(handle)
            (handle,) = plt.plot(
                dlog.timestamps,
                running_mean(slot["V"].data * slot["A"].data, 10),
                "r-",
                label="mean(P, 10)",
                markersize=1,
            )
            handles.append(handle)
            if changepoints is not None:
                plt.vlines(
                    plot_changepoints_vlines(changepoints[i]["A"]),
                    np.min(slot["V"].data * slot["A"].data),
                    np.max(slot["V"].data * slot["A"].data),
                    "g",
                    label="changepoints(I)",
                )

    plt.legend(handles=handles)
    plt.title(dlog.filename)
    plt.xlabel("Time [s]")
    plt.ylabel("Power [W]")
    plt.grid(True)
    plt.show()


def show_unit_plot(dlog, metric, plot_x, changepoints=None):

    handles = list()

    if metric == "U":
        unit = "V"
    elif metric == "I":
        unit = "A"
    elif metric == "P":
        unit = "W"

    for i, slot in enumerate(dlog.slots):
        if unit in slot:
            channel = slot[unit]
            (handle,) = plt.plot(
                dlog.timestamps,
                slot[unit].data,
                "b-",
                label=f"slot {channel.slot} ({channel.smu})",
                markersize=1,
            )
            handles.append(handle)
            (handle,) = plt.plot(
                dlog.timestamps,
                running_mean(slot[unit].data, 10),
                "r-",
                label=f"slot {channel.slot} mean",
                markersize=1,
            )
            handles.append(handle)
            if changepoints is not None:
                plt.vlines(
                    plot_changepoints_vlines(changepoints[i][unit]),
                    np.min(slot[unit].data),
                    np.max(slot[unit].data),
                    "g",
                    label=f"changepoints",
                )

    plt.legend(handles=handles)
    plt.title(dlog.filename)
    plt.xlabel("Time [s]")
    if unit == "V":
        plt.ylabel("Voltage [V]")
    elif unit == "A":
        plt.ylabel("Current [A]")
    elif unit == "W":
        plt.ylabel("Power [W]")
    else:
        plt.ylabel(f"??? [{unit}]")
    plt.grid(True)
    plt.show()


def show_raw_plot(dlog, plot_x):
    handles = list()

    for channel in dlog.channels:
        label = f"{channel.slot} / {channel.smu} {channel.unit}"
        (handle,) = plt.plot(
            dlog.timestamps, channel.data, "b-", label=label, markersize=1
        )
        handles.append(handle)
        (handle,) = plt.plot(
            dlog.timestamps,
            running_mean(channel.data, 10),
            "r-",
            label=f"mean({label}, 10)",
            markersize=1,
        )
        handles.append(handle)

    plt.legend(handles=handles)
    plt.title(dlog.filename)
    plt.xlabel("Time [s]")
    plt.ylabel("Voltage [V] / Current [A] / Power [W]")
    plt.grid(True)
    plt.show()


def export_csv(dlog, filename):
    cols, rows = dlog.data.shape
    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        channel_header = list(
            map(lambda x: f"{x.smu} in slot {x.slot} [{x.unit}]", dlog.channels)
        )
        writer.writerow(["Timestamp [s]"] + channel_header)
        for row in range(rows):
            writer.writerow([dlog.timestamps[row]] + list(dlog.data[:, row]))


def export_json(dlog, filename, extra_data=dict()):
    json_channels = list()
    for channel in dlog.channels:
        json_channels.append({"smu": channel.smu, "unit": channel.unit})
    json_data = {"channels": json_channels}
    json_data.update(extra_data)
    with open(filename, "w") as f:
        json.dump(json_data, f, cls=NpEncoder)


def main():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter, description=__doc__
    )
    parser.add_argument(
        "--csv-export",
        metavar="FILENAME",
        type=str,
        help="Export measurements to CSV file",
    )
    parser.add_argument(
        "--json-export",
        metavar="FILENAME",
        type=str,
        help="Export analysis results (e.g. changepoints) to JSON file",
    )
    parser.add_argument(
        "--skip",
        metavar="N",
        type=float,
        default=0,
        help="Skip the first N seconds of data. This is useful to avoid startup code influencing the results of a long-running measurement",
    )
    parser.add_argument(
        "--limit",
        type=float,
        metavar="N",
        help="Limit analysis to the first N seconds of data",
    )
    parser.add_argument(
        "--pelt",
        metavar="NUM",
        type=int,
        help="Perform changepoint detection on NUM samples",
    )
    parser.add_argument(
        "--plot-x",
        choices=["T", "U", "I", "P"],
        default="T",
        help="Plot time/voltage/current/power at X axis",
    )
    parser.add_argument(
        "--plot-y",
        "--plot",
        choices=["U", "I", "P", "eta", "all"],
        help="Plot voltage/current/power/efficiency at Y axis",
    )
    parser.add_argument(
        "--stat", help="Print mean voltage, current, and power", action="store_true"
    )
    parser.add_argument(
        "dlog_file", type=str, help="Input filename in Keysight dlog format"
    )

    args = parser.parse_args()

    dlog = DLog(args.dlog_file, args.skip, args.limit)

    if args.stat:
        print_stats(dlog)

    if args.csv_export:
        export_csv(dlog, args.csv_export)

    if args.pelt:
        changepoints = detect_changepoints(dlog, num_samples=args.pelt)
        for i, slot in enumerate(changepoints):
            for unit in slot:
                num_changepoints = len(slot[unit])
                print(
                    f"Found {num_changepoints} changepoints for {dlog.slots[i][unit].smu} {unit}"
                )

    if args.json_export:
        extra_data = dict()
        if args.pelt:
            extra_data["changepoints"] = changepoints
        export_json(dlog, args.json_export, extra_data)

    if args.plot_y:
        if args.plot_y == "P":
            if dlog.all_data_slots_have_power():
                if args.pelt:
                    show_power_plot(dlog, args.plot_x, changepoints)
                else:
                    show_power_plot(dlog, args.plot_x)
            else:
                print(
                    "Error: power plot requested, but neither power nor voltage*current readings present.",
                    file=sys.stderr,
                )
        elif args.plot_y == "eta":
            if dlog.count_power_slots() == 2:
                if args.pelt:
                    show_efficiency_plot(dlog, args.plot_x, changepoints)
                else:
                    show_efficiency_plot(dlog, args.plot_x)
            else:
                print(
                    "Error: efficiency plot requires 2 power measurements. This file has "
                    + dlog.count_power_slots(),
                    file=sys.stderr,
                )
        elif args.plot_y == "all":
            show_raw_plot(dlog, args.plot_x)
        else:
            if args.pelt:
                show_unit_plot(dlog, args.plot_y, args.plot_x, changepoints)
            else:
                show_unit_plot(dlog, args.plot_y, args.plot_x)


if __name__ == "__main__":
    main()