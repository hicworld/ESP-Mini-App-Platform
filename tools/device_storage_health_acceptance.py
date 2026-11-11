"""Check storage health, signed applications and restart persistence.

Does not format storage or install/remove packages. Runs the existing examples
and the board selftest, which uses its own temporary data namespaces.
"""
import argparse
import re
from pathlib import Path

from device_acceptance import Board, EXAMPLES
from device_download_cache_acceptance import require_empty_cache


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='0.3.2')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', encoding='utf-8') as log:
        board = Board(args.port, log)
        try:
            board.connect()
            status = board.command('status')
            if f'version={args.version} ' not in status or 'nand=1 ' not in status:
                raise AssertionError('Expected firmware with mounted NAND')
            baseline = board.command('list')
            if not EXAMPLES <= board.ids():
                raise AssertionError('Expected examples missing')
            for restarted in (False, True):
                if restarted:
                    board.reboot()
                status = board.command('status')
                if f'version={args.version} ' not in status or 'nand=1 ' not in status:
                    raise AssertionError('Firmware or NAND changed after restart')
                board.command('storage', timeout=45)
                board.command('nand')
                board.command('apps_migrate')
                board.command('apps_check')
                require_empty_cache(board)
                # Compare complete ID/version/name lines; Wi-Fi logs may interleave.
                listing = board.command('list')
                app_lines = lambda text: [s for s in text.splitlines() if s.startswith('com.')]
                if app_lines(listing) != app_lines(baseline):
                    raise AssertionError('Installed application metadata changed')
                for app_id in sorted(EXAMPLES):
                    board.running(app_id)
                result = board.command('selftest', timeout=60)
                selftest = re.search(r'SELFTEST RESULT (\d+)/(\d+)', result)
                if not selftest or selftest.group(1) != selftest.group(2):
                    raise AssertionError('Board selftest failed')
            board.command('apps_check')
            board.command('stop')
            board.note('ACCEPTANCE PASS: storage diagnostics, signed reads, three apps, selftest and restart')
        finally:
            board.close()


if __name__ == '__main__':
    main()
