import { Download, Terminal } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { Empty, ErrorNotice, Loading, Panel } from '../../components/common';
import { MetricChart } from '../../components/MetricChart';
import { saveFile } from '../../lib/api';
import { useTelemetry } from '../../lib/telemetry';
import type { Run, Telemetry } from '../../lib/types';

function exportMetrics(items: Telemetry[], filename: string, format: 'csv' | 'json') {
  const quote = (value: unknown) => `"${String(value).replaceAll('"', '""')}"`;
  const text =
    format === 'json'
      ? JSON.stringify(items, null, 2)
      : [
          'sequence,name,step,value,timestamp_ms',
          ...items.map((item) =>
            [item.sequence, item.name, item.step, item.value, item.timestamp_ms]
              .map(quote)
              .join(','),
          ),
        ].join('\n');
  saveFile(
    new Blob([text], { type: format === 'json' ? 'application/json' : 'text/csv' }),
    `${filename}.${format}`,
  );
}
export function Metrics({ run, attempt, live }: { run: Run; attempt: string; live: boolean }) {
  const metrics = useTelemetry('metrics', run.id, attempt, live);
  return (
    <Panel
      title="Reported metrics"
      action={
        <div className="actions">
          <button
            className="compact"
            disabled={!metrics.items.length}
            onClick={() => exportMetrics(metrics.items, `metrics-${attempt}`, 'csv')}
          >
            CSV
            <Download size={13} />
          </button>
          <button
            className="compact"
            disabled={!metrics.items.length}
            onClick={() => exportMetrics(metrics.items, `metrics-${attempt}`, 'json')}
          >
            JSON
            <Download size={13} />
          </button>
        </div>
      }
    >
      <ErrorNotice error={metrics.error} retry={metrics.retry} />
      {metrics.trimmed && (
        <p className="notice">
          Showing the most recent 10,000 samples. Exports contain this loaded set.
        </p>
      )}
      {metrics.loading ? (
        <Loading />
      ) : (
        <MetricChart series={[{ id: run.id, name: run.spec.name, items: metrics.items }]} />
      )}
    </Panel>
  );
}
export function Logs({ run, attempt, live }: { run: string; attempt: string; live: boolean }) {
  const logs = useTelemetry('logs', run, attempt, live);
  const [follow, setFollow] = useState(true);
  const viewport = useRef<HTMLPreElement>(null);
  useEffect(() => {
    if (follow && viewport.current) viewport.current.scrollTop = viewport.current.scrollHeight;
  }, [logs.items, follow]);
  return (
    <Panel
      title="Execution logs"
      className="logs-panel"
      action={
        <div className="actions">
          <label className="check-label">
            <input
              type="checkbox"
              checked={follow}
              onChange={(event) => setFollow(event.target.checked)}
            />
            Follow
          </label>
          <button
            className="compact"
            disabled={!logs.items.length}
            onClick={() =>
              saveFile(
                new Blob([logs.items.map((item) => item.text).join('')], { type: 'text/plain' }),
                `${attempt}.log`,
              )
            }
          >
            <Download size={13} />
            Save loaded logs
          </button>
        </div>
      }
    >
      <ErrorNotice error={logs.error} retry={logs.retry} />
      {logs.trimmed && (
        <p className="notice">
          Earlier text was dropped from this bounded view. Download the log archive from Artifacts
          for retained output.
        </p>
      )}
      {logs.loading ? (
        <Loading />
      ) : !logs.items.length ? (
        <Empty title="No output yet">
          Stdout and stderr will appear here once the command starts.
        </Empty>
      ) : (
        <pre className="log-viewport" ref={viewport} tabIndex={0} aria-label="Experiment logs">
          {logs.items.map((item) => (
            <span key={item.sequence} className={`log-${item.kind}`}>
              {item.text}
            </span>
          ))}
        </pre>
      )}
      <div className="log-footer">
        <Terminal size={13} />
        <span>
          {live ? 'Polling every second' : 'Attempt output'} · {logs.items.length} loaded chunks
        </span>
      </div>
    </Panel>
  );
}
