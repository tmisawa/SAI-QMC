#!/usr/bin/env python3
"""Aggregate the published Japanese work summaries, retaining raw model labels.

Date: 2026-09-08; model: OpenAI GPT-6 (Codex).
Historical malformed frontmatter is handled using the original model-line
anchor approach. Original dates and model strings are not rewritten.
"""
import argparse
import csv
from collections import Counter
from pathlib import Path
import re
import sys


def entries(path):
    lines = path.read_text().splitlines()
    for i, line in enumerate(lines):
        match = re.match(r'^model:\s*(.+)$', line)
        if not match:
            continue
        entry = {'model': match.group(1).strip(), 'file': path.name, 'line': i + 1}
        for near in lines[max(0, i - 6):i + 7]:
            date = re.match(r'^date:\s*(\d{4}-\d{2}-\d{2})', near)
            dt = re.match(r'^datetime:\s*(\d{4}-\d{2}-\d{2})(.*)', near)
            if date:
                entry.setdefault('date', date.group(1))
            if dt:
                entry['datetime'] = dt.group(1) + dt.group(2)
                entry.setdefault('date', dt.group(1))
        if 'date' not in entry:
            raise ValueError(f'{path.name}:{i + 1}: model entry has no date')
        yield entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--period', choices=('all', 'hackathon', 'after'), default='all')
    parser.add_argument('--entries', action='store_true', help='write one TSV row per entry')
    args = parser.parse_args()
    records = [e for p in sorted(Path(__file__).with_name('logs').glob('*.md')) for e in entries(p)]
    if args.period == 'hackathon':
        records = [e for e in records if '2026-06-24' <= e['date'] <= '2026-06-26']
    elif args.period == 'after':
        records = [e for e in records if e['date'] > '2026-06-26']
    if args.entries:
        writer = csv.DictWriter(sys.stdout, fieldnames=('date', 'datetime', 'model', 'file', 'line'),
                                delimiter='\t', lineterminator='\n', restval='')
        writer.writeheader()
        writer.writerows(sorted(records, key=lambda e: (e['date'], e.get('datetime', ''), e['file'])))
    else:
        print(f'period\t{args.period}\nentries\t{len(records)}')
        print('\nrecorded_model\tentries')
        for name, count in sorted(Counter(e['model'] for e in records).items()):
            print(f'{name}\t{count}')
        print('\ndate\tentries')
        for date, count in sorted(Counter(e['date'] for e in records).items()):
            print(f'{date}\t{count}')


if __name__ == '__main__':
    main()
