import type { Page } from '@playwright/test';
import type { Capacity, Run, RunSpec } from '../src/lib/types';
export const spec: RunSpec = {
  version: 1,
  name: 'learning-rate-study',
  image: `registry/training@sha256:${'a'.repeat(64)}`,
  command: ['python', 'train.py'],
  parameters: { learning_rate: 0.001, seed: 42 },
  environment: {},
  labels: { project: 'training' },
  resources: { cpu_millis: 1000, memory_mib: 512, gpu_count: 1 },
  timeout_seconds: 1800,
  priority: 0,
  retry: { max_attempts: 3, retry_exit: false, retry_timeout: false },
  source: { repository: '', revision: '' },
};
export const run: Run = {
  id: 'run-001',
  spec,
  status: 'SUCCEEDED',
  generation: 1,
  active_attempt: 'attempt-001',
  sweep_id: '',
  parent_run_id: '',
  created_at: '2026-09-18T12:00:00Z',
  updated_at: '2026-09-18T12:01:00Z',
};
export const capacity: Capacity = {
  backend: 'docker',
  gpu: {
    capacity: 2,
    allocatable: 2,
    reserved: 1,
    available_estimate: 1,
    pending: 0,
    fresh: true,
    observed_at: '',
    age_seconds: 1,
  },
  workers: [
    {
      id: 'worker-1',
      capacity: { cpu_millis: 8000, memory_mib: 16384, gpu_count: 2 },
      reserved: { cpu_millis: 1000, memory_mib: 512, gpu_count: 1 },
      drained: false,
      available: true,
      heartbeat_at: '2026-09-18T12:00:00Z',
      gpu_inventory: {
        capable: true,
        ready: true,
        fresh: true,
        observed_at: '',
        available_estimate: 1,
        devices: [
          { uuid: 'GPU-aaa', name: 'NVIDIA device', memory_mib: 24576, eligible: true, reason: '' },
        ],
      },
    },
  ],
  nodes: [],
  next_cursor: '',
};
export async function mockApi(page: Page) {
  await page.route('**/v1/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    let body: unknown;
    if (path === '/v1/capacity') body = capacity;
    else if (path === '/v1/runs')
      body = {
        items: [
          run,
          {
            ...run,
            id: 'run-002',
            status: 'RUNNING',
            spec: { ...spec, name: 'baseline', parameters: { learning_rate: 0.01, seed: 42 } },
          },
        ],
        next_cursor: '',
      };
    else if (path.endsWith('/attempts'))
      body = {
        items: [
          {
            id: 'attempt-001',
            run_id: run.id,
            generation: 1,
            worker_id: 'worker-1',
            status: run.status,
            reason: '',
            runtime_id: 'container-1',
            cleanup_status: 'DONE',
            created_at: run.created_at,
            started_at: run.created_at,
            finished_at: run.updated_at,
            acknowledged_sequence: 2,
            gpu_count: 1,
            gpu_allocations: [],
            node_name: '',
            exit_code: 0,
          },
        ],
      };
    else if (path.endsWith('/metrics'))
      body = {
        attempt_id: 'attempt-001',
        items: [0, 1, 2].map((step) => ({
          sequence: step + 1,
          kind: 'metric',
          name: 'loss',
          step,
          value: 1 / (step + 1),
          timestamp_ms: 1,
          text: '',
        })),
        next_cursor: 3,
      };
    else if (path.endsWith('/logs'))
      body = {
        attempt_id: 'attempt-001',
        items: [
          {
            sequence: 1,
            kind: 'stdout',
            text: 'Training complete\n',
            name: '',
            step: 0,
            value: 0,
            timestamp_ms: 1,
          },
        ],
        next_cursor: 1,
      };
    else if (path.endsWith('/artifacts'))
      body = {
        items: [
          {
            id: 'artifact-1',
            attempt_id: 'attempt-001',
            path: 'model.pt',
            sha256: 'b'.repeat(64),
            size: 1024,
          },
        ],
      };
    else if (path.endsWith('/events'))
      body = {
        items: [
          {
            sequence: 1,
            kind: 'run_submitted',
            detail: 'Experiment accepted',
            created_at: run.created_at,
          },
        ],
      };
    else if (/\/v1\/runs\/[^/]+$/.test(path))
      body = {
        ...run,
        id: path.split('/').pop(),
        spec: path.endsWith('002') ? { ...spec, name: 'baseline' } : spec,
      };
    else {
      await route.fulfill({
        status: 404,
        json: { error: { code: 'not_found', message: `Unexpected test route ${path}` } },
      });
      return;
    }
    await route.fulfill({ json: body });
  });
}
