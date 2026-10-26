"""Single-slot work can claim a brief handoff; no real shared slots used."""
from __future__ import annotations
from pathlib import Path
import shutil
import sys
import unittest
import uuid
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core


class Handoff(unittest.TestCase):
    def setUp(self):
        self.root = core.ROOT / ".cache/arena-tests" / ("handoff-" + uuid.uuid4().hex)
        self.root.mkdir(parents=True)
        self.addCleanup(shutil.rmtree, self.root)

    def capture_handoff(self, want):
        clock, sleeps = [0.0], []
        claimed = [False]

        def available(fd):
            if 0.04 <= clock[0] <= 0.16 and not claimed[0]:
                claimed[0] = True
                return True
            return False

        def advance(seconds):
            sleeps.append(seconds)
            clock[0] += seconds
            if clock[0] > 1:
                self.fail("missed the handoff before the next bulk chunk")

        with patch.object(core, "CACHE", self.root), \
                patch.object(core, "slot_count", return_value=2), \
                patch.object(core, "_try_lock", side_effect=available), \
                patch.object(core, "_unlock") as unlock, \
                patch.object(core.time, "sleep", side_effect=advance):
            worker = core.Slots(want)
            with worker:
                self.assertEqual(worker.count, 1)
                self.assertEqual(len(worker.fds), 1)
                self.assertLessEqual(clock[0], 0.16)
            self.assertEqual((worker.fds, worker.count), ([], 0))
            unlock.assert_called_once()
        self.assertTrue(sleeps)

    def test_small_requests_use_one_briefly_available_slot_without_reserving_another(self):
        for want in (1, 2):
            with self.subTest(want=want):
                self.capture_handoff(want)

    def test_explicit_poll_and_bulk_policy_are_preserved(self):
        with patch.object(core, "slot_count", return_value=32):
            self.assertEqual(core.Slots(1, poll=0.3).poll, 0.3)
            self.assertEqual(core.Slots(2, poll=0.3).poll, 0.3)
            self.assertEqual(core.Slots(2, minimum=2).poll, 2.0)
            self.assertEqual(core.Slots(32, minimum=4).poll, 2.0)


if __name__ == "__main__":
    unittest.main()
