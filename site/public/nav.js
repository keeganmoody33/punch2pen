(() => {
  const nav = document.getElementById("site-nav");
  const button = nav && nav.querySelector(".menu");
  if (!nav || !button) return;
  document.documentElement.classList.add("has-navigation");
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && nav.classList.contains("is-open")) {
      nav.classList.remove("is-open");
      button.setAttribute("aria-expanded", "false");
      button.focus();
    }
  });
  button.addEventListener("click", () => {
    const open = nav.classList.toggle("is-open");
    button.setAttribute("aria-expanded", open ? "true" : "false");
  });
  nav.querySelectorAll(".links a").forEach((link) => {
    link.addEventListener("click", () => {
      nav.classList.remove("is-open");
      button.setAttribute("aria-expanded", "false");
    });
  });
})();
