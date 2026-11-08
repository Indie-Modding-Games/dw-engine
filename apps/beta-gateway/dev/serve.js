// Runs the gateway on this computer with node, for tests and trying the launcher against it
// (README.md, "Probar en local"). Not deployed: Cloudflare runs worker.js by itself.
//
//   BETA_KEY=... [BETA_KEY_PREVIOUS=...] GITHUB_TOKEN=... GITHUB_REPO=owner/repo \
//     [GITHUB_API=http://127.0.0.1:8765] node apps/beta-gateway/dev/serve.js [--port 8787]
//
// Prints "listening <url>" once it accepts requests.
import http from "node:http";
import worker from "../worker.js";

const portIndex = process.argv.indexOf("--port");
const port = portIndex > 0 ? Number(process.argv[portIndex + 1]) : 8787;
const env = {
  BETA_KEY: process.env.BETA_KEY,
  BETA_KEY_PREVIOUS: process.env.BETA_KEY_PREVIOUS,
  GITHUB_TOKEN: process.env.GITHUB_TOKEN,
  GITHUB_REPO: process.env.GITHUB_REPO,
  GITHUB_API: process.env.GITHUB_API,
};

const server = http.createServer(async (req, res) => {
  const headers = new Headers();
  for (const [name, value] of Object.entries(req.headers)) {
    if (typeof value === "string") headers.set(name, value);
  }
  const request = new Request(`http://127.0.0.1:${port}${req.url}`, { method: req.method, headers });
  const pending = [];
  const response = await worker.fetch(request, env, { waitUntil: (p) => pending.push(p) });
  res.writeHead(response.status, Object.fromEntries(response.headers));
  res.end(Buffer.from(await response.arrayBuffer()));
  await Promise.allSettled(pending);
  console.log(`beta-gateway: ${req.method} ${req.url.split("?")[0]} -> ${response.status}`);
});

server.listen(port, "127.0.0.1", () => console.log(`listening http://127.0.0.1:${port}`));
