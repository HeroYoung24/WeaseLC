"""Compare compiled-corpus reports; a missed typo becoming a wrong edit fails.

Only workspace output/LocalTest artifacts are read/written. No model calls,
external documents, settings or installation changes. Exit 1 means rejected.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = (Path(__file__).resolve().parents[2] / "output" / "LocalTest").resolve()
VALID_STATES = {1, 2, 3}


def artifact(value):
    path = Path(value).resolve()
    if path.parent != ROOT:
        raise ValueError("Use output/LocalTest artifacts only")
    return path


def read(value):
    path = artifact(value)
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Oversized quality report")
    report = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(report.get("samples"), list) or not report["samples"]:
        raise ValueError("No samples")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pair", nargs=2, action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = artifact(args.output)
    if output.exists():
        raise ValueError("Preserve existing decision")
    regressions, improvements, groups = [], [], []
    seen, prompts, models, threshold = set(), None, None, None
    total = 0
    for left, right in args.pair:
        baseline, candidate = read(left), read(right)
        if baseline["corpus"] != candidate["corpus"] or len(baseline["samples"]) != len(candidate["samples"]):
            raise ValueError("Different corpora")
        if baseline["model"] != candidate["model"] or baseline["threshold"] != candidate["threshold"]:
            raise ValueError("Different models/thresholds")
        pair_prompts = baseline["systemPrompt"], candidate["systemPrompt"]
        if prompts is not None and pair_prompts != prompts:
            raise ValueError("Prompt changed between groups")
        if models is not None and (baseline["model"] != models or baseline["threshold"] != threshold):
            raise ValueError("Configuration changed between groups")
        prompts, models, threshold = pair_prompts, baseline["model"], baseline["threshold"]
        for a, b in zip(baseline["samples"], candidate["samples"]):
            if a["text"] != b["text"] or a["expected"] != b["expected"]:
                raise ValueError("Different sample ordering/reference")
            if a["text"] in seen:
                raise ValueError("Duplicate sample across groups")
            seen.add(a["text"])
            total += 1
            old_correct = a["state"] in VALID_STATES and a["displayed"] == a["expected"]
            new_correct = b["state"] in VALID_STATES and b["displayed"] == b["expected"]
            old_wrong = a["displayed"] not in (a["text"], a["expected"])
            new_wrong = b["displayed"] not in (b["text"], b["expected"])
            reasons = []
            if old_correct and not new_correct:
                reasons.append("lost_correct_output")
            if new_wrong and (not old_wrong or a["displayed"] != b["displayed"]):
                reasons.append("new_incorrect_edit")
            if a["state"] in VALID_STATES and b["state"] not in VALID_STATES:
                reasons.append("new_invalid_response")
            if reasons:
                regressions.append(dict(corpus=baseline["corpus"], text=a["text"], reasons=reasons))
            if not old_correct and new_correct:
                improvements.append(dict(corpus=baseline["corpus"], text=a["text"]))
        groups.append(baseline["corpus"])
    accepted = bool(improvements) and not regressions
    result = dict(accepted=accepted, samples=total, groups=groups, model=models, threshold=threshold,
                  improvements=improvements, regressions=regressions,
                  baselinePromptSha256=hashlib.sha256(prompts[0].encode()).hexdigest(),
                  candidatePromptSha256=hashlib.sha256(prompts[1].encode()).hexdigest(),
                  limits="Synthetic exact-reference gate; unresolved baseline failures may remain")
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(dict(accepted=accepted, samples=total, improvements=len(improvements),
                         regressions=len(regressions))))
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
