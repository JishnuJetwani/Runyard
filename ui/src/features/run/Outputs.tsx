import { useInfiniteQuery, useMutation, useQuery } from '@tanstack/react-query';
import { Download, File } from 'lucide-react';
import { Empty, ErrorNotice, Loading, Panel } from '../../components/common';
import { api, saveFile } from '../../lib/api';
import { bytes, date, words } from '../../lib/format';
import type { Artifact, Run } from '../../lib/types';

export function Artifacts({ run, attempt }: { run: string; attempt: string }) {
  const query = useQuery({
    queryKey: ['artifacts', run, attempt],
    queryFn: ({ signal }) => api.artifacts(run, attempt, signal),
    enabled: Boolean(attempt),
    refetchInterval: 5000,
  });
  const download = useMutation({ mutationFn: (artifact: Artifact) => api.download(artifact) });
  return (
    <Panel title="Saved artifacts">
      <ErrorNotice error={query.error || download.error} retry={() => query.refetch()} />
      {attempt && query.isPending ? (
        <Loading />
      ) : !query.data?.items.length ? (
        <Empty title="No artifacts published">
          Files become available after their uploads finish.
        </Empty>
      ) : (
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>File</th>
                <th>Size</th>
                <th>SHA-256</th>
                <th>
                  <span className="sr-only">Download</span>
                </th>
              </tr>
            </thead>
            <tbody>
              {query.data.items.map((item) => (
                <tr key={item.id}>
                  <td>
                    <span className="file-name">
                      <File size={17} />
                      {item.path}
                    </span>
                  </td>
                  <td>{bytes(item.size)}</td>
                  <td>
                    <code title={item.sha256}>{item.sha256.slice(0, 16)}…</code>
                  </td>
                  <td>
                    <button
                      className="compact"
                      disabled={download.isPending}
                      onClick={() => download.mutate(item)}
                      aria-label={`Download ${item.path}`}
                    >
                      <Download size={14} />
                      Download
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Panel>
  );
}
export function Configuration({ run }: { run: Run }) {
  return (
    <Panel
      title="Immutable specification"
      action={
        <button
          className="compact"
          onClick={() =>
            saveFile(
              new Blob([JSON.stringify(run.spec, null, 2)], { type: 'application/json' }),
              `${run.spec.name.replace(/[^a-zA-Z0-9_-]/g, '_')}.json`,
            )
          }
        >
          <Download size={13} />
          Download JSON
        </button>
      }
    >
      <pre className="specification" tabIndex={0}>
        {JSON.stringify(run.spec, null, 2)}
      </pre>
    </Panel>
  );
}
export function Events({ run }: { run: string }) {
  const query = useInfiniteQuery({
    queryKey: ['events', run],
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) => api.events(run, pageParam, signal),
    getNextPageParam: (page) =>
      page.items.length === 100 ? page.items.at(-1)?.sequence : undefined,
    refetchInterval: 5000,
  });
  const events = query.data?.pages.flatMap((page) => page.items) || [];
  return (
    <Panel title="Durable events">
      <ErrorNotice error={query.error} retry={() => query.refetch()} />
      {query.isPending ? (
        <Loading />
      ) : !events.length ? (
        <Empty title="No events yet" />
      ) : (
        <ol className="event-list">
          {events.map((event) => (
            <li key={event.sequence}>
              <span className="event-dot" />
              <div>
                <strong>{words(event.kind)}</strong>
                <p>{event.detail}</p>
              </div>
              <time>{date(event.created_at)}</time>
            </li>
          ))}
        </ol>
      )}
      {query.hasNextPage && (
        <div className="table-footer">
          <button disabled={query.isFetchingNextPage} onClick={() => query.fetchNextPage()}>
            Load more events
          </button>
        </div>
      )}
    </Panel>
  );
}
