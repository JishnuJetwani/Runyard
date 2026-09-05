import { expect, test } from '@playwright/test';
import { mockApi } from './fixtures';

test('searches loaded runs and preserves the comparison selection', async ({ page }) => {
  await mockApi(page); await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Experiments', exact: true })).toBeVisible();
  await page.getByRole('checkbox', { name: /Select learning/ }).check();
  await page.getByRole('textbox', { name: 'Search loaded runs' }).fill('baseline');
  await expect(page.getByRole('link', { name: 'learning-rate-study' })).toHaveCount(0);
  await page.getByRole('checkbox', { name: /Select baseline/ }).check();
  await expect(page.getByRole('link', { name: 'Compare metrics' })).toHaveAttribute('href', '/compare?runs=run-001,run-002');
});

test('shows actionable authorization errors without inventing data', async ({ page }) => {
  await page.route('**/v1/**', route => route.fulfill({ status: 401, json: { error: { message: 'Owner credentials required', request_id: 'request-123' } } }));
  await page.goto('/');
  await page.getByRole('button', { name: 'Connect', exact: true }).click();
  await expect(page.getByRole('dialog')).toBeVisible();
  await expect(page.getByLabel('Owner API key')).toHaveAttribute('type', 'password');
  await page.getByRole('button', { name: 'Cancel', exact: true }).click();
  await expect(page.getByText('request-123', { exact: false }).first()).toBeVisible();
});
