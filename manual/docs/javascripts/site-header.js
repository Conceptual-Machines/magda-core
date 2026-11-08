// The shared bar's theme switch drives Material's palette, and the bar's icon follows it.
(() => {
  const header = document.querySelector("magda-site-header");
  if (!header) return;

  const syncTheme = () => {
    const scheme = document.body.getAttribute("data-md-color-scheme");
    header.setAttribute("theme", scheme === "default" ? "light" : "dark");
  };
  syncTheme();
  new MutationObserver(syncTheme).observe(document.body, {
    attributes: true,
    attributeFilter: ["data-md-color-scheme"],
  });

  header.addEventListener("magda-theme-toggle", () => {
    // Material leaves every input unchecked until one is clicked, so go by the page's scheme.
    const inputs = [...document.querySelectorAll('[data-md-component="palette"] input')];
    if (!inputs.length) return;
    const scheme = document.body.getAttribute("data-md-color-scheme");
    const current = inputs.findIndex((i) => i.getAttribute("data-md-color-scheme") === scheme);
    const next = inputs[(current + 1) % inputs.length];
    next.checked = true;
    next.dispatchEvent(new Event("change", { bubbles: true }));
  });
})();
