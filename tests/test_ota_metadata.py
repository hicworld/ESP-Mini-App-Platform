import binascii
import struct
import pytest
from tools.ota_metadata import pending_metadata, SECTOR, NEW, VALID, PENDING, INVALID


def record(data, index, seq, state):
    offset = index * SECTOR
    struct.pack_into('<I', data, offset, seq)
    struct.pack_into('<II', data, offset + 24, state,
                     binascii.crc32(struct.pack('<I', seq), 0xffffffff) & 0xffffffff)


@pytest.mark.parametrize('active,seq', [(0, 1), (1, 2), (0, 11), (1, 12)])
def test_preserves_confirmed_sector_and_selects_opposite_slot(active, seq):
    data = bytearray(b'\xff' * (2 * SECTOR))
    record(data, active, seq, VALID)
    slot = 1 - ((seq - 1) % 2)
    result = pending_metadata(bytes(data), slot)
    assert result[active * SECTOR:(active + 1) * SECTOR] == data[active * SECTOR:(active + 1) * SECTOR]
    target = (1 - active) * SECTOR
    next_seq, = struct.unpack_from('<I', result, target)
    state, crc = struct.unpack_from('<II', result, target + 24)
    assert next_seq > seq and (next_seq - 1) % 2 == slot and state == NEW
    assert crc == binascii.crc32(struct.pack('<I', next_seq), 0xffffffff) & 0xffffffff


@pytest.mark.parametrize('state', [NEW, PENDING, INVALID])
def test_refuses_unconfirmed_fallback(state):
    data = bytearray(b'\xff' * (2 * SECTOR))
    record(data, 0, 1, state)
    with pytest.raises(ValueError):
        pending_metadata(bytes(data), 1)


def test_refuses_active_slot_and_bad_metadata():
    data = bytearray(b'\xff' * (2 * SECTOR))
    record(data, 0, 1, VALID)
    with pytest.raises(ValueError):
        pending_metadata(bytes(data), 0)
    data[28] ^= 1
    with pytest.raises(ValueError):
        pending_metadata(bytes(data), 1)
    with pytest.raises(ValueError):
        pending_metadata(bytes(data[:100]), 1)


def test_invalid_newer_record_does_not_displace_confirmed_fallback():
    data = bytearray(b'\xff' * (2 * SECTOR))
    record(data, 0, 3, VALID)
    record(data, 1, 8, INVALID)
    result = pending_metadata(bytes(data), 1)
    assert result[:SECTOR] == data[:SECTOR]
    assert struct.unpack_from('<I', result, SECTOR)[0] == 10


def test_rejects_newer_pending_record_and_sequence_overflow():
    data = bytearray(b'\xff' * (2 * SECTOR))
    record(data, 0, 1, VALID)
    record(data, 1, 2, PENDING)
    with pytest.raises(ValueError):
        pending_metadata(bytes(data), 1)
    record(data, 1, 0xfffffffe, VALID)
    with pytest.raises(ValueError):
        pending_metadata(bytes(data), 0)
