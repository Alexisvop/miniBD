"""Independent CLI oracle: fresh processes, full-state checks, temporary files only."""
import operator
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
rng = random.Random(0x51A7E)
model = {}  # key -> (name, age), or None for a retained tombstone
comparisons = {"=": operator.eq, "!=": operator.ne, "<": operator.lt,
               "<=": operator.le, ">": operator.gt, ">=": operator.ge}


def row_line(key, row):
    return f"{key} | {row[0]} | {row[1]}"


def run(path, commands, output, errors=()):
    result = subprocess.run([binary, str(path)], input="\n".join(commands) + "\n",
                            text=True, encoding="utf-8", capture_output=True, timeout=120)
    if result.returncode != 0 or result.stdout.splitlines() != output or result.stderr.splitlines() != list(errors):
        raise AssertionError(f"CLI/model mismatch: exit={result.returncode}; "
                             f"stdout rows={len(result.stdout.splitlines())}/{len(output)}; "
                             f"stderr={result.stderr[:500]}")


def snapshot(path):
    commands, output, errors = [], [], []
    for key in range(10001):
        commands.append(f"SELECT {key}")
        if key not in model:
            errors.append("ERROR: record not found")
        elif model[key] is None:
            errors.append("ERROR: record is logically deleted")
        else:
            output.append(row_line(key, model[key]))
    commands.append("SELECT_ALL")
    visible = [row_line(k, model[k]) for k in sorted(model) if model[k] is not None]
    output.extend(visible or ["OK: 0 rows"])
    for text, predicate in comparisons.items():
        commands.append(f"SELECT_ALL WHERE age {text} 75")
        selected = [row_line(k, model[k]) for k in sorted(model)
                    if model[k] is not None and predicate(model[k][1], 75)]
        output.extend(selected or ["OK: 0 rows"])
    run(path, commands, output, errors)


with tempfile.TemporaryDirectory(prefix="minidb-process-model-") as directory:
    path = Path(directory) / "model.db"
    keys = list(range(10000))
    rng.shuffle(keys)
    commands, output = [], []
    for key in keys:
        model[key] = (f"José 東京 😀 {key}", key % 151)
        commands.append(f'INSERT {key} "{model[key][0]}" {model[key][1]}')
        output.append("OK: 1 row inserted")
    commands.append(".exit")
    run(path, commands, output)
    snapshot(path)
    for cycle in range(4):
        commands, output, errors = [], [], []
        for step in range(5000):
            key = rng.randrange(10001)
            action = rng.choice(("INSERT", "UPDATE", "DELETE", "SELECT"))
            row = (f"Revisión {cycle}-{step}", rng.randrange(151))
            command = f'{action} {key}'
            if action in ("INSERT", "UPDATE"):
                command += f' "{row[0]}" {row[1]}'
            commands.append(command)
            if action == "INSERT":
                if model.get(key) is not None:
                    errors.append("ERROR: duplicate ID")
                else:
                    model[key] = row
                    output.append("OK: 1 row inserted")
            elif key not in model:
                errors.append("ERROR: record not found")
            elif model[key] is None:
                errors.append("ERROR: record is logically deleted")
            elif action == "SELECT":
                output.append(row_line(key, model[key]))
            elif action == "UPDATE":
                model[key] = row
                output.append("OK: 1 row updated")
            else:
                model[key] = None
                output.append("OK: 1 row deleted")
        # Alternate explicit exit and EOF close.
        if cycle % 2 == 0:
            commands.append(".exit")
        run(path, commands, output, errors)
        snapshot(path)
    before = path.read_bytes()
    path.chmod(0o400)
    try:
        if os.geteuid() == 0:
            print("Permission scenario skipped: root bypasses file mode permissions.")
        else:
            denied = subprocess.run([binary, str(path)], input="", text=True,
                                    capture_output=True, timeout=10)
            assert denied.returncode == 1 and "permission denied" in denied.stderr
            assert path.read_bytes() == before
    finally:
        path.chmod(0o600)
print("Process oracle: 10000 initial UTF-8 rows, 20000 mixed operations, five full snapshots passed.")
