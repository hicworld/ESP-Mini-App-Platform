"""Write a test image to an OTA slot and select it without touching app storage.

ESP-IDF 6.1 preview's otatool passes positional arguments to a newer
ParttoolTarget signature, so repeated esptool calls reset the USB port.  This
wrapper uses the same official classes with keyword arguments and keeps the ROM
download session alive until both the image and otadata are durable.
"""
import argparse
import sys
import tempfile
import time
from pathlib import Path
from ota_metadata import pending_metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--partition-table", type=Path, required=True)
    parser.add_argument("--slot", type=int, choices=(0, 1), required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--pending-verify", action="store_true",
                        help="Inject a NEW OTA record, preserving a confirmed opposite slot")
    parser.add_argument("--metadata-only", action="store_true",
                        help="Resume after a verified image write; read back image before changing otadata")
    args = parser.parse_args()

    partition_tools = args.idf / "components" / "partition_table"
    app_update = args.idf / "components" / "app_update"
    sys.path[:0] = [str(partition_tools), str(app_update)]
    from parttool import ParttoolTarget
    from otatool import OtatoolTarget, SPI_FLASH_SEC_SIZE

    class StableParttoolTarget(ParttoolTarget):
        def _call_esptool(self, command, out=None):
            result = super()._call_esptool(command, out)
            # USB-OTG COM enumeration remains present, but Windows needs a
            # moment to release the handle between esptool subprocesses.
            time.sleep(2)
            return result

        def write_partition(self, partition_id, input, ignore_readonly=False):
            partition = self.get_partition_info(partition_id)
            if partition.readonly and not ignore_readonly:
                raise ValueError('Refusing to write a read-only partition')
            if Path(input).stat().st_size > partition.size:
                raise ValueError('Image exceeds partition size')
            # write-flash erases precisely the covered sectors. Avoid a separate
            # erase operation and USB stub resets between each operation.
            self._call_esptool(['write-flash', str(partition.offset), str(input)])

    if not args.partition_table.is_file() or not args.image.is_file():
        parser.error("partition table and image must exist")
    if args.metadata_only and not args.pending_verify:
        parser.error("--metadata-only requires --pending-verify")

    parttool = StableParttoolTarget(
        port=args.port,
        baud=args.baud,
        partition_table_file=str(args.partition_table),
        esptool_args=["chip=esp32s31", "before=no-reset", "after=no-reset", "no-stub"],
    )
    ota = object.__new__(OtatoolTarget)
    ota.target = parttool
    ota.spi_flash_sec_size = SPI_FLASH_SEC_SIZE

    with tempfile.NamedTemporaryFile(delete=False) as temporary:
        otadata_path = Path(temporary.name)
    try:
        parttool.read_partition(OtatoolTarget.OTADATA_PARTITION, str(otadata_path))
        ota.otadata = otadata_path.read_bytes()
    finally:
        otadata_path.unlink(missing_ok=True)

    # Validate metadata before writing any flash. This test must not overwrite
    # the running confirmed image that the bootloader needs for rollback.
    next_metadata = pending_metadata(ota.otadata, args.slot) if args.pending_verify else None
    if args.metadata_only:
        with tempfile.NamedTemporaryFile(delete=False) as temporary:
            readback_path = Path(temporary.name)
        try:
            parttool.read_partition(ota._get_partition_id_from_ota_id(args.slot),str(readback_path))
            expected=args.image.read_bytes()
            with readback_path.open('rb') as readback:
                if readback.read(len(expected))!=expected:
                    raise ValueError('OTA slot does not match the expected image')
        finally:
            readback_path.unlink(missing_ok=True)
        print('Existing OTA image readback matches the expected image',flush=True)
    else:
        print(f"Writing {args.image.name} to ota_{args.slot}", flush=True)
        ota.write_ota_partition(args.slot, str(args.image))
    print(f"Selecting ota_{args.slot}", flush=True)
    # Reboot only after both image verification and metadata write complete.
    parttool.esptool_args[parttool.esptool_args.index('no-reset',
                           parttool.esptool_args.index('--after'))] = 'hard-reset'
    if next_metadata is None:
        ota.switch_ota_partition(args.slot)
    else:
        with tempfile.NamedTemporaryFile(delete=False) as temporary:
            temporary.write(next_metadata)
            metadata_path = Path(temporary.name)
        try:
            parttool.write_partition(OtatoolTarget.OTADATA_PARTITION, str(metadata_path))
        finally:
            metadata_path.unlink(missing_ok=True)
        print("Selected NEW image; bootloader must enter pending verification", flush=True)
    print("OTA slot write and selection complete", flush=True)


if __name__ == "__main__":
    main()
