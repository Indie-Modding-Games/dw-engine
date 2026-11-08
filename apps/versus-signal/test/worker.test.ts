// node --test: the worker's routing, with a fake room namespace. Node cannot build the 101
// responses of the Workers runtime, so the rooms answer with plain ones here.
import { test } from "node:test";
import assert from "node:assert/strict";
import worker, { type Env } from "../src/worker.ts";

function namespace(answer: (name: string, url: string) => Response) {
  const asked: string[] = [];
  const env: Env = {
    ROOMS: {
      idFromName: (name: string) => name,
      get: (id: unknown) => ({
        fetch: async (request: Request) => {
          asked.push(`${String(id)} ${request.url}`);
          return answer(String(id), request.url);
        },
      }),
    },
  };
  return { env, asked };
}

const upgrade = { headers: { Upgrade: "websocket" } };

test("only WebSocket requests to the protocol's paths reach a room", async () => {
  const { env, asked } = namespace(() => new Response("room"));
  assert.equal((await worker.fetch(new Request("https://signal.test/v1/host"), env)).status, 426);
  assert.equal((await worker.fetch(new Request("https://signal.test/other", upgrade), env)).status, 404);
  const joined = await worker.fetch(new Request("https://signal.test/v1/join/k7q2", upgrade), env);
  assert.equal(await joined.text(), "room");
  assert.deepEqual(asked, ["K7Q2 https://room/join"]);
});

test("a host gets another code while the one it drew is taken", async () => {
  let taken = 2;
  const { env, asked } = namespace(() => (taken-- > 0 ? new Response(null, { status: 409 }) : new Response("room")));
  const hosted = await worker.fetch(new Request("https://signal.test/v1/host", upgrade), env);
  assert.equal(await hosted.text(), "room");
  assert.equal(asked.length, 3);
  assert.match(asked[2], /^[2-9A-HJ-NP-Z]{4} https:\/\/room\/host\?code=[2-9A-HJ-NP-Z]{4}$/);
});
