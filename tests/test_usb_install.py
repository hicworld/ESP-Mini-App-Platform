import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from install_usb import upload


class Device:
    def __init__(self, reject_commit=False, wrong_offset=False):
        self.data = bytearray()
        self.reply = []
        self.reject_commit = reject_commit
        self.wrong_offset = wrong_offset
        self.commands = []

    def write(self, data):
        command = data.decode().strip()
        self.commands.append(command)
        if command == 'upload_abort':
            self.reply.append(b'UPLOAD ABORTED\r\n')
        elif command.startswith('upload_begin '):
            self.size = int(command.split()[1])
            self.reply.append(f'UPLOAD READY {self.size}\r\n'.encode())
        elif command.startswith('upload_chunk '):
            _, offset, data = command.split()
            assert int(offset) == len(self.data)
            self.data.extend(bytes.fromhex(data))
            self.reply.append(f'UPLOAD ACK {len(self.data)+(1 if self.wrong_offset else 0)}\r\n'.encode())
        elif command == 'upload_commit':
            self.reply.append(b'UPLOAD DONE ESP_ERR_INVALID_CRC\r\n' if self.reject_commit else b'UPLOAD DONE ESP_OK\r\n')

    def readline(self):
        return self.reply.pop(0)


def test_chunked_upload_preserves_package_bytes():
    device = Device()
    package = bytes(range(256))*3+b'last'
    upload(device, package)
    assert bytes(device.data) == package
    assert device.commands[-1] == 'upload_commit'
    assert all(len(line) < 384 for line in device.commands)


@pytest.mark.parametrize('mode', ['signature', 'offset'])
def test_failed_transfer_is_aborted(mode):
    device = Device(reject_commit=mode=='signature', wrong_offset=mode=='offset')
    with pytest.raises(RuntimeError):
        upload(device, b'x'*300)
    assert device.commands[-1] == 'upload_abort'
