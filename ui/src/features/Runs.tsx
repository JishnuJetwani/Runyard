import { useInfiniteQuery } from '@tanstack/react-query';
import { ArrowDown, ArrowUpRight, Plus, Search, SlidersHorizontal } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { Empty, ErrorNotice, Loading, PageHeading, RunLink, StatusBadge } from '../components/common';
import { api } from '../lib/api';
import { date, number, shortId, words } from '../lib/format';
import { statuses } from '../lib/types';

export default function Runs() {
  const [status, setStatus] = useState('');
  const [search, setSearch] = useState('');
  const [selected, setSelected] = useState<string[]>([]);
  const query = useInfiniteQuery({ queryKey: ['runs', status], initialPageParam: '', queryFn: ({ pageParam, signal }) => api.runs(status, pageParam, signal), getNextPageParam: (last) => last.items.length === 50 ? last.next_cursor : undefined, refetchInterval: 5000 });
  const runs = useMemo(() => {
    const unique = new Map(query.data?.pages.flatMap(page => page.items).map(run => [run.id, run]));
    return [...unique.values()].sort((a, b) => b.created_at.localeCompare(a.created_at));
  }, [query.data]);
  const visible = runs.filter(run => `${run.spec.name} ${run.id} ${Object.entries(run.spec.labels).flat().join(' ')}`.toLowerCase().includes(search.toLowerCase()));
  const active = runs.filter(run => ['STARTING', 'RUNNING', 'FINALIZING'].includes(run.status)).length;
  const toggle = (id: string) => setSelected(values => values.includes(id) ? values.filter(value => value !== id) : [...values, id].slice(0, 4));
  return <>
    <PageHeading title="Experiments" description="A workspace for every run, from first idea to final result."><Link className="button primary" to="/new"><Plus size={16} />New experiment</Link></PageHeading>
    <div className="overview-strip"><div><span className="live-dot" />Live workspace</div><span><strong>{number(runs.length)}</strong> loaded runs</span><span><strong>{active}</strong> active</span><span><strong>{runs.filter(run => run.status === 'SUCCEEDED').length}</strong> succeeded</span><span className="strip-note">Updates every 5 seconds</span></div>
    <section className="panel runs-panel" aria-label="Experiment runs">
      <div className="table-toolbar"><div className="search-input"><Search size={17} /><input aria-label="Search loaded runs" placeholder="Search loaded runs…" value={search} onChange={event => setSearch(event.target.value)} /><kbd>/</kbd></div><label className="filter-input"><SlidersHorizontal size={15} /><span className="sr-only">Filter by status</span><select value={status} onChange={event => { setStatus(event.target.value); setSelected([]); }}><option value="">All statuses</option>{statuses.map(value => <option key={value} value={value}>{words(value)}</option>)}</select></label></div>
      {selected.length > 0 && <div className="selection-bar"><span>{selected.length} of 4 runs selected</span><button className="text-button" onClick={() => setSelected([])}>Clear</button><Link className="button compact" to={`/compare?runs=${selected.join(',')}`}>Compare metrics<ArrowUpRight size={14} /></Link></div>}
      <ErrorNotice error={query.error} retry={() => query.refetch()} />
      {query.isPending ? <Loading /> : visible.length === 0 ? <Empty title={runs.length ? 'No matching experiments' : 'Your next experiment starts here'}>{runs.length ? 'Try a different search or status filter.' : <><span>Submit a container image and let Runyard handle the execution.</span><br /><Link className="inline-link" to="/new">Create your first experiment →</Link></>}</Empty> : <div className="table-scroll"><table className="runs-table"><thead><tr><th className="checkbox-cell"><span className="sr-only">Select</span></th><th>Experiment</th><th>Status</th><th>Resources</th><th>Attempts</th><th>Created <ArrowDown size={12} /></th></tr></thead><tbody>{visible.map(run => <tr key={run.id} className={selected.includes(run.id) ? 'selected' : ''}>
        <td className="checkbox-cell"><input type="checkbox" aria-label={`Select ${run.spec.name} ${shortId(run.id)}`} checked={selected.includes(run.id)} disabled={!selected.includes(run.id) && selected.length === 4} onChange={() => toggle(run.id)} /></td>
        <td><RunLink id={run.id}>{run.spec.name}</RunLink><div className="row-caption"><code>{shortId(run.id)}</code>{Object.entries(run.spec.labels).slice(0, 2).map(([key, value]) => <span key={key} className="tag">{key}: {value}</span>)}</div></td>
        <td><StatusBadge status={run.status} /></td><td><div>{number(run.spec.resources.cpu_millis / 1000)} CPU <span className="muted">/</span> {number(run.spec.resources.memory_mib)} MiB</div><small className={run.spec.resources.gpu_count ? 'gpu-label' : 'muted'}>{run.spec.resources.gpu_count ? `${run.spec.resources.gpu_count} NVIDIA GPU` : 'CPU workload'}</small></td><td>{run.generation || 'N/A'}<span className="muted"> / {run.spec.retry.max_attempts}</span></td><td className="muted nowrap">{date(run.created_at)}</td>
      </tr>)}</tbody></table></div>}
      <footer className="table-footer"><span>{visible.length} of {runs.length} loaded runs · newest loaded first</span>{query.hasNextPage && <button disabled={query.isFetchingNextPage} onClick={() => query.fetchNextPage()}>{query.isFetchingNextPage ? 'Loading…' : 'Load more runs'}</button>}</footer>
    </section>
    <p className="page-footnote">Immutable configurations. Complete attempt history. Results you can reproduce.</p>
  </>;
}
