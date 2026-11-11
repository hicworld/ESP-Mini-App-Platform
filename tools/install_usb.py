"""Install a signed EMAP package over USB CDC, without flashing system firmware."""
import argparse
import time
from pathlib import Path

import serial
from cryptography.hazmat.primitives import serialization
from app_package import verify

ROOT = Path(__file__).resolve().parents[1]


def exchange(connection, command, expected, timeout=12):
    connection.write((command + '\n').encode('ascii'))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = connection.readline().decode('utf-8', errors='replace').strip()
        if line.startswith('UPLOAD ERROR') or line.startswith('UPLOAD DONE') or line.startswith(expected.split()[0] + ' '):
            if line == expected:
                return
            raise RuntimeError(f'Device rejected transfer: {line}')
    raise TimeoutError(f'No acknowledgement for {command.split()[0]}')


def upload(connection, package):
    try:
        exchange(connection, 'upload_abort', 'UPLOAD ABORTED')
        exchange(connection, f'upload_begin {len(package)}', f'UPLOAD READY {len(package)}')
        for offset in range(0, len(package), 128):
            chunk = package[offset:offset+128]
            exchange(connection, f'upload_chunk {offset} {chunk.hex()}', f'UPLOAD ACK {offset+len(chunk)}')
        exchange(connection, 'upload_commit', 'UPLOAD DONE ESP_OK', timeout=45)
    except Exception:
        try:
            connection.write(b'upload_abort\n')
        except serial.SerialException:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--port', required=True)
    parser.add_argument('--public-key', type=Path, default=ROOT/'firmware/main/trusted_key.pem')
    args = parser.parse_args()
    package = args.package.read_bytes()
    key = serialization.load_pem_public_key(args.public_key.read_bytes())
    manifest, _ = verify(package, key)
    with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=5) as connection:
        connection.dtr = True
        connection.rts = False
        time.sleep(0.5)
        upload(connection, package)
    print(f'Installed {manifest["id"]} {manifest["version"]} ({len(package)} bytes)')


if __name__ == '__main__':
    main()
