import sys
from math import floor, ceil, log2

def main(time_s: float) -> None:

    time_us = int(time_s * 1e6)
    print(f"Time in microseconds: {time_us} us")

    exponent = floor(log2(time_us)) - 1
    mantissa = time_us / (1 << exponent)
    while True:
        exponent_new = exponent - 1
        mantissa_new = time_us / (1 << exponent_new)

        if mantissa_new > 65535:
            break

        mantissa = mantissa_new
        exponent = exponent_new

    print(f"Exponent: {exponent}")
    print(f"Mantissa: {mantissa}")

    if int(mantissa) != mantissa:
        print("Mantissa is not an integer")
        mantissa_low = floor(mantissa)
        mantissa_high = ceil(mantissa)
        print(f"Mantissa: {mantissa_low} (low), {mantissa_high} (high)")
        real_time_us_low = mantissa_low * (1 << exponent)
        real_time_us_high = mantissa_high * (1 << exponent)
        print(f"Real TWT: {real_time_us_low} (low), {real_time_us_high} (high)")

    # print(f"Time in microseconds: {time_us} us")
    # print(f"Exponent: {exponent}")

    # mantissa_low = floor(time_us / (1 << exponent))
    # mantissa_high = ceil(time_us / (1 << exponent))
    # print(f"Mantissa: {mantissa_low} (low), {mantissa_high} (high)")

    # print(f"Real TWT: {mantissa_low * (1 << exponent)} (low), {mantissa_high * (1 << exponent)} (high)")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python calculate_twt.py <time_s>")
        sys.exit(1)

    time_s = float(sys.argv[1])
    main(time_s)