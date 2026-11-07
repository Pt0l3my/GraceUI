/*
 * GraceUI mock: the few behaviours a reviewer needs to click through the pages. MIT, see ../LICENSE.
 * The sidebar accordion and its collapse button, the top bar's menus, tabs, collapsible sections and "Advanced",
 * switches, segmented choices, filter chips, sliders (cooldowns in uneven steps) and the help dialog.
 * No network, no storage, no timers. The product's web app is a separate build (DESIGN §2, G3).
 * A hash opens one state for review: #quick, #theme, #help, #collapsed, or a tab's own hash (#health and so on).
 */
(function () {
  "use strict";

  function all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }
  function byId(id) { return document.getElementById(id); }

  // Sidebar: one category open at a time (IA v2 §1.2)
  all(".nav .cat[aria-controls]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var open = btn.getAttribute("aria-expanded") !== "true";
      all(".nav .cat[aria-controls]").forEach(function (other) {
        other.setAttribute("aria-expanded", "false");
        byId(other.getAttribute("aria-controls")).hidden = true;
      });
      btn.setAttribute("aria-expanded", open ? "true" : "false");
      byId(btn.getAttribute("aria-controls")).hidden = !open;
    });
  });
  var app = document.querySelector(".app");
  all(".collapse").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var c = app.classList.toggle("collapsed");
      btn.setAttribute("aria-label", c ? "Show the sidebar's names" : "Collapse the sidebar");
    });
  });

  // Top bar menus
  function closeMenus(except) {
    all("[aria-haspopup][aria-controls]").forEach(function (b) {
      if (b === except) return;
      b.setAttribute("aria-expanded", "false");
      var m = byId(b.getAttribute("aria-controls"));
      if (m) m.hidden = true;
    });
  }
  all("[aria-haspopup][aria-controls]").forEach(function (b) {
    b.addEventListener("click", function (e) {
      e.stopPropagation();
      var m = byId(b.getAttribute("aria-controls"));
      var open = m.hidden;
      closeMenus(b);
      m.hidden = !open;
      b.setAttribute("aria-expanded", open ? "true" : "false");
    });
  });
  all(".menu").forEach(function (m) { m.addEventListener("click", function (e) { e.stopPropagation(); }); });
  document.addEventListener("click", function () { closeMenus(null); });
  all(".menu [role='menuitemradio']").forEach(function (opt) {
    opt.addEventListener("click", function () {
      all("[role='menuitemradio']", opt.parentNode).forEach(function (o) { o.setAttribute("aria-checked", "false"); });
      opt.setAttribute("aria-checked", "true");
    });
  });

  // Tabs, with arrow keys, Home and End
  function selectTab(tab) {
    var list = tab.closest("[role='tablist']");
    all("[role='tab']", list).forEach(function (t) {
      var on = t === tab;
      t.setAttribute("aria-selected", on ? "true" : "false");
      t.tabIndex = on ? 0 : -1;
      var p = byId(t.getAttribute("aria-controls"));
      if (p) p.hidden = !on;
    });
  }
  all("[role='tab']").forEach(function (tab) {
    tab.addEventListener("click", function () { selectTab(tab); });
    tab.addEventListener("keydown", function (e) {
      var tabs = all("[role='tab']", tab.closest("[role='tablist']"));
      var i = tabs.indexOf(tab), j = -1;
      if (e.key === "ArrowRight") j = (i + 1) % tabs.length;
      else if (e.key === "ArrowLeft") j = (i - 1 + tabs.length) % tabs.length;
      else if (e.key === "Home") j = 0;
      else if (e.key === "End") j = tabs.length - 1;
      if (j >= 0) { e.preventDefault(); selectTab(tabs[j]); tabs[j].focus(); }
    });
  });

  // Collapsible sections and "Advanced"
  all(".toggle[aria-controls], .adv-toggle[aria-controls]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var open = btn.getAttribute("aria-expanded") !== "true";
      btn.setAttribute("aria-expanded", open ? "true" : "false");
      byId(btn.getAttribute("aria-controls")).hidden = !open;
      var card = btn.closest(".card");
      if (card && btn.classList.contains("toggle")) card.classList.toggle("collapsed", !open);
    });
  });

  // Switches and segmented choices flip when clicked; nothing is saved
  all(".switch").forEach(function (sw) {
    sw.addEventListener("click", function () {
      var on = sw.getAttribute("aria-checked") !== "true";
      sw.setAttribute("aria-checked", on ? "true" : "false");
      var word = sw.parentNode.querySelector(".switch-word");
      if (word) word.textContent = on ? "On" : "Off";
    });
  });
  all(".seg").forEach(function (seg) {
    all("button", seg).forEach(function (b) {
      b.addEventListener("click", function () {
        all("button", seg).forEach(function (o) { o.setAttribute("aria-checked", o === b ? "true" : "false"); });
      });
    });
  });
  all(".btn[aria-pressed]").forEach(function (b) {
    b.addEventListener("click", function () {
      b.setAttribute("aria-pressed", b.getAttribute("aria-pressed") === "true" ? "false" : "true");
    });
  });

  // Filter chips: a group marked data-single picks one; data-target names the rows it filters by data-kind
  all(".chips[data-target]").forEach(function (group) {
    var rows = all(group.getAttribute("data-target") + " [data-kind]");
    all(".chip", group).forEach(function (chip) {
      chip.addEventListener("click", function () {
        all(".chip", group).forEach(function (c) { c.setAttribute("aria-pressed", c === chip ? "true" : "false"); });
        var k = chip.getAttribute("data-kind-filter");
        rows.forEach(function (r) {
          r.hidden = !(k === "all" || (" " + r.getAttribute("data-kind") + " ").indexOf(" " + k + " ") !== -1);
        });
      });
    });
  });
  all(".chips:not([data-target]) .chip").forEach(function (chip) {
    chip.addEventListener("click", function () {
      var single = chip.parentNode.hasAttribute("data-single");
      if (single) all(".chip", chip.parentNode).forEach(function (c) { c.setAttribute("aria-pressed", "false"); });
      chip.setAttribute("aria-pressed", single || chip.getAttribute("aria-pressed") !== "true" ? "true" : "false");
    });
  });

  // Sliders: the filled part and the value with its unit. A cooldown slider steps through data-durations (seconds).
  function durWords(sec) {
    if (sec === 0) return "None";
    if (sec < 60) return sec + " s";
    if (sec < 3600) { var m = Math.floor(sec / 60), r = sec % 60; return m + " min" + (r ? " " + r + " s" : ""); }
    var h = Math.floor(sec / 3600), rm = Math.floor((sec % 3600) / 60);
    return h + " h" + (rm ? " " + rm + " min" : "");
  }
  function fmt(input) {
    var steps = input.getAttribute("data-durations");
    if (steps) return durWords(Number(steps.split(",")[Number(input.value)]));
    var v = Number(input.value), step = input.getAttribute("step") || "1";
    var dec = step.indexOf(".") === -1 ? 0 : step.split(".")[1].length;
    var unit = input.getAttribute("data-unit") || "";
    var off = input.getAttribute("data-zero");
    if (off && v === 0) return off;
    return v.toFixed(dec) + (unit ? " " + unit : "");
  }
  all("input[type='range']").forEach(function (r) {
    function sync() {
      var min = Number(r.min || 0), max = Number(r.max || 100);
      r.style.setProperty("--fill", ((Number(r.value) - min) * 100 / (max - min)) + "%");
      var out = document.querySelector("[data-for='" + r.id + "']");
      if (out) out.textContent = fmt(r);
      if (r.hasAttribute("data-durations")) r.setAttribute("aria-valuetext", fmt(r));
    }
    r.addEventListener("input", sync);
    sync();
  });

  // Master–detail rows
  all("tr[data-select]").forEach(function (tr) {
    tr.addEventListener("click", function () {
      all("tr[aria-selected]", tr.closest("table")).forEach(function (o) { o.removeAttribute("aria-selected"); });
      tr.setAttribute("aria-selected", "true");
    });
  });

  // The help dialog (IA v2 §1.4)
  var help = byId("help");
  function openHelp() { if (help) { help.hidden = false; var c = help.querySelector("[data-close]"); if (c) c.focus(); } }
  function closeHelp() { if (help) help.hidden = true; }
  all(".help-mark").forEach(function (b) { b.addEventListener("click", openHelp); });
  all("[data-close]").forEach(function (b) { b.addEventListener("click", closeHelp); });
  if (help) help.addEventListener("click", function (e) { if (e.target === help) closeHelp(); });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape") { closeHelp(); closeMenus(null); }
  });

  // A hash opens one state for review
  var h = location.hash.slice(1);
  if (h === "help") openHelp();
  else if (h === "collapsed") app.classList.add("collapsed");
  else if (h) {
    var opener = document.querySelector("[data-hash='" + h + "']");
    if (opener) {
      if (opener.getAttribute("role") === "tab") selectTab(opener);
      else opener.click();
    }
  }
}());
