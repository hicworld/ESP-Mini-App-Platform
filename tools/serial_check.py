"""Capture USB CDC output and optionally send commands to a running board.

All --command values are written back-to-back when the capture starts, so
state-dependent sequences ("run X" then "status") race: the later command is
often processed before the earlier one takes effect. Use --gap to space them
out when order matters.
"""
import argparse
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--command", action="append", default=[])
    parser.add_argument("--gap", type=float, default=0,
                        help="seconds to wait between commands (default: none)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=0.2, write_timeout=3) as connection:
        connection.dtr = True
        connection.rts = False
        started = time.monotonic()
        sent = False
        with args.output.open("wb") as output:
            while time.monotonic() - started < args.seconds:
                if not sent and time.monotonic() - started >= 1:
                    for command in args.command:
                        connection.write((command + "\n").encode("utf-8"))
                        if args.gap > 0:
                            time.sleep(args.gap)
                    sent = True
                data = connection.read(4096)
                if data:
                    output.write(data)
                    output.flush()
                    print(data.decode("utf-8", errors="replace"), end="", flush=True)


if __name__ == "__main__":
    main()
