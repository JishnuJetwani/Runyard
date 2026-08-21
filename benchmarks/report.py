#!/usr/bin/env python3
"""Render benchmark summaries from the checked-in measurements."""
import argparse
import json
import os
import pathlib


def number(value):
    return 'unavailable' if value is None else f'{value:.3f}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--results', default='benchmarks/results')
    parser.add_argument('--output', default='benchmarks/RESULTS.md')
    args = parser.parse_args()
    output = pathlib.Path(args.output)
    measurements = [(path, json.loads(path.read_text())) for path in sorted(pathlib.Path(args.results).glob('*-local.json'))]
    if not measurements:
        parser.error('no recorded measurements found')
    lines = ['# Local measurements', '',
             'These CPU fixture measurements use logical workers on one shared local host.',
             'Each backend has one recorded sample; see the raw files for exact configuration.', '',
             'Each backend uses its recorded admission settings. CPU and memory figures',
             'cover the coordinator process.', '',
             '| Backend | Runs | Runs/s | Queue p50 / p95 (s) | Dispatch p50 / p95 (s) | CPU time (s) | Peak RSS (MiB) |',
             '|---|---:|---:|---:|---:|---:|---:|']
    for _, result in measurements:
        queue, dispatch = result['queue_delay_seconds'], result['dispatch_latency_seconds']
        rss = result['coordinator_peak_rss_bytes']
        lines.append(f"| {result['backend']} | {result['count']} | {number(result['completed_runs_per_second'])} | "
                     f"{number(queue['p50'])} / {number(queue['p95'])} | {number(dispatch['p50'])} / {number(dispatch['p95'])} | "
                     f"{number(result['coordinator_cpu_seconds'])} | {number(None if rss is None else rss / 1024**2)} |")
    for path, result in measurements:
        hardware = result['hardware']
        raw = os.path.relpath(path, output.parent)
        lines += ['', f"## {result['backend']}", '',
                  f"Recorded {result['timestamp_utc']}; runtime source revision `{result['revision']}`.", '',
                  f"Host: {hardware.get('cpu', hardware['architecture'])}, {hardware['logical_cpus']} logical CPUs.",
                  f"Docker VM: {hardware['docker_vm']['NCPU']} CPUs, {hardware['docker_vm']['MemTotal']/1024**3:.2f} GiB RAM.",
                  f"Non-Runyard containers running: {hardware['other_running_container_count']}.", '',
                  'Interruption-to-replacement claim (seconds): ' + ', '.join(number(v) for v in result.get('recovery_seconds', [])) + '.', '',
                  f"[Raw observations and exact settings]({raw}).", '']
        lines.extend('- ' + limitation for limitation in result['limitations'])
    lines += ['', 'Regenerate with `python3 benchmarks/report.py`. Definitions and workload instructions',
              'are in [README.md](README.md).', '']
    output.write_text('\n'.join(lines))
    print(f'Wrote {output} from {len(measurements)} recorded measurements')


if __name__ == '__main__':
    main()
