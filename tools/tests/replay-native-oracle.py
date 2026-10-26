"""Test-only transport for the existing fixed native-membership oracle.

Production replay-native-slot.py accepts only the canonical inspector. This
helper narrowly admits the existing generated oracle and uses the same contained
Slot/Job transport. It is not a general command or generated-script launcher.
"""
from pathlib import Path
import importlib.util
import os
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / '.cache/arena-tests/replay-vision'
REPLAY = ROOT / 'tools/tests/fixtures/padded-vision-cli-3.0.4.repl26a'
spec = importlib.util.spec_from_file_location(
    'replay_native_slot_for_test_oracle', ROOT / 'tools/replay-native-slot.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


def _fold(path):
    return os.path.normcase(os.path.abspath(path))


def _direct_file(value):
    path = Path(os.path.abspath(value))
    if not path.is_file() or path.is_symlink() or _fold(path) != _fold(path.resolve(strict=True)):
        raise ValueError('Test oracle inputs must be direct regular files')
    return path


def validate_argv(argv):
    argv = list(argv)
    if len(argv) != 15 or argv[1] != '--disable-warning=ExperimentalWarning':
        raise ValueError('Expected the fixed native-membership test invocation')
    if _fold(_direct_file(argv[0])) != _fold(_direct_file(bridge.core.NODE)):
        raise ValueError('Test oracle requires the configured arena Node')
    generated = _direct_file(argv[2])
    if (_fold(SCRATCH) != _fold(SCRATCH.resolve(strict=True)) or
            _fold(generated.parent) != _fold(SCRATCH) or
            re.fullmatch(r'native-membership-[0-9]+\.mjs', generated.name) is None):
        raise ValueError('Test oracle requires its fixed generated-file location')
    tail = ['--replay', str(REPLAY), '--actor', '6', '--team', '1',
            '--from-round', '500', '--to-round', '500', '--captures', '0']
    if argv[3:] != tail or _fold(_direct_file(argv[4])) != _fold(_direct_file(REPLAY)):
        raise ValueError('Test oracle arguments differ from the fixed retained case')
    return argv


def main(argv=None):
    return bridge._run_validated(validate_argv(sys.argv[1:] if argv is None else argv))


if __name__ == '__main__':
    raise SystemExit(main())
