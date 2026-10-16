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
  let data = null;
  if (!r.result.isError) {
    try { data = JSON.parse(text); } catch { data = null; }  // describe_schema returns plain text
  }
  return { isError: r.result.isError, text, data };
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
  for (const n of ['create_schema', 'show_schema', 'describe_schema', 'add_entity', 'add_association', 'add_attribute', 'add_link', 'apply']) {
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

  // describe_schema: readable MCD + derived MLD.
  const described = await call('describe_schema', { file });
  ok(!described.isError && /MCD \(conceptual model\)/.test(described.text), 'describe has an MCD section');
  ok(/client --0,n-- passer/.test(described.text), 'describe lists links with cardinalities');
  ok(/commande \(.*id_client INT FK->client\.id_client NOT NULL/.test(described.text) || /FK->/.test(described.text), 'describe derives foreign keys');
  ok(/adresse_électronique/.test(described.text), 'describe keeps accents');

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

  // A model can go in folders that do not exist yet.
  const nested = path.join(dir, 'db', 'model', 'v1.loo');
  ok((await call('create_schema', { file: nested })).data.ok && fs.existsSync(nested), 'missing folders are created');

  // apply on a file that does not exist yet creates it, and a failure removes it again.
  const fresh = path.join(dir, 'fresh.loo');
  ok((await call('apply', { file: fresh, operations: [{ op: 'create_schema' }, { op: 'add_entity', name: 'a' }, { op: 'add_entity', name: 'a' }] })).isError, 'duplicate in batch fails');
  ok(!fs.existsSync(fresh), 'failed batch leaves no file behind');
  ok(!(await call('apply', { file: fresh, operations: [{ op: 'create_schema' }, { op: 'add_entity', name: 'a' }] })).isError, 'batch can create the file');

  // Automatic layout: apply places new objects (and only those); auto_layout can re-arrange or place named ones.
  const laid = path.join(dir, 'laid.loo');
  const built = await call('apply', {
    file: laid,
    operations: [
      { op: 'create_schema' },
      { op: 'add_entity', name: 'a' },
      { op: 'add_entity', name: 'b' },
      { op: 'add_entity', name: 'c', x: 900, y: 500 },
      { op: 'add_association', name: 'ab' },
      { op: 'add_association', name: 'bc' },
      { op: 'add_link', entity: 'a', association: 'ab', cardinality: '0,n' },
      { op: 'add_link', entity: 'b', association: 'ab', cardinality: '1,1' },
      { op: 'add_link', entity: 'b', association: 'bc', cardinality: '0,n' },
      { op: 'add_link', entity: 'c', association: 'bc', cardinality: '0,n' },
    ],
  });
  const boxes = (schema) => [...schema.entities, ...schema.associations];
  const spot = (schema, name) => boxes(schema).find((o) => o.name === name);
  ok(!built.isError, 'apply with automatic layout succeeds');
  ok(spot(built.data.schema, 'c').x === 900 && spot(built.data.schema, 'c').y === 500, 'explicit coordinates are respected');
  ok(built.data.schema.associations.every((o) => o.x % 10 === 0 && o.y % 10 === 0), 'automatic positions are on the grid');
  ok(new Set(boxes(built.data.schema).map((o) => `${o.x},${o.y}`)).size === 5, 'no two objects share a position');

  const grown = await call('apply', {
    file: laid,
    operations: [{ op: 'add_entity', name: 'd' }, { op: 'add_association', name: 'cd' }, { op: 'add_link', entity: 'c', association: 'cd', cardinality: '0,n' }, { op: 'add_link', entity: 'd', association: 'cd', cardinality: '0,n' }],
  });
  ok(['a', 'b', 'c', 'ab', 'bc'].every((n) => spot(grown.data.schema, n).x === spot(built.data.schema, n).x &&
    spot(grown.data.schema, n).y === spot(built.data.schema, n).y), 'a later apply leaves existing objects where they are');
  const noLayout = await call('apply', { file: laid, auto_layout: false, operations: [{ op: 'add_entity', name: 'e' }] });
  ok(spot(noLayout.data.schema, 'e').x === 100 && spot(noLayout.data.schema, 'e').y === 480, 'auto_layout:false keeps the plain grid position');

  const all = await call('auto_layout', { file: laid });
  ok(!all.isError && all.data.moved >= 0, 'auto_layout arranges everything');
  const again = await call('auto_layout', { file: laid });
  ok(again.data.moved === 0, 'auto_layout is stable');
  const beforeOnly = (await call('show_schema', { file: laid })).data.schema;
  ok(!(await call('auto_layout', { file: laid, only: ['e'] })).isError, 'auto_layout accepts a list of names');
  const afterOnly = (await call('show_schema', { file: laid })).data.schema;
  ok(['a', 'b', 'c', 'd', 'ab', 'bc', 'cd'].every((n) => spot(afterOnly, n).x === spot(beforeOnly, n).x &&
    spot(afterOnly, n).y === spot(beforeOnly, n).y), 'only: the other objects do not move');
  ok(boxes(afterOnly).length === 8, 'all objects are still there');
  ok((await call('auto_layout', { file: laid, only: ['nope'] })).isError, 'auto_layout rejects unknown names');

  console.log(`${checks} checks passed`);
  server.kill();
  fs.rmSync(dir, { recursive: true, force: true });
})().catch((e) => {
  console.error('FAILED:', e.message);
  server.kill();
  process.exit(1);
});
