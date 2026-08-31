import { describe, expect, it } from "vitest";
import { decodeState, encodeState, type RunnerState } from "./url-state";

const base: RunnerState = {
  domain: "cache",
  policies: ["sieve"],
  trace: "zipf-1.0-100k",
  step: 0,
  params: {},
};

describe("the runner's URL state", () => {
  it("round-trips everything it carries", () => {
    const state: RunnerState = {
      domain: "kv-cache",
      policies: ["h2o", "tova", "streaming-llm"],
      trace: "decode-4096",
      step: 1_234,
      params: { budget: 512, recentWindow: 64 },
    };

    expect(decodeState(encodeState(state), base)).toEqual(state);
  });

  it("omits anything sitting at its default", () => {
    // A link should say what the sender changed, not restate the defaults.
    const fragment = encodeState(base, { trace: "zipf-1.0-100k" });
    expect(fragment).toBe("d=cache&p=sieve");
  });

  it("reads a link written by hand", () => {
    const state = decodeState("#d=cache&p=lru,arc&t=scan-heavy&n=500", base);
    expect(state.domain).toBe("cache");
    expect(state.policies).toEqual(["lru", "arc"]);
    expect(state.trace).toBe("scan-heavy");
    expect(state.step).toBe(500);
  });

  it("keeps the parts it understood when the rest is nonsense", () => {
    // A hand-edited link with a typo should land on a working page carrying
    // whatever was legible, not on an error. Being editable is the point of a
    // readable encoding.
    const state = decodeState("#d=cache&p=sieve&n=-4&nonsense", base);
    expect(state.domain).toBe("cache");
    expect(state.policies).toEqual(["sieve"]);
    expect(state.step).toBe(0);
  });

  it("skips a malformed percent-escape rather than throwing", () => {
    // `decodeURIComponent("100%")` throws, and decoding runs inside a
    // hydration-time state initializer — a bad escape in a pasted link must
    // cost that one parameter, not the island.
    const state = decodeState("#d=cache&p=100%&t=scan-heavy&x.cap=%zz", base);
    expect(state.domain).toBe("cache");
    expect(state.policies).toEqual(["sieve"]);
    expect(state.trace).toBe("scan-heavy");
    expect(state.params).toEqual({});
  });

  it("ignores a key it has never heard of", () => {
    // So a link shared from a later version of the site still opens here.
    const state = decodeState("#d=cache&p=sieve&futurething=1", base);
    expect(state).toEqual({ ...base, domain: "cache", policies: ["sieve"] });
  });

  it("ignores the retired seed key", () => {
    // `s=` used to be encoded, decoded, and consumed by nothing. An old link
    // carrying it still opens here; nothing reads it any more.
    const state = decodeState("#d=cache&p=sieve&s=9", base);
    expect(state).toEqual({ ...base, domain: "cache", policies: ["sieve"] });
  });

  it("falls back entirely on an empty fragment", () => {
    expect(decodeState("", base)).toEqual(base);
    expect(decodeState("#", base)).toEqual(base);
  });

  it("does not let a parameter overwrite a reserved key", () => {
    // Parameters are namespaced, so a policy with a parameter called `step`
    // cannot silently move the runner's position.
    const state = decodeState("#d=cache&x.step=99&n=3", base);
    expect(state.step).toBe(3);
    expect(state.params["step"]).toBe(99);
  });

  it("survives a slug that needs escaping", () => {
    const state: RunnerState = { ...base, trace: "zipf-0.75-1m", policies: ["w-tinylfu"] };
    expect(decodeState(encodeState(state), base)).toEqual(state);
  });
});
