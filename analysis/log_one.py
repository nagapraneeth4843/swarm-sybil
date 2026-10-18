
#!/usr/bin/env python3
"""Log one ESP32's telemetry to a timestamped CSV file."""

import argparse
import csv
import re
import time
from datetime import datetime
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description="Log ESP32 serial telemetry.")
    parser.add_argument("port", help="Serial port, e.g. COM5")
    parser.add_argument("label", help="Experiment label, e.g. EXP_NONE_N0_V00_D0")
    parser.add_argument("--duration", type=float, default=60,
                        help="Logging duration in seconds (default: 60)")
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        parser.error("Label may contain only letters, numbers, _ and -")
    if args.duration <= 0:
        parser.error("Duration must be greater than zero")

    output_dir = Path(__file__).resolve().parent.parent / "data" / "experiments"
    output_dir.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = output_dir / f"{args.label}_{timestamp}.csv"

    print(f"Opening {args.port} at 921600 baud...")
    print("Close any serial monitor using this port before continuing.")

    rows_written = 0
    start = None

    try:
        with serial.Serial(args.port, 921600, timeout=1) as ser:
            ser.reset_input_buffer()
            start = time.time()

            with output_path.open("w", newline="", encoding="utf-8") as file:
                writer = csv.writer(file)
                writer.writerow([
                    "WALL_S", "NODE", "TS_MS", "STATE",
                    "SENSOR", "NNB", "DEF", "NEIGHBOURS"
                ])
                file.flush()

                print(f"Logging to: {output_path}")
                print(f"Duration: {args.duration:g} seconds. Press Ctrl+C to stop.")

                while time.time() - start < args.duration:
                    raw = ser.readline().decode("utf-8", "replace").strip()

                    if not raw or raw.startswith("#"):
                        continue

                    fields = raw.split(",", 6)
                    if len(fields) < 6:
                        continue

                    try:
                        int(fields[0], 16)
                        int(fields[1])
                        float(fields[2])
                        float(fields[3])
                        int(fields[4])
                        int(fields[5])
                    except ValueError:
                        continue

                    neighbours = fields[6] if len(fields) == 7 else ""
                    elapsed = time.time() - start

                    writer.writerow([
                        f"{elapsed:.2f}",
                        *fields[:6],
                        neighbours
                    ])
                    file.flush()
                    rows_written += 1
                    print(
                        f"\rValid rows logged: {rows_written}",
                        end="",
                        flush=True
                    )

    except KeyboardInterrupt:
        print("\nLogging stopped by user.")
    except serial.SerialException as exc:
        print(f"\nSerial error: {exc}")
        raise SystemExit(1)

    print(f"\nFinished. Rows saved: {rows_written}")
    print(f"CSV: {output_path}")


if __name__ == "__main__":
    main()
