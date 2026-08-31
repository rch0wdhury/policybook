/**
 * The site, driven in a real browser, end to end.
 *
 *   home → a domain → a policy → run 100 steps → compare → tutorial
 *
 * Everything else that checks the site checks it *statically*: the unit tests
 * run the island logic without a DOM, and `check-dist.mjs` reads the built
 * HTML. Neither can tell you whether the worker starts, whether the canvas
 * paints, or whether pressing a button does anything — and those are precisely
 * the parts a reader meets first.
 *
 * Best-effort by design: browser dependencies
 * do not always install without root. It exits 0 with a clear note when
 * Playwright or its browser is unavailable, so CI does not fail on a machine
 * that simply cannot run it, and fails properly when the site is broken.
 *
 *   node apps/web/scripts/smoke.mjs
 *
 * Serves `dist/` itself rather than shelling out to a preview server: one
 * process, no port negotiation with a tool that prints its port on stdout, and
 * nothing left running if an assertion throws.
 */

import { createReadStream, existsSync, statSync } from "node:fs";
import { createServer } from "node:http";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = join(HERE, "..", "dist");

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".svg": "image/svg+xml",
  ".woff2": "font/woff2",
};

/** A static server over `dist`, resolving directories to `index.html`. */
function serve() {
  const server = createServer((request, response) => {
    const url = new URL(request.url ?? "/", "http://localhost");
    // `normalize` plus the prefix check keeps `..` from escaping dist.
    let path = normalize(join(DIST, decodeURIComponent(url.pathname)));
    if (!path.startsWith(DIST)) {
      response.writeHead(403).end();
      return;
    }

    if (existsSync(path) && statSync(path).isDirectory()) path = join(path, "index.html");
    if (!existsSync(path)) {
      response.writeHead(404).end("not found");
      return;
    }

    response.writeHead(200, { "content-type": TYPES[extname(path)] ?? "application/octet-stream" });
    createReadStream(path).pipe(response);
  });

  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      resolve({ server, origin: `http://127.0.0.1:${address.port}` });
    });
  });
}

function ok(message) {
  console.log(`  ok   ${message}`);
}

async function main() {
  if (!existsSync(DIST)) {
    console.error("smoke: no dist. Build the site first.");
    process.exit(1);
  }

  let chromium;
  try {
    ({ chromium } = await import("playwright"));
  } catch {
    console.log("smoke: playwright is not installed — skipping (deviation 13).");
    return;
  }

  let browser;
  try {
    browser = await chromium.launch();
  } catch (error) {
    // Missing system libraries, which need root to install.
    console.log(`smoke: chromium will not launch here — skipping (deviation 13).`);
    console.log(`       ${error instanceof Error ? error.message.split("\n")[0] : error}`);
    return;
  }

  const { server, origin } = await serve();
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });

  // A page that throws during hydration still renders its server-side HTML, so
  // every static check would pass while the site did nothing. Collect errors
  // and fail on them at the end.
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));

  try {
    console.log("smoke: home → domain → policy → run → compare → tutorial");

    await page.goto(`${origin}/`, { waitUntil: "load" });
    await page.waitForSelector("h1");
    ok(`home: ${await page.textContent("h1")}`);

    await page.click('a[href$="/d/cache/"]');
    await page.waitForSelector("h1");
    ok("domain page reached");

    await page.click('a[href$="/p/cache/sieve/"]');
    await page.waitForSelector("h1");
    ok("policy page reached");

    // The runner is the point. It is mounted `client:visible`, so it does not
    // hydrate until it is scrolled into view — the server-rendered markup is
    // there from the start, with a disabled button and "Step 0 of 0", which is
    // indistinguishable from a broken runner if you never scroll. That is worth
    // knowing: a reader who lands on a policy page sees exactly this until the
    // sidebar reaches the viewport.
    const play = page.locator(".runner-controls button.primary");
    await play.scrollIntoViewIfNeeded();
    await play.waitFor({ state: "visible" });
    await page.waitForFunction(
      () => {
        const button = document.querySelector(".runner-controls button.primary");
        return button instanceof HTMLButtonElement && !button.disabled;
      },
      { timeout: 30_000 },
    );
    ok("worker loaded the policy");

    // A hundred steps: shift+ArrowRight moves a hundred at a time.
    await page.locator(".scrubber").focus();
    await page.locator("body").click({ position: { x: 5, y: 5 } });
    await page.keyboard.press("Shift+ArrowRight");

    await page.waitForFunction(
      () => {
        const text = document.querySelector(".runner-position")?.textContent ?? "";
        const match = /Step ([\d,]+)/.exec(text);
        return match !== null && Number(match[1].replace(/,/g, "")) >= 100;
      },
      { timeout: 30_000 },
    );
    ok(`stepped: ${(await page.textContent(".runner-position"))?.trim()}`);

    // The metric must be a real number, not a dash: that is the difference
    // between a runner that ran and one that merely rendered.
    const metric = await page.textContent(".runner-metrics .tabular");
    if (metric === null || metric.trim() === "—") {
      throw new Error("the runner produced no metric after stepping");
    }
    ok(`metric shown: ${metric.trim()}`);

    // The canvas must have painted something, not just been sized.
    const painted = await page.evaluate(() => {
      const canvas = document.querySelector(".viz canvas");
      if (!(canvas instanceof HTMLCanvasElement)) return false;
      const context = canvas.getContext("2d");
      if (context === null) return false;
      const { data } = context.getImageData(0, 0, canvas.width, canvas.height);
      for (let i = 3; i < data.length; i += 4) if (data[i] !== 0) return true;
      return false;
    });
    if (!painted) throw new Error("the visualisation canvas is blank");
    ok("visualisation painted");

    /*
     * The policy page links to a *preset*: this policy plus its two neighbours
     * in the ranking, encoded in the URL fragment.
     *
     * The count has to be read after hydration, not after the picker appears.
     * A fragment is never sent to a server, so the pre-rendered HTML
     * necessarily shows the page's own default pair and the linked trio only
     * appears once the island runs. Asserting too early gave 3 on one run and
     * 2 on the next. Waiting for the runner to be ready is the honest moment:
     * it is when the page is showing what the link asked for.
     */
    await page.click('a[href*="/compare/cache/"]');
    await page.waitForSelector(".runner-picker");
    await page.waitForFunction(
      () => {
        const button = document.querySelector(".runner-controls button.primary");
        return button instanceof HTMLButtonElement && !button.disabled;
      },
      { timeout: 30_000 },
    );

    const chosen = await page.locator(".runner-picker input:checked").count();
    if (chosen !== 3) {
      throw new Error(`compare page opened with ${chosen} policies, expected the linked 3`);
    }
    ok(`compare page opened on the linked ${chosen} policies`);

    /*
     * The tutorial is the one page with TWO runners, and both of its sharp
     * edges live only there: step four runs a policy registered under its
     * full id (`tutorial/evict-newest`), which the worker once broke by
     * prefixing the page's domain onto it — so this leg is the guard that the
     * comparison actually *runs*, not merely renders — and two runners on one
     * page once fought over the shared fragment and the window's keyboard.
     */
    await page.goto(`${origin}/tutorial/`, { waitUntil: "load" });
    await page.waitForSelector("h1");

    // Both runners are client:visible; each hydrates when scrolled to.
    for (const id of ["#step-3", "#step-4"]) {
      await page.locator(`${id} .runner-controls button.primary`).scrollIntoViewIfNeeded();
      await page.waitForFunction(
        (selector) => {
          const button = document.querySelector(`${selector} .runner-controls button.primary`);
          return button instanceof HTMLButtonElement && !button.disabled;
        },
        id,
        { timeout: 30_000 },
      );
    }
    ok("both tutorial runners hydrated");

    // Embedded runners own no fragment: two of them adopting one URL is how a
    // later-hydrating runner ends up on the other's configuration.
    const hydratedHash = await page.evaluate(() => window.location.hash);
    if (hydratedHash !== "") {
      throw new Error(`a tutorial runner wrote the fragment: ${hydratedHash}`);
    }

    // Keys are element-scoped on this page. Space with focus in the prose
    // must start neither runner.
    await page.locator("h1").click();
    await page.keyboard.press("Space");
    for (const id of ["#step-3", "#step-4"]) {
      const label = (await page.textContent(`${id} .runner-controls button.primary`))?.trim();
      if (label !== "Play") {
        throw new Error(`${id} runner answered an unfocused keypress (button says "${label}")`);
      }
    }

    // The step-4 comparison must actually step. With the worker's old
    // domain-prefixed id join it threw before ready and never got this far.
    await page.focus("#step-4 .runner");
    await page.keyboard.press("Shift+ArrowRight");
    await page.waitForFunction(
      () => {
        const text = document.querySelector("#step-4 .runner-position")?.textContent ?? "";
        const match = /Step ([\d,]+)/.exec(text);
        return match !== null && Number(match[1].replace(/,/g, "")) >= 100;
      },
      { timeout: 30_000 },
    );
    ok(`tutorial comparison stepped: ${(await page.textContent("#step-4 .runner-position"))?.trim()}`);

    // ...and only it: the runner above must not have heard those keys, and
    // stepping an embedded runner must not have written the URL.
    const bystander = (await page.textContent("#step-3 .runner-position"))?.trim();
    if (!/^Step 0 of/.test(bystander ?? "")) {
      throw new Error(`keys leaked into the step-3 runner: "${bystander}"`);
    }
    const steppedHash = await page.evaluate(() => window.location.hash);
    if (steppedHash !== "") {
      throw new Error(`stepping an embedded runner wrote the fragment: ${steppedHash}`);
    }
    ok("fragment untouched, keys scoped to the focused runner");

    if (errors.length > 0) {
      throw new Error(`uncaught page errors:\n  ${errors.join("\n  ")}`);
    }

    console.log("smoke: passed");
  } finally {
    await browser.close();
    server.close();
  }
}

main().catch((error) => {
  console.error(`smoke: FAILED — ${error instanceof Error ? error.message : error}`);
  process.exit(1);
});
