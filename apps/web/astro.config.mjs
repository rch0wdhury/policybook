// @ts-check
import { fileURLToPath } from "node:url";
import mdx from "@astrojs/mdx";
import preact from "@astrojs/preact";
import { defineConfig } from "astro/config";

const repoRoot = fileURLToPath(new URL("../..", import.meta.url));

/**
 * The site.
 *
 * Static output, because there is nothing here a server needs to decide: every
 * page is a policy, a domain or a comparison, and all of them are known at
 * build time. The only JavaScript that reaches a browser is the runner islands,
 * and those are opt-in per page (concept.md §13.5).
 */
export default defineConfig({
  // GitHub project pages serve from a subpath, and every asset URL has to know
  // it at build time. The Pages workflow sets this; locally it is the root.
  base: process.env.PUBLIC_BASE_PATH ?? "/",
  output: "static",
  trailingSlash: "always",

  integrations: [preact(), mdx()],

  markdown: {
    shikiConfig: {
      // The css-variables theme emits `var(--shiki-*)` rather than baked
      // colours, so highlighting follows the site's own tokens into dark mode
      // — and costs zero runtime JavaScript, because Shiki runs at build time.
      theme: "css-variables",
      wrap: false,
    },
  },

  vite: {
    worker: {
      // The simulation worker loads policies with a dynamic import, so its
      // bundle is code-split — and Vite's default IIFE worker format cannot
      // code-split. ES is what the worker is constructed as anyway
      // (`new Worker(url, { type: "module" })`), so this makes the build agree
      // with the code rather than the other way round.
      format: "es",
    },

    server: {
      fs: {
        // The catalog is read from `policies/**` at the repository root, which
        // is outside the site's own directory. Nothing is copied in: the page
        // and the tests read the same file (concept.md §13.6).
        allow: [repoRoot],
      },
    },
  },
});
