/**
 * Punch2Pen public site worker.
 * Serves static assets, plus /env.js so PostHog can be enabled via
 * Cloudflare env without committing a project key.
 */
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/env.js") {
      const payload = {
        POSTHOG_KEY: typeof env.POSTHOG_KEY === "string" ? env.POSTHOG_KEY : "",
        POSTHOG_HOST:
          typeof env.POSTHOG_HOST === "string" && env.POSTHOG_HOST.length > 0
            ? env.POSTHOG_HOST
            : "https://us.i.posthog.com",
      };
      return new Response(`window.__P2P_ENV__=${JSON.stringify(payload)};\n`, {
        headers: {
          "content-type": "application/javascript; charset=utf-8",
          "cache-control": "no-store",
        },
      });
    }
    return env.ASSETS.fetch(request);
  },
};
