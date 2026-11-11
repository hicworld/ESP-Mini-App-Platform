"""Verify built application modules match ABI v1 and the runtime load policy.

Two passes: first the checker proves it can still reject every violation it
claims to detect, then it checks every module in dist/. A new language backend
can compile cleanly and still produce a module the device refuses, which is
exactly what this catches.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT/'tools/abi_check.mjs'


def run(*args):
    return subprocess.run(['node', str(CHECKER), *args]).returncode


def main():
    modules = sorted((ROOT/'dist').glob('*.wasm'))
    if not modules:
        print('No built modules in dist/; build an application first.')
        return 1
    if run('--selftest'):
        return 1
    return run(*(str(m) for m in modules))


if __name__ == '__main__':
    sys.exit(main())
