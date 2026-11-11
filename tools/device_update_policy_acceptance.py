"""Verify online Install/Update/Reinstall/Older policy on a connected board.

The test temporarily installs lower and higher signed variants of one example
application. It restores the original signed package before returning.
"""

import argparse
import re
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization

from app_package import create, verify
from device_acceptance import Board
from install_usb import upload


ROOT = Path(__file__).resolve().parents[1]
STORE_LINE = re.compile(
    r"^(?P<index>\d+) (?P<id>[a-z][a-z0-9.]+) "
    r"(?P<version>\d+\.\d+\.\d+) .+ action=(?P<action>\w+)$"
)
APP_LINE = re.compile(
    r"^(?P<id>[a-z][a-z0-9.]+) (?P<version>\d+\.\d+\.\d+) "
)


def parse_apps(output):
    result = {}
    for line in output.splitlines():
        match = APP_LINE.match(line)
        if match:
            result[match.group("id")] = match.group("version")
    return result


def parse_store(output):
    result = {}
    for line in output.splitlines():
        match = STORE_LINE.match(line)
        if match:
            result[match.group("id")] = {
                "index": int(match.group("index")),
                "version": match.group("version"),
                "action": match.group("action"),
            }
    return result


def neighboring_versions(version):
    major, minor, patch = (int(value) for value in version.split("."))
    if patch:
        lower = (major, minor, patch - 1)
    elif minor:
        lower = (major, minor - 1, 65535)
    elif major:
        lower = (major - 1, 65535, 65535)
    else:
        raise ValueError("catalog version 0.0.0 has no valid lower test version")

    if patch < 65535:
        higher = (major, minor, patch + 1)
    elif minor < 65535:
        higher = (major, minor + 1, 0)
    elif major < 65535:
        higher = (major + 1, 0, 0)
    else:
        raise ValueError("catalog version 65535.65535.65535 has no higher test version")
    return ".".join(map(str, lower)), ".".join(map(str, higher))


def install_bytes(board, label, package):
    board.note(f"> USB install {label}")
    upload(board.connection, package)
    board.note("UPLOAD DONE ESP_OK")


def require_action(board, app_id, expected):
    board.command("store_refresh", timeout=45)
    entries = parse_store(board.command("store_list"))
    if app_id not in entries:
        raise AssertionError(f"{app_id} missing from online directory")
    entry = entries[app_id]
    if entry["action"] != expected:
        raise AssertionError(
            f"expected {expected} for {app_id}, got {entry['action']}: {entries}"
        )
    return entry


def require_empty_cache(board):
    output = board.command("cache")
    expected = "part=0 part_bytes=0 ready=0 ready_bytes=0"
    if expected not in output:
        raise AssertionError(f"download cache is not empty: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="0.3.1")
    parser.add_argument("--app-id", default="com.example.hello")
    parser.add_argument("--package", type=Path, default=ROOT / "dist" / "hello.app")
    parser.add_argument("--private-key", type=Path, default=ROOT / "local" / "dev_private.pem")
    parser.add_argument("--public-key", type=Path, default=ROOT / "firmware" / "main" / "trusted_key.pem")
    args = parser.parse_args()

    public_key = serialization.load_pem_public_key(args.public_key.read_bytes())
    original = args.package.read_bytes()
    manifest, wasm = verify(original, public_key)
    if manifest["id"] != args.app_id:
        raise ValueError(f"package contains {manifest['id']}, expected {args.app_id}")
    private_key = serialization.load_pem_private_key(args.private_key.read_bytes(), password=None)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as log:
        board = Board(args.port, log)
        restore_needed = False
        original_error = None
        try:
            board.connect()
            status = board.command("status")
            if f"version={args.version}" not in status:
                raise AssertionError(f"unexpected platform version: {status}")

            baseline = parse_apps(board.command("list"))
            if args.app_id not in baseline:
                raise AssertionError(f"{args.app_id} must be installed before this test")
            entry = require_action(board, args.app_id, "Reinstall")
            if baseline[args.app_id] != entry["version"]:
                raise AssertionError("installed and catalog versions must initially match")
            if manifest["version"] != entry["version"]:
                raise AssertionError("restore package version does not match the catalog")

            lower, higher = neighboring_versions(entry["version"])
            lower_manifest = dict(manifest, version=lower)
            higher_manifest = dict(manifest, version=higher)
            lower_package = create(lower_manifest, wasm, private_key)
            higher_package = create(higher_manifest, wasm, private_key)

            restore_needed = True
            install_bytes(board, f"{args.app_id} {lower}", lower_package)
            update_entry = require_action(board, args.app_id, "Update")
            board.command(f"store_install {update_entry['index']}", timeout=45)
            if parse_apps(board.command("list")).get(args.app_id) != entry["version"]:
                raise AssertionError("online update did not restore the catalog version")
            require_action(board, args.app_id, "Reinstall")

            install_bytes(board, f"{args.app_id} {higher}", higher_package)
            older_entry = require_action(board, args.app_id, "Older")
            require_empty_cache(board)
            board.command(
                f"store_install {older_entry['index']}",
                expected="ESP_ERR_INVALID_VERSION",
                timeout=10,
            )
            require_empty_cache(board)
            if parse_apps(board.command("list")).get(args.app_id) != higher:
                raise AssertionError("rejected downgrade changed the installed application")

            install_bytes(board, f"restore {args.package.name}", original)
            restore_needed = False
            require_action(board, args.app_id, "Reinstall")
            final_apps = parse_apps(board.command("list"))
            if final_apps != baseline:
                raise AssertionError(f"application state was not restored: {final_apps}")
            board.note("ACCEPTANCE PASS: Update, Reinstall and pre-download Older rejection")
        except Exception as error:  # Preserve the primary failure after best-effort restore.
            original_error = error
        finally:
            if restore_needed and board.connection is not None:
                try:
                    install_bytes(board, f"emergency restore {args.package.name}", original)
                except Exception as restore_error:
                    board.note(f"RESTORE FAILED: {restore_error}")
                    if original_error is None:
                        original_error = restore_error
            board.close()
        if original_error is not None:
            raise original_error


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"ACCEPTANCE FAILED: {error}", file=sys.stderr)
        raise
