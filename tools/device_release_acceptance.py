"""Run a non-destructive release acceptance suite on a connected board."""

import argparse
import re
import time
from pathlib import Path

from device_acceptance import Board
from device_download_cache_acceptance import require_empty_cache


EXPECTED_APPS = {
    "com.example.hello": "0.1.0",
    "com.example.touch": "0.1.0",
    "com.example.snake": "0.1.1",
    "com.example.garden": "0.1.5",
    "com.example.bounce": "0.1.1",
    "com.example.rustbounce": "0.1.1",
}
EXPECTED_STORE_NAMES = {"Hello WASM", "Touch Paint", "Snake", "Pocket Garden"}


def require_status(board, version, runtime=None):
    status = board.command("status")
    # phase records the latest diagnostic checkpoint (for example
    # runtime:app-init); it is not a current-state enum and need not stay ready.
    required = [f"version={version}", "ota_state=2", "nand=1", "nand_mount=ESP_OK"]
    if runtime is not None:
        required.append(f"runtime={runtime}")
    missing = [fragment for fragment in required if fragment not in status]
    if missing:
        raise AssertionError(f"status missing {missing}: {status}")
    return status


def installed_apps(board):
    listing = board.command("list")
    apps = {}
    for line in listing.splitlines():
        match = re.match(r"^(com\.[a-z0-9.]+) (\d+\.\d+\.\d+) ", line)
        if match:
            apps[match.group(1)] = match.group(2)
    return apps


def require_language(board, expected=None):
    output = board.command("language")
    match = re.search(r"^language=(zh|en)$", output, re.MULTILINE)
    if not match:
        raise AssertionError(f"language query failed: {output}")
    language = match.group(1)
    if expected is not None and language != expected:
        raise AssertionError(f"expected language {expected}, got {language}")
    return language


def run_app(board, app_id, version):
    board.command("run " + app_id)
    require_status(board, version, runtime=2)
    if app_id == "com.example.snake":
        board.command("touch 380 350")
        time.sleep(4)
    elif app_id == "com.example.garden":
        time.sleep(2)
        first = board.command("audio_status")
        time.sleep(1)
        second = board.command("audio_status")
        codec = re.search(r"codec=(-?\d+)", second)
        blocks_first = re.search(r"blocks=(\d+)", first)
        blocks_second = re.search(r"blocks=(\d+)", second)
        if not codec or codec.group(1) != "0":
            raise AssertionError(f"Garden codec is not ready: {second}")
        if not blocks_first or not blocks_second or int(blocks_second.group(1)) <= int(blocks_first.group(1)):
            raise AssertionError(f"Garden audio did not advance: {first}\n{second}")
    else:
        time.sleep(2)
    require_status(board, version, runtime=2)
    board.command("stop")
    require_status(board, version, runtime=0)


def run_suite(args):
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as log:
        board = Board(args.port, log)
        try:
            board.connect()
            require_status(board, args.version, runtime=0)
            original_language = require_language(board)
            board.command("hardware")
            board.command("storage", timeout=45)
            board.command("nand")
            board.command("nand_test", timeout=45)
            board.command("apps_check", timeout=45)
            require_empty_cache(board)

            apps = installed_apps(board)
            if apps != EXPECTED_APPS:
                raise AssertionError(f"unexpected installed applications: {apps}")

            selftest = board.command("selftest", timeout=90)
            match = re.search(r"SELFTEST RESULT (\d+)/(\d+)", selftest)
            if not match or match.group(1) != match.group(2) or int(match.group(2)) < 26:
                raise AssertionError(f"board selftest failed: {selftest}")

            board.command("store_refresh", timeout=45)
            store = board.command("store_list")
            names = {
                match.group(1)
                for line in store.splitlines()
                if (match := re.match(r"^\d+ com\.[a-z0-9.]+ \d+\.\d+\.\d+ (.+) action=", line))
            }
            if names != EXPECTED_STORE_NAMES or "com.example.snake 0.1.1 Snake action=Reinstall" not in store:
                raise AssertionError(f"unexpected online catalog: {store}")

            for app_id in EXPECTED_APPS:
                run_app(board, app_id, args.version)

            board.command("wifi_scan")
            deadline = time.monotonic() + 25
            while True:
                scan = board.command("wifi_list")
                match = re.search(r"WIFI SCAN busy=(\d+) count=(\d+) result=(\w+)", scan)
                if match and match.group(1) == "0":
                    if match.group(3) != "ESP_OK" or int(match.group(2)) == 0:
                        raise AssertionError(f"Wi-Fi scan failed: {scan}")
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("Wi-Fi scan")
                time.sleep(1)

            alternate = "en" if original_language == "zh" else "zh"
            board.command("language " + alternate)
            require_language(board, alternate)
            board.reboot()
            require_status(board, args.version, runtime=0)
            require_language(board, alternate)
            board.command("language " + original_language)
            require_language(board, original_language)
            board.reboot()
            require_status(board, args.version, runtime=0)
            require_language(board, original_language)

            if installed_apps(board) != EXPECTED_APPS:
                raise AssertionError("installed applications changed after restart")
            board.command("apps_check", timeout=45)
            require_empty_cache(board)
            board.note("ACCEPTANCE PASS: release candidate, six apps, online store, audio, Wi-Fi, language and reboot persistence")
        finally:
            board.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--version", default="0.4.9")
    parser.add_argument("--output", type=Path, default=Path("local/device-049-release-acceptance.log"))
    ARGS = parser.parse_args()
    run_suite(ARGS)
