import { chromium, expect } from '@playwright/test';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { loadEnv } from 'vite';

const baseURL = process.env.RUNYARD_UI_URL || 'http://127.0.0.1:5180';
const fixturePath = process.env.RUNYARD_UI_FIXTURE || '../.local/fixture.json';
const fixture = JSON.parse(await readFile(fixturePath, 'utf8'));
const token =
  process.env.RUNYARD_OWNER_TOKEN || loadEnv('development', '..', 'RUNYARD_').RUNYARD_OWNER_TOKEN;
const browser = await chromium.launch();
const page = await browser.newPage({ baseURL, viewport: { width: 1440, height: 1000 } });
const browserErrors = [];
page.on('pageerror', (error) => browserErrors.push(error.message));
if (token)
  await page.addInitScript((value) => sessionStorage.setItem('runyard.owner-key', value), token);
const record = { started_at: new Date().toISOString(), base_url: baseURL, runs: [], checks: [] };

async function importSpec(spec) {
  await page.goto('/new');
  await page
    .getByLabel('Import specification')
    .setInputFiles({
      name: 'fixture.json',
      mimeType: 'application/json',
      buffer: Buffer.from(JSON.stringify(spec)),
    });
}
async function successful() {
  await expect(page.locator('.run-summary')).toContainText('succeeded', { timeout: 90000 });
  const id = page.url().split('/').pop();
  record.runs.push(id);
  return id;
}
try {
  const base = {
    ...fixture,
    name: 'ui-acceptance',
    parameters: { seed: 41, steps: 5, delay_ms: 100 },
    labels: { purpose: 'ui-acceptance' },
  };
  await importSpec(base);
  await page.getByRole('button', { name: 'Submit experiment', exact: true }).click();
  const original = await successful();
  await expect(page.getByRole('img', { name: /score by step/ })).toBeVisible();
  await page.getByRole('tab', { name: 'Logs', exact: true }).click();
  await expect(page.getByLabel('Experiment logs')).toContainText('step 4 seed 41');
  await page.getByRole('tab', { name: 'Artifacts', exact: true }).click();
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download result.json', exact: true }).click();
  const downloaded = await download;
  const content = await readFile(await downloaded.path());
  if (JSON.parse(content).seed !== 41)
    throw new Error('Artifact does not match submitted parameters');
  record.artifact_sha256 = createHash('sha256').update(content).digest('hex');
  record.checks.push('submit', 'metric chart', 'logs', 'artifact download');

  await page.getByRole('button', { name: 'Rerun', exact: true }).click();
  await page.getByRole('button', { name: 'Create rerun', exact: true }).click();
  const rerun = await successful();
  if (rerun === original) throw new Error('Rerun did not create a new run');
  await expect(page.locator('.lineage')).toContainText('Rerun of');
  record.checks.push('linked rerun');

  await page.goto(`/compare?runs=${original},${rerun}`);
  await expect(page.getByRole('img', { name: /score by step/ })).toBeVisible();
  record.checks.push('comparison');
  await mkdir('../.local', { recursive: true });
  await page.screenshot({ path: '../.local/ui-comparison.png', fullPage: true });

  await importSpec({ base: { ...base, name: 'ui-sweep' }, grid: { seed: [51, 52] } });
  await page.getByRole('button', { name: 'Submit sweep', exact: true }).click();
  await expect(page.getByRole('heading', { name: /Sweep submitted/ })).toContainText(
    '2 experiments',
  );
  const sweepLinks = await page
    .locator('.sweep-results .run-link')
    .evaluateAll((links) => links.map((link) => link.getAttribute('href')));
  for (const link of sweepLinks) {
    await page.goto(link);
    await successful();
  }
  record.checks.push('two-run sweep');

  await importSpec({ ...base, name: 'ui-cancellation', parameters: { mode: 'hang' } });
  await page.getByRole('button', { name: 'Submit experiment', exact: true }).click();
  await expect(page.locator('.run-summary')).toContainText('running', { timeout: 90000 });
  record.runs.push(page.url().split('/').pop());
  await page.getByRole('button', { name: 'Cancel run', exact: true }).click();
  await page.getByRole('button', { name: 'Confirm cancellation' }).click();
  await expect(page.locator('.run-summary')).toContainText('cancelled');
  await page.getByRole('tab', { name: 'Attempts', exact: true }).click();
  await expect(page.locator('.attempt-list')).toContainText('Complete', { timeout: 60000 });
  record.checks.push('cancellation and cleanup');
  await page.goto('/capacity');
  await expect(page.getByRole('heading', { name: /inventory/ })).toBeVisible();
  record.checks.push('capacity');
  if (browserErrors.length) throw new Error(browserErrors.join('\n'));
  record.finished_at = new Date().toISOString();
  await writeFile('../.local/ui-acceptance.json', JSON.stringify(record, null, 2));
  console.log(JSON.stringify(record, null, 2));
} finally {
  await browser.close();
}
