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

  return (
    <main className="app-shell">
      <header className="topbar">
        <a className="brand" href="#" aria-label="C Minor Compiler Lab home">
          <span className="brand-mark">Cₘ</span>
          <span>C MINOR <b>COMPILER LAB</b></span>
        </a>
        <div className="tool-status" aria-live="polite">
          <span className={`status-dot ${status.compilerAvailable ? "online" : "offline"}`} />
          {status.loading
            ? "Checking compiler"
            : status.error
              ? "Compiler service unavailable"
              : status.compilerAvailable
                ? `Compiler ready${status.llcAvailable ? " · llc ready" : " · llc missing"}${status.executionAvailable ? " · run-ready" : ""}`
                : "Build compiler first"}
        </div>
      </header>

      <section className="intro">
        <div>
          <p className="eyebrow">COMPILER CONSTRUCTION · C MINOR SRS 1.0</p>
          <h1>Try the compiler, <span>stage by stage.</span></h1>
          <p className="intro-copy">
            Write C Minor, run a program when the native backend is available, or inspect each compiler stage.
          </p>
        </div>
        <div className="intro-actions">
          <button className="suite-button run-program-button" onClick={runProgram} disabled={busy || !status.compilerAvailable}>
            <span aria-hidden="true">▶</span> Run program
          </button>
          <button className="suite-button" onClick={runSuite} disabled={busy || !status.compilerAvailable}>
            <span aria-hidden="true">✓</span> Run test suite
          </button>
        </div>
      </section>
      {status.error && (
        <p className="connection-warning" role="alert">{status.error}</p>
      )}

      <section className="workspace">
        <div className="editor-panel">
          <div className="panel-heading">
            <div>
              <span className="file-icon">CM</span>
              <strong>main.cm</strong>
              <span className="muted">C Minor source</span>
            </div>
            <span className="line-count">{source.split("\n").length} lines</span>
          </div>
          <div className="editor">
            <pre className="line-numbers" aria-hidden="true">
              {source.split("\n").map((_, index) => `${index + 1}\n`)}
            </pre>
            <textarea
              aria-label="C Minor source code"
              autoCapitalize="off"
              autoComplete="off"
              autoCorrect="off"
              spellCheck="false"
              value={source}
              onChange={(event) => setSource(event.target.value)}
            />
          </div>
          <footer className="editor-footer">
            <span>UTF-8 <i /> C MINOR</span>
            <span>{source.length} characters</span>
          </footer>
        </div>

        <aside className="stage-panel">
          <div className="panel-heading stage-heading">
            <div>
              <p className="eyebrow">INSPECTION</p>
              <h2>Compiler stage</h2>
            </div>
            <span className="stage-count">07</span>
          </div>
          <div className="stage-list" role="group" aria-label="Compiler stage">
            {stages.map((item, index) => (
              <button
                className={`stage-option ${stage === item.id ? "selected" : ""}`}
                key={item.id}
                onClick={() => setStage(item.id)}
                aria-pressed={stage === item.id}
              >
                <span className="stage-index">{String(index + 1).padStart(2, "0")}</span>
                <span className="stage-text">
                  <strong>{item.label}</strong>
                  <small>{item.description}</small>
                </span>
                <span className="stage-arrow" aria-hidden="true">↗</span>
              </button>
            ))}
          </div>
          <button
            className="run-button"
            onClick={() => runStage()}
            disabled={busy || !status.compilerAvailable}
          >
            {busy ? <span className="spinner" /> : <span aria-hidden="true">▶</span>}
            {busy ? "Running…" : `Run ${selectedStage.label}`}
          </button>
          <p className="stage-note">
            Stage 7 needs LLVM llc. {status.llcAvailable
              ? "Assembly generation is available. Run program uses your host platform and GCC."
              : "Install LLVM and restart this UI. Stages 1–6 work without it."}
          </p>
        </aside>
      </section>
      <p className="execution-note">
        C Minor supports string and character literals plus the built-in variadic <code>printf</code>.
        Run program reports its actual output streams and <code>main</code> return status.
        Run program requires LLVM llc and GCC; Windows uses a Windows-native executable.
      </p>

      <section className="output-panel" aria-live="polite">
        <div className="output-heading">
          <div>
            <p className="eyebrow">RESULT</p>
            <h2>{result ? result.label : "Compiler output"}</h2>
          </div>
          {result && (
            <span className={`result-badge ${result.success ? "passed" : "failed"}`}>
              <span>{result.programRun ? (result.compiled ? result.exitCode : "!") : result.success ? "✓" : "!"}</span>
              {result.programRun
                ? (result.compiled
                  ? (result.timedOut ? "TIMEOUT" : "PROGRAM EXIT")
                  : "NOT RUN")
                : result.success ? "PASS" : `FAILED${Number.isInteger(result.exitCode) ? ` · ${result.exitCode}` : ""}`}
            </span>
          )}
        </div>
        <pre className={`output-content ${output ? "" : "empty"}`}>
          {output || "Choose a compiler stage and run it to see the actual output here."}
        </pre>
      </section>

      <footer className="page-footer">
        <span>STUDENT-IMPLEMENTED FRONTEND · FLEX + RECURSIVE-DESCENT PARSER</span>
        <span>LLVM native execution requires llc and GCC</span>
      </footer>
    </main>
  );
}

if (window.location.protocol !== "file:") {
  createRoot(document.getElementById("root")).render(<App />);
}
