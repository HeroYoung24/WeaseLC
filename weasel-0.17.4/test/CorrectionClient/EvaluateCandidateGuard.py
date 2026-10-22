"""Research-only ablation of a single-character homophone candidate guard.

Consumes compiled synthetic quality reports, never user text/settings. The
production parser remains authoritative: invalid replies stay invalid. This
cannot certify production safety or input-to-hint latency.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import time

SOURCE = Path(__file__).resolve().parents[2]
ARTIFACTS = (SOURCE / "output" / "LocalTest").resolve()
DICTIONARY = SOURCE / "librime" / "build-ninja" / "bin" / "luna_pinyin.dict.yaml"
DICTIONARY_SHA256 = "989f4da38bf7efc98704f5ac3a9ae35d8f84d3afda203da2ee22c5ea9e0b01e9"
VALID = {1, 2, 3}


def artifact(value):
    path = Path(value).resolve()
    if path.parent != ARTIFACTS:
        raise ValueError("Use output/LocalTest artifacts only")
    return path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dictionary():
    raw = DICTIONARY.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DICTIONARY_SHA256:
        raise ValueError("Frozen static dictionary changed; do not silently swap data")
    readings = {}
    after_header = False
    for line in raw.decode("utf-8-sig").splitlines():
        if line.strip() == "...":
            after_header = True
            continue
        if not after_header or not line or line.startswith("#"):
            continue
        columns = line.split("\t")
        if len(columns) >= 2 and len(columns[0]) == 1 and re.fullmatch("[a-zv]+", columns[1]):
            readings.setdefault(columns[0], set()).add(columns[1])
    if len(readings) < 1000:
        raise ValueError("Unexpected dictionary coverage")
    return readings


def guard(original, replacement, readings):
    if len(original) != len(replacement):
        return "length_change"
    different = [(a, b) for a, b in zip(original, replacement) if a != b]
    if len(different) != 1:
        return "not_single_change"
    a, b = different[0]
    if not ("\u3400" <= a <= "\u9fff" and "\u3400" <= b <= "\u9fff"):
        return "not_han"
    if a not in readings or b not in readings:
        return "unknown_reading"
    return "pass" if readings[a] & readings[b] else "different_reading"


def proposals(sample, threshold):
    """Reconstruct ONLY originally accepted suggestions, never lower scores."""
    state = sample["state"]
    if sample.get("http") != 200 or sample.get("nativeError") != 0 or state not in VALID | {11}:
        raise ValueError("Transport/setup failure is not a model-quality result")
    if state not in VALID:
        if sample["displayed"] != sample["text"]:
            raise ValueError("Invalid response unexpectedly edited text")
        return []
    items = []
    for proposal in sample["scoreOnlyDiagnostic"]:
        old, new, score = proposal["original"], proposal["replacement"], proposal["score"]
        if not old or old == new or sample["text"].count(old) != 1:
            raise ValueError("Malformed diagnostic proposal")
        if type(score) not in (int, float) or not math.isfinite(score) or not 0 <= score <= 1:
            raise ValueError("Invalid diagnostic score")
        if score >= threshold:
            items.append(dict(start=sample["text"].index(old), original=old, replacement=new, score=score))
    items.sort(key=lambda item: item["start"])
    for previous, current in zip(items, items[1:]):
        if previous["start"] + len(previous["original"]) > current["start"]:
            raise ValueError("Overlapping diagnostic proposals")
    if render(sample["text"], items) != sample["displayed"] or (bool(items) != (state == 3)):
        raise ValueError("Diagnostic reconstruction differs from production display")
    return items


def render(text, items):
    for item in sorted(items, key=lambda item: item["start"], reverse=True):
        text = text[:item["start"]] + item["replacement"] + text[item["start"] + len(item["original"]):]
    return text


def metrics(samples, field):
    counts = dict(typos=0, controls=0, corrected=0, wrong=0, falseAlarms=0, invalid=0, missed=0)
    for sample in samples:
        typo = sample["text"] != sample["expected"]
        counts["typos" if typo else "controls"] += 1
        if sample["state"] not in VALID:
            counts["invalid"] += 1
        elif typo and sample[field] == sample["expected"]:
            counts["corrected"] += 1
        elif typo and sample[field] != sample["text"]:
            counts["wrong"] += 1
        elif typo:
            counts["missed"] += 1
        elif sample[field] != sample["text"]:
            counts["falseAlarms"] += 1
    shown = counts["corrected"] + counts["wrong"] + counts["falseAlarms"]
    counts["precision"] = counts["corrected"] / shown if shown else 0
    return counts


def timing(values):
    values = sorted(values)
    return dict(median=values[len(values)//2], p95=values[math.ceil(len(values)*.95)-1], maximum=values[-1])


def evaluate(data, readings):
    if data.get("threshold") != .9 or data.get("candidate") != "current" or data.get("profile") != "greedy":
        raise ValueError("Keep current prompt, 0.90 threshold and greedy profile")
    if data.get("requestParameters", {}).get("temperature") != 0 or data.get("thinkingBudget", 0) != 0:
        raise ValueError("Non-thinking greedy reports required")
    samples = data["samples"]
    expected_size = {"tuning-development": 70, "candidate-guard": 40}.get(data.get("corpus"))
    if len(samples) != expected_size or len({s["text"] for s in samples}) != expected_size:
        raise ValueError("Only fixed development reports, never final validation")
    result, overhead = [], []
    for sample in samples:
        accepted = proposals(sample, .9)
        start = time.perf_counter_ns()
        decisions = [dict(**item, decision=guard(item["original"], item["replacement"], readings)) for item in accepted]
        kept = [item for item in decisions if item["decision"] == "pass"]
        guarded = render(sample["text"], kept)
        overhead.append((time.perf_counter_ns() - start) / 1_000_000)
        if type(sample.get("elapsedMs")) is not int or sample["elapsedMs"] < 0:
            raise ValueError("Invalid HTTP latency")
        result.append(dict(text=sample["text"], expected=sample["expected"], displayed=sample["displayed"],
                           guarded=guarded, state=sample["state"], decisions=decisions))
    before, after = metrics(result, "displayed"), metrics(result, "guarded")
    shape = (before["typos"], before["controls"])
    if shape != ((20, 50) if data["corpus"] == "tuning-development" else (16, 24)):
        raise ValueError("Reference labels changed")
    http = timing([s["elapsedMs"] for s in samples])
    extra = timing(overhead)
    minimum = 14 if data["corpus"] == "tuning-development" else 12
    entry = (after["corrected"] >= minimum and after["precision"] >= .95 and after["falseAlarms"] == 0
             and after["wrong"] <= 1 and after["invalid"] <= 1 and http["median"] <= 600
             and http["p95"] <= 1200 and extra["p95"] <= 5)
    lost = sum(s["displayed"] == s["expected"] != s["text"] and s["guarded"] != s["expected"] for s in result)
    blocked = sum(s["displayed"] not in (s["text"], s["expected"]) and s["guarded"] == s["text"] for s in result)
    return dict(corpus=data["corpus"], model=data["model"], baseline=before, guarded=after,
                sourceHttpMs=http, guardOnlyMs=extra, correctSuggestionsLost=lost, wrongOrFalseFullyBlocked=blocked,
                developmentEntryAccepted=entry, productionAccepted=False, finalValidationRun=False, samples=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", nargs="+", required=True)
    parser.add_argument("--output", required=True)
    options = parser.parse_args()
    destination = artifact(options.output)
    if destination.exists():
        raise ValueError("Preserve earlier artifacts")
    start = time.perf_counter_ns()
    readings = dictionary()
    load_ms = (time.perf_counter_ns() - start) / 1_000_000
    reports = []
    prompt_hash = None
    for value in options.reports:
        path = artifact(value)
        if path.stat().st_size > 2 * 1024 * 1024:
            raise ValueError("Oversized report")
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        current_hash = hashlib.sha256(data["systemPrompt"].encode("utf-8")).hexdigest()
        if prompt_hash is not None and prompt_hash != current_hash:
            raise ValueError("Prompt changed across reports")
        prompt_hash = current_hash
        result = evaluate(data, readings)
        result.update(source=path.name, sha256=digest(path))
        reports.append(result)
    report = dict(mechanism="single-changed-Han-character/shared-toneless-reading/v1",
                  testOnly=True, productionChanged=False, finalValidationRun=False,
                  dictionary=dict(path=str(DICTIONARY), sha256=DICTIONARY_SHA256, characters=len(readings), loadMs=load_ms),
                  promptSha256=prompt_hash, corpusSha256=digest(SOURCE / "test/CorrectionClient/CandidateGuardCorpus.h"),
                  reports=reports)
    with destination.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps([{k: v for k, v in r.items() if k != "samples"} for r in reports], ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
