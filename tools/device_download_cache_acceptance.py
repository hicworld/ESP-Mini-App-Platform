"""Exercise NAND-backed HTTPS application downloads on a connected board."""

import argparse
import re
import time
from pathlib import Path

from device_acceptance import Board, EXAMPLES


DEFAULT_BAD_URL = (
    "https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/"
    "does-not-exist-cache-test.app"
)


def wait_status(board, fragment, timeout=12):
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = board.command("status")
        if fragment in last:
            return last
        time.sleep(0.5)
    raise AssertionError(f"status did not contain {fragment!r}: {last}")


def require_empty_cache(board):
    output = board.command("cache")
    expected = "part=0 part_bytes=0 ready=0 ready_bytes=0"
    if expected not in output:
        raise AssertionError(f"download cache is not empty: {output}")


def command_must_fail(board, command, timeout=45):
    board.note("> " + command)
    board.connection.write((command + "\n").encode("utf-8"))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = board.connection.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        board.note(line)
        if line.startswith("result: "):
            if line == "result: ESP_OK":
                raise AssertionError(f"{command}: unexpectedly succeeded")
            return line
    raise TimeoutError(command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--store-index", type=int, default=2)
    parser.add_argument("--bad-url", default=DEFAULT_BAD_URL)
    parser.add_argument("--version", default="0.3.0")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as log:
        board = Board(args.port, log)
        try:
            board.connect()
            status = board.command("status")
            if f"version={args.version}" not in status:
                raise AssertionError(f"unexpected platform version: {status}")
            require_empty_cache(board)
            baseline = board.ids()
            if not EXAMPLES <= baseline:
                raise AssertionError("example applications are missing before acceptance")

            board.command("store_refresh", timeout=45)
            listing = board.command("store_list")
            if "com.example.snake" not in listing:
                raise AssertionError("Snake is missing from the online catalog")
            board.command(f"store_install {args.store_index}", timeout=45)
            require_empty_cache(board)
            if board.ids() != baseline:
                raise AssertionError("successful update changed the application set")

            board.command("run com.example.snake")
            wait_status(board, "runtime=2")
            board.command("touch 380 350")
            time.sleep(4)
            wait_status(board, "runtime=2")
            board.command("touch 100 100")
            board.command("stop")
            wait_status(board, "runtime=0")

            command_must_fail(board, "install " + args.bad_url)
            require_empty_cache(board)
            if board.ids() != baseline:
                raise AssertionError("failed download changed the application set")

            board.reboot()
            status = board.command("status")
            if f"version={args.version}" not in status:
                raise AssertionError(f"version changed after reboot: {status}")
            require_empty_cache(board)
            if board.ids() != baseline:
                raise AssertionError("applications changed after reboot")
            selftest = board.command("selftest", timeout=45)
            result = re.search(r"SELFTEST RESULT (\d+)/(\d+)", selftest)
            if not result or result.group(1) != result.group(2):
                raise AssertionError("board selftest did not pass all checks")
            board.note("ACCEPTANCE PASS: NAND HTTPS cache, cleanup and reboot recovery")
        finally:
            board.close()


if __name__ == "__main__":
    main()
