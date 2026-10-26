"""Stubbed replay bridge containment/output tests; no Node, decoder or native process.

The real reviewed _slotted_command/_command functions run against stateful fake
Slot and private-Job process objects. Run only after review with the pinned Python:
    python -B tools/tests/test-replay-native-slot.py
"""
from pathlib import Path
import importlib.util
import io
import json
import os
import subprocess
import shutil
import threading
import unittest
import uuid
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'replay_native_slot_under_test', PROJECT / 'tools/replay-native-slot.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class State:
    def __init__(self):
        self.held = False
        self.enters = 0
        self.exits = 0
        self.slots = []
        self.processes = []
        self.enter_error = None
        self.wait_error = None
        self.terminate_failures = 0
        self.code = 0
        self.timeout = False
        self.stdout = b'raw\x00stdout\xff\r\n'
        self.stderr = b'raw\x00stderr\xfe\r\n'
        self.events = []
        self.expected_parent_env = os.environ.get('ARENA_REPLAY_SLOT')


class GuardedBuffer(io.BytesIO):
    def __init__(self, state):
        super().__init__()
        self.state = state

    def write(self, value):
        if self.state.held or any(not p.empty_confirmed for p in self.state.processes):
            raise AssertionError('Output forwarded before exact owned cleanup')
        if bridge.shared._PENDING_CLEANUP:
            raise AssertionError('Output forwarded with pending owned handles')
        self.state.events.append('forward')
        return super().write(value)


class BridgeFixtures(unittest.TestCase):
    def setUp(self):
        scratch = PROJECT / '.cache/tools-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.base = scratch / ('replay-slot-' + uuid.uuid4().hex)
        self.base.mkdir()
        self.addCleanup(shutil.rmtree, self.base)
        self.state = State()
        state = self.state
        shared = bridge.shared

        class FakeSlots:
            def __init__(self, want, quiet=False, **kwargs):
                if want != 1:
                    raise AssertionError('Bridge requested more than its one real slot')
                self.count = 1
                self.quiet = quiet
                state.slots.append(self)

            def __enter__(self):
                if os.environ.get('ARENA_REPLAY_SLOT') != state.expected_parent_env:
                    raise AssertionError('Parent environment changed while acquiring Slot')
                state.enters += 1
                state.events.append('slot-enter')
                if state.enter_error is not None:
                    raise state.enter_error
                if state.held:
                    raise AssertionError('Nested Slot acquisition')
                state.held = True
                return self

            def __exit__(self, *args):
                if os.environ.get('ARENA_REPLAY_SLOT') != state.expected_parent_env:
                    raise AssertionError('Parent environment changed during cleanup')
                if not state.held:
                    raise AssertionError('Slot released twice')
                if any(not p.empty_confirmed for p in state.processes):
                    raise AssertionError('Slot released before Job/process empty')
                state.held = False
                state.exits += 1
                state.events.append('slot-exit')

        class FakeProcess:
            def __init__(self, argv, cwd, stdout, stderr):
                if not state.held:
                    raise AssertionError('Process started without real Slot')
                if len(argv) < 3 or argv[1] != bridge.CHILD_SLOT_IMPORT:
                    raise AssertionError('Child-only recursion marker missing or changed')
                if os.environ.get('ARENA_REPLAY_SLOT') != state.expected_parent_env:
                    raise AssertionError('Child launch changed the parent environment')
                self.argv, self.cwd = list(argv), Path(cwd)
                self.pid = 9000 + len(state.processes)
                self.returncode = None
                self.assigned = self.resumed = True
                self.empty_confirmed = self.forced = self.closed = False
                self.terminate_calls = self.close_calls = 0
                self.wait_timeout = None
                state.processes.append(self)
                state.events.append('process-start')
                stdout.write(state.stdout)
                stderr.write(state.stderr)

            def wait(self, timeout):
                if not state.held:
                    raise AssertionError('Wait started after releasing Slot')
                self.wait_timeout = timeout
                state.events.append('wait')
                if state.timeout:
                    raise subprocess.TimeoutExpired(self.argv, timeout)
                if state.wait_error is not None:
                    raise state.wait_error
                self.returncode = state.code
                self.empty_confirmed = True
                state.events.append('empty')
                return self.returncode

            def terminate_tree(self):
                if os.environ.get('ARENA_REPLAY_SLOT') != state.expected_parent_env:
                    raise AssertionError('Recovery changed the parent environment')
                self.terminate_calls += 1
                state.events.append('terminate')
                if state.terminate_failures:
                    state.terminate_failures -= 1
                    raise OSError('stub Job termination still unconfirmed')
                self.forced = True
                self.empty_confirmed = True
                self.returncode = 1
                state.events.append('empty')

            def record(self):
                return {'kind': 'windows-private-job', 'initial_pid': self.pid,
                        'assigned_before_resume': self.assigned, 'resumed': self.resumed,
                        'kill_on_close': True, 'empty_confirmed': self.empty_confirmed,
                        'forced_termination': self.forced,
                        'observed_owned_pids': [self.pid]}

            def close(self):
                if not self.empty_confirmed:
                    raise AssertionError('Closed live owned handles')
                self.closed = True
                self.close_calls += 1
                state.events.append('close')

        self.addCleanup(mock.patch.stopall)
        mock.patch.object(shared, '_PENDING_CLEANUP', []).start()
        mock.patch.object(shared.core, 'Slots', FakeSlots).start()
        if bridge.core is not shared.core:
            mock.patch.object(bridge.core, 'Slots', FakeSlots).start()
        mock.patch.object(shared, '_WinJobProcess', FakeProcess).start()
        mock.patch.object(bridge, 'RAW_ROOT', self.base / 'raw').start()
        self.argv = [str(bridge.core.NODE), '--disable-warning=ExperimentalWarning',
                     str(PROJECT / 'tools/inspect-strategy-replay.mjs'),
                     '--replay', str(self.base / 'retained replay.repl26a'),
                     '--actor', '0', '--team', '0', '--captures', '0']
        self.out, self.err = GuardedBuffer(state), GuardedBuffer(state)

    def run_bridge(self, argv=None, stdout=None, stderr=None):
        self.state.expected_parent_env = os.environ.get('ARENA_REPLAY_SLOT')
        return bridge.run(self.argv if argv is None else argv,
                          stdout=self.out if stdout is None else stdout,
                          stderr=self.err if stderr is None else stderr)

    def raw_directory(self):
        directories = list((self.base / 'raw').iterdir())
        self.assertEqual(len(directories), 1)
        self.assertTrue(directories[0].is_dir())
        return directories[0]

    def assert_finished(self):
        self.assertFalse(self.state.held)
        self.assertEqual(self.state.enters, 1)
        self.assertEqual(self.state.exits, 1)
        self.assertEqual(len(self.state.processes), 1)
        self.assertTrue(self.state.processes[0].closed)
        self.assertEqual(self.state.processes[0].close_calls, 1)
        self.assertEqual(bridge.shared._PENDING_CLEANUP, [])

    def test_nonzero_child_preserves_argv_binary_streams_and_single_slot(self):
        self.state.code = 7
        self.assertEqual(bridge.CHILD_SLOT_IMPORT,
                         '--import=data:text/javascript,process.env.ARENA_REPLAY_SLOT%3D%271%27%3B')
        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'outer'}):
            self.assertEqual(self.run_bridge(), 7)
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'outer')
        self.assert_finished()
        process = self.state.processes[0]
        launched = bridge._child_argv(self.argv)
        self.assertEqual(launched, [self.argv[0], bridge.CHILD_SLOT_IMPORT, *self.argv[1:]])
        self.assertEqual(process.argv, launched)
        self.assertEqual(process.cwd, PROJECT)
        self.assertEqual(process.wait_timeout, 120)
        self.assertEqual(bridge.WALL_TIMEOUT_SECONDS, 120)
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)
        dispatch = json.loads((raw / 'dispatch.json').read_text())
        self.assertEqual(dispatch['argv'], launched)
        self.assertEqual(dispatch['timeout'], 120)
        end = json.loads((raw / 'process-end.json').read_text())
        self.assertEqual(end['returncode'], 7)
        self.assertTrue(end['tree_exit_confirmed'])
        result = json.loads((raw / 'bridge-result.json').read_text())
        self.assertEqual(result['argv'], self.argv)
        self.assertEqual(result['launchedArgv'], launched)
        self.assertTrue(result['streamsForwarded'])
        self.assertLess(self.state.events.index('slot-exit'), self.state.events.index('forward'))

    def test_success_removes_only_both_fully_forwarded_streams_and_keeps_metadata(self):
        unrelated = self.base / 'unrelated.bin'
        unrelated.write_bytes(b'caller-owned input')
        other = self.base / 'raw' / 'unrelated-invocation'
        other.mkdir(parents=True)
        (other / 'stdout.bin').write_bytes(b'other output')
        dispatch = bridge.shared._slotted_command
        directory = None
        flushes = []

        def command(*args, **kwargs):
            nonlocal directory
            record = dispatch(*args, **kwargs)
            directory = args[2]
            (directory / 'keep.bin').write_bytes(b'compact sibling')
            # Cleanup must use exact staging leaves, never record-supplied paths.
            record['stdout']['path'] = str(unrelated)
            return record

        class FlushBuffer(GuardedBuffer):
            def flush(buffer):
                flushes.append(tuple((directory / name).exists() for name in ('stdout.bin', 'stderr.bin')))
                return super().flush()

        out, err = FlushBuffer(self.state), FlushBuffer(self.state)
        with mock.patch.object(bridge.shared, '_slotted_command', side_effect=command):
            self.assertEqual(self.run_bridge(stdout=out, stderr=err), 0)
        self.assert_finished()
        self.assertEqual(out.getvalue(), self.state.stdout)
        self.assertEqual(err.getvalue(), self.state.stderr)
        self.assertEqual(flushes, [(True, True), (True, True)])
        self.assertFalse((directory / 'stdout.bin').exists())
        self.assertFalse((directory / 'stderr.bin').exists())
        for name in ('dispatch.json', 'process-start.json', 'process-end.json', 'bridge-result.json'):
            self.assertTrue((directory / name).is_file(), name)
        result = json.loads((directory / 'bridge-result.json').read_text())
        self.assertTrue(result['streamsForwarded'])
        self.assertEqual((directory / 'keep.bin').read_bytes(), b'compact sibling')
        self.assertEqual(unrelated.read_bytes(), b'caller-owned input')
        self.assertEqual((other / 'stdout.bin').read_bytes(), b'other output')

    def test_short_writes_are_completed_before_successful_cleanup(self):
        self.state.stdout = bytes(range(256)) * 513

        class ShortBuffer(GuardedBuffer):
            def write(buffer, value):
                return super().write(value[:257])

        out, err = ShortBuffer(self.state), ShortBuffer(self.state)
        self.assertEqual(self.run_bridge(stdout=out, stderr=err), 0)
        self.assert_finished()
        self.assertEqual(out.getvalue(), self.state.stdout)
        self.assertEqual(err.getvalue(), self.state.stderr)
        raw = self.raw_directory()
        self.assertFalse((raw / 'stdout.bin').exists() or (raw / 'stderr.bin').exists())

    def test_zero_write_retains_both_streams_after_confirmed_cleanup(self):
        class ZeroBuffer(GuardedBuffer):
            def write(buffer, value):
                return 0

        with self.assertRaisesRegex(OSError, 'did not accept'):
            self.run_bridge(stderr=ZeroBuffer(self.state))
        self.assert_finished()
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)
        self.assertFalse(json.loads((raw / 'bridge-result.json').read_text())['streamsForwarded'])

    def test_unconfirmed_receipt_retains_streams_and_does_not_forward(self):
        dispatch = bridge.shared._slotted_command

        def unconfirmed(*args, **kwargs):
            record = dispatch(*args, **kwargs)
            record['tree_exit_confirmed'] = False
            return record

        with mock.patch.object(bridge.shared, '_slotted_command', side_effect=unconfirmed):
            with self.assertRaisesRegex(RuntimeError, 'not confirmed empty'):
                self.run_bridge()
        self.assert_finished()
        self.assertEqual(self.out.getvalue(), b'')
        self.assertEqual(self.err.getvalue(), b'')
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)
        result = json.loads((raw / 'bridge-result.json').read_text())
        self.assertFalse(result['stopped'] or result['streamsForwarded'])

    def test_missing_parent_recursion_flag_stays_missing_and_raw_paths_are_fresh(self):
        with mock.patch.dict(os.environ):
            os.environ.pop('ARENA_REPLAY_SLOT', None)
            self.assertEqual(self.run_bridge(), 0)
            self.assertNotIn('ARENA_REPLAY_SLOT', os.environ)
            self.assertEqual(self.run_bridge(), 0)
            self.assertNotIn('ARENA_REPLAY_SLOT', os.environ)
        directories = list((self.base / 'raw').iterdir())
        self.assertEqual(len(directories), 2)
        self.assertEqual(self.state.enters, 2)
        self.assertEqual(self.state.exits, 2)
        self.assertEqual(len(self.state.processes), 2)

    def test_unknown_cleanup_retains_same_process_and_slot_until_confirmed(self):
        signal = KeyboardInterrupt('stub parent interruption')
        self.state.wait_error = signal
        self.state.terminate_failures = 1
        confirm = bridge.shared.confirm_pending_cleanup
        seen = []

        def confirm_after_one_wait():
            self.assertTrue(self.state.held)
            self.assertEqual(os.environ.get('ARENA_REPLAY_SLOT'), 'before')
            pending = bridge.shared._PENDING_CLEANUP
            self.assertEqual(len(pending), 1)
            self.assertIs(pending[0]['process'], self.state.processes[0])
            self.assertIs(pending[0]['slots'], self.state.slots[0])
            self.assertIs(pending[0]['error'], signal)
            self.assertFalse(self.out.getvalue() or self.err.getvalue())
            seen.append(pending[0])
            return False if len(seen) == 1 else confirm()

        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'before'}), \
                mock.patch.object(bridge.shared, 'confirm_pending_cleanup', side_effect=confirm_after_one_wait), \
                mock.patch.object(bridge.time, 'sleep', side_effect=KeyboardInterrupt('sleep interrupted')):
            with self.assertRaises(KeyboardInterrupt) as caught:
                self.run_bridge()
            self.assertIs(caught.exception, signal)
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'before')
        self.assertEqual(len(seen), 2)
        self.assertIs(seen[0], seen[1])
        self.assert_finished()
        self.assertEqual(self.state.processes[0].terminate_calls, 2)
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)
        self.assertTrue((self.raw_directory() / 'cleanup-confirmed.json').is_file())

    def test_known_cleanup_exception_is_the_original_object_with_notes(self):
        original = RuntimeError('stub wait failure')
        original.add_note('original caller note')
        self.state.wait_error = original
        with self.assertRaises(RuntimeError) as caught:
            self.run_bridge()
        self.assertIs(caught.exception, original)
        self.assertIn('original caller note', caught.exception.__notes__)
        self.assertTrue(caught.exception.compiled_process_record['tree_exit_confirmed'])
        self.assert_finished()
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)

    def test_timeout_returns_124_only_after_confirmed_cleanup_and_raw_output(self):
        self.state.timeout = True
        self.assertEqual(self.run_bridge(), 124)
        self.assert_finished()
        end = json.loads((self.raw_directory() / 'process-end.json').read_text())
        self.assertTrue(end['timed_out'])
        self.assertTrue(end['forced_termination'])
        self.assertTrue(end['tree_exit_confirmed'])
        self.assertEqual(self.state.processes[0].wait_timeout, 120)
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)

    def test_slot_enter_interruption_keeps_parent_environment_without_launch(self):
        original = KeyboardInterrupt('stub waiting for machine-wide Slot')
        self.state.enter_error = original
        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'old'}):
            with self.assertRaises(KeyboardInterrupt) as caught:
                self.run_bridge()
            self.assertIs(caught.exception, original)
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'old')
        self.assertEqual(self.state.processes, [])
        self.assertEqual(self.state.exits, 0)
        self.assertFalse(self.state.held)
        self.assertEqual(self.out.getvalue(), b'')
        self.assertEqual(self.err.getvalue(), b'')

    def test_invalid_node_script_and_code_loading_flags_never_acquire_a_slot(self):
        node, _, inspector, *tail = self.argv
        cases = [[str(Path(node).with_name('other-node.exe')), inspector, *tail],
                 [node, str(PROJECT / 'tools/arbitrary.mjs'), *tail]]
        for flag in ['--eval', '-e', '--print', '-p', '--require', '--import', '--loader']:
            cases.append([node, flag, 'arbitrary-code', inspector, *tail])
        cases.append([node, bridge.CHILD_SLOT_IMPORT, inspector, *tail])
        for argv in cases:
            with self.subTest(argv=argv):
                with self.assertRaises((ValueError, SystemExit)):
                    self.run_bridge(argv)
        self.assertEqual(self.state.enters, 0)
        self.assertEqual(self.state.processes, [])

    def test_broken_output_pipe_happens_after_cleanup_without_relaunch(self):
        class BrokenBuffer(GuardedBuffer):
            def write(self, value):
                super().write(value)
                raise BrokenPipeError('stub consumer closed its pipe')

        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'before'}):
            with self.assertRaises(BrokenPipeError):
                self.run_bridge(stdout=BrokenBuffer(self.state))
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'before')
        self.assert_finished()
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)

    def test_stderr_flush_failure_retains_both_streams_and_original_error(self):
        original = BrokenPipeError('stub stderr flush failed')

        class FlushFailure(GuardedBuffer):
            def flush(buffer):
                raise original

        err = FlushFailure(self.state)
        with self.assertRaises(BrokenPipeError) as caught:
            self.run_bridge(stderr=err)
        self.assertIs(caught.exception, original)
        self.assert_finished()
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(err.getvalue(), self.state.stderr)
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)
        self.assertFalse(json.loads((raw / 'bridge-result.json').read_text())['streamsForwarded'])

    def test_forwarding_metadata_failure_retains_successful_child_streams(self):
        write = bridge.shared._write
        original = OSError('stub forwarding metadata publication failed')

        def fail_forwarded(path, value):
            if value.get('schema') == 'arena-replay-inspector-bridge/v1' and value.get('streamsForwarded'):
                raise original
            return write(path, value)

        with mock.patch.object(bridge.shared, '_write', side_effect=fail_forwarded):
            with self.assertRaises(OSError) as caught:
                self.run_bridge()
        self.assertIs(caught.exception, original)
        self.assert_finished()
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)

    def test_initial_metadata_error_retains_streams_and_is_preserved_in_final_metadata(self):
        write = bridge.shared._write
        original = OSError('stub initial metadata publication failed')

        def fail_initial(path, value):
            if path.name == 'bridge-result.json':
                raise original
            return write(path, value)

        with mock.patch.object(bridge.shared, '_write', side_effect=fail_initial):
            with self.assertRaises(OSError) as caught:
                self.run_bridge()
        self.assertIs(caught.exception, original)
        self.assert_finished()
        raw = self.raw_directory()
        self.assertEqual((raw / 'stdout.bin').read_bytes(), self.state.stdout)
        self.assertEqual((raw / 'stderr.bin').read_bytes(), self.state.stderr)
        result = json.loads((raw / 'bridge-result.json').read_text())
        self.assertTrue(result['streamsForwarded'])
        self.assertEqual(result['error'], repr(original))

    def assert_stream_cleanup_error(self, failed_name):
        unlink = Path.unlink
        original = PermissionError('stub staging stream remains locked')

        def fail_selected(path, *args, **kwargs):
            if path.name == failed_name and path.parent.parent == bridge.RAW_ROOT:
                raise original
            return unlink(path, *args, **kwargs)

        with mock.patch.object(Path, 'unlink', new=fail_selected):
            self.assertEqual(self.run_bridge(), 0)
        self.assert_finished()
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)
        raw = self.raw_directory()
        result = json.loads((raw / 'bridge-result.json').read_text())
        self.assertTrue(result['streamsForwarded'])
        self.assertIsNone(result['error'])
        self.assertEqual(result['streamCleanupError'], {'stream': failed_name, 'error': repr(original)})
        self.assertEqual((raw / failed_name).read_bytes(),
                         self.state.stdout if failed_name == 'stdout.bin' else self.state.stderr)
        self.assertEqual((raw / 'stdout.bin').exists(), failed_name == 'stdout.bin')
        self.assertTrue((raw / 'stderr.bin').exists())
        self.assertTrue((raw / 'process-end.json').is_file())

    def test_first_unlink_failure_keeps_successful_transport_and_both_streams(self):
        self.assert_stream_cleanup_error('stdout.bin')

    def test_second_unlink_failure_keeps_successful_transport_and_remaining_stream(self):
        self.assert_stream_cleanup_error('stderr.bin')

    def test_other_import_can_recover_owned_process_before_owner_snapshot(self):
        second_spec = importlib.util.spec_from_file_location(
            'replay_native_slot_other_recovery', PROJECT / 'tools/replay-native-slot.py')
        second = importlib.util.module_from_spec(second_spec)
        second_spec.loader.exec_module(second)
        self.assertIs(second.shared, bridge.shared)
        self.assertIs(second._RECOVERY_LOCK, bridge._RECOVERY_LOCK)
        original = KeyboardInterrupt('stub interrupted owner')
        self.state.wait_error = original
        self.state.terminate_failures = 1
        real_dispatch = bridge.shared._slotted_command
        worker_errors = []

        def recover_in_other_thread():
            try:
                second.recover_retained_cleanup()
            except BaseException as error:
                worker_errors.append(error)

        def dispatch_then_other_recovers(*args, **kwargs):
            try:
                return real_dispatch(*args, **kwargs)
            except BaseException:
                self.assertEqual(len(bridge.shared._PENDING_CLEANUP), 1)
                worker = threading.Thread(target=recover_in_other_thread, daemon=True)
                worker.start()
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive(), 'Other import did not finish exact cleanup')
                self.assertEqual(worker_errors, [])
                self.assertEqual(bridge.shared._PENDING_CLEANUP, [])
                raise

        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'concurrent-sentinel'}), \
                mock.patch.object(bridge.shared, '_slotted_command', side_effect=dispatch_then_other_recovers):
            with self.assertRaises(KeyboardInterrupt) as caught:
                self.run_bridge()
            self.assertIs(caught.exception, original)
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'concurrent-sentinel')
        self.assert_finished()
        self.assertEqual(self.state.processes[0].terminate_calls, 2)
        self.assertEqual(self.out.getvalue(), self.state.stdout)
        self.assertEqual(self.err.getvalue(), self.state.stderr)
        result = json.loads((self.raw_directory() / 'bridge-result.json').read_text())
        self.assertTrue(result['cleanupRecovered'])
        self.assertTrue(result['stopped'])
        self.assertFalse(result['jobs'])

    def test_cleanup_lock_is_shared_across_imports_and_serializes_recovery(self):
        second_spec = importlib.util.spec_from_file_location(
            'replay_native_slot_shared_lock', PROJECT / 'tools/replay-native-slot.py')
        second = importlib.util.module_from_spec(second_spec)
        second_spec.loader.exec_module(second)
        self.assertIs(second.shared, bridge.shared)
        self.assertIs(second._RECOVERY_LOCK, bridge._RECOVERY_LOCK)
        bridge.shared._PENDING_CLEANUP.append({'stub': 'retained-only-for-lock-test'})
        entered = threading.Event()
        release = threading.Event()
        second_started = threading.Event()
        count_lock = threading.Lock()
        worker_errors, calls = [], []

        def confirm_once():
            with count_lock:
                calls.append(threading.get_ident())
            if os.environ.get('ARENA_REPLAY_SLOT') != 'lock-sentinel':
                raise AssertionError('Concurrent recovery changed parent environment')
            entered.set()
            if not release.wait(timeout=2):
                raise AssertionError('Lock fixture release was not signalled')
            bridge.shared._PENDING_CLEANUP.clear()
            return True

        def recover(module, started=None):
            if started is not None:
                started.set()
            try:
                module.recover_retained_cleanup()
            except BaseException as error:
                worker_errors.append(error)

        with mock.patch.dict(os.environ, {'ARENA_REPLAY_SLOT': 'lock-sentinel'}), \
                mock.patch.object(bridge.shared, 'confirm_pending_cleanup', side_effect=confirm_once):
            first = threading.Thread(target=recover, args=(bridge,), daemon=True)
            later = threading.Thread(target=recover, args=(second, second_started), daemon=True)
            first.start()
            try:
                self.assertTrue(entered.wait(timeout=2), 'First recovery never entered')
                later.start()
                self.assertTrue(second_started.wait(timeout=2), 'Second recovery never started')
                acquired = second._RECOVERY_LOCK.acquire(blocking=False)
                if acquired:
                    second._RECOVERY_LOCK.release()
                self.assertFalse(acquired, 'Separate import did not share the held cleanup lock')
            finally:
                release.set()
                first.join(timeout=2)
                if later.ident is not None:
                    later.join(timeout=2)
            self.assertFalse(first.is_alive() or later.is_alive(), 'Recovery fixture threads did not finish')
            self.assertEqual(os.environ['ARENA_REPLAY_SLOT'], 'lock-sentinel')
        self.assertEqual(worker_errors, [])
        self.assertEqual(len(calls), 1, 'A retained object was recovered twice')
        self.assertEqual(bridge.shared._PENDING_CLEANUP, [])


if __name__ == '__main__':
    unittest.main()
