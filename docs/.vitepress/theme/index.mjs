// The default theme, plus the Mermaid diagrams (the <pre class="mermaid"> blocks of config.mjs), drawn in the browser
// after every page change with the colours of the current light or dark mode.
import DefaultTheme from "vitepress/theme";
import { useData, useRoute } from "vitepress";
import { nextTick, onMounted, watch } from "vue";

const diagramSelector = "pre.mermaid:not([data-processed])";

async function drawDiagrams(isDark) {
  if (!document.querySelector(diagramSelector)) {
    return;
  }
  const { default: mermaid } = await import("mermaid");
  mermaid.initialize({ startOnLoad: false, theme: isDark ? "dark" : "default" });
  await mermaid.run({ querySelector: diagramSelector });
}

export default {
  extends: DefaultTheme,
  setup() {
    const route = useRoute();
    const { isDark } = useData();
    const redraw = () => nextTick(() => drawDiagrams(isDark.value));
    onMounted(redraw);
    watch(() => route.path, redraw);
  },
};
