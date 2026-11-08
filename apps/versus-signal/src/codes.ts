// Room codes and the paths of the signaling protocol (the game's side: packages/engine/net/signal.hpp).

/** Four characters without the ones that look alike (0/O, 1/I). */
export const ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
export const CODE_LENGTH = 4;
export const PROTOCOL = "v1";

/** A new random room code. */
export function newCode(random: (values: Uint8Array) => Uint8Array = (v) => crypto.getRandomValues(v)): string {
  const bytes = random(new Uint8Array(CODE_LENGTH));
  let code = "";
  for (const byte of bytes) {
    // 256 is a multiple of 32: every character is as likely.
    code += ALPHABET[byte % ALPHABET.length];
  }
  return code;
}

/** A typed code in its canonical form, or null when it cannot be one. */
export function normalizedCode(typed: string): string | null {
  const code = typed.replace(/[\s-]/g, "").toUpperCase();
  if (code.length !== CODE_LENGTH) return null;
  for (const letter of code) {
    if (!ALPHABET.includes(letter)) return null;
  }
  return code;
}

export type Route = { kind: "host" } | { kind: "join"; code: string } | { kind: "bad-code" } | { kind: "unknown" };

/** What a request's path asks for: /v1/host makes a room, /v1/join/<code> joins one. */
export function routeOf(pathname: string): Route {
  if (pathname === `/${PROTOCOL}/host`) return { kind: "host" };
  const join = new RegExp(`^/${PROTOCOL}/join/([^/]+)$`).exec(pathname);
  if (join) {
    const code = normalizedCode(decodeURIComponent(join[1]));
    return code ? { kind: "join", code } : { kind: "bad-code" };
  }
  return { kind: "unknown" };
}
