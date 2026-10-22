"""Boundary tests for the experimental operating-point evaluator."""
import runpy
from pathlib import Path
import unittest

MODULE = runpy.run_path(str(Path(__file__).with_name("Evaluate4BBalance.py")))


class BalanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.readings = MODULE["COMMON"]["dictionary"]()

    @staticmethod
    def item(old="操做", new="操作", score=.85):
        return dict(original=old, replacement=new, score=score)

    def test_high_nonphonetic_preserved(self):
        self.assertEqual(MODULE["decision"](self.item("资枓", "资料", .95), "tiered-85-90", {}), "keep_high_score")

    def test_lower_boundary(self):
        self.assertEqual(MODULE["decision"](self.item(), "tiered-85-90", self.readings), "keep_low_score_homophone")

    def test_below_boundary(self):
        self.assertEqual(MODULE["decision"](self.item(score=.849999), "tiered-85-90", self.readings), "below_lower_threshold")

    def test_upper_boundary(self):
        self.assertEqual(MODULE["decision"](self.item(score=.9), "tiered-85-90", {}), "keep_high_score")

    def test_low_nonphonetic_rejected(self):
        self.assertEqual(MODULE["decision"](self.item("收倒", "收掉"), "tiered-85-90", self.readings), "low_score_different_reading")

    def test_low_unknown_rejected(self):
        self.assertEqual(MODULE["decision"](self.item(), "tiered-85-90", {}), "low_score_unknown_reading")

    def test_growth_rejected_only_when_low(self):
        self.assertEqual(MODULE["decision"](self.item("错子", "错别字"), "tiered-85-90", self.readings), "low_score_length_change")

    def test_global_threshold_inclusive(self):
        self.assertEqual(MODULE["decision"](self.item(score=.95), "threshold-95", {}), "keep")
        self.assertEqual(MODULE["decision"](self.item(score=.949), "threshold-95", {}), "below_threshold")

    def test_unknown_policy(self):
        with self.assertRaises(ValueError):
            MODULE["decision"](self.item(), "unknown", self.readings)

    @staticmethod
    def sample():
        return dict(text="请操做。", displayed="请操做。", state=2, http=200, nativeError=0,
                    scoreOnlyDiagnostic=[dict(original="操做", replacement="操作", score=.85)])

    def test_low_diagnostic_available_without_modifying_source(self):
        sample = self.sample()
        self.assertEqual(len(MODULE["all_proposals"](sample)), 1)
        self.assertEqual(sample["state"], 2)
        self.assertEqual(sample["displayed"], sample["text"])

    def test_invalid_response_never_repaired(self):
        sample = self.sample()
        sample["state"] = 11
        self.assertEqual(MODULE["all_proposals"](sample), [])

    def test_low_overlap_rejected(self):
        sample = self.sample()
        sample["scoreOnlyDiagnostic"].append(dict(original="做", replacement="作", score=.85))
        with self.assertRaises(ValueError):
            MODULE["all_proposals"](sample)

    def test_transport_failure_rejected(self):
        sample = self.sample()
        sample["http"] = 503
        with self.assertRaises(ValueError):
            MODULE["all_proposals"](sample)

    def test_pareto_requires_strict_improvement(self):
        point = dict(quality=dict(corrected=5, wrong=1, falseAlarms=0, invalid=0), sourceHttpMs=dict(median=250, p95=600))
        self.assertFalse(MODULE["dominates"](point, point))
        improved = dict(quality=dict(corrected=6, wrong=1, falseAlarms=0, invalid=0), sourceHttpMs=point["sourceHttpMs"])
        self.assertTrue(MODULE["dominates"](improved, point))


if __name__ == "__main__":
    unittest.main()
