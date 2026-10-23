#!/usr/bin/env python3
"""Check tracked repository files for common accidental public disclosures.

This is a lightweight hygiene check, NOT a comprehensive secret scanner.
It never prints matched credential contents. History requires a separate review.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAX_FILE_BYTES = 5_000_000

# Example/template placeholders are allowed; actual .env files are not.
BLOCKED_FILENAMES = re.compile(
    r"(?i)(?:^|/)(?:\.env(?:\.(?!example$|sample$|template$)[^/]*)?"
    r"|id_rsa|id_ed25519|credentials\.json|service-account\.json)$"
)
BLOCKED_SUFFIXES = (".pem", ".p12", ".pfx", ".key", ".db", ".sqlite", ".sqlite3",
                    ".bak", ".zip", ".log")

CONTENT_RULES = (
    ("private-key header",
     re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----")),
    ("GitHub credential-shaped value",
     re.compile(rb"\b(?:gh[pousr]_[A-Za-z0-9_]{25,}|github_pat_[A-Za-z0-9_]{25,})\b")),
    ("AWS access-key-shaped value",
     re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("possible embedded URL credentials",
     re.compile(rb"https?://[^\s/@:]+:[^\s/@]+@")),
    ("possible credential assignment",
     re.compile(rb"""(?i)\b(?:password|passwd|secret|api[_-]?key|access[_-]?token|
                            refresh[_-]?token|client[_-]?secret)\b\s*[:=]\s*
                            ["']?[A-Za-z0-9_+./=-]{20,}""", re.VERBOSE)),
    ("local home directory path",
     re.compile(rb"/home/[A-Za-z0-9_.-]+/")),
    ("email address in tracked contents",
     re.compile(rb"[A-Za-z0-9_.+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")),
)


def tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    return [p.decode("utf-8", "surrogateescape")
            for p in result.stdout.split(b"\0") if p]


def main() -> int:
    issues: set[tuple[str, int, str]] = set()
    paths = tracked_files()

    for rel in paths:
        path = ROOT / rel
        name = path.name.lower()
        if BLOCKED_FILENAMES.search(rel) or name.endswith(BLOCKED_SUFFIXES):
            issues.add((rel, 0, "sensitive or generated filename"))
        if path.is_symlink():
            issues.add((rel, 0, "tracked symlink requires manual review"))
            continue
        if not path.is_file():
            issues.add((rel, 0, "tracked file missing from working tree"))
            continue
        if path.stat().st_size > MAX_FILE_BYTES:
            issues.add((rel, 0, "tracked file exceeds size review limit"))
            continue
        data = path.read_bytes()
        if b"\0" in data:
            issues.add((rel, 0, "binary file requires manual review"))
            continue
        for label, pattern in CONTENT_RULES:
            for match in pattern.finditer(data):
                line = data.count(b"\n", 0, match.start()) + 1
                issues.add((rel, line, label))

    if issues:
        print("Repository hygiene review FAILED:", file=sys.stderr)
        for rel, line, label in sorted(issues):
            print(f"  {rel}:{line}: {label}", file=sys.stderr)
        print("Matches are deliberately redacted. Review locally; rotate any real",
              "leaked credential before changing Git history.", file=sys.stderr)
        return 1

    print(f"Repository hygiene review passed: {len(paths)} tracked files checked.")
    print("Pattern-based only; does not prove that no secrets were exposed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
