import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createServer as createHttpServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const UI_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(UI_DIR, "..");
const DIST_DIR = path.join(UI_DIR, "dist");
const COMPILER = process.env.C_MINOR_COMPILER ||
  path.join(ROOT, "build", process.platform === "win32" ? "cminorcc.exe" : "cminorcc");
const MAX_BODY_BYTES = 128 * 1024;
const PROCESS_TIMEOUT_MS = 120_000;
const PROGRAM_TIMEOUT_MS = 5_000;

const stageArguments = {
  check: (sourcePath) => ["--check", sourcePath],
  tokens: (sourcePath) => ["--dump-tokens", sourcePath],
  ast: (sourcePath) => ["--dump-ast", sourcePath],
  symbols: (sourcePath) => ["--dump-symbols", sourcePath],
  ir: (sourcePath) => ["--dump-ir", sourcePath],
  llvm: (sourcePath) => ["--emit-backend-ir", sourcePath],
  assembly: (sourcePath, outputPath) => ["--backend=llvm", sourcePath, "-o", outputPath],
};

function sendJson(response, status, value) {
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
  });
  response.end(JSON.stringify(value));
}

function readJson(request) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    request.on("data", (chunk) => {
      size += chunk.length;
      if (size > MAX_BODY_BYTES) {
        reject(Object.assign(new Error("Request body exceeds 128 KiB"), { status: 413 }));
        request.destroy();
        return;
      }
      chunks.push(chunk);
    });
    request.on("end", () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      } catch {
        reject(Object.assign(new Error("Request body must be valid JSON"), { status: 400 }));
      }
    });
    request.on("error", reject);
  });
}

function compilerProcessEnvironment() {
  const paths = (process.env.PATH || "").split(path.delimiter).filter(Boolean);
  if (process.platform === "win32") {
    const runtimePaths = [
      ...(process.env.C_MINOR_RUNTIME_PATH || "").split(path.delimiter),
      ...(process.env.C_MINOR_LLVM_PATH || "").split(path.delimiter),
    ]
      .filter(Boolean);
    runtimePaths.push(
      "C:\\msys64\\ucrt64\\bin",
      "C:\\msys64\\mingw64\\bin",
      "C:\\msys64\\usr\\bin",
      "C:\\Program Files\\LLVM\\bin",
    );
    for (const runtimePath of runtimePaths.reverse()) {
      if (existsSync(runtimePath) &&
          !paths.some((entry) => entry.toLowerCase() === runtimePath.toLowerCase())) {
        paths.unshift(runtimePath);
      }
    }
  }
  return { ...process.env, PATH: paths.join(path.delimiter) };
}

function commandOnPath(command) {
  const extensions = process.platform === "win32"
    ? (process.env.PATHEXT || ".EXE;.CMD;.BAT").split(";")
    : [""];
  const directories = (compilerProcessEnvironment().PATH || "")
    .split(path.delimiter)
    .filter(Boolean);
  return directories.some((directory) =>
    extensions.some((extension) => existsSync(path.join(directory, command + extension))),
  );
}

function runProcess(command, args, timeoutMs = PROCESS_TIMEOUT_MS) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: ROOT,
      env: compilerProcessEnvironment(),
      windowsHide: true,
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    let settled = false;
    let timedOut = false;
    const timeout = setTimeout(() => {
      timedOut = true;
      child.kill();
      finish(null, { stdout, stderr, exitCode: null, signal: "timeout", timedOut });
    }, timeoutMs);

    function finish(error, result) {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      if (error) reject(error);
      else resolve(result);
    }

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => { stdout += chunk; });
    child.stderr.on("data", (chunk) => { stderr += chunk; });
    child.on("error", (error) => finish(error));
    child.on("close", (code, signal) => {
      finish(null, { stdout, stderr, exitCode: code, signal, timedOut });
    });
  });
}

async function serveStatic(request, response) {
  const requestPath = new URL(request.url, "http://127.0.0.1").pathname;
  const assetPath = requestPath === "/" ? "index.html" : requestPath.slice(1);
  const resolvedPath = path.resolve(DIST_DIR, assetPath);
  if (!resolvedPath.startsWith(DIST_DIR + path.sep) && resolvedPath !== path.join(DIST_DIR, "index.html")) {
    sendJson(response, 404, { error: "Not found" });
    return;
  }
  if (!existsSync(resolvedPath)) {
    sendJson(response, 503, { error: "UI build is missing. Run npm --prefix ui run build." });
    return;
  }
  const types = {
    ".css": "text/css; charset=utf-8",
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
  };
  const content = await readFile(resolvedPath);
  response.writeHead(200, {
    "Content-Type": types[path.extname(resolvedPath)] || "application/octet-stream",
    "Cache-Control": "no-cache",
    "Content-Security-Policy": "default-src 'self'; connect-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; font-src 'self' https://fonts.gstatic.com",
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "no-referrer",
  });
  response.end(content);
}

async function compileStage(body) {
  if (!body || typeof body.source !== "string" || typeof body.stage !== "string") {
    throw Object.assign(new Error("Expected JSON fields 'source' and 'stage'"), { status: 400 });
  }
  if (Buffer.byteLength(body.source, "utf8") > MAX_BODY_BYTES) {
    throw Object.assign(new Error("Source code exceeds 128 KiB"), { status: 413 });
  }
  const getArguments = stageArguments[body.stage];
  if (!getArguments) {
    throw Object.assign(new Error("Unknown compiler stage"), { status: 400 });
  }
  if (body.stage === "assembly" && !commandOnPath("llc")) {
    throw Object.assign(
      new Error(
        "Assembly requires LLVM llc, which was not found. Install LLVM, then restart this UI. " +
        "If LLVM is installed outside PATH, set C_MINOR_LLVM_PATH to the folder containing llc.exe.",
      ),
      { status: 501 },
    );
  }
  if (!existsSync(COMPILER)) {
    throw Object.assign(new Error("Compiler executable not found. Build it with make first."), { status: 503 });
  }

  const directory = await mkdtemp(path.join(tmpdir(), "cminor-ui-"));
  const sourcePath = path.join(directory, "main.cm");
  const outputPath = path.join(directory, "main.s");
  try {
    await writeFile(sourcePath, body.source, "utf8");
    const result = await runProcess(COMPILER, getArguments(sourcePath, outputPath));
    let stdout = result.stdout;
    if (body.stage === "assembly" && result.exitCode === 0) {
      stdout = await readFile(outputPath, "utf8");
    }
    return {
      success: result.exitCode === 0,
      exitCode: result.exitCode,
      stdout,
      stderr: result.stderr,
      outputName: body.stage === "assembly" && result.exitCode === 0 ? "main.s" : null,
    };
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

async function runProgram(body) {
  if (!body || typeof body.source !== "string") {
    throw Object.assign(new Error("Expected JSON field 'source'"), { status: 400 });
  }
  if (Buffer.byteLength(body.source, "utf8") > MAX_BODY_BYTES) {
    throw Object.assign(new Error("Source code exceeds 128 KiB"), { status: 413 });
  }
  const windowsNative = process.platform === "win32";
  if ((!windowsNative && process.platform !== "linux") || !["x64", "amd64"].includes(process.arch)) {
    throw Object.assign(
      new Error("Program execution requires Windows or Linux x86-64 with LLVM llc and GCC."),
      { status: 501 },
    );
  }
  if (!existsSync(COMPILER)) {
    throw Object.assign(new Error("Compiler executable not found. Build it with make first."), { status: 503 });
  }
  if (!commandOnPath("llc")) {
    throw Object.assign(new Error("Run program requires llc. Install LLVM and make llc available on PATH."), { status: 501 });
  }
  if (!commandOnPath("gcc")) {
    throw Object.assign(
      new Error(windowsNative
        ? "Run program on Windows requires MinGW-w64 GCC for linking."
        : "Run program requires GCC for assembling and linking."),
      { status: 501 },
    );
  }

  const directory = await mkdtemp(path.join(tmpdir(), "cminor-run-"));
  const sourcePath = path.join(directory, "main.cm");
  const irPath = path.join(directory, "main.ll");
  const assemblyPath = path.join(directory, "main.s");
  const objectPath = path.join(directory, "main.o");
  const executablePath = path.join(directory, windowsNative ? "main.exe" : "main");
  try {
    await writeFile(sourcePath, body.source, "utf8");
    const compile = await runProcess(
      COMPILER,
      windowsNative
        ? ["--emit-backend-ir", sourcePath, "-o", irPath]
        : ["--backend=llvm", sourcePath, "-o", assemblyPath],
    );
    if (compile.timedOut || compile.exitCode !== 0) {
      return {
        success: false,
        compiled: false,
        phase: "compiler",
        exitCode: compile.exitCode,
        stdout: compile.stdout,
        stderr: compile.stderr,
        error: compile.timedOut
          ? "Compiler timed out."
          : `Compilation failed (exit code ${compile.exitCode}).`,
      };
    }

    if (windowsNative) {
      const backend = await runProcess(
        "llc",
        [
          "-O0",
          "-mtriple=x86_64-w64-windows-gnu",
          "-filetype=obj",
          irPath,
          "-o",
          objectPath,
        ],
      );
      if (backend.timedOut || backend.exitCode !== 0) {
        return {
          success: false,
          compiled: false,
          phase: "backend",
          exitCode: backend.exitCode,
          stdout: backend.stdout,
          stderr: backend.stderr,
          error: backend.timedOut
            ? "LLVM backend timed out."
            : `Windows code generation failed (exit code ${backend.exitCode}).`,
        };
      }
    } else {
      const assemble = await runProcess("gcc", ["-c", assemblyPath, "-o", objectPath]);
      if (assemble.timedOut || assemble.exitCode !== 0) {
        return {
          success: false,
          compiled: false,
          phase: "assembler",
          exitCode: assemble.exitCode,
          stdout: assemble.stdout,
          stderr: assemble.stderr,
          error: assemble.timedOut
            ? "GCC assembler timed out."
            : `Assembly failed (exit code ${assemble.exitCode}).`,
        };
      }
    }

    const linkArguments = windowsNative
      ? [objectPath, "-o", executablePath]
      : ["-no-pie", objectPath, "-o", executablePath];
    const link = await runProcess("gcc", linkArguments);
    if (link.timedOut || link.exitCode !== 0) {
      return {
        success: false,
        compiled: false,
        phase: "linker",
        exitCode: link.exitCode,
        stdout: link.stdout,
        stderr: link.stderr,
        error: link.timedOut
          ? "GCC linker timed out."
          : `Linking failed (exit code ${link.exitCode}).`,
      };
    }

    const execution = await runProcess(executablePath, [], PROGRAM_TIMEOUT_MS);
    return {
      success: !execution.timedOut && execution.exitCode !== null && execution.signal === null,
      compiled: true,
      phase: "execution",
      exitCode: execution.exitCode,
      timedOut: execution.timedOut,
      stdout: execution.stdout,
      stderr: execution.stderr,
      error: execution.timedOut
        ? `Program exceeded the ${PROGRAM_TIMEOUT_MS / 1000}-second execution limit.`
        : undefined,
    };
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

async function runTestSuite() {
  if (!existsSync(COMPILER)) {
    throw Object.assign(new Error("Compiler executable not found. Build it with make first."), { status: 503 });
  }
  const python = process.platform === "win32" ? "python" : "python3";
  const result = await runProcess(python, ["tests/run_tests.py", COMPILER]);
  return { success: result.exitCode === 0, ...result };
}

async function status() {
  if (!existsSync(COMPILER)) {
    return {
      compilerAvailable: false,
      llcAvailable: false,
      executionAvailable: false,
    };
  }
  const result = await runProcess(COMPILER, ["--capabilities"], 10_000);
  const supportedHost = ["linux", "win32"].includes(process.platform) &&
    ["x64", "amd64"].includes(process.arch);
  const gccAvailable = commandOnPath("gcc");
  const llcAvailable = result.exitCode === 0 && !result.stdout.includes("llc not found or failed");
  const processError = result.exitCode === 0
    ? undefined
    : result.stderr || result.stdout ||
      `Compiler exited without diagnostics (exit code ${result.exitCode ?? "unknown"}). Check its runtime dependencies.`;
  return {
    compilerAvailable: result.exitCode === 0,
    llcAvailable,
    executionAvailable: supportedHost && result.exitCode === 0 && llcAvailable && gccAvailable,
    executionUnavailableReason: !supportedHost
      ? "Run program requires Windows or Linux x86-64."
      : !llcAvailable
        ? "Install LLVM (llc) to generate native code."
        : !gccAvailable
          ? process.platform === "win32"
            ? "Run program on Windows requires MinGW-w64 GCC for linking."
            : "Run program requires GCC for assembling and linking."
          : undefined,
    capabilities: result.stdout,
    error: processError,
  };
}

export function createServer(options = {}) {
  return createHttpServer(async (request, response) => {
    try {
      if (request.method === "GET" && request.url === "/api/status") {
        sendJson(response, 200, await status());
      } else if (request.method === "POST" && request.url === "/api/compile") {
        sendJson(response, 200, await compileStage(await readJson(request)));
      } else if (request.method === "POST" && request.url === "/api/test-suite") {
        sendJson(response, 200, await runTestSuite());
      } else if (request.method === "POST" && request.url === "/api/run") {
        sendJson(response, 200, await runProgram(await readJson(request)));
      } else if (request.method === "GET" && request.url && !request.url.startsWith("/api/")) {
        if (options.viteServer) {
          options.viteServer.middlewares(request, response, (error) => {
            if (error) {
              sendJson(response, 500, { error: error.message });
            } else if (!response.writableEnded) {
              sendJson(response, 404, { error: "Not found" });
            }
          });
        } else {
          await serveStatic(request, response);
        }
      } else {
        sendJson(response, 404, { error: "Not found" });
      }
    } catch (error) {
      if (!response.headersSent && !response.destroyed) {
        sendJson(response, error.status || 500, { error: error.message });
      }
    }
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const port = Number(process.env.PORT || 4173);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error("PORT must be an integer between 1 and 65535");
  }
  const server = createServer();
  server.listen(port, "127.0.0.1", () => {
    process.stdout.write(`C Minor Compiler UI: http://127.0.0.1:${port}\n`);
  });
}
