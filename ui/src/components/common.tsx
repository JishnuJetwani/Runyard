import { AlertCircle, ArrowUpRight, LoaderCircle, X } from 'lucide-react';
import { useEffect, useRef, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { ApiError } from '../lib/api';
import { words } from '../lib/format';
import type { Status } from '../lib/types';

export function StatusBadge({ status }: { status: Status }) {
  return <span className={`status status-${status.toLowerCase()}`}><i />{words(status)}</span>;
}
export function ErrorNotice({ error, retry }: { error: unknown; retry?: () => void }) {
  if (!error) return null;
  return <div className="error-notice" role="alert"><AlertCircle size={17} /><div>{error instanceof Error ? error.message : 'Something went wrong.'}{error instanceof ApiError && error.requestId && <small>Request {error.requestId}</small>}</div>{retry && <button onClick={retry}>Try again</button>}</div>;
}
export function Loading() { return <div className="empty"><LoaderCircle className="spin" size={20} /><p>Loading…</p></div>; }
export function Empty({ title, children }: { title: string; children?: ReactNode }) { return <div className="empty"><h3>{title}</h3>{children && <p>{children}</p>}</div>; }
export function PageHeading({ title, description, children }: { title: string; description: string; children?: ReactNode }) {
  return <header className="page-heading"><div><h1>{title}</h1><p>{description}</p></div><div className="actions">{children}</div></header>;
}
export function Panel({ title, action, children, className = '' }: { title: string; action?: ReactNode; children: ReactNode; className?: string }) {
  return <section className={`panel ${className}`}><div className="panel-heading"><h2>{title}</h2>{action}</div>{children}</section>;
}
export function Dialog({ title, onClose, children }: { title: string; onClose: () => void; children: ReactNode }) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => { const dialog = ref.current!; dialog.showModal(); return () => dialog.close(); }, []);
  return <dialog ref={ref} onCancel={onClose} aria-labelledby="dialog-title"><div className="dialog-heading"><h2 id="dialog-title">{title}</h2><button className="icon-button" aria-label="Close dialog" onClick={onClose}><X size={20} /></button></div>{children}</dialog>;
}
export function RunLink({ id, children }: { id: string; children: ReactNode }) { return <Link className="run-link" to={`/runs/${encodeURIComponent(id)}`}>{children}<ArrowUpRight size={14} /></Link>; }
