import { useMutation, useQueryClient } from '@tanstack/react-query';
import { ArrowLeft, ArrowUpRight, FileJson, Play } from 'lucide-react';
import { useRef, useState, type FormEvent } from 'react';
import { Link, useLocation, useNavigate } from 'react-router-dom';
import { ErrorNotice, PageHeading, RunLink } from '../components/common';
import { api } from '../lib/api';
import type { Run, RunSpec, Scalar, Sweep } from '../lib/types';

const defaults: RunSpec = {
  version: 1,
  name: '',
  image: '',
  command: ['python', 'train.py'],
  parameters: {},
  environment: {},
  labels: {},
  resources: { cpu_millis: 1000, memory_mib: 512, gpu_count: 0 },
  timeout_seconds: 1800,
  priority: 0,
  retry: { max_attempts: 3, retry_exit: false, retry_timeout: false },
  source: { repository: '', revision: '' },
};
function object(value: string, name: string) {
  const parsed: unknown = JSON.parse(value);
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed))
    throw new Error(`${name} must be a JSON object.`);
  return parsed as Record<string, unknown>;
}
const scalar = (value: unknown) =>
  value === null || ['string', 'boolean', 'number'].includes(typeof value);

export default function Submit() {
  const location = useLocation();
  const initial: RunSpec = location.state?.spec || defaults;
  const [spec, setSpec] = useState<RunSpec>(initial);
  const [command, setCommand] = useState(JSON.stringify(initial.command));
  const [parameters, setParameters] = useState(JSON.stringify(initial.parameters, null, 2));
  const [advanced, setAdvanced] = useState(
    JSON.stringify(
      {
        environment: initial.environment,
        labels: initial.labels,
        source: initial.source,
        retry: initial.retry,
      },
      null,
      2,
    ),
  );
  const [mode, setMode] = useState<'single' | 'sweep'>('single');
  const [grid, setGrid] = useState('{\n  "learning_rate": [0.001, 0.01]\n}');
  const [importError, setImportError] = useState<unknown>(null);
  const [sweep, setSweep] = useState<Sweep>();
  const request = useRef({ payload: '', key: '' });
  const importInput = useRef<HTMLInputElement>(null);
  const navigate = useNavigate();
  const client = useQueryClient();
  function resolved() {
    const args: unknown = JSON.parse(command);
    if (
      !Array.isArray(args) ||
      !args.length ||
      args.some((value) => typeof value !== 'string') ||
      !args[0]
    )
      throw new Error('Command must be a nonempty JSON array of argument strings.');
    const params = object(parameters, 'Parameters');
    if (Object.values(params).some((value) => !scalar(value)))
      throw new Error('Parameter values must be strings, numbers, booleans, or null.');
    const extra = object(advanced, 'Advanced settings');
    if (
      Object.keys(extra).some((key) => !['environment', 'labels', 'source', 'retry'].includes(key))
    )
      throw new Error('Advanced settings support environment, labels, source, and retry only.');
    const result = { ...spec, ...extra, command: args, parameters: params } as RunSpec;
    return result;
  }
  const mutation = useMutation({
    mutationFn: async () => {
      const base = resolved();
      let values: Record<string, Scalar[]> = {};
      if (mode === 'sweep') {
        const parsed = object(grid, 'Parameter grid');
        if (
          !Object.keys(parsed).length ||
          Object.values(parsed).some(
            (value) =>
              !Array.isArray(value) || !value.length || value.some((item) => !scalar(item)),
          )
        )
          throw new Error('Each grid parameter needs a nonempty array of scalar values.');
        values = parsed as Record<string, Scalar[]>;
        const count = Object.values(values).reduce((total, items) => total * items.length, 1);
        if (count > 1000) throw new Error('A sweep can contain at most 1,000 runs.');
      }
      const payload = JSON.stringify({ mode, base, grid: values });
      // A response can be lost after acceptance. Keep the request key when retrying
      // an unchanged submission, and rotate it only when the payload changes.
      if (request.current.payload !== payload)
        request.current = { payload, key: crypto.randomUUID() };
      return mode === 'sweep'
        ? api.sweep(base, values, request.current.key)
        : api.submit(base, request.current.key);
    },
    onSuccess: (result) => {
      void client.invalidateQueries({ queryKey: ['runs'] });
      if ('run_ids' in result) setSweep(result as Sweep);
      else navigate(`/runs/${(result as Run).id}`);
    },
  });
  function submit(event: FormEvent) {
    event.preventDefault();
    setImportError(null);
    mutation.mutate();
  }
  async function importSpec(file?: File) {
    if (!file) return;
    try {
      if (file.size > 128 * 1024)
        throw new Error('Specification files must be smaller than 128 KB.');
      const parsed = JSON.parse(await file.text());
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed))
        throw new Error('Expected a JSON specification.');
      const value = parsed.base || parsed;
      if (value.version && value.version !== 1)
        throw new Error('Only specification version 1 is supported.');
      if (
        typeof value.name !== 'string' ||
        typeof value.image !== 'string' ||
        !Array.isArray(value.command)
      )
        throw new Error('The specification needs a name, image, and command array.');
      const next = {
        ...defaults,
        ...value,
        resources: { ...defaults.resources, ...value.resources },
      };
      setSpec(next);
      setCommand(JSON.stringify(next.command));
      setParameters(JSON.stringify(next.parameters, null, 2));
      setAdvanced(
        JSON.stringify(
          {
            environment: next.environment,
            labels: next.labels,
            source: next.source,
            retry: next.retry,
          },
          null,
          2,
        ),
      );
      if (parsed.grid) {
        setMode('sweep');
        setGrid(JSON.stringify(parsed.grid, null, 2));
      } else setMode('single');
      setImportError(null);
      mutation.reset();
    } catch (error) {
      setImportError(error);
    }
  }
  const resource = (key: keyof RunSpec['resources'], value: number) =>
    setSpec((current) => ({ ...current, resources: { ...current.resources, [key]: value } }));
  return (
    <>
      <Link className="back-link" to="/">
        <ArrowLeft size={14} />
        Experiments
      </Link>
      <PageHeading
        title="New experiment"
        description="Choose an image, command, and resources for your run."
      >
        <button onClick={() => importInput.current?.click()}>
          <FileJson size={15} />
          Import JSON
        </button>
        <input
          hidden
          ref={importInput}
          type="file"
          accept=".json,application/json"
          aria-label="Import specification"
          onChange={(event) => {
            void importSpec(event.target.files?.[0]);
            event.target.value = '';
          }}
        />
      </PageHeading>
      {sweep ? (
        <section className="panel">
          <div className="panel-heading">
            <h2>Sweep submitted · {sweep.run_ids.length} experiments</h2>
            <Link className="button" to="/">
              View workspace
              <ArrowUpRight size={14} />
            </Link>
          </div>
          <div className="sweep-results">
            <p>Every combination is now an independent run with its own history.</p>
            <code>{sweep.id}</code>
            <div>
              {sweep.run_ids.map((id, index) => (
                <RunLink key={id} id={id}>
                  Run {index + 1} · {id.slice(0, 8)}
                </RunLink>
              ))}
            </div>
          </div>
        </section>
      ) : (
        <div className="submission-layout">
          <form className="panel submission-form" onSubmit={submit}>
            <fieldset disabled={mutation.isPending}>
              <div className="form-section">
                <h2>Experiment definition</h2>
                <p className="help">Use a prebuilt image that includes the Runyard runner.</p>
                <label className="field">
                  Name
                  <input
                    required
                    maxLength={200}
                    placeholder="e.g. learning-rate-study"
                    value={spec.name}
                    onChange={(event) => setSpec({ ...spec, name: event.target.value })}
                  />
                </label>
                <label className="field">
                  Container image
                  <input
                    required
                    className="code-input"
                    pattern=".+@sha256:[a-fA-F0-9]{64}"
                    title="Use an immutable image reference ending in @sha256: and a 64-character digest"
                    placeholder="registry/image@sha256:…"
                    value={spec.image}
                    onChange={(event) => setSpec({ ...spec, image: event.target.value })}
                  />
                  <span className="help">
                    Pin the image by digest so this experiment is reproducible.
                  </span>
                </label>
                <label className="field">
                  Command{' '}
                  <textarea
                    className="code-input"
                    rows={2}
                    value={command}
                    onChange={(event) => setCommand(event.target.value)}
                    spellCheck={false}
                  />
                  <span className="help">
                    A JSON argument array. Commands run directly, without a shell.
                  </span>
                </label>
              </div>
              <div className="form-section">
                <h2>Resources</h2>
                <div className="form-grid three">
                  <label className="field">
                    CPU (millicores)
                    <input
                      required
                      type="number"
                      min={1}
                      step={1}
                      value={spec.resources.cpu_millis}
                      onChange={(event) => resource('cpu_millis', event.target.valueAsNumber)}
                    />
                  </label>
                  <label className="field">
                    Memory (MiB)
                    <input
                      required
                      type="number"
                      min={1}
                      step={1}
                      value={spec.resources.memory_mib}
                      onChange={(event) => resource('memory_mib', event.target.valueAsNumber)}
                    />
                  </label>
                  <label className="field">
                    NVIDIA GPUs
                    <input
                      required
                      type="number"
                      min={0}
                      max={64}
                      step={1}
                      value={spec.resources.gpu_count || 0}
                      onChange={(event) => resource('gpu_count', event.target.valueAsNumber)}
                    />
                  </label>
                </div>
                <div className="form-grid">
                  <label className="field">
                    Timeout (seconds)
                    <input
                      required
                      type="number"
                      min={1}
                      step={1}
                      value={spec.timeout_seconds}
                      onChange={(event) =>
                        setSpec({ ...spec, timeout_seconds: event.target.valueAsNumber })
                      }
                    />
                  </label>
                  <label className="field">
                    Priority
                    <select
                      value={spec.priority}
                      onChange={(event) =>
                        setSpec({ ...spec, priority: Number(event.target.value) })
                      }
                    >
                      {Array.from({ length: 10 }, (_, value) => (
                        <option key={value} value={value}>
                          {value}
                          {value === 0 ? ' · Normal' : value === 9 ? ' · Highest' : ''}
                        </option>
                      ))}
                    </select>
                  </label>
                </div>
              </div>
              <div className="form-section">
                <div className="section-title">
                  <h2>Parameters</h2>
                  <div className="segmented">
                    <button
                      type="button"
                      aria-pressed={mode === 'single'}
                      onClick={() => setMode('single')}
                    >
                      Single run
                    </button>
                    <button
                      type="button"
                      aria-pressed={mode === 'sweep'}
                      onClick={() => setMode('sweep')}
                    >
                      Sweep
                    </button>
                  </div>
                </div>
                <label className="field">
                  Base parameters
                  <textarea
                    className="code-input"
                    spellCheck={false}
                    rows={4}
                    value={parameters}
                    onChange={(event) => setParameters(event.target.value)}
                  />
                  <span className="help">
                    Flat JSON values, written to the experiment’s parameter file.
                  </span>
                </label>
                {mode === 'sweep' && (
                  <label className="field">
                    Parameter grid
                    <textarea
                      className="code-input"
                      spellCheck={false}
                      rows={5}
                      value={grid}
                      onChange={(event) => setGrid(event.target.value)}
                    />
                    <span className="help">
                      Every combination becomes a run. Grid values override base parameters; up to
                      1,000 combinations.
                    </span>
                  </label>
                )}
                <details className="advanced-settings">
                  <summary>Retry policy, environment, labels & source</summary>
                  <label className="field">
                    <span className="sr-only">Advanced settings JSON</span>
                    <textarea
                      className="code-input"
                      rows={15}
                      spellCheck={false}
                      value={advanced}
                      onChange={(event) => setAdvanced(event.target.value)}
                    />
                    <span className="help">
                      Environment values are stored with the run. Keep credentials out of the
                      specification.
                    </span>
                  </label>
                </details>
                <ErrorNotice error={importError || mutation.error} />
                <div className="form-actions">
                  <Link className="button" to="/">
                    Cancel
                  </Link>
                  <button type="submit" className="primary">
                    <Play size={14} />
                    {mutation.isPending
                      ? 'Submitting…'
                      : mode === 'sweep'
                        ? 'Submit sweep'
                        : 'Submit experiment'}
                  </button>
                </div>
                {request.current.key && (
                  <p className="request-key">
                    Request key: <code>{request.current.key}</code>
                  </p>
                )}
              </div>
            </fieldset>
          </form>
          <aside className="submission-aside">
            <div className="note-icon">
              <FlaskIcon />
            </div>
            <h3>
              Configuration
              <br />
              and results
            </h3>
            <p>
              Your configuration is saved with every run. Logs, metrics, and artifacts stay attached
              to the attempt that produced them.
            </p>
            <hr />
            <h4>Resource requests</h4>
            <p>
              1,000 millicores = 1 CPU.
              <br />
              Memory includes runner overhead.
            </p>
            <p>
              GPU requests reserve whole NVIDIA devices on a single worker or node. Set this to zero
              for CPU experiments.
            </p>
            <Link className="inline-link" to="/capacity">
              Check available capacity →
            </Link>
          </aside>
        </div>
      )}
    </>
  );
}
function FlaskIcon() {
  return <Play size={22} strokeWidth={1.5} />;
}
