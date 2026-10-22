"""Compare 4B score operating points from the SAME synthetic responses.

Test-only: no model calls, production setting writes or final-corpus access.
Invalid production responses remain invalid, even if a partial proposal exists.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import runpy
import time

COMMON = runpy.run_path(str(Path(__file__).with_name("EvaluateCandidateGuard.py")))
PROMPT_SHA256 = "b3e2736a967fd86f594eb83c082d9b8afd4c0b60da1fa1c4ba8711914aca621f"
POLICIES = tuple(f"threshold-{value}" for value in (80, 85, 90, 95, 98, 99)) + ("tiered-85-90",)
VALID = {1, 2, 3}


def all_proposals(sample):
    COMMON["proposals"](sample, .9)  # Original display must reproduce exactly.
    if sample["state"] not in VALID:
        return []
    result = []
    for item in sample["scoreOnlyDiagnostic"]:
        old, new, score = item["original"], item["replacement"], item["score"]
        if not old or old == new or sample["text"].count(old) != 1:
            raise ValueError("Invalid zero-threshold diagnostic")
        if type(score) not in (float, int) or not math.isfinite(score) or not 0 <= score <= 1:
            raise ValueError("Invalid zero-threshold score")
        result.append(dict(start=sample["text"].index(old), original=old, replacement=new, score=score))
    result.sort(key=lambda item: item["start"])
    if any(a["start"] + len(a["original"]) > b["start"] for a, b in zip(result, result[1:])):
        raise ValueError("Overlapping zero-threshold proposals")
    return result


def decision(item, policy, readings):
    if policy not in POLICIES:
        raise ValueError("Unknown operating point")
    if policy.startswith("threshold-"):
        return "keep" if item["score"] >= int(policy.split("-")[1])/100 else "below_threshold"
    if item["score"] >= .9:
        return "keep_high_score"
    if item["score"] < .85:
        return "below_lower_threshold"
    evidence = COMMON["guard"](item["original"], item["replacement"], readings)
    return "keep_low_score_homophone" if evidence == "pass" else "low_score_" + evidence


def summarize(data, readings):
    if data.get("model") != "weasel-qwen3-4b" or data.get("candidate") != "current" or data.get("threshold") != .9:
        raise ValueError("Only original 4B/current prompt/0.90 source reports")
    if hashlib.sha256(data["systemPrompt"].encode("utf-8")).hexdigest() != PROMPT_SHA256:
        raise ValueError("Frozen production prompt changed")
    shape = {"tuning-development": (70, 20, 50), "candidate-guard": (40, 16, 24), "balance-confirmation": (32, 12, 20)}.get(data.get("corpus"))
    samples = data["samples"]
    if shape is None or len(samples) != shape[0] or len({s["text"] for s in samples}) != shape[0]:
        raise ValueError("Only frozen development/confirmation corpora; no final corpus")
    sent = data["requestParameters"]
    if sent.get("max_tokens") != 768 or sent.get("stream") is not False:
        raise ValueError("Unexpected transport parameters")
    for sample in samples:
        if type(sample.get("elapsedMs")) is not int or sample["elapsedMs"] < 0:
            raise ValueError("Invalid timing")
    proposed = [all_proposals(sample) for sample in samples]
    baseline = COMMON["metrics"](samples, "displayed")
    if (baseline["typos"], baseline["controls"]) != shape[1:]:
        raise ValueError("Changed reference labels")
    http_ms = COMMON["timing"]([s["elapsedMs"] for s in samples])
    rows = []
    for policy in POLICIES:
        replay, overhead = [], []
        for sample, items in zip(samples, proposed):
            start = time.perf_counter_ns()
            decisions = [dict(**item, decision=decision(item, policy, readings)) for item in items]
            kept = [item for item in decisions if item["decision"].startswith("keep")]
            displayed = COMMON["render"](sample["text"], kept)
            overhead.append((time.perf_counter_ns() - start)/1_000_000)
            replay.append(dict(text=sample["text"], expected=sample["expected"], state=sample["state"],
                               displayed=displayed, originalDisplay=sample["displayed"], decisions=decisions))
        quality = COMMON["metrics"](replay, "displayed")
        latency_ok = http_ms["median"] <= 600 and http_ms["p95"] <= 1200 and http_ms["maximum"] <= 3000
        reference = dict(corrected=9, wrong=1, falseAlarms=0, invalid=1) if data["corpus"] == "tuning-development" else (
            dict(corrected=10, wrong=0, falseAlarms=0, invalid=1) if data["corpus"] == "candidate-guard" else baseline)
        nonregression = all(quality[k] <= reference[k] for k in ("wrong", "falseAlarms", "invalid")) and quality["corrected"] >= reference["corrected"]
        improvement = quality["corrected"] > reference["corrected"] or quality["wrong"] + quality["falseAlarms"] < reference["wrong"] + reference["falseAlarms"]
        rows.append(dict(policy=policy, quality=quality,
                         utility={str(cost): quality["corrected"] - cost*(quality["wrong"] + quality["falseAlarms"]) for cost in (2, 4, 8)},
                         speedAccepted=latency_ok, nonregression=nonregression,
                         relativeImprovement=improvement, followupEligible=latency_ok and nonregression and improvement,
                         policyOnlyMs=COMMON["timing"](overhead), samples=replay))
    return dict(corpus=data["corpus"], profile=data["profile"], thinkingBudget=data.get("thinkingBudget", 0),
                requestParameters=sent, seed=data.get("seed"), baseline=baseline, sourceHttpMs=http_ms, policies=rows)


def dominates(first, second):
    a, b = first["quality"], second["quality"]
    le = a["corrected"] >= b["corrected"] and all(a[k] <= b[k] for k in ("wrong", "falseAlarms", "invalid"))
    times_a, times_b = first["sourceHttpMs"], second["sourceHttpMs"]
    le = le and times_a["median"] <= times_b["median"] and times_a["p95"] <= times_b["p95"]
    strict = a != b or times_a["median"] < times_b["median"] or times_a["p95"] < times_b["p95"]
    return le and strict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", nargs="+", required=True)
    parser.add_argument("--output", required=True)
    options = parser.parse_args()
    destination = COMMON["artifact"](options.output)
    if destination.exists():
        raise ValueError("Preserve earlier evidence")
    readings = COMMON["dictionary"]()
    reports, candidates = [], []
    reference_order = {}
    for value in options.reports:
        path = COMMON["artifact"](value)
        if path.stat().st_size > 2*1024*1024:
            raise ValueError("Oversized source report")
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        ordering = [(s["text"], s["expected"]) for s in data["samples"]]
        if data["corpus"] in reference_order and reference_order[data["corpus"]] != ordering:
            raise ValueError("References/order changed within corpus")
        reference_order[data["corpus"]] = ordering
        result = summarize(data, readings)
        result.update(source=path.name, sha256=COMMON["digest"](path))
        reports.append(result)
        for policy in result["policies"]:
            candidates.append(dict(source=path.name, corpus=result["corpus"], profile=result["profile"],
                                   thinkingBudget=result["thinkingBudget"], sourceHttpMs=result["sourceHttpMs"],
                                   **{k: v for k, v in policy.items() if k != "samples"}))
    for candidate in candidates:
        candidate["pareto"] = not any(other["corpus"] == candidate["corpus"] and dominates(other, candidate) for other in candidates)
    ranked = sorted(candidates, key=lambda c: (not c["followupEligible"], -c["utility"]["4"],
                                              c["sourceHttpMs"]["median"], c["thinkingBudget"], c["source"]))
    output = dict(testOnly=True, productionChanged=False, acceptancePassed=False, finalValidationRun=False,
                  plan="MODEL_4B_BALANCE_PLAN_2026-10-22.md", dictionarySha256=COMMON["DICTIONARY_SHA256"],
                  reports=reports, rankedOperatingPoints=ranked)
    with destination.open("x", encoding="utf-8") as stream:
        json.dump(output, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps([c for c in ranked if c["followupEligible"]], ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
