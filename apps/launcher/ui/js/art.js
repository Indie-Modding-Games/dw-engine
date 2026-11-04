// The launcher's own art: abstract emblems for the four games (shapes and gradients only, made for
// this project). Static strings, never built from outside data.

const EMBLEMS = {
  base: {
    hue: ["#5eead4", "#0ea5a4"],
    shape: `<path d="M50 12 L83 31 L83 69 L50 88 L17 69 L17 31 Z" fill="none" stroke="url(#g)" stroke-width="6" stroke-linejoin="round"/>
      <circle cx="50" cy="50" r="15" fill="url(#g)"/><circle cx="50" cy="50" r="6" fill="#081420"/>
      <circle cx="68" cy="27" r="4" fill="#a7f3d0"/>`,
  },
  cards: {
    hue: ["#fcd34d", "#f59e0b"],
    shape: `<rect x="22" y="20" width="36" height="52" rx="6" fill="none" stroke="url(#g)" stroke-width="5" transform="rotate(-10 40 46)"/>
      <rect x="42" y="28" width="36" height="52" rx="6" fill="url(#g)" opacity=".9" transform="rotate(8 60 54)"/>
      <path d="M60 44 L68 54 L60 64 L52 54 Z" fill="#081420"/>`,
  },
  second: {
    hue: ["#93c5fd", "#6366f1"],
    shape: `<circle cx="50" cy="50" r="32" fill="none" stroke="url(#g)" stroke-width="5" stroke-dasharray="150 52" stroke-linecap="round"/>
      <path d="M36 58 L50 40 L64 58" fill="none" stroke="url(#g)" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M36 70 L50 52 L64 70" fill="none" stroke="url(#g)" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" opacity=".6"/>`,
  },
  third: {
    hue: ["#fda4af", "#e11d48"],
    shape: `<path d="M18 74 L38 38 L58 74 Z" fill="url(#g)" opacity=".75"/>
      <path d="M42 74 L62 30 L82 74 Z" fill="url(#g)"/>
      <circle cx="30" cy="28" r="6" fill="url(#g)"/>`,
  },
};

let next = 0;

// An emblem as SVG markup; each copy gets its own gradient id.
export function emblem(game, size = 48) {
  const art = EMBLEMS[game] ?? EMBLEMS.base;
  const id = `g${next++}`;
  return `<svg class="emblem" viewBox="0 0 100 100" width="${size}" height="${size}" aria-hidden="true" focusable="false">
    <defs><linearGradient id="${id}" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="${art.hue[0]}"/><stop offset="1" stop-color="${art.hue[1]}"/></linearGradient></defs>
    ${art.shape.replaceAll("url(#g)", `url(#${id})`)}</svg>`;
}

export const GAMES = ["base", "cards", "second", "third"];
