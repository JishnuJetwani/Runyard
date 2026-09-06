import type {
  Artifact,
  Attempt,
  Capacity,
  Event,
  Page,
  Run,
  RunSpec,
  Scalar,
  Sweep,
  TelemetryPage,
} from './types';

const keyName = 'runyard.owner-key';
export const credentials = {
  get: () => sessionStorage.getItem(keyName) || '',
  set: (key: string) =>
    key ? sessionStorage.setItem(keyName, key.trim()) : sessionStorage.removeItem(keyName),
};
export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly requestId = '',
  ) {
    super(message);
  }
}
function headers() {
  const token = credentials.get();
  return token ? { Authorization: `Bearer ${token}` } : undefined;
}
async function response(path: string, options: RequestInit = {}) {
  const result = await fetch(`/v1${path}`, {
    ...options,
    headers: { ...headers(), ...options.headers },
    signal: options.signal ?? AbortSignal.timeout(20_000),
  });
  if (!result.ok) {
    const body = await result.json().catch(() => null);
    throw new ApiError(
      body?.error?.message || `Request failed (${result.status})`,
      result.status,
      body?.error?.request_id || result.headers.get('X-Request-ID') || '',
    );
  }
  return result;
}
async function get<T>(path: string, signal?: AbortSignal): Promise<T> {
  return (await response(path, { signal })).json();
}
async function post<T>(path: string, body: unknown, key?: string): Promise<T> {
  return (
    await response(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...(key ? { 'Idempotency-Key': key } : {}) },
      body: JSON.stringify(body),
    })
  ).json();
}
const id = encodeURIComponent;
export const api = {
  runs: (status = '', after = '', signal?: AbortSignal) =>
    get<Page<Run>>(
      `/runs?${new URLSearchParams({ limit: '50', ...(status ? { status } : {}), ...(after ? { after } : {}) })}`,
      signal,
    ),
  run: (runId: string, signal?: AbortSignal) => get<Run>(`/runs/${id(runId)}`, signal),
  submit: (spec: RunSpec, key: string) => post<Run>('/runs', spec, key),
  sweep: (base: RunSpec, grid: Record<string, Scalar[]>, key: string) =>
    post<Sweep>('/sweeps', { base, grid }, key),
  cancel: (runId: string) => post<Run>(`/runs/${id(runId)}/cancel`, {}),
  rerun: (runId: string, key: string) => post<Run>(`/runs/${id(runId)}/rerun`, {}, key),
  attempts: (runId: string, signal?: AbortSignal) =>
    get<{ items: Attempt[] }>(`/runs/${id(runId)}/attempts`, signal),
  events: (runId: string, after = 0, signal?: AbortSignal) =>
    get<{ items: Event[] }>(`/runs/${id(runId)}/events?after=${after}&limit=100`, signal),
  telemetry: (
    kind: 'logs' | 'metrics',
    runId: string,
    attempt: string,
    after = 0,
    signal?: AbortSignal,
  ) =>
    get<TelemetryPage>(
      `/runs/${id(runId)}/${kind}?${new URLSearchParams({ attempt, after: String(after), limit: '1000' })}`,
      signal,
    ),
  artifacts: (runId: string, attempt: string, signal?: AbortSignal) =>
    get<{ items: Artifact[] }>(`/runs/${id(runId)}/artifacts?attempt=${id(attempt)}`, signal),
  capacity: (after = '', signal?: AbortSignal) =>
    get<Capacity>(`/capacity?limit=50&after=${id(after)}`, signal),
  drain: (worker: string, drained: boolean) =>
    post<{ id: string; drained: boolean }>(`/workers/${id(worker)}/drain`, { drained }),
  download: async (artifact: Artifact) => {
    const result = await response(`/artifacts/${id(artifact.id)}/download`);
    saveFile(await result.blob(), artifact.path.split('/').pop() || 'artifact');
  },
};
export function saveFile(blob: Blob, name: string) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
