"""Two narrow v2 coverage guards, using the unchanged portable parser fixture."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_economic_renewal_metadata as fixtures


class V2CoverageGuards(unittest.TestCase):
    def setUp(self):
        self.base=fixtures.MetadataFixtures()
        self.base.setUp()
        self.addCleanup(self.base.doCleanups)

    def test_complete_window_cannot_omit_requested_first_round(self):
        def missing_first(meta,rows):
            meta['fromRound']-=1
            meta['interactions']['coverage']['requested']['fromRound']-=1
        self.base.reject(missing_first)

    def test_duplicate_candidate_partition_cannot_exceed_pushes(self):
        def impossible(meta,rows):
            coverage=meta['interactions']['coverage']
            coverage['duplicateStates']=coverage['nativeObservations']
        self.base.reject(impossible)

    def test_nonzero_disjoint_duplicate_partition_is_allowed(self):
        def valid_partition(meta,rows):
            coverage=meta['interactions']['coverage']
            coverage['duplicateStates']=coverage['nativeObservations']-coverage['candidateEvents']
        case=self.base.fixture(valid_partition)
        events,pins=fixtures.e.load_traces(case,self.base.root,fixtures.FakePinnedHelper)
        self.assertEqual(len(events),2)
        self.assertEqual(len(pins),1)


if __name__=='__main__':unittest.main(verbosity=2)
