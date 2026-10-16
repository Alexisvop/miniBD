"""Validate raw trial groups and report median/min/max/MAD; Python stdlib only."""
import csv
from collections import defaultdict
import math
import statistics
import sys

groups = defaultdict(list)
with open(sys.argv[1], newline="", encoding="utf-8") as source:
    for row in csv.DictReader(source):
        groups[(int(row["records"]), row["operation"])].append(row)
expected_operations = {"insert", "sync_insert", "open_validate", "indexed_lookup",
                       "sequential_lookup", "update", "logical_delete", "ordered_scan",
                       "age_filter", "reactivate", "sync_changes"}
if set(groups) != {(n, op) for n in (1000, 10000, 50000, 100000) for op in expected_operations}:
    raise ValueError("Incomplete experiment matrix")
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(("records", "operation", "operations", "trials", "median_ms", "min_ms", "max_ms", "mad_ms", "median_ns_per_operation"))
for (records, operation), rows in sorted(groups.items()):
    if sorted(int(r["trial"]) for r in rows) != [1, 2, 3, 4, 5]:
        raise ValueError(f"Missing or repeated trials: {records}/{operation}")
    if len({(r["checksum"], r["operations"]) for r in rows}) != 1:
        raise ValueError(f"Inconsistent workload/results: {records}/{operation}")
    values = [float(r["elapsed_ms"]) for r in rows]
    if not all(math.isfinite(value) and value > 0 for value in values):
        raise ValueError("Timings must be positive finite milliseconds")
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    operations = int(rows[0]["operations"])
    writer.writerow((records, operation, operations, len(rows), f"{median:.6f}",
                     f"{min(values):.6f}", f"{max(values):.6f}", f"{mad:.6f}",
                     f"{median * 1e6 / operations:.2f}"))
