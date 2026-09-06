import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { ArrowLeft, RotateCcw, Square } from 'lucide-react';
import { useRef, useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import { Dialog, ErrorNotice, Loading, PageHeading, StatusBadge } from '../components/common';
import { Metrics, Logs } from './run/Telemetry';
import { Artifacts, Configuration, Events } from './run/Outputs';
import { AttemptHistory } from './run/AttemptHistory';
import { api } from '../lib/api';
import { date, number, shortId, words } from '../lib/format';
import { terminal, type Run } from '../lib/types';

export default function RunDetail() {
  const { id = '' } = useParams();
  const query = useQuery({
    queryKey: ['run', id],
    queryFn: ({ signal }) => api.run(id, signal),
    refetchInterval: 3000,
  });
  if (query.isPending) return <Loading />;
  if (query.isError) return <ErrorNotice error={query.error} retry={() => query.refetch()} />;
  return <RunView key={id} run={query.data} />;
}
function RunView({ run }: { run: Run }) {
  const [tab, setTab] = useState('Metrics');
  const [attemptId, setAttemptId] = useState('');
  const [confirm, setConfirm] = useState<'cancel' | 'rerun' | null>(null);
  const requestKey = useRef(crypto.randomUUID());
  const client = useQueryClient();
  const navigate = useNavigate();
  const attempts = useQuery({
    queryKey: ['attempts', run.id],
    queryFn: ({ signal }) => api.attempts(run.id, signal),
    refetchInterval: 3000,
  });
  const selected = attemptId || run.active_attempt;
  const attempt = attempts.data?.items.find((item) => item.id === selected);
  const mutation = useMutation({
    mutationFn: (action: 'cancel' | 'rerun') =>
      action === 'cancel' ? api.cancel(run.id) : api.rerun(run.id, requestKey.current),
    onSuccess: (result) => {
      setConfirm(null);
      void client.invalidateQueries();
      if (result.id !== run.id) navigate(`/runs/${result.id}`);
    },
  });
  return (
    <>
      <Link className="back-link" to="/">
        <ArrowLeft size={14} />
        Experiments
      </Link>
      <PageHeading
        title={run.spec.name}
        description={`Created ${date(run.created_at)} · ${shortId(run.id)}`}
      >
        <Link className="button" to="/new" state={{ spec: run.spec }}>
          Use as template
        </Link>
        {terminal(run.status) ? (
          <button className="primary" onClick={() => setConfirm('rerun')}>
            <RotateCcw size={14} />
            Rerun
          </button>
        ) : (
          <button className="danger" onClick={() => setConfirm('cancel')}>
            <Square size={13} />
            Cancel run
          </button>
        )}
      </PageHeading>
      <div className="run-summary">
        <div>
          <span>Status</span>
          <StatusBadge status={run.status} />
        </div>
        <div>
          <span>Compute</span>
          <strong>
            {number(run.spec.resources.cpu_millis / 1000)} CPU ·{' '}
            {number(run.spec.resources.memory_mib)} MiB
          </strong>
        </div>
        <div>
          <span>GPUs</span>
          <strong>
            {run.spec.resources.gpu_count || 0}
            <small> whole devices</small>
          </strong>
        </div>
        <div>
          <span>Attempts</span>
          <strong>
            {run.generation} <small>/ {run.spec.retry.max_attempts}</small>
          </strong>
        </div>
        <div>
          <span>Timeout</span>
          <strong>
            {number(run.spec.timeout_seconds / 60)}
            <small> min</small>
          </strong>
        </div>
      </div>
      {run.parent_run_id && (
        <p className="lineage">
          Rerun of <Link to={`/runs/${run.parent_run_id}`}>{shortId(run.parent_run_id)}</Link>
        </p>
      )}
      {run.sweep_id && (
        <p className="lineage">
          Sweep <code>{run.sweep_id}</code>
        </p>
      )}
      {attempts.data?.items.some(
        (item) => terminal(item.status) && item.cleanup_status === 'PENDING',
      ) && (
        <div className="notice">
          <strong>Runtime cleanup is pending.</strong> The terminal decision is recorded. Resources
          stay reserved until execution has stopped.
        </div>
      )}
      <div className="detail-toolbar">
        <div className="tabs" role="tablist" aria-label="Run details">
          {['Metrics', 'Logs', 'Artifacts', 'Attempts', 'Configuration', 'Events'].map((value) => (
            <button
              key={value}
              role="tab"
              id={`tab-${value}`}
              aria-selected={tab === value}
              aria-controls="detail-panel"
              onClick={() => setTab(value)}
            >
              {value}
            </button>
          ))}
        </div>
      </div>
      <ErrorNotice error={attempts.error} retry={() => attempts.refetch()} />
      <div className="attempt-toolbar">
        <label>
          Viewing
          <select
            aria-label="Attempt"
            value={attemptId}
            onChange={(event) => setAttemptId(event.target.value)}
          >
            <option value="">Current attempt{run.generation ? ` · #${run.generation}` : ''}</option>
            {attempts.data?.items.map((item) => (
              <option key={item.id} value={item.id}>
                Attempt #{item.generation} · {words(item.status)}
              </option>
            ))}
          </select>
        </label>
        <span>
          {attempt?.node_name || attempt?.worker_id || 'Waiting for assignment'}
          {attempt?.started_at ? ` · started ${date(attempt.started_at)}` : ''}
        </span>
      </div>
      <div id="detail-panel" role="tabpanel" aria-labelledby={`tab-${tab}`}>
        {tab === 'Metrics' && (
          <Metrics
            run={run}
            attempt={selected}
            live={attempt ? !terminal(attempt.status) : !terminal(run.status)}
          />
        )}
        {tab === 'Logs' && (
          <Logs
            run={run.id}
            attempt={selected}
            live={attempt ? !terminal(attempt.status) : !terminal(run.status)}
          />
        )}
        {tab === 'Artifacts' && <Artifacts run={run.id} attempt={selected} />}
        {tab === 'Attempts' && (
          <AttemptHistory
            attempts={attempts.data?.items || []}
            onSelect={(value) => {
              setAttemptId(value);
              setTab('Logs');
            }}
          />
        )}
        {tab === 'Configuration' && <Configuration run={run} />}
        {tab === 'Events' && <Events run={run.id} />}
      </div>
      {confirm && (
        <Dialog
          title={confirm === 'cancel' ? 'Cancel this experiment?' : 'Run this experiment again?'}
          onClose={() => {
            if (!mutation.isPending) setConfirm(null);
          }}
        >
          <p className="muted">
            {confirm === 'cancel'
              ? 'Runyard will accept cancellation and stop the active attempt. If the worker is unreachable, cleanup remains pending until it can be confirmed.'
              : 'This creates a new linked run with the exact same configuration. The current run and its results stay in your history.'}
          </p>
          <ErrorNotice error={mutation.error} />
          <div className="form-actions">
            <button disabled={mutation.isPending} onClick={() => setConfirm(null)}>
              Go back
            </button>
            <button
              className={confirm === 'cancel' ? 'danger' : 'primary'}
              disabled={mutation.isPending}
              onClick={() => mutation.mutate(confirm)}
            >
              {mutation.isPending
                ? 'Working…'
                : confirm === 'cancel'
                  ? 'Confirm cancellation'
                  : 'Create rerun'}
            </button>
          </div>
        </Dialog>
      )}
    </>
  );
}
