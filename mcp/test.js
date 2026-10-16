// End-to-end test: drives mcp/server.js over stdio like a real MCP client. Needs looping-cli to be built.
'use strict';

const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'looping-mcp-test-'));
const file = path.join(dir, 'test.loo');

const server = spawn(process.execPath, [path.join(__dirname, 'server.js')], { stdio: ['pipe', 'pipe', 'inherit'] });
const pending = new Map();
let buffer = '';
server.stdout.on('data', (d) => {
  buffer += d.toString('utf8');
  let i;
  while ((i = buffer.indexOf('\n')) >= 0) {
    const line = buffer.slice(0, i);
    buffer = buffer.slice(i + 1);
    if (!line.trim()) continue;
    const msg = JSON.parse(line);
    if (pending.has(msg.id)) pending.get(msg.id)(msg);
  }
});

let nextId = 1;
function rpc(method, params) {
  const id = nextId++;
  return new Promise((resolve) => {
    pending.set(id, resolve);
    server.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
  });
}
async function call(name, args) {
  const r = await rpc('tools/call', { name, arguments: args });
  assert.ok(r.result, `no result for ${name}: ${JSON.stringify(r)}`);
  const text = r.result.content[0].text;
  return { isError: r.result.isError, text, data: r.result.isError ? null : JSON.parse(text) };
}

(async () => {
  let checks = 0;
  const ok = (cond, msg) => {
    assert.ok(cond, msg);
    checks++;
  };

  const init = await rpc('initialize', { protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 'test', version: '0' } });
  ok(init.result.serverInfo.name === 'looping-mcp', 'initialize');
  ok(init.result.protocolVersion === '2025-06-18', 'protocol negotiation');
  server.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');

  const list = await rpc('tools/list', {});
  const names = list.result.tools.map((t) => t.name);
  for (const n of ['create_schema', 'show_schema', 'add_entity', 'add_association', 'add_attribute', 'add_link', 'apply']) {
    ok(names.includes(n), `tool ${n} listed`);
  }
  ok(list.result.tools.every((t) => t.inputSchema.type === 'object'), 'every tool has an object schema');

  ok((await call('create_schema', { file })).data.ok, 'create');
  ok((await call('create_schema', { file })).isError, 'create twice fails');

  // Single calls, including a non-ASCII name.
  await call('add_entity', { file, name: 'client' });
  await call('add_entity', { file, name: 'commande', x: 500, y: 100 });
  await call('add_association', { file, name: 'passer' });
  await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'id_client', type: 'INT', identifier: true });
  await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'adresse_électronique', type: 'VARCHAR', unique: true, comment: 'e-mail' });
  await call('add_link', { file, entity: 'client', association: 'passer', cardinality: '0,n' });
  await call('add_link', { file, entity: 'commande', association: 'passer', cardinality: '1,1' });

  let s = (await call('show_schema', { file })).data.schema;
  ok(s.entities.length === 2 && s.associations.length === 1 && s.links.length === 2, 'schema counts');
  ok(s.entities[0].attributes[1].name === 'adresse_électronique', 'accented name survives');
  ok(s.entities[0].attributes[0].identifier === true, 'identifier flag');
  ok(s.entities[1].x === 500, 'position');
  ok(s.links[1].cardinality === '1,1', 'cardinality');

  await call('set_cardinality', { file, entity: 'commande', association: 'passer', cardinality: '1,n' });
  await call('update_attribute', { file, kind: 'entity', owner: 'client', attribute: 'id_client', type: 'BIGINT' });
  s = (await call('show_schema', { file })).data.schema;
  ok(s.links[1].cardinality === '1,n', 'set_cardinality');
  ok(s.entities[0].attributes[0].type === 'BIGINT', 'update_attribute');

  // Errors come back as tool errors (isError), not protocol errors.
  const bad = await call('add_attribute', { file, kind: 'entity', owner: 'nope', name: 'x' });
  ok(bad.isError && /not found/.test(bad.text), 'unknown owner is reported');
  await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'y', type: 'VARCHAR(255)' });
  ok((await call('show_schema', { file })).data.schema.entities[0].attributes.at(-1).type === 'VARCHAR(255)', 'text length');
  ok((await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'z', type: 'VARCHAR(0)' })).isError, 'bad length is refused');
  await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'prix', type: 'DECIMAL(10,2)' });
  await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'cree_le', type: 'DATETIME', time_zone: true });
  const extra = (await call('show_schema', { file })).data.schema.entities[0].attributes.slice(-2);
  ok(extra[0].type === 'DECIMAL(10,2)', 'decimal digits');
  ok(extra[1].timeZone === true, 'time zone');
  ok((await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'q', type: 'INT', time_zone: true })).isError, 'time zone only on DATETIME');
  ok((await call('add_attribute', { file, kind: 'entity', owner: 'client', name: 'q', type: 'DECIMAL(2,5)' })).isError, 'decimals cannot exceed digits');
  await call('set_comment', { file, kind: 'entity', name: 'client', comment: 'ligne 1\nligne 2' });
  ok((await call('show_schema', { file })).data.schema.entities[0].comment === 'ligne 1\nligne 2', 'entity comment');
  await call('set_comment', { file, kind: 'entity', name: 'client' });
  ok((await call('show_schema', { file })).data.schema.entities[0].comment === '', 'comment cleared');
  await call('set_link_role', { file, entity: 'client', association: 'passer', role: 'acheteur' });
  ok((await call('show_schema', { file })).data.schema.links[0].role === 'acheteur', 'link role');
  await call('set_link_role', { file, entity: 'client', association: 'passer' });
  ok((await call('show_schema', { file })).data.schema.links[0].role === '', 'link role removed');
  ok((await call('add_entity', { file: 'notes.txt', name: 'a' })).isError, 'only .loo files');
  ok((await call('add_link', { file, entity: 'client', association: 'passer', cardinality: 'many' })).isError, 'bad cardinality');
  ok((await call('no_such_tool', {})).isError, 'unknown tool');

  // apply: all or nothing.
  const before = fs.readFileSync(file);
  const failing = await call('apply', {
    file,
    operations: [
      { op: 'add_entity', name: 'produit' },
      { op: 'add_attribute', kind: 'entity', owner: 'produit', name: 'id_produit', identifier: true },
      { op: 'add_link', entity: 'produit', association: 'does_not_exist', cardinality: '0,n' },
    ],
  });
  ok(failing.isError && /operation 3/.test(failing.text), 'apply names the failing operation');
  ok(Buffer.compare(before, fs.readFileSync(file)) === 0, 'apply rolls the file back');

  const good = await call('apply', {
    file,
    operations: [
      { op: 'add_entity', name: 'produit' },
      { op: 'add_attribute', kind: 'entity', owner: 'produit', name: 'id_produit', type: 'INT', identifier: true },
      { op: 'add_association', name: 'contenir' },
      { op: 'add_link', entity: 'produit', association: 'contenir', cardinality: '0,n' },
      { op: 'add_link', entity: 'commande', association: 'contenir', cardinality: '1,n' },
    ],
  });
  ok(!good.isError && good.data.applied === 5, 'apply succeeds');
  ok(good.data.schema.links.length === 4, 'apply returns the new schema');

  // apply on a file that does not exist yet creates it, and a failure removes it again.
  const fresh = path.join(dir, 'fresh.loo');
  ok((await call('apply', { file: fresh, operations: [{ op: 'create_schema' }, { op: 'add_entity', name: 'a' }, { op: 'add_entity', name: 'a' }] })).isError, 'duplicate in batch fails');
  ok(!fs.existsSync(fresh), 'failed batch leaves no file behind');
  ok(!(await call('apply', { file: fresh, operations: [{ op: 'create_schema' }, { op: 'add_entity', name: 'a' }] })).isError, 'batch can create the file');

  console.log(`${checks} checks passed`);
  server.kill();
  fs.rmSync(dir, { recursive: true, force: true });
})().catch((e) => {
  console.error('FAILED:', e.message);
  server.kill();
  process.exit(1);
});
