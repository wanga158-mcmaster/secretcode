"""Lock acquisition failure paths; all descriptors and locks are mocked."""
from contextlib import ExitStack
from pathlib import Path
import sys
import unittest
from unittest.mock import call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core


class LockCleanup(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.root = core.ROOT / 'mock-lock-root'
        self.stack.enter_context(patch.object(core.Path, 'mkdir'))
        self.stack.enter_context(patch.object(core, 'CACHE', self.root))
        self.stack.enter_context(patch.object(core, 'slot_count', return_value=2))
        self.open = self.stack.enter_context(patch.object(core.os, 'open', return_value=41))
        self.close = self.stack.enter_context(patch.object(core.os, 'close'))
        self.unlock = self.stack.enter_context(patch.object(core, '_unlock'))
        self.lock = self.stack.enter_context(patch.object(core, '_try_lock', return_value=True))

    def file_lock(self, **kwargs):
        return core.FileLock(self.root / 'single.lock', **kwargs)

    def test_file_lock_closes_descriptor_if_probe_or_clock_raises(self):
        for target in ('probe', 'clock'):
            with self.subTest(target=target):
                self.close.reset_mock()
                worker = self.file_lock()
                with patch.object(core, '_try_lock', side_effect=KeyboardInterrupt('probe') if target == 'probe' else None), \
                        patch.object(core.time, 'monotonic', side_effect=KeyboardInterrupt('clock') if target == 'clock' else None):
                    with self.assertRaises(KeyboardInterrupt):
                        worker.__enter__()
                self.close.assert_called_once_with(41)
                self.assertIsNone(worker.fd)

    def test_file_lock_closes_descriptor_if_wait_is_interrupted(self):
        self.lock.return_value = False
        worker = self.file_lock()
        with patch.object(core.time, 'sleep', side_effect=KeyboardInterrupt('wait')):
            with self.assertRaises(KeyboardInterrupt):
                worker.__enter__()
        self.close.assert_called_once_with(41)
        self.assertIsNone(worker.fd)

    def test_file_lock_timeout_closes_once(self):
        self.lock.return_value = False
        worker = self.file_lock(timeout=0)
        with patch.object(core.time, 'monotonic', side_effect=[0, 1]):
            with self.assertRaisesRegex(core.ArenaError, 'timed out'):
                worker.__enter__()
        self.close.assert_called_once_with(41)
        self.assertIsNone(worker.fd)

    def test_file_lock_success_closes_only_at_exit_even_if_unlock_fails(self):
        worker = self.file_lock()
        self.assertIs(worker.__enter__(), worker)
        self.close.assert_not_called()
        self.unlock.side_effect = OSError('unlock')
        with self.assertRaisesRegex(OSError, 'unlock'):
            worker.__exit__()
        self.close.assert_called_once_with(41)
        self.assertIsNone(worker.fd)

    def test_slots_close_prior_claim_when_later_open_raises(self):
        self.open.side_effect = [41, OSError('open')]
        worker = core.Slots(2, minimum=2)
        with self.assertRaisesRegex(OSError, 'open'):
            worker.__enter__()
        self.unlock.assert_called_once_with(41)
        self.close.assert_called_once_with(41)
        self.assertEqual((worker.fds, worker.count), ([], 0))

    def test_slots_close_current_and_prior_descriptors_if_probe_raises(self):
        self.open.side_effect = [41, 42]
        self.lock.side_effect = [True, KeyboardInterrupt('probe')]
        worker = core.Slots(2, minimum=2)
        with self.assertRaises(KeyboardInterrupt):
            worker.__enter__()
        self.assertEqual(self.close.call_args_list, [call(42), call(41)])
        self.unlock.assert_called_once_with(41)
        self.assertEqual((worker.fds, worker.count), ([], 0))

    def test_slots_partial_claim_is_not_closed_twice_on_print_or_sleep_interrupt(self):
        for target in ('print', 'sleep'):
            with self.subTest(target=target):
                self.open.side_effect = [41, 42]
                self.lock.side_effect = [True, False]
                self.close.reset_mock()
                self.unlock.reset_mock()
                worker = core.Slots(2, minimum=2)
                with patch('builtins.print', side_effect=KeyboardInterrupt('print') if target == 'print' else None), \
                        patch.object(core.time, 'sleep', side_effect=KeyboardInterrupt('sleep') if target == 'sleep' else None):
                    with self.assertRaises(KeyboardInterrupt):
                        worker.__enter__()
                self.assertEqual(self.close.call_args_list, [call(42), call(41)])
                self.unlock.assert_called_once_with(41)
                self.assertEqual((worker.fds, worker.count), ([], 0))

    def test_slots_attempt_every_release_and_keep_original_acquisition_error(self):
        self.stack.enter_context(patch.object(core, 'slot_count', return_value=4))
        self.open.side_effect = [41, 42, 43, OSError('open')]
        self.unlock.side_effect = [RuntimeError('unlock'), None, None]
        worker = core.Slots(4, minimum=4)
        with self.assertRaisesRegex(OSError, 'open') as raised:
            worker.__enter__()
        self.assertEqual(self.unlock.call_args_list, [call(43), call(42), call(41)])
        self.assertEqual(self.close.call_args_list, [call(43), call(42), call(41)])
        self.assertEqual((worker.fds, worker.count), ([], 0))
        self.assertIn('Slot acquisition cleanup also failed', raised.exception.__notes__[0])

    def test_successful_slot_acquisition_keeps_descriptors_until_exit(self):
        self.open.side_effect = [41, 42]
        worker = core.Slots(2, minimum=2)
        self.assertIs(worker.__enter__(), worker)
        self.assertEqual((worker.fds, worker.count), ([41, 42], 2))
        self.close.assert_not_called()
        worker.__exit__()
        self.assertEqual(self.unlock.call_args_list, [call(42), call(41)])
        self.assertEqual(self.close.call_args_list, [call(42), call(41)])
        self.assertEqual((worker.fds, worker.count), ([], 0))


if __name__ == '__main__':
    unittest.main()
