// Parses every ```mermaid block of the given Markdown files with Mermaid
// itself (backlog B-22, FR-007 criterion 1). Prints "file:line: error" for
// each block that does not parse and exits 1; exits 2 on a usage or I/O
// error. Never renders and never opens a network connection.
//
// Mermaid sanitises labels with DOMPurify, which needs a DOM window when the
// module is loaded: without one, mermaid.parse() of any flowchart with a
// label fails with "DOMPurify.addHook is not a function" (tried with Mermaid
// 11.17.2 on Node 22). jsdom supplies that window; nothing is laid out.
import { readFile } from "node:fs/promises";
import { JSDOM } from "jsdom";

const { window } = new JSDOM("");
globalThis.window = window;
globalThis.document = window.document;
// Imported only after the window exists: DOMPurify binds to it on load.
const { default: mermaid } = await import("mermaid");

// The version that actually ran, as the lock file installed it.
const MERMAID_VERSION = JSON.parse(
  await readFile(new URL("./node_modules/mermaid/package.json", import.meta.url), "utf8"),
).version;

const FENCE_OPEN = /^(`{3,}|~{3,})mermaid\s*$/;

/** Each ```mermaid block of a Markdown text as { line, source }, line 1-based. */
function mermaid_blocks(text) {
  const blocks = [];
  const lines = text.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const open = FENCE_OPEN.exec(lines[i]);
    if (!open) continue;
    const fence = open[1];
    const start = i;
    const body = [];
    for (i++; i < lines.length && lines[i].trimEnd() !== fence; i++) {
      body.push(lines[i]);
    }
    blocks.push({ line: start + 1, source: body.join("\n") + "\n" });
  }
  return blocks;
}

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error("usage: check-mermaid.mjs <file.md>...");
  process.exit(2);
}

let failures = 0;
let blocks_checked = 0;
for (const file of files) {
  let text;
  try {
    text = await readFile(file, "utf8");
  } catch (error) {
    console.error(`${file}: cannot read: ${error.message}`);
    process.exit(2);
  }
  const blocks = mermaid_blocks(text);
  if (blocks.length === 0) {
    // A map without its graph is a failure too: the check would pass vacuously.
    console.error(`${file}: no mermaid block`);
    failures++;
    continue;
  }
  for (const block of blocks) {
    blocks_checked++;
    try {
      await mermaid.parse(block.source);
    } catch (error) {
      failures++;
      console.error(`${file}:${block.line}: Mermaid ${MERMAID_VERSION} cannot parse this block:\n${error.message ?? error}`);
    }
  }
}

if (failures > 0) {
  console.error(`check-mermaid: ${failures} failure(s) in ${files.length} file(s)`);
  process.exit(1);
}
console.log(`check-mermaid: OK (${blocks_checked} block(s) in ${files.length} file(s), Mermaid ${MERMAID_VERSION})`);
