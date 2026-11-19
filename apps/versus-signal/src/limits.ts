// What the server takes from a public room's host, and its limits against abuse (README.md,
// "Limits"). The game's side checks the same (apps/game/src/online_signal_directory.cpp).

/** The most rooms the public list holds open at once, and the most it answers. */
export const MAX_OPEN_ROOMS = 50;
/** Open public rooms one address may have at a time. */
export const OPEN_ROOMS_PER_IP = 1;
/** A nickname's longest length, in letters. */
export const NAME_LENGTH = 16;

/**
 * A nickname: letters (with the Latin accents), digits, spaces and . _ ' -, at most NAME_LENGTH, no
 * spaces at the ends. Nothing free-form reaches other players.
 */
const NAME = /^[A-Za-z0-9À-ÖØ-öø-ɏ._' -]+$/u;

export function validName(name: string): boolean {
  return [...name].length <= NAME_LENGTH && name.trim() === name && name.length > 0 && NAME.test(name);
}

/** A profile icon's id (the game's fixed list): lower-case letters. */
export function validIcon(icon: string): boolean {
  return /^[a-z]{1,16}$/.test(icon);
}

/** A compatibility key (protocol and the first bytes of the content sum, as the game makes it). */
export function validCompat(compat: string): boolean {
  return /^[A-Za-z0-9._-]{1,64}$/.test(compat);
}

/** The modes a public room may be listed with. */
export const MODES = ["campaign", "versus"] as const;

export function validMode(mode: string): boolean {
  return (MODES as readonly string[]).includes(mode);
}

/** The Workers Rate Limiting binding (wrangler.toml: ratelimits); it counts per Cloudflare location. */
export interface RateLimit {
  limit(options: { key: string }): Promise<{ success: boolean }>;
}

/** Whether an address may go on; with no binding (local tests) it always may. */
export async function allowed(limiter: RateLimit | undefined, key: string): Promise<boolean> {
  if (!limiter) return true;
  return (await limiter.limit({ key })).success;
}
