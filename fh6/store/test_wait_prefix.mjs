import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, writeFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {waitForPrefix} from './wait-prefix.mjs';

async function withServer(body, fn) {
  const dir = mkdtempSync(path.join(tmpdir(), 'fh6-wait-test-'));
  const server = path.join(dir, 'wineserver');
  writeFileSync(server, '#!/bin/sh\n' + body + '\n', {mode: 0o700});
  try { return await fn(server); } finally { rmSync(dir, {recursive: true}); }
}

test('normal prefix exit preserves status', async () => {
  await withServer('exit 7', async server => {
    assert.equal(await waitForPrefix({server, isGameRunning: () => false}), 7);
  });
});

test('releases a stale waiter only after the live game disappears', async () => {
  await withServer('exec /bin/sleep 10', async server => {
    const start = Date.now();
    let announced = false;
    const code = await waitForPrefix({server, graceMs: 50, pollMs: 10,
      isGameRunning: () => Date.now() - start < 120,
      log: () => { announced = true; assert.ok(Date.now() - start >= 170); }});
    assert.equal(code, 0);
    assert.equal(announced, true);
    assert.ok(Date.now() - start < 2000);
  });
});

test('failed process inspection cannot trigger stale cleanup', async () => {
  await withServer('/bin/sleep 0.2\nexit 9', async server => {
    const messages = [];
    const code = await waitForPrefix({server, graceMs: 20, pollMs: 10,
      isGameRunning: () => { throw Error('unavailable'); }, log: s => messages.push(s)});
    assert.equal(code, 9);
    assert.equal(messages.length, 1);
    assert.match(messages[0], /retaining prefix wait/);
  });
});
