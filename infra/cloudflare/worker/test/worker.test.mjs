// SPDX-License-Identifier: GPL-2.0-or-later
//
// Unit tests for the UniLume package router Worker.
// Run with: node --test test/worker.test.mjs

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';

// Mock R2 bucket
class MockR2Object {
  constructor(body, opts = {}) {
    this.body = body;
    this.etag = opts.etag || 'abc123';
    this.uploaded = opts.uploaded || new Date();
    this.httpMetadata = {};
  }
  async text() { return this.body; }
}

class MockR2Bucket {
  constructor() {
    this.objects = new Map();
  }
  async get(key) {
    return this.objects.get(key) || null;
  }
  set(key, value) {
    this.objects.set(key, value);
  }
}

function createRequest(url, method = 'GET') {
  return new Request(url, { method });
}

describe('UniLume Package Router Worker', () => {
  let env;
  let worker;

  before(async () => {
    const mod = await import('../src/index.mjs');
    worker = mod.default;
    env = {
      UNILUME_PACKAGES: new MockR2Bucket(),
    };
    // Set up test objects
    env.UNILUME_PACKAGES.set('stable/deb/amd64/unilume_0.1.0~rc1_amd64.deb', 'deb-content');
    env.UNILUME_PACKAGES.set('stable/generic/x86_64/unilume-0.1.0-rc1-linux-x86_64.tar.zst', 'tar-content');
    env.UNILUME_PACKAGES.set('keys/unilume-archive-key.asc', 'key-content');
    env.UNILUME_PACKAGES.set('status.json', JSON.stringify({ version: '0.1.0-rc.1' }));
    env.UNILUME_PACKAGES.set('stable/latest-x86_64.txt', '0.1.0-rc.1');
  });

  it('serves a .deb from the pool', async () => {
    const req = createRequest('https://packages.dismon.me/pool/stable/deb/amd64/unilume_0.1.0~rc1_amd64.deb');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 200);
    assert.strictEqual(resp.headers.get('Content-Type'), 'application/vnd.debian.binary-package');
  });

  it('serves a generic tar.zst', async () => {
    const req = createRequest('https://packages.dismon.me/pool/stable/generic/x86_64/unilume-0.1.0-rc1-linux-x86_64.tar.zst');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 200);
  });

  it('serves public keys', async () => {
    const req = createRequest('https://packages.dismon.me/keys/unilume-archive-key.asc');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 200);
    assert.strictEqual(resp.headers.get('Cache-Control'), 'public, max-age=86400, immutable');
  });

  it('serves status.json', async () => {
    const req = createRequest('https://packages.dismon.me/status.json');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 200);
    assert.strictEqual(resp.headers.get('Content-Type'), 'application/json');
  });

  it('redirects /latest/stable/x86_64', async () => {
    const req = createRequest('https://packages.dismon.me/latest/stable/x86_64');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 302);
    assert.ok(resp.headers.get('Location').includes('0.1.0-rc.1'));
  });

  it('rejects path traversal', async () => {
    const req = createRequest('https://packages.dismon.me/../../etc/passwd');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 400);
  });

  it('returns 404 for unknown objects', async () => {
    const req = createRequest('https://packages.dismon.me/pool/stable/deb/amd64/nonexistent.deb');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 404);
  });

  it('rejects unknown channel', async () => {
    const req = createRequest('https://packages.dismon.me/latest/nonexistent/x86_64');
    const resp = await worker.fetch(req, env);
    assert.strictEqual(resp.status, 404);
  });

  it('supports HEAD requests', async () => {
    const req = createRequest('https://packages.dismon.me/status.json', 'HEAD');
    const resp = await worker.fetch(req, env);
    assert.ok(resp.status === 200 || resp.status === 405);
  });
});
