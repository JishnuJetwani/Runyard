import { useQuery, useQueryClient } from '@tanstack/react-query';
import {
  Activity,
  ArrowUpRight,
  FlaskConical,
  GitCompareArrows,
  KeyRound,
  Layers3,
  Server,
  X,
} from 'lucide-react';
import { lazy, Suspense, useEffect, useState, type FormEvent } from 'react';
import { NavLink, Route, Routes, useLocation } from 'react-router-dom';
import { Dialog, ErrorNotice, Loading } from './components/common';
import { api, ApiError, credentials } from './lib/api';
import Runs from './features/Runs';
const Submit = lazy(() => import('./features/Submit'));
const RunDetail = lazy(() => import('./features/RunDetail'));
const Compare = lazy(() => import('./features/Compare'));
const Capacity = lazy(() => import('./features/Capacity'));

export default function App() {
  const client = useQueryClient();
  const location = useLocation();
  const [connection, setConnection] = useState(false);
  const [key, setKey] = useState('');
  const [dismissed, setDismissed] = useState(false);
  const capacity = useQuery({
    queryKey: ['capacity', ''],
    queryFn: ({ signal }) => api.capacity('', signal),
    refetchInterval: 15000,
  });
  const unauthorized = capacity.error instanceof ApiError && capacity.error.status === 401;
  useEffect(() => {
    document.title = `Runyard · ${location.pathname === '/capacity' ? 'Capacity' : location.pathname === '/compare' ? 'Compare' : 'Experiments'}`;
  }, [location.pathname]);
  useEffect(() => {
    function shortcut(event: KeyboardEvent) {
      if (
        event.key === '/' &&
        !['INPUT', 'TEXTAREA', 'SELECT'].includes((event.target as HTMLElement).tagName)
      ) {
        const input = document.querySelector<HTMLInputElement>('[aria-label="Search loaded runs"]');
        if (input) {
          event.preventDefault();
          input.focus();
        }
      }
    }
    window.addEventListener('keydown', shortcut);
    return () => window.removeEventListener('keydown', shortcut);
  }, []);
  async function connect(event: FormEvent) {
    event.preventDefault();
    await client.cancelQueries();
    credentials.set(key);
    setKey('');
    setConnection(false);
    setDismissed(false);
    await client.resetQueries();
  }
  return (
    <div className="app-layout">
      <a className="skip-link" href="#main">
        Skip to content
      </a>
      <aside className="sidebar">
        <NavLink className="brand" to="/">
          <span className="brand-mark">
            <Layers3 size={23} strokeWidth={1.8} />
          </span>
          runyard<span className="brand-period">.</span>
        </NavLink>
        <div className="workspace-label">
          <span className="workspace-avatar">R</span>
          <div>
            Personal workspace<small>Experiment console</small>
          </div>
        </div>
        <nav aria-label="Main navigation">
          <NavLink to="/" end>
            <FlaskConical size={18} />
            Experiments
          </NavLink>
          <NavLink to="/compare">
            <GitCompareArrows size={18} />
            Compare
          </NavLink>
          <NavLink to="/capacity">
            <Server size={18} />
            Capacity
          </NavLink>
        </nav>
        <div className="sidebar-bottom">
          <div className="backend-status">
            <span
              className={`live-dot ${capacity.isError ? 'offline' : capacity.isPending ? 'waiting' : ''}`}
            />
            <div>
              {capacity.isError
                ? 'Connection needed'
                : capacity.isPending
                  ? 'Connecting'
                  : 'Coordinator connected'}
              <small>
                {capacity.data
                  ? `${capacity.data.backend === 'docker' ? 'Docker' : 'Kubernetes'} execution`
                  : 'Private workspace'}
              </small>
            </div>
          </div>
          <button className="connection-button" onClick={() => setConnection(true)}>
            <KeyRound size={15} />
            Connection settings
            <ArrowUpRight size={13} />
          </button>
        </div>
      </aside>
      <div className="main-layout">
        <div className="topbar">
          <span>
            <span className="muted">Workspace</span>
            <span className="breadcrumb-divider">/</span>
            {location.pathname === '/capacity'
              ? 'Capacity'
              : location.pathname === '/compare'
                ? 'Compare'
                : 'Experiments'}
          </span>
          <span className="private-label">
            <Activity size={13} />
            Private console
          </span>
        </div>
        <main id="main">
          {unauthorized && (
            <div className="error-notice">
              <KeyRound size={17} />
              <span>Connect with your owner API key to view this workspace.</span>
              <button onClick={() => setConnection(true)}>Connect</button>
            </div>
          )}
          {capacity.isError && !unauthorized && !dismissed && (
            <div className="connection-warning">
              <ErrorNotice error={capacity.error} retry={() => capacity.refetch()} />
              <button
                className="icon-button"
                aria-label="Dismiss connection warning"
                onClick={() => setDismissed(true)}
              >
                <X size={16} />
              </button>
            </div>
          )}
          <Suspense fallback={<Loading />}>
            <Routes>
              <Route path="/" element={<Runs />} />
              <Route path="/new" element={<Submit />} />
              <Route path="/runs/:id" element={<RunDetail />} />
              <Route path="/compare" element={<Compare />} />
              <Route path="/capacity" element={<Capacity />} />
              <Route
                path="*"
                element={
                  <div className="empty">
                    <h1>Page not found</h1>
                    <NavLink to="/">Back to experiments</NavLink>
                  </div>
                }
              />
            </Routes>
          </Suspense>
        </main>
      </div>
      {connection && (
        <Dialog title="Workspace connection" onClose={() => setConnection(false)}>
          <form onSubmit={connect}>
            <p className="muted">
              Use your coordinator’s owner API key. It stays in this browser tab and is sent only to
              this console’s API.
            </p>
            <label className="field">
              Owner API key
              <input
                type="password"
                autoComplete="off"
                value={key}
                onChange={(event) => setKey(event.target.value)}
                placeholder="Paste your owner key"
              />
            </label>
            <p className="help">Leave blank to use the local development gateway, if configured.</p>
            <div className="form-actions">
              <button type="button" onClick={() => setConnection(false)}>
                Cancel
              </button>
              <button className="primary" type="submit">
                Connect
              </button>
            </div>
          </form>
        </Dialog>
      )}
    </div>
  );
}
