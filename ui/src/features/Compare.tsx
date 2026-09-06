import { useQueries } from '@tanstack/react-query';
import { GitCompareArrows, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import {
  Empty,
  ErrorNotice,
  Loading,
  PageHeading,
  Panel,
  RunLink,
  StatusBadge,
} from '../components/common';
import { chartColors, MetricChart, type MetricSeries } from '../components/MetricChart';
import { api } from '../lib/api';
import { shortId } from '../lib/format';
import { useTelemetry } from '../lib/telemetry';
import { terminal, type Run } from '../lib/types';

export default function Compare() {
  const [params, setParams] = useSearchParams();
  const ids = [...new Set((params.get('runs') || '').split(',').filter(Boolean))].slice(0, 4);
  const queries = useQueries({
    queries: ids.map((id) => ({
      queryKey: ['run', id],
      queryFn: ({ signal }: { signal: AbortSignal }) => api.run(id, signal),
      refetchInterval: 5000,
    })),
  });
  const runs = queries.flatMap((query) => (query.data ? [query.data] : []));
  const selectionKey = runs.map((run) => `${run.id}:${run.active_attempt}`).join(',');
  return (
    <>
      <PageHeading
        title="Compare experiments"
        description="Compare metrics and parameters across up to four runs."
      >
        <Link className="button" to="/">
          Choose experiments
        </Link>
      </PageHeading>
      {!ids.length ? (
        <section className="panel">
          <Empty title="Choose runs to compare">
            <GitCompareArrows size={28} strokeWidth={1.4} />
            <br />
            Select up to four runs from the experiment table to compare metrics and parameters.
            <br />
            <Link className="inline-link" to="/">
              Browse experiments →
            </Link>
          </Empty>
        </section>
      ) : (
        <>
          {queries.map(
            (query, index) =>
              query.error && (
                <ErrorNotice key={ids[index]} error={query.error} retry={() => query.refetch()} />
              ),
          )}
          <div className="compare-cards">
            {runs.map((run, index) => (
              <div
                key={run.id}
                className="compare-run"
                style={{ borderTopColor: chartColors[index] }}
              >
                <button
                  className="icon-button"
                  aria-label={`Remove ${run.spec.name}`}
                  onClick={() => setParams({ runs: ids.filter((id) => id !== run.id).join(',') })}
                >
                  <X size={15} />
                </button>
                <RunLink id={run.id}>{run.spec.name}</RunLink>
                <code>{shortId(run.id)}</code>
                <StatusBadge status={run.status} />
              </div>
            ))}
          </div>
          {queries.some((query) => query.isPending) ? (
            <Loading />
          ) : (
            <Comparison key={selectionKey} runs={runs} />
          )}
        </>
      )}
    </>
  );
}
function Comparison({ runs }: { runs: Run[] }) {
  const [series, setSeries] = useState<Record<string, MetricSeries>>({});
  const names = [...new Set(runs.flatMap((run) => Object.keys(run.spec.parameters)))].sort();
  return (
    <>
      {runs.map((run) => (
        <MetricSource
          key={`${run.id}:${run.active_attempt}`}
          run={run}
          onData={(entry) => setSeries((current) => ({ ...current, [run.id]: entry }))}
        />
      ))}
      <Panel
        title="Metric comparison"
        action={<span className="help">Current attempt of each run</span>}
      >
        <MetricChart
          series={runs.map(
            (run) =>
              series[run.id] || {
                id: run.id,
                name: `${run.spec.name} · ${shortId(run.id)}`,
                items: [],
              },
          )}
        />
      </Panel>
      <Panel title="Parameters">
        <div className="table-scroll">
          <table className="comparison-table">
            <thead>
              <tr>
                <th>Parameter</th>
                {runs.map((run) => (
                  <th key={run.id}>
                    {run.spec.name}
                    <br />
                    <code>{shortId(run.id)}</code>
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {names.map((name) => {
                const values = runs.map((run) => JSON.stringify(run.spec.parameters[name]));
                const differs = new Set(values).size > 1;
                return (
                  <tr key={name} className={differs ? 'parameter-difference' : ''}>
                    <td>
                      {name}
                      {differs && <small>Varies</small>}
                    </td>
                    {values.map((value, index) => (
                      <td key={runs[index].id}>
                        <code>{value ?? 'N/A'}</code>
                      </td>
                    ))}
                  </tr>
                );
              })}
              {!names.length && (
                <tr>
                  <td colSpan={runs.length + 1}>These experiments have no parameters.</td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </Panel>
    </>
  );
}
function MetricSource({ run, onData }: { run: Run; onData: (entry: MetricSeries) => void }) {
  const metrics = useTelemetry('metrics', run.id, run.active_attempt, !terminal(run.status));
  const [callback] = useState(() => onData);
  useEffect(() => {
    callback({ id: run.id, name: `${run.spec.name} · ${shortId(run.id)}`, items: metrics.items });
  }, [metrics.items, run.id, run.spec.name, callback]);
  return (
    <>
      <ErrorNotice error={metrics.error} retry={metrics.retry} />
      {metrics.trimmed && (
        <p className="notice">{run.spec.name}: showing the latest 10,000 metric samples.</p>
      )}
    </>
  );
}
