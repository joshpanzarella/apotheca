// Renders every <section class="screen"> in screens.html to a PNG beside it.
// Needs Node and Playwright:  node docs/screens/render.js
// The PNGs are 1280x960: the true 640x480 screen at twice the size.

const path = require('path');
const { chromium } = require('playwright');

(async () => {
  const dir = __dirname;
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1400, height: 1700 }, deviceScaleFactor: 2 });
  await page.goto('file://' + path.join(dir, 'screens.html'));
  for (const screen of await page.$$('section.screen')) {
    const id = await screen.getAttribute('id');
    await screen.screenshot({ path: path.join(dir, id + '.png') });
    console.log(id + '.png');
  }
  await browser.close();
})();
