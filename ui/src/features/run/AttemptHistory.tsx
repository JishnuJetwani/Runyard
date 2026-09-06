import { Empty, Panel, StatusBadge } from '../../components/common';
import { date, words } from '../../lib/format';
import type { Attempt } from '../../lib/types';

export function AttemptHistory({
  attempts,
  onSelect,
}: {
  attempts: Attempt[];
  onSelect: (id: string) => void;
}) {
  return (
    <Panel title="Attempt history">
      {!attempts.length ? (
        <Empty title="Waiting for the first attempt">The experiment is queued for execution.</Empty>
      ) : (
        <div className="attempt-list">
          {[...attempts].reverse().map((item) => (
            <article key={item.id}>
              <div className="attempt-title">
                <strong>Attempt #{item.generation}</strong>
                <StatusBadge status={item.status} />
                <button className="compact" onClick={() => onSelect(item.id)}>
                  View logs
                </button>
              </div>
              <dl className="metadata-grid">
                <div>
                  <dt>Placement</dt>
                  <dd>{item.node_name || item.worker_id || 'Unassigned'}</dd>
                </div>
                <div>
                  <dt>Started</dt>
                  <dd>{date(item.started_at)}</dd>
                </div>
                <div>
                  <dt>Finished</dt>
                  <dd>{date(item.finished_at)}</dd>
                </div>
                <div>
                  <dt>Exit code</dt>
                  <dd>{item.exit_code ?? 'N/A'}</dd>
                </div>
                <div>
                  <dt>Cleanup</dt>
                  <dd>{item.cleanup_status === 'PENDING' ? 'Pending' : 'Complete'}</dd>
                </div>
                <div>
                  <dt>Requested GPUs</dt>
                  <dd>{item.gpu_count}</dd>
                </div>
              </dl>
              {item.reason && <p className="attempt-reason">{words(item.reason)}</p>}
              {item.gpu_allocations?.length > 0 && (
                <div className="device-list">
                  {item.gpu_allocations.map((allocation) => (
                    <p key={allocation.device.uuid}>
                      <code>{allocation.device.uuid}</code>
                      <span>
                        {allocation.device.name} ·{' '}
                        {allocation.released_at
                          ? `Released ${date(allocation.released_at)}`
                          : 'Reserved until cleanup'}
                      </span>
                    </p>
                  ))}
                </div>
              )}
              <code className="muted">{item.id}</code>
            </article>
          ))}
        </div>
      )}
    </Panel>
  );
}
