import { useInfiniteQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { Cpu, RefreshCw, Server } from 'lucide-react';
import { useState } from 'react';
import { Dialog, Empty, ErrorNotice, Loading, PageHeading, Panel } from '../components/common';
import { api } from '../lib/api';
import { date, number } from '../lib/format';
import type { Worker } from '../lib/types';

export default function Capacity() {
  const query = useInfiniteQuery({
    queryKey: ['capacity-pages'],
    initialPageParam: '',
    queryFn: ({ pageParam, signal }) => api.capacity(pageParam, signal),
    getNextPageParam: (last) => last.next_cursor || undefined,
    refetchInterval: 15000,
  });
  const capacity = query.data?.pages[0];
  const workers = [
    ...new Map(
      query.data?.pages.flatMap((page) => page.workers).map((worker) => [worker.id, worker]),
    ).values(),
  ];
  const nodes = [
    ...new Map(
      query.data?.pages.flatMap((page) => page.nodes).map((node) => [node.name, node]),
    ).values(),
  ];
  const [target, setTarget] = useState<Worker>();
  const client = useQueryClient();
  const mutation = useMutation({
    mutationFn: (worker: Worker) => api.drain(worker.id, !worker.drained),
    onSuccess: () => {
      setTarget(undefined);
      void client.invalidateQueries({ queryKey: ['capacity-pages'] });
      void client.invalidateQueries({ queryKey: ['capacity'] });
    },
  });
  return (
    <>
      <PageHeading
        title="Capacity"
        description="Check available resources and current reservations."
      >
        <button disabled={query.isFetching} onClick={() => query.refetch()}>
          <RefreshCw size={14} className={query.isFetching ? 'spin' : ''} />
          Refresh
        </button>
      </PageHeading>
      <ErrorNotice error={query.error} retry={() => query.refetch()} />
      {query.isPending ? (
        <Loading />
      ) : (
        capacity && (
          <>
            <div className="capacity-heading">
              <span className="backend-pill">
                <Server size={14} />
                {capacity.backend === 'docker' ? 'Docker workers' : 'Kubernetes cluster'}
              </span>
              <span>Updated every 15 seconds</span>
            </div>
            <div className="capacity-summary">
              <div>
                <span>GPU capacity</span>
                <strong>{capacity.gpu.capacity}</strong>
                <small>Whole NVIDIA devices</small>
              </div>
              <div>
                <span>Reserved</span>
                <strong>{capacity.gpu.reserved}</strong>
                <small>
                  {capacity.backend === 'docker'
                    ? 'Held until runtime cleanup'
                    : 'Effective bound Pod requests'}
                </small>
              </div>
              <div>
                <span>Estimated available</span>
                <strong>{capacity.gpu.available_estimate ?? 'N/A'}</strong>
                <small>
                  {capacity.gpu.available_estimate == null
                    ? 'Unknown · inventory is stale'
                    : 'Eligible, unreserved devices'}
                </small>
              </div>
              <div>
                <span>Pending GPU demand</span>
                <strong>{capacity.gpu.pending}</strong>
                <small>
                  {capacity.backend === 'docker'
                    ? 'Queued and retrying runs'
                    : 'Unscheduled Pod requests'}
                </small>
              </div>
            </div>
            {!capacity.gpu.fresh && capacity.gpu.capacity > 0 && (
              <div className="notice">
                GPU inventory is stale. Availability is unknown; existing reservations are retained.
                Last observation: {date(capacity.gpu.observed_at)}.
              </div>
            )}
            {capacity.backend === 'docker' ? (
              <Panel
                title="Worker inventory"
                action={<span className="help">{workers.length} loaded workers</span>}
              >
                {!workers.length ? (
                  <Empty title="No workers registered">
                    Start a Docker agent connected to this coordinator.
                  </Empty>
                ) : (
                  <div className="worker-list">
                    {workers.map((worker) => (
                      <WorkerRow
                        key={worker.id}
                        worker={worker}
                        onDrain={() => {
                          mutation.reset();
                          setTarget(worker);
                        }}
                      />
                    ))}
                  </div>
                )}
              </Panel>
            ) : (
              <Panel
                title="Node inventory"
                action={<span className="help">Availability per node</span>}
              >
                {!nodes.length ? (
                  <Empty title="No nodes observed">
                    Capacity will appear after a successful cluster inventory refresh.
                  </Empty>
                ) : (
                  <div className="table-scroll">
                    <table>
                      <thead>
                        <tr>
                          <th>Node</th>
                          <th>State</th>
                          <th>GPUs</th>
                          <th>Allocatable</th>
                          <th>Reserved</th>
                          <th>Available</th>
                        </tr>
                      </thead>
                      <tbody>
                        {nodes.map((node) => (
                          <tr key={node.name}>
                            <td>
                              <strong>{node.name}</strong>
                              <small className="cell-caption">
                                {node.eligible
                                  ? 'Runyard eligible'
                                  : 'Not eligible for Runyard GPUs'}
                              </small>
                            </td>
                            <td>
                              <span
                                className={`status ${node.ready && node.schedulable ? 'status-succeeded' : 'status-queued'}`}
                              >
                                <i />
                                {!node.ready
                                  ? 'Not ready'
                                  : !node.schedulable
                                    ? 'Cordoned'
                                    : 'Ready'}
                              </span>
                            </td>
                            <td>{node.capacity}</td>
                            <td>{node.allocatable}</td>
                            <td>{node.reserved}</td>
                            <td>{node.available_estimate ?? 'Unknown'}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>
                )}
              </Panel>
            )}
            {query.hasNextPage && (
              <div className="form-actions">
                <button disabled={query.isFetchingNextPage} onClick={() => query.fetchNextPage()}>
                  Load more {capacity.backend === 'docker' ? 'workers' : 'nodes'}
                </button>
              </div>
            )}
            <p className="page-footnote">
              {capacity.backend === 'docker'
                ? 'Reservations include attempts awaiting cleanup. Draining a worker prevents new assignments; current experiments continue.'
                : 'Cluster reservations include bound, nonterminal Pods across namespaces. Availability is an estimate; all GPUs for one run must fit on a single eligible node.'}
            </p>
          </>
        )
      )}
      {target && (
        <Dialog
          title={target.drained ? `Resume ${target.id}?` : `Drain ${target.id}?`}
          onClose={() => {
            if (!mutation.isPending) setTarget(undefined);
          }}
        >
          <p className="muted">
            {target.drained
              ? 'This worker will be eligible for new assignments when it is available.'
              : 'This worker will stop receiving new assignments. Its existing experiments continue and retain their resources.'}
          </p>
          <ErrorNotice error={mutation.error} />
          <div className="form-actions">
            <button disabled={mutation.isPending} onClick={() => setTarget(undefined)}>
              Go back
            </button>
            <button
              className="primary"
              disabled={mutation.isPending}
              onClick={() => mutation.mutate(target)}
            >
              {mutation.isPending ? 'Updating…' : target.drained ? 'Resume worker' : 'Drain worker'}
            </button>
          </div>
        </Dialog>
      )}
    </>
  );
}
function WorkerRow({ worker, onDrain }: { worker: Worker; onDrain: () => void }) {
  const inventory = worker.gpu_inventory;
  return (
    <article className="worker-row">
      <div className="worker-title">
        <span className="worker-icon">
          <Cpu size={20} strokeWidth={1.5} />
        </span>
        <div>
          <h3>{worker.id}</h3>
          <small>Heartbeat {date(worker.heartbeat_at)}</small>
        </div>
        <span
          className={`status ${worker.drained ? 'status-queued' : worker.available ? 'status-succeeded' : 'status-failed'}`}
        >
          <i />
          {worker.drained ? 'Drained' : worker.available ? 'Available' : 'Unavailable'}
        </span>
        <button className="compact" onClick={onDrain}>
          {worker.drained ? 'Resume' : 'Drain'}
        </button>
      </div>
      <div className="worker-resources">
        <ResourceBar
          name="CPU"
          used={worker.reserved.cpu_millis / 1000}
          total={worker.capacity.cpu_millis / 1000}
          unit="cores"
        />
        <ResourceBar
          name="Memory"
          used={worker.reserved.memory_mib}
          total={worker.capacity.memory_mib}
          unit="MiB"
        />
        <ResourceBar
          name="GPU"
          used={worker.reserved.gpu_count || 0}
          total={worker.capacity.gpu_count || 0}
          unit="devices"
        />
      </div>
      {inventory.capable && (
        <details className="gpu-inventory">
          <summary>
            {inventory.devices.length} GPU devices{' '}
            <span>
              {!inventory.fresh
                ? 'Inventory stale'
                : !inventory.ready
                  ? 'Not ready for GPU work'
                  : 'Inventory fresh'}
            </span>
          </summary>
          <div className="device-list">
            {inventory.devices.map((device) => (
              <p key={device.uuid}>
                <strong>
                  {device.name} · {number(device.memory_mib)} MiB
                </strong>
                <code>{device.uuid}</code>
                <span>{device.eligible ? 'Eligible' : device.reason || 'Ineligible'}</span>
              </p>
            ))}
          </div>
          <p className="help">
            Observed {date(inventory.observed_at)} · Available estimate:{' '}
            {inventory.available_estimate ?? 'unknown'}
          </p>
        </details>
      )}
    </article>
  );
}
function ResourceBar({
  name,
  used,
  total,
  unit,
}: {
  name: string;
  used: number;
  total: number;
  unit: string;
}) {
  return (
    <div className="resource-bar">
      <div>
        <span>{name}</span>
        <strong>
          {number(used)}{' '}
          <span>
            / {number(total)} {unit}
          </span>
        </strong>
      </div>
      <div
        className="bar-track"
        role="meter"
        aria-label={`${name} reserved`}
        aria-valuenow={used}
        aria-valuemin={0}
        aria-valuemax={Math.max(total, used, 1)}
      >
        <i style={{ width: `${total ? Math.min(100, (used / total) * 100) : 0}%` }} />
      </div>
    </div>
  );
}
