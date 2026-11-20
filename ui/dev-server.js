import { createServer as createHttpServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createServer as createViteServer } from "vite";
import { createServer } from "./server.js";

const UI_DIR = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.PORT || 4173);

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error("PORT must be an integer between 1 and 65535");
}

const vite = await createViteServer({
  configFile: path.join(UI_DIR, "vite.config.js"),
  root: UI_DIR,
  server: { middlewareMode: true, hmr: false },
  appType: "spa",
});
const server = createServer({ viteServer: vite });

server.listen(port, "127.0.0.1", () => {
  process.stdout.write(`C Minor Compiler UI (development): http://127.0.0.1:${port}\n`);
});

async function shutdown() {
  server.close();
  await vite.close();
}

process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);
