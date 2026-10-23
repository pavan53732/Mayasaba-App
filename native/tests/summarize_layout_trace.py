"""Summarize a quiet interval of the opt-in native geometry trace.

Visibility is an element's own XAML Visibility, not proof of reachability.
Use the recording and interaction checks for clipping/scroll behavior.
"""
import argparse
import csv
import json
import math
from pathlib import Path


def summarize(path, seconds, end_ms=None, require_search=False):
    with path.open(encoding='utf-8') as source:
        all_rows = list(csv.DictReader(source))
    end = end_ms if end_ms is not None else max(float(r['elapsed_ms']) for r in all_rows)
    start = end - seconds * 1000
    rows = [r for r in all_rows if start <= float(r['elapsed_ms']) <= end]
    report = {'trace': str(path), 'start_ms': start, 'end_ms': end, 'surfaces': {}}
    for name in ('chat', 'header', 'attention', 'problems', 'composer', 'search', 'results', 'paging'):
        samples = [r for r in rows if r['element'] == name]
        if not samples:
            report['surfaces'][name] = {'stable': False, 'reason': 'No samples'}
            continue
        span = float(samples[-1]['elapsed_ms']) - float(samples[0]['elapsed_ms'])
        fields = ('x', 'y', 'width', 'height', 'visible')
        ranges = {k: max(float(r[k]) for r in samples) - min(float(r[k]) for r in samples) for k in fields}
        report['surfaces'][name] = {
            'samples': len(samples), 'span_ms': span, 'range': ranges,
            'last': {k: samples[-1][k] for k in fields},
            'stable': span >= 10000 and all(math.isfinite(v) and v <= 0.5 for v in ranges.values()),
        }
    report['stable'] = all(s['stable'] for s in report['surfaces'].values())
    if require_search:
        visible = all(float(r['visible']) == 1 and float(r['width']) > 0 and float(r['height']) > 0
                      for r in rows if r['element'] in ('search', 'results', 'paging'))
        report['search_visible_geometry'] = visible
        report['stable'] = report['stable'] and visible
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--end-ms', type=float)
    parser.add_argument('--require-search', action='store_true',
                        help='Reject a stable but hidden/zero-sized search, results or paging surface')
    args = parser.parse_args()
    if args.seconds < 10:
        parser.error('The observation interval must be at least 10 seconds')
    result = summarize(args.trace, args.seconds, args.end_ms, args.require_search)
    args.trace.with_suffix('.summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['stable'] else 1)
