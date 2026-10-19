
#!/usr/bin/env python3
"""Load an experiment CSV and plot node state trajectories."""

import argparse
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_file", help="Experiment CSV file")
    args = parser.parse_args()

    csv_path = Path(args.csv_file)
    df = pd.read_csv(csv_path, on_bad_lines="skip")

    required = {"WALL_S", "NODE", "STATE"}
    if not required.issubset(df.columns):
        raise SystemExit(f"CSV must contain columns: {sorted(required)}")

    df["WALL_S"] = pd.to_numeric(df["WALL_S"], errors="coerce")
    df["STATE"] = pd.to_numeric(df["STATE"], errors="coerce")
    df = df.dropna(subset=["WALL_S", "STATE", "NODE"])

    if df.empty:
        raise SystemExit("No valid telemetry rows found.")

    fig, ax = plt.subplots()
    for node, group in df.groupby("NODE"):
        group = group.sort_values("WALL_S")
        ax.plot(group["WALL_S"], group["STATE"], label=str(node))

    ax.set_xlabel("Wall time (s)")
    ax.set_ylabel("Consensus state")
    ax.set_title(f"State trajectory — {csv_path.stem}")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    output_path = csv_path.with_name(csv_path.stem + "_trajectory.png")
    fig.savefig(output_path, dpi=150)
    print(f"Plot saved: {output_path}")
    plt.show()


if __name__ == "__main__":
    main()
