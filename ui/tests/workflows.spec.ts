import { expect, test } from '@playwright/test';
import { capacity, mockApi, run, spec } from './fixtures';

test('retries a lost submission response using the same key and GPU request', async ({ page }) => {
  await mockApi(page);
  const keys: string[] = [];
  const payloads: unknown[] = [];
  await page.route('**/v1/runs', async (route) => {
    if (route.request().method() !== 'POST') return route.fallback();
    keys.push(route.request().headers()['idempotency-key']);
    payloads.push(route.request().postDataJSON());
    await route.fulfill(
      keys.length === 1
        ? {
            status: 503,
            json: { error: { message: 'Response lost; retry safely', request_id: 'lost-1' } },
          }
        : { json: run },
    );
  });
  await page.goto('/new');
  await page.getByLabel('Import specification').setInputFiles({
    name: 'run.json',
    mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify(spec)),
  });
  await page.getByRole('button', { name: 'Submit experiment' }).click();
  await expect(page.getByRole('alert')).toContainText('Response lost');
  await page.getByRole('button', { name: 'Submit experiment' }).click();
  await expect(page).toHaveURL(/\/runs\/run-001$/);
  expect(keys).toHaveLength(2);
  expect(keys[0]).toBeTruthy();
  expect(keys[1]).toBe(keys[0]);
  expect(payloads[0]).toEqual(spec);
  expect(payloads[1]).toEqual(spec);
});

test('submits an atomic parameter grid and links its resulting runs', async ({ page }) => {
  await mockApi(page);
  let payload: unknown;
  await page.route('**/v1/sweeps', async (route) => {
    payload = route.request().postDataJSON();
    await route.fulfill({
      json: {
        id: 'sweep-1',
        run_ids: ['run-001', 'run-002'],
        created_at: run.created_at,
        spec: payload,
      },
    });
  });
  await page.goto('/new');
  await page.getByLabel('Import specification').setInputFiles({
    name: 'sweep.json',
    mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify({ base: spec, grid: { seed: [1, 2] } })),
  });
  await page.getByRole('button', { name: 'Submit sweep', exact: true }).click();
  await expect(page.getByRole('heading', { name: /Sweep submitted/ })).toContainText(
    '2 experiments',
  );
  expect(payload).toEqual({ base: spec, grid: { seed: [1, 2] } });
  await expect(page.getByRole('link', { name: /Run 2/ })).toHaveAttribute('href', '/runs/run-002');
});

test('shows metrics, escaped logs, attempt history, and artifact downloads', async ({ page }) => {
  await mockApi(page);
  await page.route('**/v1/artifacts/artifact-1/download', (route) =>
    route.fulfill({ body: 'model contents', contentType: 'application/octet-stream' }),
  );
  await page.goto('/runs/run-001');
  await expect(page.getByRole('img', { name: /loss by step/ })).toBeVisible();
  await page.getByRole('tab', { name: 'Logs', exact: true }).click();
  await expect(page.getByLabel('Experiment logs')).toContainText('Training complete');
  await page.getByRole('tab', { name: 'Attempts', exact: true }).click();
  await expect(page.getByText('Attempt #1', { exact: true })).toBeVisible();
  await page.getByRole('tab', { name: 'Artifacts', exact: true }).click();
  const downloadPromise = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download model.pt' }).click();
  expect((await downloadPromise).suggestedFilename()).toBe('model.pt');
  await page.getByRole('tab', { name: 'Configuration', exact: true }).click();
  await expect(page.locator('.specification')).toContainText('gpu_count');
});

test('cancellation retains the visible cleanup-pending distinction', async ({ page }) => {
  await mockApi(page);
  let cancelled = false;
  await page.route('**/v1/runs/run-001', (route) =>
    route.fulfill({ json: { ...run, status: cancelled ? 'CANCELLED' : 'RUNNING' } }),
  );
  await page.route('**/v1/runs/run-001/cancel', (route) => {
    cancelled = true;
    return route.fulfill({ json: { ...run, status: 'CANCELLED' } });
  });
  await page.route('**/v1/runs/run-001/attempts', (route) =>
    route.fulfill({
      json: {
        items: [
          {
            id: run.active_attempt,
            generation: 1,
            worker_id: 'worker-1',
            status: cancelled ? 'CANCELLED' : 'RUNNING',
            cleanup_status: 'PENDING',
            gpu_count: 1,
            gpu_allocations: [],
            exit_code: null,
          },
        ],
      },
    }),
  );
  await page.goto('/runs/run-001');
  await page.getByRole('button', { name: 'Cancel run', exact: true }).click();
  expect(cancelled).toBe(false);
  await page.getByRole('button', { name: 'Confirm cancellation' }).click();
  await expect(page.getByText('Runtime cleanup is pending.', { exact: true })).toBeVisible();
  await expect(page.locator('.run-summary')).toContainText('cancelled');
});

test('compares selected metrics and highlights parameter differences', async ({ page }) => {
  await mockApi(page);
  await page.route('**/v1/runs/run-002', (route) =>
    route.fulfill({
      json: {
        ...run,
        id: 'run-002',
        spec: { ...spec, name: 'baseline', parameters: { learning_rate: 0.01, seed: 42 } },
      },
    }),
  );
  await page.goto('/compare?runs=run-001,run-002');
  await expect(page.getByRole('img', { name: /loss by step/ })).toBeVisible();
  await expect(page.locator('.parameter-difference')).toContainText('learning_rate');
  await expect(page.locator('.parameter-difference')).toContainText('0.01');
  await page.getByRole('button', { name: 'Remove baseline' }).click();
  await expect(page).toHaveURL(/runs=run-001$/);
});

test('drains a worker after confirmation and renders unknown Kubernetes capacity', async ({
  page,
}) => {
  await mockApi(page);
  let drained = false;
  await page.route('**/v1/workers/worker-1/drain', (route) => {
    drained = route.request().postDataJSON().drained;
    return route.fulfill({ json: { id: 'worker-1', drained } });
  });
  await page.goto('/capacity');
  await page.getByRole('button', { name: 'Drain', exact: true }).click();
  expect(drained).toBe(false);
  await page.getByRole('button', { name: 'Drain worker' }).click();
  await expect(page.getByRole('dialog')).toHaveCount(0);
  expect(drained).toBe(true);
  await page.route('**/v1/capacity?**', (route) =>
    route.fulfill({
      json: {
        ...capacity,
        backend: 'kubernetes',
        workers: [],
        gpu: { ...capacity.gpu, fresh: false, available_estimate: null },
        nodes: [
          {
            name: 'gpu-node',
            capacity: 2,
            allocatable: 2,
            reserved: 1,
            ready: true,
            schedulable: false,
            eligible: false,
            available_estimate: null,
          },
        ],
      },
    }),
  );
  await page.getByRole('button', { name: 'Refresh', exact: true }).click();
  await expect(page.getByText('Unknown · inventory is stale')).toBeVisible();
  await expect(page.getByText('Cordoned', { exact: true })).toBeVisible();
});

test('preserves delayed telemetry through cursor polling and switches attempts', async ({
  page,
}) => {
  await mockApi(page);
  const cursors: string[] = [];
  await page.route('**/v1/runs/run-001', (route) =>
    route.fulfill({ json: { ...run, status: 'RUNNING' } }),
  );
  await page.route('**/v1/runs/run-001/attempts', (route) =>
    route.fulfill({
      json: {
        items: [
          { id: 'attempt-001', generation: 2, status: 'RUNNING', gpu_allocations: [] },
          { id: 'attempt-old', generation: 1, status: 'FAILED', gpu_allocations: [] },
        ],
      },
    }),
  );
  await page.route('**/v1/runs/run-001/logs?**', (route) => {
    const params = new URL(route.request().url()).searchParams;
    const attempt = params.get('attempt')!;
    const after = params.get('after')!;
    cursors.push(after);
    const sequence = Number(after) + 1;
    return route.fulfill({
      json: {
        attempt_id: attempt,
        next_cursor: sequence,
        items: [
          {
            sequence,
            kind: 'stdout',
            text: `${attempt === 'attempt-old' ? 'Old attempt' : `Line ${sequence}`} <script>evil</script>\n`,
          },
        ],
      },
    });
  });
  await page.goto('/runs/run-001');
  await page.getByRole('tab', { name: 'Logs', exact: true }).click();
  await expect(page.getByLabel('Experiment logs')).toContainText('Line 2');
  // StrictMode may abort the first request and restart the effect at cursor zero.
  expect([...new Set(cursors)].slice(0, 2)).toEqual(['0', '1']);
  await expect(page.getByLabel('Experiment logs').locator('script')).toHaveCount(0);
  await page.getByLabel('Attempt', { exact: true }).selectOption('attempt-old');
  await expect(page.getByLabel('Experiment logs')).toContainText('Old attempt');
  await expect(page.getByLabel('Experiment logs')).not.toContainText('Line 1');
});

test('keeps mobile pages within the viewport and navigation usable', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await mockApi(page);
  for (const path of ['/', '/new', '/runs/run-001', '/capacity', '/compare?runs=run-001,run-002']) {
    await page.goto(path);
    await expect(page.getByRole('navigation', { name: 'Main navigation' })).toBeVisible();
    expect(
      await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth),
    ).toBe(true);
  }
});
