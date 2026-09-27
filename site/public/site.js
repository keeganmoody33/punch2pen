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
      href: source === "hero" ? "https://github.com/keeganmoody33/punch2pen/releases/latest" : downloadUrl,
    });
    if (!assetName) {
      track("download_unavailable", { source, daw: dawValue(), release_tag: releaseTag });
    }
    if (!sent && statusDownload) {
      const extra = " Analytics key is not configured, so this click was not sent to PostHog.";
      const cur = statusDownload.textContent || "";
      if (cur.indexOf("Analytics key is not configured") === -1) {
        setNote(statusDownload, cur + extra, "warn");
      }
    }
  }

  if (macBtn) macBtn.addEventListener("click", () => onDownloadClick("mac_pkg"));
  if (heroBtn) heroBtn.addEventListener("click", () => onDownloadClick("hero"));

  const wouldPay = document.getElementById("would-pay");
  if (wouldPay) wouldPay.addEventListener("click", () => {
    const sent = track("interest_would_pay", { daw: dawValue() });
    setNote(
      statusInterest,
      sent
        ? "Recorded. No price was attached — we only needed the signal."
        : "Wired. PostHog key is missing on this host, so the click stayed in the browser.",
      sent ? "ok" : "warn"
    );
  });

  const justLooking = document.getElementById("just-looking");
  if (justLooking) justLooking.addEventListener("click", () => {
    track("interest_just_looking", { daw: dawValue() });
    setNote(statusInterest, "Stay as long as you want. Download is still free / lite.", "ok");
  });

  const waitlist = document.getElementById("waitlist");
  if (waitlist) waitlist.addEventListener("submit", (ev) => {
    ev.preventDefault();
    const email = (document.getElementById("email").value || "").trim();
    if (!email) {
      setNote(statusInterest, "Add an email, or use “I would pay for the portable dictionary” without one.", "warn");
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

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (ch) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;",
    }[ch]));
  }

  function initReceipt() {
    const freeBtn = document.getElementById("tier-free");
    const paidBtn = document.getElementById("tier-paid");
    const pill = document.getElementById("receipt-pill");
    const status = document.getElementById("receipt-status");
    const panel = document.getElementById("dict-panel");
    const word = document.getElementById("receipt-word");
    const sheet = document.getElementById("receipt-sheet");
    const input = document.getElementById("receipt-correction");
    const apply = document.getElementById("receipt-apply");
    const cancel = document.getElementById("receipt-cancel");
    const replay = document.getElementById("receipt-replay");
    if (!freeBtn || !paidBtn || !pill || !status || !panel || !word || !sheet || !input || !apply || !cancel) {
      return;
    }

    let tier = "free";
    let applied = null;

    function render() {
      const paid = tier === "paid";
      pill.textContent = paid ? "Vocal seat" : "Local";
      pill.classList.toggle("signed", paid);
      if (!applied) {
        word.textContent = "topp";
        word.classList.remove("corrected");
        status.textContent = "Following the playhead · click a word to correct it";
        panel.innerHTML = paid
          ? '<p class="eyebrow">Vocal seat dictionary</p><p>Signed in. No pairs yet. Apply a correction and it is written on this seat.</p>'
          : '<p class="eyebrow">This session</p><p>No account. Corrections stay in memory on this Mac and drop when the engine restarts. Nothing is written.</p>';
        return;
      }
      word.textContent = applied.to;
      word.classList.add("corrected");
      const from = escapeHtml(applied.from);
      const to = escapeHtml(applied.to);
      const row = '<p class="pair"><span>' + from + "</span><span>→</span><span>" + to + "</span>";
      if (paid) {
        status.textContent = '"' + applied.from + '" → "' + applied.to + '" (in 1 place) · saved to Vocal seat dictionary · synced';
        panel.innerHTML = '<p class="eyebrow">Vocal seat dictionary</p>' + row + '<span class="count">1</span></p><p>Saved on the seat and synced. Bar and beat stay in the plugin.</p>';
      } else {
        status.textContent = '"' + applied.from + '" → "' + applied.to + '" (in 1 place) · session-only on this Mac. A profile carries your dictionary between rooms.';
        panel.innerHTML = '<p class="eyebrow">This session</p>' + row + "</p><p>Session-only. Not written to disk, not sent to the profile API. Restart drops it.</p>";
      }
    }

    function setTier(next) {
      tier = next;
      freeBtn.setAttribute("aria-pressed", tier === "free" ? "true" : "false");
      paidBtn.setAttribute("aria-pressed", tier === "paid" ? "true" : "false");
      render();
    }

    word.addEventListener("click", () => {
      sheet.hidden = false;
      input.value = applied ? applied.to : "top";
      input.focus();
    });
    cancel.addEventListener("click", () => {
      sheet.hidden = true;
    });
    apply.addEventListener("click", () => {
      const to = input.value.trim();
      if (!to) return;
      applied = { from: "topp", to: to };
      sheet.hidden = true;
      render();
    });
    input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        ev.preventDefault();
        apply.click();
      } else if (ev.key === "Escape") {
        sheet.hidden = true;
      }
    });
    if (replay) {
      replay.addEventListener("click", () => {
        applied = null;
        sheet.hidden = true;
        input.value = "top";
        render();
      });
    }
    freeBtn.addEventListener("click", () => setTier("free"));
    paidBtn.addEventListener("click", () => setTier("paid"));
    render();
  }

  loadPosthog().then((ok) => {
    if (ok) track("site_ready", { posthog: true });
  });
  if (macBtn) {
    loadRelease();
  }
  initReceipt();
})();
