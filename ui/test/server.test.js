import assert from "node:assert/strict";
import { after, before, test } from "node:test";
import { createServer } from "../server.js";

let server;
let baseUrl;

before(async () => {
  server = createServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve, reject) => {
    server.close((error) => error ? reject(error) : resolve());
  });
});

test("compiler status reports the built frontend", async () => {
  const response = await fetch(`${baseUrl}/api/status`);
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.compilerAvailable, true);
  assert.equal(typeof body.llcAvailable, "boolean");
});

test("compiler interface checks valid source using the executable", async () => {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      stage: "check",
      source: "int main(void) { return 0; }\n",
    }),
  });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.success, true);
  assert.equal(body.exitCode, 0);
});

test("compiler interface returns tokens from the Flex scanner", async () => {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      stage: "tokens",
      source: "int main(void) { return 0; }\n",
    }),
  });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.success, true);
  assert.match(body.stdout, /identifier "main"/);
});

test("compiler interface displays source diagnostics", async () => {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      stage: "check",
      source: "int main(void) { return missing; }\n",
    }),
  });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.success, false);
  assert.match(body.stderr, /error\[/);
});

test("assembly stage explains how to install the missing LLVM backend", async () => {
  const statusResponse = await fetch(`${baseUrl}/api/status`);
  const status = await statusResponse.json();
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      stage: "assembly",
      source: "int main(void) { return 0; }\n",
    }),
  });
  if (status.llcAvailable) {
    assert.equal(response.status, 200);
    const body = await response.json();
    assert.equal(body.success, true, body.stderr);
    assert.match(body.stdout, /\.text/);
    assert.match(body.stdout, /main/);
    return;
  }

  assert.equal(response.status, 501);
  const body = await response.json();
  assert.match(body.error, /LLVM llc.*not found/);
  assert.match(body.error, /C_MINOR_LLVM_PATH/);
});

test("test-suite action runs the compiler acceptance suite", async () => {
  const response = await fetch(`${baseUrl}/api/test-suite`, { method: "POST" });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.success, true, body.stderr);
  assert.match(body.stdout, /PASS: 64 positive, 37 negative, 100 malformed\/mutated cases/);
});

test("run-program action either executes or explains the unavailable backend", async () => {
  const statusResponse = await fetch(`${baseUrl}/api/status`);
  const status = await statusResponse.json();
  const response = await fetch(`${baseUrl}/api/run`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      source: "int add(int left, int right) { return left + right; }\n" +
        "int main(void) { if (add(10, 2) != 12) return 1; return 7; }\n",
    }),
  });
  if (!status.executionAvailable) {
    assert.equal(response.status, 501);
    const body = await response.json();
    assert.match(body.error, /Linux x86-64|llc|GCC/);
    return;
  }
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.compiled, true);
  assert.equal(body.success, true);
  assert.equal(body.exitCode, 7);
  assert.equal(body.stdout, "");
});

test("run-program action captures string, character, and variadic printf output", async () => {
  const statusResponse = await fetch(`${baseUrl}/api/status`);
  const status = await statusResponse.json();
  const response = await fetch(`${baseUrl}/api/run`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      source: 'int main(void) { int value = 7; ' +
        'printf("Hello World"); printf("\\nvalue=%d char=%c\\n", value, \'A\'); ' +
        'printf("%d%d%d%d%d%d%d\\n", 1, 2, 3, 4, 5, 6, 7); return 0; }\n',
    }),
  });
  if (!status.executionAvailable) {
    assert.equal(response.status, 501);
    return;
  }
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.compiled, true);
  assert.equal(body.success, true);
  assert.equal(body.exitCode, 0);
  assert.equal(
    body.stdout.replace(/\r\n/g, "\n"),
    "Hello World\nvalue=7 char=A\n1234567\n",
  );
});

test("unknown compiler stages are rejected", async () => {
  const response = await fetch(`${baseUrl}/api/compile`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ stage: "shell", source: "anything" }),
  });
  assert.equal(response.status, 400);
});
