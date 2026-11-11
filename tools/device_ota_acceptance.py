"""Verify a confirmed OTA upgrade followed by an automatic bootloader rollback."""
import argparse
import re
import time
from pathlib import Path

import serial


STATUS = re.compile(r"STATUS version=(\S+) partition=(\S+) ota_state=(\d+) ")
SELFTEST = re.compile(r"SELFTEST RESULT (\d+)/(\d+)")
APP_ROW = re.compile(r"^[a-z][a-z0-9.]+ \d+\.\d+\.\d+ .+$")


class Board:
    def __init__(self, port: str, log):
        self.port = port
        self.log = log
        self.connection = None

    def note(self, text: str):
        print(text, flush=True)
        self.log.write(text + "\n")
        self.log.flush()

    def connect(self, timeout: float = 45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self.connection = serial.Serial(
                    self.port, 115200, timeout=0.25, write_timeout=5
                )
                self.connection.dtr = True
                self.connection.rts = False
                time.sleep(2)
                return
            except serial.SerialException:
                time.sleep(0.5)
        raise TimeoutError(f"could not open {self.port}")

    def close(self):
        if self.connection:
            self.connection.close()
            self.connection = None

    def command(self, command: str, timeout: float = 20):
        self.note("> " + command)
        self.connection.write((command + "\n").encode())
        lines = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.connection.readline().decode(errors="replace").strip()
            if line:
                lines.append(line)
                self.note(line)
                if line.startswith("result: "):
                    if line != "result: ESP_OK":
                        raise AssertionError(f"{command}: {line}")
                    return "\n".join(lines)
        raise TimeoutError(command)

    def status(self):
        output = self.command("status")
        match = STATUS.search(output)
        if not match:
            raise AssertionError("firmware does not expose OTA status fields")
        return match.groups(), output

    def apps(self):
        return {line for line in self.command("list").splitlines() if APP_ROW.fullmatch(line)}

    def ota(self, url: str):
        self.note("> ota " + url)
        self.connection.write(("ota " + url + "\n").encode())
        deadline = time.monotonic() + 120
        disconnected = False
        while time.monotonic() < deadline:
            try:
                line = self.connection.readline().decode(errors="replace").strip()
                if line:
                    self.note(line)
                    if line.startswith("result: ") and line != "result: ESP_OK":
                        raise AssertionError(f"OTA failed: {line}")
            except serial.SerialException:
                disconnected = True
                break
        self.close()
        if not disconnected:
            raise TimeoutError("OTA did not reboot the board")
        self.connect(timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--production-url", required=True)
    parser.add_argument("--rollback-url", required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for url in (args.production_url, args.rollback_url):
        if not url.startswith("https://"):
            raise SystemExit("OTA URLs must use HTTPS")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as log:
        board = Board(args.port, log)
        try:
            board.connect()
            (old_version, old_partition, _), _ = board.status()
            baseline_apps = board.apps()
            if not baseline_apps:
                raise AssertionError("no installed apps available for persistence check")
            if old_partition not in ("ota_0", "ota_1"):
                raise AssertionError("initial firmware is not running from an OTA slot")
            expected_partition = "ota_1" if old_partition == "ota_0" else "ota_0"
            board.note(
                f"BASELINE version={old_version} partition={old_partition}; "
                f"expect {args.expected_version} in {expected_partition}"
            )
            board.note("BEGIN confirmed OTA upgrade")
            board.ota(args.production_url)
            (version, partition, state), status = board.status()
            if version != args.expected_version or partition != expected_partition or state != "2":
                raise AssertionError("production image was not confirmed: " + status)
            if board.apps() != baseline_apps:
                raise AssertionError("application list changed during production OTA")
            selftest = board.command("selftest", timeout=30)
            result = SELFTEST.search(selftest)
            if not result or result.group(1) != result.group(2) or int(result.group(1)) < 20:
                raise AssertionError("production selftest did not fully pass")

            board.note("BEGIN forced rollback validation")
            board.ota(args.rollback_url)
            # The test image immediately reboots once more into the confirmed image.
            time.sleep(8)
            board.close()
            board.connect(timeout=60)
            (version, partition, state), status = board.status()
            if version != args.expected_version or partition != expected_partition or state != "2":
                raise AssertionError("bootloader did not return to the confirmed image: " + status)
            if "previous=ota:forced-rollback" not in status:
                raise AssertionError("forced rollback phase was not retained: " + status)
            board.command("store_refresh", timeout=45)
            if board.apps() != baseline_apps:
                raise AssertionError("application storage did not survive OTA/rollback")
            board.note("OTA ACCEPTANCE PASS: upgrade, confirmation, rollback and app persistence")
        finally:
            board.close()


if __name__ == "__main__":
    main()
