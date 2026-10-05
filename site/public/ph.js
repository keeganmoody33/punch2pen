// PostHog loader for every punch2pen.com page.
// Reads window.__P2P_ENV__ (served by the worker at /env.js). With no key,
// nothing loads and nothing leaves the browser.
// window.__p2pPosthog resolves true once posthog is ready, false otherwise.
(() => {
  const env = window.__P2P_ENV__ || { POSTHOG_KEY: "", POSTHOG_HOST: "https://us.i.posthog.com" };
  const key = typeof env.POSTHOG_KEY === "string" ? env.POSTHOG_KEY.trim() : "";
  const host = (env.POSTHOG_HOST || "https://us.i.posthog.com").replace(/\/+$/, "");
  // The managed proxy serves assets at <host>/static; the direct host uses us-assets.
  const assetHost = host === "https://us.i.posthog.com" ? "https://us-assets.i.posthog.com" : host;

  window.__p2pPosthog = new Promise((resolve) => {
    if (!key) return resolve(false);
    const s = document.createElement("script");
    s.async = true;
    s.src = assetHost + "/static/array.js";
    s.onload = () => {
      if (!window.posthog || typeof window.posthog.init !== "function") return resolve(false);
      window.posthog.init(key, {
        api_host: host,
        ui_host: "https://us.posthog.com",
        defaults: "2026-05-30",
        persistence: "localStorage+cookie",
        capture_exceptions: true,
        // Tag bots as $browser_type=bot instead of dropping them, for the human vs machine tally.
        opt_out_useragent_filter: true,
        session_recording: { maskAllInputs: true },
      });
      window.posthog.register({ site: "punch2pen" });
      resolve(true);
    };
    s.onerror = () => resolve(false);
    document.head.appendChild(s);
  });
})();
