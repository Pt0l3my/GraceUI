// Screenshot GraceUI's mock pages from file:// with Playwright's Chromium. Every request that is not a file: or
// data: URL is aborted and logged, so nothing leaves the machine. Also reports horizontal overflow and clipped text.
// usage: NODE_PATH="$(npm root -g)" node mock/build/shoot.js mock OUT_DIR   (Playwright and its Chromium installed globally)
// Our own code, MIT (see ../../LICENSE). OUT_DIR should be outside the repository: screenshots are not committed.
const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');

const MOCK = path.resolve(process.argv[2]);
const OUT = path.resolve(process.argv[3]);
const PAGES = ['index', 'ai-providers', 'ai-models', 'characters-types', 'characters-named', 'history-memories',
  'behaviour-speaking', 'behaviour-pacing', 'voice-tts'];
const SIZES = [[1280, 720], [2560, 1440]];
const EXTRAS = [
  ['index', 'quick', 1280, 720], ['index', 'help', 1280, 720], ['ai-providers', 'health', 1280, 720],
  ['characters-named', 'allowances', 1280, 720], ['characters-named', 'reactions', 1280, 720], ['index', 'collapsed', 1280, 720], ['index', 'quick', 2560, 1440],
];

async function overflow(page) {
  return page.evaluate(() => {
    const out = [];
    const all = document.querySelectorAll('body *');
    for (const el of all) {
      const cs = getComputedStyle(el);
      if (cs.display === 'none' || cs.visibility === 'hidden') continue;
      if (el.closest('[hidden]') || el.closest('.sprite')) continue;
      const r = el.getBoundingClientRect();
      if (r.width === 0 && r.height === 0) continue;
      // horizontal overflow of a box that does not scroll on purpose
      const scrolls = ['auto', 'scroll'].includes(cs.overflowX);
      if (!scrolls && el.scrollWidth > el.clientWidth + 1 && el.clientWidth > 0 && !['svg', 'use', 'path', 'input', 'select'].includes(el.tagName.toLowerCase())) {
        if (cs.overflowX === 'hidden' && cs.textOverflow === 'ellipsis') {
          out.push(`ellipsis ${el.tagName}.${el.className} "${(el.textContent || '').trim().slice(0, 40)}"`);
        } else if (cs.overflowX !== 'hidden' && cs.overflowX !== 'clip') {
          out.push(`overflow ${el.tagName}.${typeof el.className === 'string' ? el.className : ''} sw=${el.scrollWidth} cw=${el.clientWidth} "${(el.textContent || '').trim().slice(0, 40)}"`);
        }
      }
      if (scrolls && el.scrollWidth > el.clientWidth + 1 && el.clientWidth > 0) {
        out.push(`scroll-x ${el.tagName}.${el.className} sw=${el.scrollWidth} cw=${el.clientWidth}`);
      }
    }
    // anything beyond the viewport's right edge
    const vw = document.documentElement.clientWidth;
    for (const el of all) {
      if (el.closest('[hidden]') || el.closest('.sprite')) continue;
      const r = el.getBoundingClientRect();
      if (r.width > 0 && r.right > vw + 1 && !el.closest('.scroll-x')) {
        out.push(`beyond-viewport ${el.tagName}.${el.className} right=${Math.round(r.right)} vw=${vw}`);
        break;
      }
    }
    const pg = document.querySelector('.page');
    const side = document.querySelector('.nav');
    return { issues: [...new Set(out)].slice(0, 30), pageScroll: pg ? [pg.scrollHeight, pg.clientHeight] : null,
      navScroll: side ? [side.scrollHeight, side.clientHeight] : null, k: getComputedStyle(document.documentElement).fontSize };
  });
}

(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-proxy-server', '--disable-background-networking',
    '--disable-component-update', '--disable-sync', '--disable-domain-reliability', '--no-pings'] });
  const blocked = [];
  const report = {};
  async function shot(name, hash, w, h, full) {
    const ctx = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 1, serviceWorkers: 'block' });
    await ctx.route('**/*', (route) => {
      const u = route.request().url();
      if (u.startsWith('file:') || u.startsWith('data:')) return route.continue();
      blocked.push(u); return route.abort();
    });
    const page = await ctx.newPage();
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.text()); });
    await page.goto('file://' + path.join(MOCK, name + '.html') + (hash ? '#' + hash : ''));
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.waitForTimeout(150);
    const ov = await overflow(page);
    const tag = `${name}${hash ? '-' + hash : ''}-${w}x${h}`;
    await page.screenshot({ path: path.join(OUT, tag + '.png') });
    if (full && ov.pageScroll && ov.pageScroll[0] > ov.pageScroll[1]) {
      // the page scrolls inside .page: grow the viewport to show it whole, for review only
      const extra = ov.pageScroll[0] - ov.pageScroll[1];
      await page.setViewportSize({ width: w, height: Math.min(h + extra + 8, 9000) });
      await page.waitForTimeout(100);
      await page.screenshot({ path: path.join(OUT, `${name}-${w}-full.png`) });
    }
    report[tag] = { ...ov, errors };
    await ctx.close();
  }
  for (const n of PAGES) for (const [w, h] of SIZES) await shot(n, '', w, h, true);
  for (const [n, hash, w, h] of EXTRAS) await shot(n, hash, w, h, false);
  await browser.close();
  fs.writeFileSync(path.join(OUT, 'report.json'), JSON.stringify({ blocked, report }, null, 1));
  console.log('blocked requests:', blocked.length);
  for (const [k, v] of Object.entries(report)) {
    if (v.issues.length || v.errors.length) console.log(k, JSON.stringify({ issues: v.issues, errors: v.errors }));
    else console.log(k, 'ok', 'page', v.pageScroll, 'nav', v.navScroll, v.k);
  }
})();
