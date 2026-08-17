#!/usr/bin/env python3
"""Analyze Renz-Fi serial [mem]/[health] logs for production qualification trends.

Usage:
  py -3 production-qualification.py --log serial_capture.txt
  py -3 production-qualification.py --log serial_capture.txt --min-samples 360

Optional HTTP smoke (requires appliance IP):
  py -3 production-qualification.py --host 192.168.88.1 --http-cycles 50

Pass criteria (default thresholds):
  - No fatal strings in log
  - heap/dma largest block drift <= 4096 bytes across run
  - heap/dma minimum does not monotonically degrade in final quartile
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

MEM_RE = re.compile(
    r"\[mem\] heap=(\d+) largest=(\d+) minimum=(\d+) dma=(\d+) largest=(\d+) "
    r"minimum=(\d+) jobs=(\d+) queue=(\d+) sse=(\d+) inspection=(\d+) "
    r"wificache=(\d+) portal=(\d+) roscpu=(\d+)"
)

FATAL_PATTERNS = (
    "Guru Meditation",
    "LoadProhibited",
    "Failed to allocate priv TX buffer",
    "Failed to allocate priv RX buffer",
    "setup_dma_priv_buffer",
    "spi transmit failed",
    "write TX buffer failed",
    "abort() was called",
)


@dataclass
class MemSample:
    heap: int
    heap_largest: int
    heap_minimum: int
    dma: int
    dma_largest: int
    dma_minimum: int
    jobs: int
    queue: int
    sse: int
    inspection: int
    wificache: int
    portal: int
    roscpu: int


@dataclass
class QualReport:
    samples: list[MemSample] = field(default_factory=list)
    fatals: list[str] = field(default_factory=list)
    http_errors: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def parse_log(path: Path) -> QualReport:
    report = QualReport()
    text = path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        for pat in FATAL_PATTERNS:
            if pat in line:
                report.fatals.append(line.strip())
        m = MEM_RE.search(line)
        if not m:
            continue
        vals = tuple(int(x) for x in m.groups())
        report.samples.append(MemSample(*vals))
    return report


def drift(a: list[int]) -> int:
    if len(a) < 2:
        return 0
    return max(a) - min(a)


def analyze(report: QualReport, min_samples: int) -> tuple[bool, list[str]]:
    issues: list[str] = []
    if report.fatals:
        issues.append(f"fatal events: {len(report.fatals)}")
    if len(report.samples) < min_samples:
        issues.append(
            f"insufficient [mem] samples: {len(report.samples)} (need>={min_samples})"
        )
    if not report.samples:
        return False, issues

    heap_l = [s.heap_largest for s in report.samples]
    dma_l = [s.dma_largest for s in report.samples]
    heap_min = [s.heap_minimum for s in report.samples]
    dma_min = [s.dma_minimum for s in report.samples]

    if drift(heap_l) > 4096:
        issues.append(f"heap largest drift {drift(heap_l)} B (>4096)")
    if drift(dma_l) > 4096:
        issues.append(f"DMA largest drift {drift(dma_l)} B (>4096)")

    q = max(1, len(report.samples) // 4)
    tail_heap_min = heap_min[-q:]
    tail_dma_min = dma_min[-q:]
    if len(tail_heap_min) >= 2 and tail_heap_min[-1] < tail_heap_min[0] - 2048:
        issues.append("heap minimum degraded in final quartile")
    if len(tail_dma_min) >= 2 and tail_dma_min[-1] < tail_dma_min[0] - 1024:
        issues.append("DMA minimum degraded in final quartile")

    report.notes.append(f"[mem] samples={len(report.samples)}")
    report.notes.append(
        f"heap free mean={statistics.mean(s.heap for s in report.samples):.0f} "
        f"largest drift={drift(heap_l)} min drift={drift(heap_min)}"
    )
    report.notes.append(
        f"dma free mean={statistics.mean(s.dma for s in report.samples):.0f} "
        f"largest drift={drift(dma_l)} min drift={drift(dma_min)}"
    )
    report.notes.append(
        f"sse max={max(s.sse for s in report.samples)} "
        f"inspection max={max(s.inspection for s in report.samples)} "
        f"wificache max={max(s.wificache for s in report.samples)}"
    )
    return len(issues) == 0, issues


def http_smoke(host: str, cycles: int) -> list[str]:
    paths = ("/", "/generate_204", "/api/setup/status", "/admin/setup")
    errors: list[str] = []
    for i in range(cycles):
        for path in paths:
            url = f"http://{host}{path}"
            try:
                with urllib.request.urlopen(url, timeout=5) as resp:
                    resp.read(256)
            except urllib.error.HTTPError as exc:
                if exc.code >= 500:
                    errors.append(f"{url} -> HTTP {exc.code}")
            except Exception as exc:  # noqa: BLE001 — qualification harness
                errors.append(f"{url} -> {exc}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Renz-Fi production qualification analyzer")
    parser.add_argument("--log", type=Path, help="Serial capture containing [mem] lines")
    parser.add_argument("--min-samples", type=int, default=360,
                        help="Minimum [mem] samples (360 ~= 1h @10s)")
    parser.add_argument("--host", help="Appliance IP for optional HTTP smoke")
    parser.add_argument("--http-cycles", type=int, default=0)
    args = parser.parse_args()

    report = QualReport()
    if args.log:
        if not args.log.exists():
            print(f"missing log: {args.log}", file=sys.stderr)
            return 2
        report = parse_log(args.log)

    if args.host and args.http_cycles > 0:
        report.http_errors = http_smoke(args.host, args.http_cycles)

    ok, issues = analyze(report, args.min_samples)
    if report.http_errors:
        ok = False
        issues.append(f"HTTP smoke errors: {len(report.http_errors)}")

    print("=== Renz-Fi Production Qualification ===")
    for note in report.notes:
        print(note)
    if report.fatals:
        print(f"FATAL lines: {len(report.fatals)}")
        for line in report.fatals[:10]:
            print(f"  {line}")
    if report.http_errors:
        print(f"HTTP errors: {len(report.http_errors)}")
        for err in report.http_errors[:10]:
            print(f"  {err}")
    if issues:
        print("RESULT: FAIL")
        for issue in issues:
            print(f"  - {issue}")
        return 1

    print("RESULT: PASS (log thresholds)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
