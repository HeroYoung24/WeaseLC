"""Apply the 2026-10-21 predeclared gate to compiled-corpus reports.

Recomputes metrics from each synthetic sample. Does not call models or alter
preferences, reference answers, thresholds, or system installation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = (Path(__file__).resolve().parents[2] / "output" / "LocalTest").resolve()
VALID = {1, 2, 3}


def artifact(value):
    path = Path(value).resolve()
    if path.parent != ROOT:
        raise ValueError("Use output/LocalTest reports only")
    return path


def read(value):
    path = artifact(value)
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Oversized report")
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if data["threshold"] != .9:
        raise ValueError("Threshold changed")
    samples = data["samples"]
    if not samples or len({s["text"] for s in samples}) != len(samples):
        raise ValueError("Empty or duplicate samples")
    counts = dict(corrected=0, typos=0, controls=0, wrong=0, falseAlarms=0, invalid=0)
    for s in samples:
        if s.get("http") != 200 or s.get("nativeError") != 0 or s["state"] not in VALID | {11}:
            raise ValueError("Transport/setup failure cannot be scored as model quality")
        typo = s["text"] != s["expected"]
        counts["typos" if typo else "controls"] += 1
        valid = s["state"] in VALID
        if not valid:
            counts["invalid"] += 1
        elif typo and s["displayed"] == s["expected"]:
            counts["corrected"] += 1
        elif typo and s["displayed"] != s["text"]:
            counts["wrong"] += 1
        elif not typo and s["displayed"] != s["text"]:
            counts["falseAlarms"] += 1
    times = sorted(s["elapsedMs"] for s in samples)
    if any(not isinstance(t, int) or t < 0 for t in times):
        raise ValueError("Invalid timing")
    proposed = counts["corrected"] + counts["wrong"] + counts["falseAlarms"]
    counts.update(precision=counts["corrected"] / proposed if proposed else 0,
                  medianMs=times[len(times)//2], p95Ms=times[math.ceil(len(times)*.95)-1], maxMs=times[-1])
    return data, counts, hashlib.sha256(path.read_bytes()).hexdigest()


def aligned(a, b, check_prompt=True):
    if (check_prompt and a["systemPrompt"] != b["systemPrompt"]) or [(s["text"], s["expected"]) for s in a["samples"]] != [
            (s["text"], s["expected"]) for s in b["samples"]]:
        raise ValueError("Different prompt, order or references")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--development", nargs=2, required=True)
    p.add_argument("--validation", nargs="+", default=[])
    p.add_argument("--development-only", action="store_true")
    p.add_argument("--validation-corpus", choices=["tuning-validation", "tuning-final"], default="tuning-validation")
    p.add_argument("--allow-prompt-change", action="store_true")
    p.add_argument("--output", required=True)
    args = p.parse_args()
    if args.development_only == bool(args.validation):
        p.error("Choose validation reports or --development-only")
    destination = artifact(args.output)
    if destination.exists():
        raise ValueError("Keep earlier decisions")
    baseline, old, old_hash = read(args.development[0])
    selected, new, new_hash = read(args.development[1])
    aligned(baseline, selected, not args.allow_prompt_change)
    if new["typos"] != 20 or new["controls"] != 50:
        raise ValueError("Unexpected development corpus")
    reasons = []
    if new["corrected"] < old["corrected"] or new["wrong"] > old["wrong"] or new["falseAlarms"] or new["invalid"] > old["invalid"]:
        reasons.append("development_regression")
    development_accepted = not reasons
    if args.development_only:
        reasons.append("validation_not_run")
    rows, seeds, reference = [], set(), None
    for name in args.validation:
        data, metrics, digest = read(name)
        if data["corpus"] != args.validation_corpus or data["model"] != selected["model"] or data["profile"] != selected["profile"]:
            raise ValueError("Different validation configuration")
        if data["systemPrompt"] != selected["systemPrompt"]:
            raise ValueError("Validation prompt changed")
        if reference is not None:
            aligned(reference, data)
        reference = data
        seeds.add(data.get("seed"))
        if metrics["typos"] != 24 or metrics["controls"] != 24:
            raise ValueError("Unexpected validation corpus")
        if metrics["corrected"] < 17 or metrics["precision"] < .95 or metrics["falseAlarms"] or metrics["invalid"]:
            reasons.append("validation_quality")
        if metrics["medianMs"] > 600 or metrics["p95Ms"] > 1200:
            reasons.append("validation_latency")
        rows.append(dict(file=Path(name).name, sha256=digest, seed=data.get("seed"), **metrics))
    if not args.development_only and selected["temperature"] > 0 and len(seeds) < 3:
        reasons.append("insufficient_sampling_seeds")
    result = dict(accepted=not reasons, reasons=sorted(set(reasons)), model=selected["model"],
                  profile=selected["profile"], baselineDevelopment=old, selectedDevelopment=new,
                  developmentAccepted=development_accepted,
                  validationCorpus=args.validation_corpus, promptChanged=baseline["systemPrompt"] != selected["systemPrompt"],
                  promptSha256=[hashlib.sha256(d["systemPrompt"].encode()).hexdigest() for d in (baseline, selected)],
                  developmentSha256=[old_hash, new_hash], validation=rows,
                  limits="Frozen synthetic exact-reference gate; not a population accuracy estimate or real-app acceptance")
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
