#!/usr/bin/env node
// MCP server (stdio) that lets AI agents build Looping MCD schemas.
//
// A thin wrapper: every tool becomes one or more calls to `looping-cli` (see docs/usage.md), which does the real work
// on the .loo file. No dependencies; speaks newline-delimited JSON-RPC 2.0 as required by the MCP stdio transport.
'use strict';

const { execFile } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');

const ROOT = path.resolve(__dirname, '..');
const CLI =
  process.env.LOOPING_CLI ||
  path.join(ROOT, 'build', 'cli', process.platform === 'win32' ? 'looping-cli.exe' : 'looping-cli');
const SERVER_INFO = { name: 'looping-mcp', version: '0.1.0' };
const PROTOCOLS = ['2025-06-18', '2025-03-26', '2024-11-05'];

// ---- running the CLI ---------------------------------------------------------------------------------------------

function runCli(argv) {
  return new Promise((resolve, reject) => {
    execFile(CLI, argv, { encoding: 'utf8', windowsHide: true, maxBuffer: 64 * 1024 * 1024 }, (err, stdout) => {
      const line = String(stdout || '').trim().split('\n').pop();
      let parsed;
      try {
        parsed = JSON.parse(line);
      } catch {
        return reject(new Error(err ? `looping-cli failed: ${err.message}` : `unexpected looping-cli output: ${line}`));
      }
      if (parsed.ok === false) return reject(new Error(parsed.error || 'looping-cli error'));
      resolve(parsed);
    });
  });
}

// ---- argument helpers --------------------------------------------------------------------------------------------

function str(args, key, { required = true } = {}) {
  const v = args[key];
  if (v === undefined || v === null || v === '') {
    if (required) throw new Error(`'${key}' is required`);
    return undefined;
  }
  if (typeof v !== 'string') throw new Error(`'${key}' must be a string`);
  return v;
}

function int(args, key) {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (!Number.isInteger(v)) throw new Error(`'${key}' must be an integer`);
  return v;
}

function bool(args, key) {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== 'boolean') throw new Error(`'${key}' must be true or false`);
  return v;
}

function kind(args) {
  const k = args.kind;
  if (k !== 'entity' && k !== 'association') throw new Error("'kind' must be \"entity\" or \"association\"");
  return k;
}

function file(args) {
  const f = str(args, 'file');
  if (!/\.loo$/i.test(f)) throw new Error("'file' must be a path ending in .loo");
  return path.resolve(f);
}

function pointArgs(args) {
  const x = int(args, 'x');
  const y = int(args, 'y');
  if ((x === undefined) !== (y === undefined)) throw new Error("give both 'x' and 'y', or neither");
  return x === undefined ? [] : ['--x', String(x), '--y', String(y)];
}

function commentArgs(args) {
  const c = str(args, 'comment', { required: false });
  return c ? ['--comment', c] : [];
}

const ownerFlag = (k) => (k === 'entity' ? '--entity' : '--association');

// ---- tool definitions --------------------------------------------------------------------------------------------

const FILE = { type: 'string', description: 'Path to the .loo file (absolute, or relative to the working directory).' };
const KIND = {
  type: 'string',
  enum: ['entity', 'association'],
  description: 'Whether the object is an entity or an association.',
};
const TYPE_HELP =
  'SQL-style type: VARCHAR, NVARCHAR, VARBINARY, CHAR, NCHAR, BINARY, TEXT, NTEXT, BLOB, TINYINT, SMALLINT, INT, ' +
  'BIGINT, DECIMAL, FLOAT, DOUBLE, MONEY, COUNTER, DATE, TIME, DATETIME, BOOLEAN, XML, GEOGRAPHY, GEOMETRY. ' +
  'Text types take a length, e.g. "VARCHAR(255)" or "CHAR(10)" (1-65535, default 50; TEXT/NTEXT/BLOB take none). ' +
  'DECIMAL takes the total number of digits and the digits after the decimal point, e.g. "DECIMAL(10,2)" (default ' +
  '15,2). FLOAT is 32-bit and DOUBLE 64-bit; TINYINT/SMALLINT/INT/BIGINT are 8/16/32/64-bit integers. ' +
  'N-types are the Unicode variants; VARBINARY/BINARY/BLOB are the binary variants.';

// build(args) returns the list of CLI argument vectors to run (usually one).
const TOOLS = {
  create_schema: {
    description: 'Create a new empty Looping file. Fails if the file already exists.',
    properties: { file: FILE },
    required: ['file'],
    build: (a) => [['new', file(a)]],
  },
  show_schema: {
    description:
      'Read a Looping file and return its schema as JSON: entities and associations (with position and attributes) ' +
      'and the links between them with their cardinalities.',
    properties: { file: FILE },
    required: ['file'],
    readOnly: true,
    build: (a) => [['show', file(a)]],
  },
  list_types: {
    description: 'List the attribute type names accepted by add_attribute / update_attribute.',
    properties: {},
    required: [],
    readOnly: true,
    build: () => [['types']],
  },
  add_entity: {
    description:
      'Add an entity (a table-to-be in the conceptual model). Names must be unique across entities and associations. ' +
      'Position is optional (pixels, top-left corner); entities are laid out in a grid when omitted.',
    properties: {
      file: FILE,
      name: { type: 'string', description: 'Entity name.' },
      x: { type: 'integer' },
      y: { type: 'integer' },
      comment: { type: 'string', description: 'Optional comment shown in the entity dialog ("Commentaire").' },
    },
    required: ['file', 'name'],
    build: (a) => [['add-entity', file(a), str(a, 'name'), ...pointArgs(a), ...commentArgs(a)]],
  },
  add_association: {
    description:
      'Add an association (a relationship between entities, e.g. "passer" between client and commande). Link it to ' +
      'entities with add_link. Position is the centre of the ellipse; laid out in a grid when omitted.',
    properties: {
      file: FILE,
      name: { type: 'string', description: 'Association name.' },
      x: { type: 'integer' },
      y: { type: 'integer' },
      comment: { type: 'string', description: 'Optional comment shown in the association dialog ("Commentaire").' },
    },
    required: ['file', 'name'],
    build: (a) => [['add-association', file(a), str(a, 'name'), ...pointArgs(a), ...commentArgs(a)]],
  },
  set_comment: {
    description:
      'Set the comment of an entity or association (the "Commentaire" box of its dialog). Multi-line text is ' +
      'supported (lines separated by newlines). An empty or missing comment clears it. For a comment on an ' +
      'attribute use add_attribute / update_attribute.',
    properties: { file: FILE, kind: KIND, name: { type: 'string' }, comment: { type: 'string' } },
    required: ['file', 'kind', 'name'],
    build: (a) => {
      const argv = ['set-comment', file(a), ownerFlag(kind(a)), str(a, 'name')];
      const comment = str(a, 'comment', { required: false });
      if (comment) argv.push('--comment', comment);
      return [argv];
    },
  },
  rename_object: {
    description: 'Rename an entity or an association.',
    properties: { file: FILE, kind: KIND, name: { type: 'string' }, new_name: { type: 'string' } },
    required: ['file', 'kind', 'name', 'new_name'],
    build: (a) => [[`rename-${kind(a)}`, file(a), str(a, 'name'), str(a, 'new_name')]],
  },
  move_object: {
    description: 'Move an entity (top-left corner) or an association (centre) to pixel coordinates.',
    properties: { file: FILE, kind: KIND, name: { type: 'string' }, x: { type: 'integer' }, y: { type: 'integer' } },
    required: ['file', 'kind', 'name', 'x', 'y'],
    build: (a) => [[`move-${kind(a)}`, file(a), str(a, 'name'), String(int(a, 'x') ?? 0), String(int(a, 'y') ?? 0)]],
  },
  remove_object: {
    description: 'Delete an entity or an association together with all its attributes and every link attached to it.',
    properties: { file: FILE, kind: KIND, name: { type: 'string' } },
    required: ['file', 'kind', 'name'],
    build: (a) => [[`remove-${kind(a)}`, file(a), str(a, 'name')]],
  },
  add_attribute: {
    description:
      'Add an attribute to an entity or association. Mark primary-key attributes with identifier=true (this also ' +
      'implies NOT NULL). Looping derives foreign keys in the logical model (MLD) itself, so do not add them by hand.',
    properties: {
      file: FILE,
      kind: KIND,
      owner: { type: 'string', description: 'Name of the entity or association that owns the attribute.' },
      name: { type: 'string', description: 'Attribute name.' },
      type: { type: 'string', description: TYPE_HELP + ' Default: INT.' },
      identifier: { type: 'boolean', description: 'Part of the identifier (primary key).' },
      not_null: { type: 'boolean' },
      unique: { type: 'boolean' },
      time_zone: { type: 'boolean', description: 'DATETIME only: store the time zone ("avec fuseau horaire").' },
      logical_name: { type: 'string', description: 'Column name in the logical model. Empty means same as name.' },
      comment: { type: 'string', description: 'Free-text comment stored with the attribute.' },
    },
    required: ['file', 'kind', 'owner', 'name'],
    build: (a) => {
      const argv = ['add-attribute', file(a), ownerFlag(kind(a)), str(a, 'owner'), '--name', str(a, 'name')];
      const type = str(a, 'type', { required: false });
      if (type) argv.push('--type', type);
      if (bool(a, 'identifier')) argv.push('--identifier');
      if (bool(a, 'not_null')) argv.push('--not-null');
      if (bool(a, 'unique')) argv.push('--unique');
      if (bool(a, 'time_zone')) argv.push('--time-zone');
      const logical = str(a, 'logical_name', { required: false });
      if (logical) argv.push('--logical-name', logical);
      const comment = str(a, 'comment', { required: false });
      if (comment) argv.push('--comment', comment);
      return [argv];
    },
  },
  update_attribute: {
    description: 'Change an existing attribute. Only the properties you pass are changed.',
    properties: {
      file: FILE,
      kind: KIND,
      owner: { type: 'string' },
      attribute: { type: 'string', description: 'Current name of the attribute.' },
      name: { type: 'string', description: 'New name.' },
      type: { type: 'string', description: TYPE_HELP },
      identifier: { type: 'boolean' },
      not_null: { type: 'boolean' },
      unique: { type: 'boolean' },
      time_zone: { type: 'boolean', description: 'DATETIME only.' },
      logical_name: { type: 'string' },
      comment: { type: 'string' },
    },
    required: ['file', 'kind', 'owner', 'attribute'],
    build: (a) => {
      const argv = ['update-attribute', file(a), ownerFlag(kind(a)), str(a, 'owner'), '--attribute', str(a, 'attribute')];
      for (const [key, flag] of [['name', '--name'], ['type', '--type'], ['logical_name', '--logical-name'], ['comment', '--comment']]) {
        if (a[key] !== undefined && a[key] !== null) {
          if (typeof a[key] !== 'string') throw new Error(`'${key}' must be a string`);
          argv.push(flag, a[key]);
        }
      }
      for (const [key, flag] of [['identifier', '--identifier'], ['not_null', '--not-null'], ['unique', '--unique'], ['time_zone', '--time-zone']]) {
        const v = bool(a, key);
        if (v !== undefined) argv.push(flag, String(v));
      }
      return [argv];
    },
  },
  remove_attribute: {
    description: 'Delete an attribute from an entity or association.',
    properties: { file: FILE, kind: KIND, owner: { type: 'string' }, attribute: { type: 'string' } },
    required: ['file', 'kind', 'owner', 'attribute'],
    build: (a) => [['remove-attribute', file(a), ownerFlag(kind(a)), str(a, 'owner'), '--attribute', str(a, 'attribute')]],
  },
  add_link: {
    description:
      'Link an entity to an association with a cardinality. The cardinality is read on the entity side: "0,1" = the ' +
      'entity takes part in at most one occurrence of the association, "1,1" exactly one, "0,n" any number, "1,n" at ' +
      'least one. A pair (entity, association) can be linked once.',
    properties: {
      file: FILE,
      entity: { type: 'string' },
      association: { type: 'string' },
      cardinality: { type: 'string', enum: ['0,1', '1,1', '0,n', '1,n'] },
      role: { type: 'string', description: 'Optional role label ("Libell\u00e9") for the link; it names the foreign key.' },
    },
    required: ['file', 'entity', 'association', 'cardinality'],
    build: (a) => {
      const argv = ['add-link', file(a), str(a, 'entity'), str(a, 'association'), str(a, 'cardinality')];
      const role = str(a, 'role', { required: false });
      if (role) argv.push('--role', role);
      return [argv];
    },
  },
  set_link_role: {
    description: 'Set the role label of an existing link. An empty or missing role removes it.',
    properties: { file: FILE, entity: { type: 'string' }, association: { type: 'string' }, role: { type: 'string' } },
    required: ['file', 'entity', 'association'],
    build: (a) => {
      const argv = ['set-link-role', file(a), str(a, 'entity'), str(a, 'association')];
      const role = str(a, 'role', { required: false });
      if (role) argv.push(role);
      return [argv];
    },
  },
  set_cardinality: {
    description: 'Change the cardinality of an existing link.',
    properties: {
      file: FILE,
      entity: { type: 'string' },
      association: { type: 'string' },
      cardinality: { type: 'string', enum: ['0,1', '1,1', '0,n', '1,n'] },
    },
    required: ['file', 'entity', 'association', 'cardinality'],
    build: (a) => [['set-cardinality', file(a), str(a, 'entity'), str(a, 'association'), str(a, 'cardinality')]],
  },
  remove_link: {
    description: 'Delete the link between an entity and an association.',
    properties: { file: FILE, entity: { type: 'string' }, association: { type: 'string' } },
    required: ['file', 'entity', 'association'],
    build: (a) => [['remove-link', file(a), str(a, 'entity'), str(a, 'association')]],
  },
};

const BATCHABLE = Object.keys(TOOLS).filter((n) => !TOOLS[n].readOnly);

TOOLS.apply = {
  description:
    'Run several editing operations on one file, in order, as a single all-or-nothing step: if any operation fails, ' +
    'the file is restored to its state before the call and the error says which operation failed. Each operation is ' +
    'an object {"op": <tool name>, ...that tool\'s arguments without "file">. Allowed ops: ' +
    BATCHABLE.join(', ') +
    '. Prefer this over many single calls when building a whole schema.',
  properties: {
    file: FILE,
    operations: {
      type: 'array',
      description: 'Operations to run in order.',
      items: {
        type: 'object',
        properties: { op: { type: 'string', enum: BATCHABLE } },
        required: ['op'],
        additionalProperties: true,
      },
    },
  },
  required: ['file', 'operations'],
  async run(a) {
    const f = file(a);
    if (!Array.isArray(a.operations) || a.operations.length === 0) throw new Error("'operations' must be a non-empty array");
    const existed = fs.existsSync(f);
    const original = existed ? fs.readFileSync(f) : null;
    const results = [];
    try {
      for (let i = 0; i < a.operations.length; i++) {
        const { op, ...rest } = a.operations[i] || {};
        try {
          if (!BATCHABLE.includes(op)) throw new Error(`unknown or unsupported op '${op}'`);
          for (const argv of TOOLS[op].build({ ...rest, file: f })) await runCli(argv);
          results.push(op);
        } catch (e) {
          throw new Error(`operation ${i + 1} (${op}) failed: ${e.message}`);
        }
      }
    } catch (e) {
      if (existed) fs.writeFileSync(f, original);
      else fs.rmSync(f, { force: true });
      throw new Error(`${e.message}. The file was left unchanged.`);
    }
    const shown = await runCli(['show', f]);
    return { ok: true, applied: results.length, schema: shown.schema };
  },
};

// ---- MCP plumbing ------------------------------------------------------------------------------------------------

function toolList() {
  return Object.entries(TOOLS).map(([name, t]) => ({
    name,
    description: t.description,
    inputSchema: { type: 'object', properties: t.properties, required: t.required },
    annotations: { readOnlyHint: !!t.readOnly, destructiveHint: !t.readOnly, openWorldHint: false },
  }));
}

async function callTool(name, args) {
  const tool = TOOLS[name];
  if (!tool) throw new Error(`unknown tool '${name}'`);
  if (tool.run) return tool.run(args || {});
  let last;
  for (const argv of tool.build(args || {})) last = await runCli(argv);
  return last;
}

function send(msg) {
  process.stdout.write(JSON.stringify(msg) + '\n');
}

async function handle(msg) {
  const { id, method, params } = msg;
  const isRequest = id !== undefined && id !== null;
  try {
    switch (method) {
      case 'initialize': {
        const wanted = params && params.protocolVersion;
        send({
          jsonrpc: '2.0',
          id,
          result: {
            protocolVersion: PROTOCOLS.includes(wanted) ? wanted : PROTOCOLS[0],
            capabilities: { tools: { listChanged: false } },
            serverInfo: SERVER_INFO,
            instructions:
              'Builds Looping MCD schemas (.loo files). Create a file, add entities, associations and attributes, ' +
              'then link entities to associations with a cardinality. Open the file in Looping to see the MCD and MLD.',
          },
        });
        return;
      }
      case 'ping':
        if (isRequest) send({ jsonrpc: '2.0', id, result: {} });
        return;
      case 'tools/list':
        send({ jsonrpc: '2.0', id, result: { tools: toolList() } });
        return;
      case 'tools/call': {
        let result;
        try {
          const data = await callTool(params && params.name, params && params.arguments);
          result = { content: [{ type: 'text', text: JSON.stringify(data) }], isError: false };
        } catch (e) {
          result = { content: [{ type: 'text', text: e.message }], isError: true };
        }
        send({ jsonrpc: '2.0', id, result });
        return;
      }
      default:
        if (isRequest) send({ jsonrpc: '2.0', id, error: { code: -32601, message: `method not found: ${method}` } });
    }
  } catch (e) {
    if (isRequest) send({ jsonrpc: '2.0', id, error: { code: -32603, message: e.message } });
  }
}

if (require.main === module) {
  if (!fs.existsSync(CLI)) {
    process.stderr.write(`looping-cli not found at ${CLI}. Build it first (cmake --build build) or set LOOPING_CLI.\n`);
  }
  const rl = readline.createInterface({ input: process.stdin });
  rl.on('line', (line) => {
    if (!line.trim()) return;
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      send({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'parse error' } });
      return;
    }
    handle(msg);
  });
}

module.exports = { TOOLS, callTool };
