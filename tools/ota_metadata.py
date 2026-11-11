"""Test-only A/B NEW-record builder for this project's 8 KiB otadata partition.

Uses the ESP-IDF esp_ota_select_entry_t layout (seq, 20-byte label, state,
CRC32 of seq). Requires a confirmed current slot and preserves its sector.
This is for USB boot-validation injection, not a firmware download protocol.
"""
import binascii
import struct

SECTOR = 4096
NEW, PENDING, VALID, INVALID, ABORTED = range(5)


def pending_metadata(data: bytes, slot: int) -> bytes:
    if len(data) != SECTOR * 2 or slot not in (0, 1):
        raise ValueError('Expected 8 KiB otadata and an A/B slot')
    records = []
    for index in range(2):
        offset = index * SECTOR
        seq, = struct.unpack_from('<I', data, offset)
        state, crc = struct.unpack_from('<II', data, offset + 24)
        expected = binascii.crc32(struct.pack('<I', seq), 0xffffffff) & 0xffffffff
        if 0 < seq < 0xffffffff and crc == expected:
            records.append((seq, state, index))
    bootable = [entry for entry in records if entry[1] in (NEW, PENDING, VALID)]
    if not bootable:
        raise ValueError('No confirmed bootable fallback record')
    seq, state, active = max(bootable)
    if state != VALID:
        raise ValueError('Current image is not confirmed')
    if (seq - 1) % 2 == slot:
        raise ValueError('Refusing to overwrite the confirmed running slot')
    next_seq = max(entry[0] for entry in records) + 1
    if (next_seq - 1) % 2 != slot:
        next_seq += 1
    if next_seq >= 0xffffffff:
        raise ValueError('Sequence rollover requires manual recovery')
    result = bytearray(data)
    target = (1 - active) * SECTOR
    result[target:target + SECTOR] = b'\xff' * SECTOR
    struct.pack_into('<I', result, target, next_seq)
    struct.pack_into('<I', result, target + 24, NEW)
    struct.pack_into('<I', result, target + 28,
                     binascii.crc32(struct.pack('<I', next_seq), 0xffffffff) & 0xffffffff)
    return bytes(result)
