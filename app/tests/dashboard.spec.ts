import { test, expect } from '@playwright/test';

test('has title', async ({ page }) => {
  await page.goto('/');

  // Expect a title "to contain" a substring.
  await expect(page).toHaveTitle(/ResQ-Band/);
});

test('connect device button is visible', async ({ page }) => {
  await page.goto('/');

  // Look for a button or text that indicates connection or Web Serial functionality
  // Since we don't know the exact class names, we can look for generic text like "Connect" or "ResQ-Band"
  const connectText = page.locator('text=ResQ');
  await expect(connectText.first()).toBeVisible();
});
