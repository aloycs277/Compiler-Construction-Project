import React, { useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import "./style.css";

const initialSource = `int add(int left, int right) {
    return left + right;
}

int main(void) {
    int total = add(10, 2);
    if (total != 12) return 1;
    return 0;
}`;

const stages = [
  { id: "check", label: "Check", description: "Lex + parse + semantic analysis" },
  { id: "tokens", label: "Tokens", description: "Flex scanner output" },
  { id: "ast", label: "AST", description: "Recursive-descent parser output" },
  { id: "symbols", label: "Symbols", description: "Semantic symbols and scopes" },
  { id: "ir", label: "Student IR", description: "Compiler-owned three-address IR" },
  { id: "llvm", label: "LLVM IR", description: "LLVM backend input" },
  { id: "assembly", label: "Assembly", description: "Linux x86-64 assembly via LLVM llc" },
];

const serviceUnavailableMessage =
  "Cannot reach the local compiler service. Start it with `npm --prefix ui run dev` and open http://127.0.0.1:4173.";

function requestErrorMessage(error) {
  return error instanceof TypeError || error.message === "Failed to fetch"
    ? serviceUnavailableMessage
    : error.message;
}

function parseTokenRows(output) {
  const rows = output.split(/\r?\n/).filter(Boolean).map((line) => {
    const match = line.match(/^(\d+):(\d+)\s+(.+?)(?:\s+"(.*)")?$/);
    if (!match) return null;
    return { line: match[1], column: match[2], kind: match[3], lexeme: match[4] };
  });
  return rows.length > 0 && rows.every(Boolean) ? rows : null;
}

function parseAstTree(output) {
  const lines = output.split(/\r?\n/).filter(Boolean);
  if (!lines.length || lines[0].trim() !== "Program") return null;

  const root = { label: "Program", children: [] };
  const stack = [{ depth: 0, node: root }];
  for (const line of lines.slice(1)) {
    const indentation = line.match(/^ */)[0].length;
    if (indentation % 2 !== 0) return null;
    const depth = indentation / 2;
    const node = { label: line.trim(), children: [] };
    while (stack.length > 1 && stack[stack.length - 1].depth >= depth) stack.pop();
    const parent = stack[stack.length - 1];
    if (!parent || parent.depth !== depth - 1) return null;
    parent.node.children.push(node);
    stack.push({ depth, node });
  }
  return root;
}

function TokenTable({ rows }) {
  return (
    <div className="max-h-[360px] overflow-auto">
      <table className="w-full border-collapse text-left font-mono text-[11px]">
        <thead className="sticky top-0 bg-surface-raised text-[9px] uppercase tracking-[.12em] text-muted">
          <tr>
            <th className="px-4 py-3 font-medium">Location</th>
            <th className="px-4 py-3 font-medium">Token</th>
            <th className="px-4 py-3 font-medium">Lexeme</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((token, index) => (
            <tr className="border-t border-stroke/60 even:bg-white/[.015]" key={`${token.line}:${token.column}:${index}`}>
              <td className="whitespace-nowrap px-4 py-2 text-muted">{token.line}:{token.column}</td>
              <td className="whitespace-nowrap px-4 py-2 text-mint">{token.kind}</td>
              <td className="break-all px-4 py-2 text-[#d0e0e5]">{token.lexeme ?? "—"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function AstNode({ node, root = false }) {
  const nodeStyle = root
    ? "border-mint/35 bg-mint/[.08] text-mint"
    : node.label.startsWith("Function ")
      ? "border-sky-300/25 bg-sky-300/[.06] text-sky-100"
      : node.label.startsWith("Expr ")
        ? "border-violet-300/25 bg-violet-300/[.06] text-violet-100"
        : "border-stroke bg-surface-raised text-[#d0e0e5]";
  const markerStyle = root
    ? "bg-mint shadow-[0_0_8px_rgb(114_224_186_/_55%)]"
    : node.label.startsWith("Function ")
      ? "bg-sky-300"
      : node.label.startsWith("Expr ")
        ? "bg-violet-300"
        : "bg-[#8da2ad]";

  return (
    <li
      className={`relative py-[2px] ${root
        ? ""
        : "before:absolute before:-left-5 before:top-1/2 before:h-px before:w-5 before:bg-stroke before:content-['']"}`}
      role="treeitem"
      aria-expanded={node.children.length ? true : undefined}
    >
      <div className={`inline-flex min-h-[34px] items-center gap-2.5 rounded-lg border px-3 py-2 font-mono text-[11px] leading-[1.4] ${nodeStyle}`}>
        <span className={`size-2.5 shrink-0 rounded-full ring-2 ring-surface ${markerStyle}`} aria-hidden="true" />
        {node.label}
      </div>
      {node.children.length > 0 && (
        <ul className="relative ml-4 space-y-1 border-l border-stroke pl-5" role="group">
          {node.children.map((child, index) => (
            <AstNode key={`${child.label}:${index}`} node={child} />
          ))}
        </ul>
      )}
    </li>
  );
}

function AstTree({ tree }) {
  return (
    <div className="max-h-[360px] overflow-auto p-3">
      <ul className="min-w-max py-2" role="tree" aria-label="Abstract syntax tree">
        <AstNode node={tree} root />
      </ul>
    </div>
  );
}

function App() {
  const [source, setSource] = useState(initialSource);
  const [stage, setStage] = useState("check");
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState(null);
  const [status, setStatus] = useState({ loading: true });

  useEffect(() => {
    fetch("/api/status")
      .then(async (response) => {
        const body = await response.json();
        if (!response.ok) throw new Error(body.error || "Status request failed");
        setStatus(body);
      })
      .catch((error) => setStatus({ error: requestErrorMessage(error) }));
  }, []);

  async function runStage(selectedStage = stage) {
    setBusy(true);
    setResult(null);
    try {
      const response = await fetch("/api/compile", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source, stage: selectedStage }),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "Compiler request failed");
      setResult({ ...body, label: stages.find((item) => item.id === selectedStage).label });
    } catch (error) {
      setResult({
        success: false,
        error: requestErrorMessage(error),
        label: stages.find((item) => item.id === selectedStage).label,
      });
    } finally {
      setBusy(false);
    }
  }

  async function runSuite() {
    setBusy(true);
    setResult(null);
    try {
      const response = await fetch("/api/test-suite", { method: "POST" });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "Test suite request failed");
      setResult({ ...body, label: "Compiler test suite" });
    } catch (error) {
      setResult({ success: false, error: requestErrorMessage(error), label: "Request error" });
    } finally {
      setBusy(false);
    }
  }

  async function runProgram() {
    setBusy(true);
    setResult(null);
    try {
      const response = await fetch("/api/run", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source }),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "Program run request failed");
      setResult({ ...body, programRun: true, label: "Program run" });
    } catch (error) {
      setResult({
        success: false,
        error: requestErrorMessage(error),
        programRun: true,
        label: "Program run",
      });
    } finally {
      setBusy(false);
    }
  }

  const selectedStage = stages.find((item) => item.id === stage);
  let output = "";
  if (result?.programRun) {
    const parts = [];
    if (result.stdout) parts.push(`stdout:\n${result.stdout.trimEnd()}`);
    if (result.stderr) parts.push(`stderr:\n${result.stderr.trimEnd()}`);
    if (result.compiled === false && result.phase) {
      parts.push(`Build stopped during ${result.phase} (exit code ${result.exitCode}).`);
    }
    if (result.compiled && !result.timedOut) {
      parts.push(`Program exited with status ${result.exitCode}.`);
      if (!result.stdout && !result.stderr) {
        parts.push("The program produced no stdout or stderr.");
      }
    }
    if (result.error) parts.push(result.error);
    output = parts.join("\n\n") || "The program did not run.";
  } else if (result) {
    output = [result.stdout, result.stderr, result.error].filter(Boolean).join("\n").trim() ||
      (result.success ? "No diagnostics. The compiler stage completed successfully." : "The compiler stage failed.");
  }
  const tokenRows = result?.label === "Tokens" && result.success
    ? parseTokenRows(result.stdout || "")
    : null;
  const astTree = result?.label === "AST" && result.success
    ? parseAstTree(result.stdout || "")
    : null;

  return (
    <main className="mx-auto w-[calc(100%-64px)] max-w-[1320px] max-[850px]:w-[calc(100%-32px)] max-[850px]:max-w-[680px] max-[560px]:w-[calc(100%-24px)]">
      <header className="flex h-[74px] items-center justify-between border-b border-muted/[.16] max-[560px]:h-[62px]">
        <a className="flex items-center gap-3 text-[11px] font-bold tracking-[.14em] text-[#e9f0f3] no-underline max-[560px]:gap-2 max-[560px]:text-[9px]" href="#" aria-label="C Minor Compiler Lab home">
          <span className="grid size-[34px] place-items-center rounded-[9px] border border-[#45635e] font-sans text-base font-semibold tracking-normal text-mint">Cₘ</span>
          <span>C MINOR <b className="font-medium text-muted">COMPILER LAB</b></span>
        </a>
        <div className="flex items-center gap-[9px] font-mono text-[11px] text-[#b7c5cc] max-[560px]:text-[9px]" aria-live="polite">
          <span className={`size-[7px] rounded-full ${status.compilerAvailable
            ? "bg-mint shadow-[0_0_12px_rgb(114_224_186_/_50%)]"
            : "bg-error"}`} />
          {status.loading
            ? "Checking compiler"
            : status.error
              ? "Compiler service unavailable"
              : status.compilerAvailable
                ? `Compiler ready${status.llcAvailable ? " · llc ready" : " · llc missing"}${status.executionAvailable ? " · run-ready" : ""}`
                : "Build compiler first"}
        </div>
      </header>

      <section className="flex items-end justify-between gap-6 py-12 pb-[30px] max-[560px]:flex-col max-[560px]:items-start max-[560px]:py-[34px] max-[560px]:pb-[22px]">
        <div>
          <p className="mb-[9px] font-mono text-[10px] font-medium tracking-[.12em] text-mint">COMPILER CONSTRUCTION · C MINOR SRS 1.0</p>
          <h1 className="mb-3 text-[clamp(28px,4vw,42px)] font-semibold leading-[1.15] tracking-[-.045em]">Try the compiler, <span className="text-mint">stage by stage.</span></h1>
          <p className="mb-0 max-w-[620px] text-sm leading-[1.65] text-muted max-[560px]:text-xs">
            Write C Minor, run a program when the native backend is available, or inspect each compiler stage.
          </p>
        </div>
        <div className="flex gap-[9px] max-[560px]:flex-wrap">
          <button className="flex cursor-pointer items-center gap-2.5 whitespace-nowrap rounded-lg border border-mint/45 px-[15px] py-[11px] text-xs font-semibold text-mint transition hover:border-[#5da88e] hover:bg-mint/[.06] disabled:cursor-not-allowed disabled:opacity-[.48]" onClick={runProgram} disabled={busy || !status.compilerAvailable}>
            <span className="text-[10px]" aria-hidden="true">▶</span> Run program
          </button>
          <button className="flex cursor-pointer items-center gap-2.5 whitespace-nowrap rounded-lg border border-stroke px-[15px] py-[11px] text-xs font-semibold text-[#ced9de] transition hover:border-[#5da88e] hover:bg-mint/[.06] disabled:cursor-not-allowed disabled:opacity-[.48]" onClick={runSuite} disabled={busy || !status.compilerAvailable}>
            <span className="text-[10px] text-mint" aria-hidden="true">✓</span> Run test suite
          </button>
        </div>
      </section>
      {status.error && (
        <p className="mb-5 -mt-3 rounded-[7px] border border-error/35 bg-error/[.08] px-[13px] py-[11px] font-mono text-[11px] leading-relaxed text-[#ffaaa4]" role="alert">{status.error}</p>
      )}

      <section className="grid grid-cols-[minmax(0,1fr)_330px] items-stretch gap-[18px] max-[850px]:grid-cols-1">
        <div className="overflow-hidden rounded-[10px] border border-stroke bg-surface/90">
          <div className="flex h-[54px] items-center justify-between border-b border-stroke px-[17px]">
            <div className="flex items-center gap-[10px]">
              <span className="inline-grid size-[23px] place-items-center rounded-[5px] border border-[#45635e] font-mono text-[8px] text-mint">CM</span>
              <strong>main.cm</strong>
              <span className="text-[11px] text-muted">C Minor source</span>
            </div>
            <span className="text-[11px] text-muted">{source.split("\n").length} lines</span>
          </div>
          <div className="flex max-h-[480px] min-h-[358px] overflow-auto bg-[#121c23] py-[17px]">
            <pre className="m-0 min-w-12 select-none whitespace-pre pr-[13px] text-right font-mono text-xs leading-[1.7] text-[#536873]" aria-hidden="true">
              {source.split("\n").map((_, index) => `${index + 1}\n`)}
            </pre>
            <textarea
              className="block min-h-[325px] w-full min-w-[420px] resize-y border-0 bg-transparent pr-[17px] font-mono text-xs leading-[1.7] whitespace-pre text-[#d7e5e9] outline-none [tab-size:4]"
              aria-label="C Minor source code"
              autoCapitalize="off"
              autoComplete="off"
              autoCorrect="off"
              spellCheck="false"
              value={source}
              onChange={(event) => setSource(event.target.value)}
            />
          </div>
          <footer className="flex h-[35px] items-center justify-between border-t border-stroke px-[14px] font-mono text-[9px] tracking-[.04em] text-muted">
            <span>UTF-8 <i className="mx-[6px] mb-0.5 inline-block size-1 rounded-full bg-mint" /> C MINOR</span>
            <span>{source.length} characters</span>
          </footer>
        </div>

        <aside className="flex flex-col overflow-hidden rounded-[10px] border border-stroke bg-surface/90 max-[850px]:min-h-0">
          <div className="flex h-[62px] items-center justify-between border-b border-stroke px-[17px]">
            <div>
              <p className="mb-[3px] font-mono text-[10px] font-medium tracking-[.12em] text-mint">INSPECTION</p>
              <h2 className="m-0 text-sm font-semibold">Compiler stage</h2>
            </div>
            <span className="font-mono text-[11px] text-[#60737e]">07</span>
          </div>
          <div className="grid gap-1 p-[9px] max-[850px]:grid-cols-2 max-[560px]:grid-cols-1" role="group" aria-label="Compiler stage">
            {stages.map((item, index) => (
              <button
                className={`flex min-h-[43px] cursor-pointer items-center gap-[11px] rounded-[7px] border px-[9px] py-1.5 text-left transition hover:bg-white/[.025] disabled:cursor-not-allowed disabled:opacity-[.48] ${stage === item.id
                  ? "border-mint/25 bg-mint/[.08]"
                  : "border-transparent bg-transparent"}`}
                key={item.id}
                onClick={() => setStage(item.id)}
                aria-pressed={stage === item.id}
              >
                <span className={`w-5 font-mono text-[9px] ${stage === item.id ? "text-mint" : "text-[#647884]"}`}>{String(index + 1).padStart(2, "0")}</span>
                <span className="grid flex-1 gap-[3px]">
                  <strong className="text-[11px] font-semibold">{item.label}</strong>
                  <small className="text-[9px] text-[#8497a2]">{item.description}</small>
                </span>
                <span className={`text-xs ${stage === item.id ? "text-mint" : "text-[#596e78]"}`} aria-hidden="true">↗</span>
              </button>
            ))}
          </div>
          <button
            className="mx-[18px] mt-auto flex min-h-[41px] cursor-pointer items-center justify-center gap-[9px] rounded-[7px] border-0 bg-mint text-xs font-bold text-[#10251e] transition hover:bg-[#8af0cb] disabled:cursor-not-allowed disabled:opacity-[.48] max-[850px]:mt-2"
            onClick={() => runStage()}
            disabled={busy || !status.compilerAvailable}
          >
            {busy ? <span className="size-3 animate-spin rounded-full border-2 border-[#10251e]/30 border-t-[#10251e]" /> : <span className="text-[9px]" aria-hidden="true">▶</span>}
            {busy ? "Running…" : `Run ${selectedStage.label}`}
          </button>
          <p className="mx-[18px] mb-[15px] mt-[10px] text-center text-[9px] leading-[1.5] text-[#8497a2]">
            Stage 7 needs LLVM llc. {status.llcAvailable
              ? "Assembly generation is available. Run program uses your host platform and GCC."
              : "Install LLVM and restart this UI. Stages 1–6 work without it."}
          </p>
        </aside>
      </section>
      <p className="mx-0.5 mb-0 mt-3 text-[11px] leading-relaxed text-muted">
        C Minor supports string and character literals plus the built-in variadic{" "}
        <code className="font-mono text-mint">printf</code>.
        Run program reports its actual output streams and <code className="font-mono text-mint">main</code> return status.
        Run program requires LLVM llc and GCC; Windows uses a Windows-native executable.
      </p>

      <section className="mt-[18px] overflow-hidden rounded-[10px] border border-stroke bg-surface/90" aria-live="polite">
        <div className="flex h-[62px] items-center justify-between border-b border-stroke px-[17px]">
          <div>
            <p className="mb-[3px] font-mono text-[10px] font-medium tracking-[.12em] text-mint">RESULT</p>
            <h2 className="m-0 text-sm font-semibold">{result ? result.label : "Compiler output"}</h2>
          </div>
          {result && (
            <span className={`flex items-center gap-[7px] rounded-full px-[9px] py-1.5 font-mono text-[10px] ${result.success
              ? "bg-mint/10 text-mint"
              : "bg-error/10 text-[#ff9a94]"}`}>
              <span className="text-[11px]">{result.programRun ? (result.compiled ? result.exitCode : "!") : result.success ? "✓" : "!"}</span>
              {result.programRun
                ? (result.compiled
                  ? (result.timedOut ? "TIMEOUT" : "PROGRAM EXIT")
                  : "NOT RUN")
                : result.success ? "PASS" : `FAILED${Number.isInteger(result.exitCode) ? ` · ${result.exitCode}` : ""}`}
            </span>
          )}
        </div>
        {tokenRows
          ? <TokenTable rows={tokenRows} />
          : astTree
            ? <AstTree tree={astTree} />
            : <pre className={`m-0 max-h-[360px] min-h-[116px] overflow-auto whitespace-pre-wrap [overflow-wrap:anywhere] p-[17px] font-mono text-[11px] leading-[1.7] ${output
              ? "text-[#d0e0e5]"
              : "text-[#71858f]"}`}>
              {output || "Choose a compiler stage and run it to see the actual output here."}
            </pre>}
        {(tokenRows || astTree) && (result.stderr || result.error) && (
          <pre className="m-0 overflow-auto border-t border-stroke px-[17px] py-3 font-mono text-[11px] leading-[1.7] text-[#ffaaa4]">
            {[result.stderr, result.error].filter(Boolean).join("\n")}
          </pre>
        )}
      </section>

      <footer className="flex justify-between gap-3 px-0.5 pb-7 pt-[18px] font-mono text-[9px] tracking-[.035em] text-[#70838d] max-[560px]:flex-col">
        <span>STUDENT-IMPLEMENTED FRONTEND · FLEX + RECURSIVE-DESCENT PARSER</span>
        <span>LLVM native execution requires llc and GCC</span>
      </footer>
    </main>
  );
}

if (window.location.protocol !== "file:") {
  createRoot(document.getElementById("root")).render(<App />);
}
