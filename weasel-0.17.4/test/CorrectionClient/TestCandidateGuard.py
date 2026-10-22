"""Guard invariants independent of model responses; synthetic fixtures only."""
import copy
import runpy
from pathlib import Path
import unittest

MODULE = runpy.run_path(str(Path(__file__).with_name("EvaluateCandidateGuard.py")))


class CandidateGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.readings = MODULE["dictionary"]()

    def test_homophone(self):
        self.assertEqual(MODULE["guard"]("错子", "错字", self.readings), "pass")

    def test_length_change(self):
        self.assertEqual(MODULE["guard"]("错子", "错别字", self.readings), "length_change")

    def test_semantic_rewrite(self):
        self.assertEqual(MODULE["guard"]("收下", "收好", self.readings), "different_reading")

    def test_unknown_fails_closed(self):
        self.assertEqual(MODULE["guard"]("甲", "乙", {}), "unknown_reading")

    def test_not_one_change(self):
        self.assertEqual(MODULE["guard"]("甲乙", "丙丁", self.readings), "not_single_change")

    def test_noop(self):
        self.assertEqual(MODULE["guard"]("甲", "甲", self.readings), "not_single_change")

    def test_ascii(self):
        self.assertEqual(MODULE["guard"]("a", "b", self.readings), "not_han")

    def test_non_bmp(self):
        self.assertEqual(MODULE["guard"]("𠀀", "甲", self.readings), "not_han")

    def test_multi_reading_intersection(self):
        self.assertEqual(MODULE["guard"]("行", "型", {"行": {"xing", "hang"}, "型": {"xing"}}), "pass")

    def test_known_limitation(self):
        # This guard intentionally cannot recognize an intentional homophone name.
        self.assertEqual(MODULE["guard"]("程序", "程序", self.readings), "not_single_change")
        self.assertEqual(MODULE["guard"]("再励", "再厉", self.readings), "pass")

    @staticmethod
    def sample():
        return dict(text="有错子。", displayed="有错字。", state=3, http=200, nativeError=0,
                    scoreOnlyDiagnostic=[dict(original="错子", replacement="错字", score=.99)])

    def test_reconstruction(self):
        self.assertEqual(len(MODULE["proposals"](self.sample(), .9)), 1)

    def test_low_score_never_enabled(self):
        sample = self.sample()
        sample.update(displayed=sample["text"], state=2)
        sample["scoreOnlyDiagnostic"][0]["score"] = .85
        self.assertEqual(MODULE["proposals"](sample, .9), [])

    def test_invalid_never_repaired(self):
        sample = self.sample()
        sample.update(displayed=sample["text"], state=11)
        self.assertEqual(MODULE["proposals"](sample, .9), [])

    def test_mismatch_rejected(self):
        sample = self.sample()
        sample["displayed"] = "其他"
        with self.assertRaises(ValueError):
            MODULE["proposals"](sample, .9)

    def test_bad_score_rejected(self):
        for score in (float("nan"), float("inf"), -1, 2, True):
            sample = self.sample()
            sample["scoreOnlyDiagnostic"][0]["score"] = score
            with self.assertRaises(ValueError):
                MODULE["proposals"](sample, .9)

    def test_overlap_rejected(self):
        sample = self.sample()
        sample["scoreOnlyDiagnostic"].append(dict(original="子", replacement="字", score=.99))
        with self.assertRaises(ValueError):
            MODULE["proposals"](sample, .9)

    def test_transport_not_quality(self):
        sample = self.sample()
        sample["http"] = 500
        with self.assertRaises(ValueError):
            MODULE["proposals"](sample, .9)

    def test_partial_keep_positions(self):
        text = "错子与收下"
        items = [dict(start=0, original="错子", replacement="错字"), dict(start=3, original="收下", replacement="收好")]
        kept = [item for item in items if MODULE["guard"](item["original"], item["replacement"], self.readings) == "pass"]
        self.assertEqual(MODULE["render"](text, kept), "错字与收下")

    def test_source_unchanged(self):
        sample = self.sample()
        before = copy.deepcopy(sample)
        MODULE["proposals"](sample, .9)
        self.assertEqual(sample, before)


if __name__ == "__main__":
    unittest.main()
