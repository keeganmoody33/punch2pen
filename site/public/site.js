(() => {
  const RELEASES = "https://api.github.com/repos/keeganmoody33/punch2pen/releases/latest";
  const RELEASE_PAGE = "https://github.com/keeganmoody33/punch2pen/releases/latest";
  const env = window.__P2P_ENV__ || { POSTHOG_KEY: "", POSTHOG_HOST: "https://us.i.posthog.com" };
  const statusDownload = document.getElementById("download-status");
  const statusInterest = document.getElementById("interest-status");
  const macBtn = document.getElementById("mac-download");
  const heroBtn = document.getElementById("download-btn");
  const daw = document.getElementById("daw");

  let downloadUrl = RELEASE_PAGE;
  let releaseTag = "";
  let assetName = "";
  let posthogReady = false;

  function dawValue() {
    return daw && daw.value ? daw.value : "";
  }

  function track(event, props) {
    const payload = Object.assign({ product: "punch2pen", surface: "site" }, props || {});
    if (posthogReady && window.posthog && typeof window.posthog.capture === "function") {
      window.posthog.capture(event, payload);
      return true;
    }
    return false;
  }

  function setNote(el, text, kind) {
    if (!el) return;
    el.textContent = text;
    el.classList.remove("ok", "warn");
    if (kind) el.classList.add(kind);
  }

  function loadPosthog() {
    const key = typeof env.POSTHOG_KEY === "string" ? env.POSTHOG_KEY.trim() : "";
    if (!key) {
      return Promise.resolve(false);
    }
    return new Promise((resolve) => {
      const s = document.createElement("script");
      s.async = true;
      s.src = "https://us-assets.i.posthog.com/static/array.js";
      s.onload = () => {
        if (!window.posthog || typeof window.posthog.init !== "function") {
          resolve(false);
          return;
        }
        window.posthog.init(key, {
          api_host: env.POSTHOG_HOST || "https://us.i.posthog.com",
          persistence: "localStorage+cookie",
          capture_pageview: true,
          capture_pageleave: true,
        });
        posthogReady = true;
        resolve(true);
      };
      s.onerror = () => resolve(false);
      document.head.appendChild(s);
    });
  }

  async function loadRelease() {
    try {
      const res = await fetch(RELEASES, { headers: { Accept: "application/vnd.github+json" } });
      if (!res.ok) throw new Error("release lookup failed");
      const data = await res.json();
      releaseTag = data.tag_name || "";
      const assets = Array.isArray(data.assets) ? data.assets : [];
      const pkg = assets.find((a) => typeof a.name === "string" && a.name.toLowerCase().endsWith(".pkg"));
      if (pkg && pkg.browser_download_url) {
        downloadUrl = pkg.browser_download_url;
        assetName = pkg.name;
        macBtn.href = downloadUrl;
        setNote(
          statusDownload,
          `Latest Release ${releaseTag}: ${assetName}. Unsigned. Right-click open if Gatekeeper blocks.`,
          "ok"
        );
      } else {
        macBtn.href = data.html_url || RELEASE_PAGE;
        setNote(
          statusDownload,
          releaseTag
            ? `Release ${releaseTag} is up, but no .pkg asset yet. The button opens GitHub Releases.`
            : "No .pkg on the latest Release yet. The button opens GitHub Releases.",
          "warn"
        );
      }
    } catch (err) {
      macBtn.href = RELEASE_PAGE;
      setNote(
        statusDownload,
        "Could not read GitHub Releases from here. Button goes to the latest Release page.",
        "warn"
      );
    }
  }

  function onDownloadClick(source) {
    const sent = track("download_click", {
      source,
      daw: dawValue(),
      release_tag: releaseTag,
      asset: assetName || "",
      href: downloadUrl,
    });
    if (!assetName) {
      track("download_unavailable", { source, daw: dawValue(), release_tag: releaseTag });
    }
    if (!sent) {
      setNote(
        statusDownload,
        (statusDownload.textContent || "") + " Analytics key is not configured, so this click was not sent to PostHog.",
        "warn"
      );
    }
  }

  macBtn.addEventListener("click", () => onDownloadClick("mac_pkg"));
  heroBtn.addEventListener("click", (ev) => {
    if (heroBtn.getAttribute("href") === "#download") return;
    onDownloadClick("hero");
  });

  document.getElementById("would-pay").addEventListener("click", () => {
    const sent = track("interest_would_pay", { daw: dawValue() });
    setNote(
      statusInterest,
      sent
        ? "Recorded. No price was attached — we only needed the signal."
        : "Wired. PostHog key is missing on this host, so the click stayed in the browser.",
      sent ? "ok" : "warn"
    );
  });

  document.getElementById("just-looking").addEventListener("click", () => {
    track("interest_just_looking", { daw: dawValue() });
    setNote(statusInterest, "Stay as long as you want. Download is still free / lite.", "ok");
  });

  document.getElementById("waitlist").addEventListener("submit", (ev) => {
    ev.preventDefault();
    const email = (document.getElementById("email").value || "").trim();
    if (!email) {
      setNote(statusInterest, "Add an email, or just hit “I would pay” without one.", "warn");
      return;
    }
    const sent = track("waitlist_submit", { daw: dawValue(), email });
    if (sent && window.posthog && typeof window.posthog.identify === "function") {
      window.posthog.identify(email, { email, daw: dawValue() });
    }
    setNote(
      statusInterest,
      sent
        ? "On the list. We will not invent a price in the follow-up."
        : "PostHog is not configured, so that email was not stored anywhere.",
      sent ? "ok" : "warn"
    );
  });

  loadPosthog().then((ok) => {
    if (ok) track("site_ready", { posthog: true });
  });
  loadRelease().then(() => {
    heroBtn.href = downloadUrl;
    heroBtn.removeAttribute("aria-disabled");
  });
})();
