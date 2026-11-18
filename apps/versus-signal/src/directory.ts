import type { RoomListing } from "./room.ts";

const MAX_AGE_MS = 5 * 60 * 1000;
const MAX_LISTINGS = 100;

interface DirectoryState {
  storage: {
    get(key: string): Promise<unknown>;
    put(key: string, value: unknown): Promise<void>;
  };
}

/** Serializes room registration and returns only public lobby metadata. */
export class DirectoryObject {
  #ctx: DirectoryState;

  constructor(ctx: DirectoryState) {
    this.#ctx = ctx;
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/rooms") {
      const now = Date.now();
      const stored = await this.#ctx.storage.get("rooms");
      const rooms = Array.isArray(stored) ? stored as RoomListing[] : [];
      const live = rooms.filter((room) => now - room.createdAt < MAX_AGE_MS);
      if (live.length !== rooms.length) await this.#ctx.storage.put("rooms", live);
      return Response.json({ rooms: live.slice(0, MAX_LISTINGS) }, {
        headers: { "Cache-Control": "no-store" },
      });
    }
    if (request.method !== "POST" || url.pathname !== "/register") {
      return new Response("Not found.\n", { status: 404 });
    }
    let listing: RoomListing | null;
    try {
      listing = await request.json() as RoomListing | null;
    } catch {
      return new Response("Invalid JSON.\n", { status: 400 });
    }
    const stored = await this.#ctx.storage.get("rooms");
    const rooms = (Array.isArray(stored) ? stored as RoomListing[] : [])
      .filter((room) => Date.now() - room.createdAt < MAX_AGE_MS);
    const code = url.searchParams.get("code");
    if (!code) return new Response("Missing room code.\n", { status: 400 });
    const updated = rooms.filter((room) => room.joinCode !== code);
    if (listing !== null) {
      if (typeof listing !== "object" || listing === null ||
          listing.joinCode !== code || listing.roomId !== code || listing.capacity !== 2 ||
          !Number.isInteger(listing.players) || listing.players < 1 || listing.players > 2 ||
          typeof listing.name !== "string" || typeof listing.message !== "string" ||
          listing.name.length > 48 || listing.message.length > 120) {
        return new Response("Invalid room listing.\n", { status: 400 });
      }
      updated.push(listing);
    }
    await this.#ctx.storage.put("rooms", updated.slice(-MAX_LISTINGS));
    return new Response(null, { status: 204 });
  }
}
