"""Exercise a connected board's signed app delivery and lifecycle.

The optional empty-registry test removes only the three example apps and restores
them before returning. It refuses this operation if other apps are installed.
"""
import argparse
import re
import time
from pathlib import Path

import serial

from install_usb import upload

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE_NAMES = ('hello', 'touch', 'snake')
EXAMPLES = {'com.example.'+name for name in EXAMPLE_NAMES}


class Board:
    def __init__(self, port, log):
        self.port, self.log, self.connection = port, log, None

    def note(self, text):
        print(text, flush=True)
        self.log.write(text + '\n')
        self.log.flush()

    def connect(self):
        deadline = time.monotonic() + 25
        while True:
            try:
                self.connection = serial.Serial(self.port, 115200, timeout=0.25, write_timeout=5)
                self.connection.dtr = True
                self.connection.rts = False
                time.sleep(1)
                return
            except serial.SerialException:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)

    def close(self):
        if self.connection:
            self.connection.close()
            self.connection = None

    def command(self, command, expected='ESP_OK', timeout=20):
        self.note('> ' + command)
        self.connection.write((command + '\n').encode('utf-8'))
        lines = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.connection.readline().decode('utf-8', errors='replace').strip()
            if line:
                lines.append(line)
                self.note(line)
                if line.startswith('result: '):
                    if line != f'result: {expected}':
                        raise AssertionError(f'{command}: unexpected result {line}')
                    # UI commands acknowledge enqueueing; allow the main loop to apply them.
                    time.sleep(0.3)
                    return '\n'.join(lines)
        raise TimeoutError(command)

    def ids(self):
        return {line.split()[0] for line in self.command('list').splitlines()
                if re.match(r'^[a-z][a-z0-9.]+ \d+\.\d+\.\d+ ', line)}

    def install(self, name):
        self.note(f'> USB install {name}.app')
        upload(self.connection, (ROOT/'dist'/f'{name}.app').read_bytes())
        self.note('UPLOAD DONE ESP_OK')

    def running(self, app_id):
        self.command('run ' + app_id)
        status = self.command('status')
        if 'runtime=2' not in status:
            raise AssertionError('Application did not enter running state')
        if app_id=='com.example.snake':
            self.command('touch 380 350')
            time.sleep(4)  # Exercise real timer callbacks and LCD drawing until wall collision.
            if 'runtime=2' not in self.command('status'):
                raise AssertionError('Snake crashed during movement')
            self.command('touch 100 100')
        self.command('stop')
        if 'runtime=0' not in self.command('status'):
            raise AssertionError('Application did not stop')

    def reboot(self):
        self.note('> reboot')
        self.connection.write(b'reboot\n')
        time.sleep(0.5)
        self.close()
        time.sleep(3)
        self.connect()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exercise-empty-registry', action='store_true')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', encoding='utf-8') as log:
        board = Board(args.port, log)
        restore_examples = False
        try:
            board.connect()
            baseline = board.ids()
            if args.exercise_empty_registry and not baseline <= EXAMPLES:
                raise RuntimeError('Refusing to remove unrelated installed apps')
            if 'com.example.hello' in baseline:
                board.running('com.example.hello')
                board.note('PASS existing Hello compatibility')
            board.command('bundle 1')
            board.command('bundle 2')
            board.install('hello')
            if not EXAMPLES <= board.ids():
                raise AssertionError('Installed apps missing from registry')
            board.running('com.example.hello')
            board.running('com.example.touch')
            board.running('com.example.snake')
            board.command('library')
            board.command('settings')
            board.command('stop')
            before = board.ids()
            corrupt = bytearray((ROOT/'dist/hello.app').read_bytes())
            corrupt[-1] ^= 1
            try:
                upload(board.connection, corrupt)
            except RuntimeError as error:
                board.note(f'PASS tampered package rejected: {error}')
                # Drain the abort acknowledgement before the next upload.
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if board.connection.readline().strip() == b'UPLOAD ABORTED':
                        break
                else:
                    raise TimeoutError('upload abort')
            else:
                raise AssertionError('Tampered package accepted')
            if board.ids() != before:
                raise AssertionError('Rejected upload changed registry')
            board.running('com.example.hello')
            if args.exercise_empty_registry:
                restore_examples = True
                for app_id in sorted(EXAMPLES):
                    board.command('remove ' + app_id)
                if board.ids():
                    raise AssertionError('Registry is not empty after uninstall')
                board.reboot()
                if board.ids():
                    raise AssertionError('Uninstalled apps were resurrected after reboot')
                board.note('PASS empty registry persists across reboot')
                for name in EXAMPLE_NAMES:
                    board.install(name)
                restore_examples = False
            board.reboot()
            if not EXAMPLES <= board.ids():
                raise AssertionError('Installed apps missing after reboot')
            board.running('com.example.snake')
            board.note('ACCEPTANCE PASS: signed delivery, rejection, lifecycle and persistence')
        finally:
            if restore_examples:
                board.note('Restoring example apps after interrupted empty-registry test')
                if board.connection is None:
                    board.connect()
                board.command('stop')
                for name in EXAMPLE_NAMES:
                    board.install(name)
            board.close()


if __name__ == '__main__':
    main()
