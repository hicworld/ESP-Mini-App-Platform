"""Find Node.js for the WASM ABI checker in desktop and terminal builds."""
import os
import shutil
from pathlib import Path


def node_executable():
    """Return an absolute executable path, including when a GUI has a short PATH."""
    candidates = [os.environ.get('NODE_EXE'), shutil.which('node')]
    if os.name == 'nt':
        candidates.extend([
            Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'nodejs/node.exe',
            Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'Programs/nodejs/node.exe',
            Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe',
        ])
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise FileNotFoundError(
        'Node.js is required for the WASM ABI check. Install Node.js or set NODE_EXE '
        'to the absolute path of node.exe, then build again.')
