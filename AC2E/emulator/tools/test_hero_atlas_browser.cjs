// Run with Playwright installed and CHROME_PATH set if using system Chromium.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {pathToFileURL} = require("node:url");
const {chromium} = require("playwright");

async function main() {
  const root = path.resolve(__dirname, "../../..");
  const output = path.join(root, "Saved/AtlasTests", new Date().toISOString().replaceAll(":", "-"));
  fs.mkdirSync(output, {recursive:true});
  const browser = await chromium.launch({executablePath:process.env.CHROME_PATH || undefined, headless:true});
  const checks = [], failures = [], tiles = new Set();
  try {
    const page = await browser.newPage({viewport:{width:1440,height:900}});
    page.on("pageerror", error => failures.push(error.message));
    page.on("requestfailed", request => failures.push(request.url() + " " + request.failure().errorText));
    page.on("request", request => {
      const match = new URL(request.url()).pathname.match(/\/tiles\/(\d+\/\d+-\d+)\.webp$/);
      if (match) tiles.add(match[1]);
    });
    await page.goto(pathToFileURL(path.join(root, "Docs/Atlas/index.html")).href);
    await page.waitForSelector("#results button");
    const settled = async () => {
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      await page.waitForFunction(() => !terrain.loading && !pendingFrame);
    };
    async function withinViewport() {
      const dimensions = await page.evaluate(() => {
        const d = document.documentElement;
        return [d.scrollWidth, d.scrollHeight, innerWidth, innerHeight, scrollX, scrollY];
      });
      assert.deepEqual(dimensions.slice(0, 2), dimensions.slice(2, 4));
      assert.deepEqual(dimensions.slice(4), [0, 0]);
    }
    await settled();
    const overviewTileCount = tiles.size;
    assert(overviewTileCount > 0 && overviewTileCount < 100, "Load only overview levels on entry");
    assert(![...tiles].some(t => t.startsWith("6/")));
    await page.screenshot({path:path.join(output,"overview.png")});
    await page.locator("#detail").getByRole("button", {name:"Played through UDP",exact:true}).click();
    const expectedCount = await page.evaluate(() => window.AC2QuestReview.indexedSummary["Played through UDP"]);
    assert.equal(await page.locator("#count").innerText(), expectedCount + " records");
    await page.selectOption("#coverage", "");
    await page.selectOption("#kind", "");
    await page.click("#next");
    assert.match(await page.locator("#page").innerText(), /^2 \/ /);
    await page.click("#prev");
    checks.push("Overview tile demand, result paging and offline load");
    await page.fill("#search", "47002e27");
    assert.equal(await page.locator("#results button").count(), 1);
    await page.locator("#results button").click();
    await page.locator("#detail").getByRole("heading", {name:"Officer Cameron", exact:true}).waitFor();
    const catalog = await page.evaluate(() => {
      const rows = window.AC2Atlas.records;
      const officer = rows.find(r => r.kind === "NPC" && r.name === "Officer Cameron");
      const beard = rows.find(r => r.kind === "Item" && r.name === "Banderling Beard");
      return {
        cells: officer.spawns.map(s => s[2]).sort(),
        patrols: officer.places.map(p => p.slice(1,3)),
        quests: officer.links,
        placeholderTitles: rows.filter(r => /^(?:(?:0x)?[a-f\d]{8}|\?+)$/i.test(r.name)).map(r => r.name),
        recoveredNames: ["Lost Wish Glyph", "Virindi Death Trap", "Gear Badge #?????"].filter(name =>
          rows.some(r => r.name === name)),
        unnamedCarrier: rows.some(r => r.kind === "Creature" && r.name === "???"),
        inherited: beard.links.some(([role,kind,name]) =>
          role === "Native trophy" && kind === "Creature" && name === "Banderling Marauder"),
        danglingCarrier: rows.some(r => r.links.some(([,kind,name]) => kind === "Creature" && name === "???"))
      };
    });
    assert.deepEqual(catalog.cells, ["6fa8000d", "cf8c0015"]);
    assert.deepEqual(catalog.patrols, [[32.2,-12.8],[10.1,62.5],[-10.4,-31.4]]);
    assert(catalog.quests.some(([,kind,name]) => kind === "Quest" && name === "Busted!"));
    assert(catalog.inherited && !catalog.unnamedCarrier && !catalog.danglingCarrier);
    assert.deepEqual(catalog.placeholderTitles, []);
    assert.equal(catalog.recoveredNames.length, 3);
    checks.push("Officer Cameron ID search, guide patrols, captured placements and named trophy carriers");
    await page.getByRole("button",{name:"Locate on map"}).click();
    await settled();
    assert(await page.evaluate(()=>markers.some(m=>m[2]===selected)));
    const settings=page.getByRole("button",{name:"Map settings"});
    await settings.click();
    await page.locator('#markerKinds input[value="NPC"]').uncheck();
    await page.keyboard.press("Escape");
    await settled();
    assert(await page.evaluate(()=>markers.every(m=>m[2].kind!=="NPC")));
    assert.equal(await page.locator("#results button").count(),1);
    await page.reload();
    await settings.click();
    assert(!await page.locator('#markerKinds input[value="NPC"]').isChecked());
    await page.locator("#allMarkerKinds").check();
    await page.getByRole("button",{name:"Done"}).click();
    checks.push("Marker kinds hide selected dots and hit targets independently of the catalog and persist on reload");
    await page.evaluate(() => location.hash=encodeURIComponent("NPC/Gallus"));
    await page.waitForFunction(() => selected?.name === "Gallus");
    await page.getByRole("button",{name:"Locate on map",exact:true}).click();
    const selectedMarkers = () => page.evaluate(() =>
      markers.filter(m=>m[2]===selected).map(([x,y])=>{
        const box=canvas.getBoundingClientRect();
        return [center[0]-(y-box.height/2)/scale(),center[1]+(x-box.width/2)/scale()]
          .map(v=>Number(v.toFixed(3)));
      }));
    await settled();
    const gallus = await page.evaluate(() => ({
      spawn:selected.spawns[0].slice(6), guides:selected.places.map(p=>p.slice(1,3))
    }));
    assert(gallus.guides.length>2);
    assert.deepEqual(await selectedMarkers(),[gallus.spawn]);
    await page.uncheck("#spawnLayer");
    await settled();
    const guideOnly=await selectedMarkers();
    assert.equal(guideOnly.length,1);
    assert(gallus.guides.some(g=>g[0]===guideOnly[0][0] && g[1]===guideOnly[0][1]));
    await page.uncheck("#guideLayer");
    await settled();
    assert.deepEqual(await selectedMarkers(),[]);
    await page.check("#spawnLayer");
    await settled();
    assert.deepEqual(await selectedMarkers(),[gallus.spawn]);
    await page.check("#guideLayer");
    await page.evaluate(() => location.hash=encodeURIComponent("NPC/Jlorryen"));
    await page.waitForFunction(() => selected?.name === "Jlorryen");
    await page.getByRole("button",{name:"Locate on map",exact:true}).click();
    await settled();
    assert.equal((await selectedMarkers()).length,1);
    await page.evaluate(() => location.hash=encodeURIComponent("NPC/Warder of Cragstone"));
    await page.waitForFunction(() => selected?.name === "Warder of Cragstone");
    await page.getByRole("button",{name:"Locate on map",exact:true}).click();
    await settled();
    assert.equal((await selectedMarkers()).length,2);
    assert.equal(await page.evaluate(() => sitesByRecord.get(byName.get("NPC/Officer Cameron")).length),3);
    checks.push("NPC sites use one dot per location, retain nearby independent posts and patrol stops, and respect both source layers");
    const npcCoverage = await page.evaluate(() => {
      const npcs = records.filter(r => r.kind === "NPC");
      return {
        records:npcs.length,
        preparedPlacements:npcs.reduce((n,r)=>n+(r.spawns?.length || 0),0),
        outdoorPlacements:npcs.reduce((n,r)=>n+(r.spawns || []).filter(onWorldSpawn).length,0),
        outdoorGuideCitations:npcs.reduce((n,r)=>n+r.places.filter(onWorldGuide).length,0),
        outdoorSites:npcs.reduce((n,r)=>n+sitesByRecord.get(r).length,0)
      };
    });
    const placements = await page.evaluate(() => Object.fromEntries([
      "Endaraen Brightstar","Furs Turfna","Lost Wish Explorer","Naokim","Armoredillo Doom Ancestor"
    ].map(name => [name, byName.get("NPC/"+name).spawns.length])));
    assert.deepEqual(placements, {
      "Endaraen Brightstar":1,"Furs Turfna":1,"Lost Wish Explorer":1,"Naokim":1,"Armoredillo Doom Ancestor":3
    });
    checks.push("Corrected NPC records match their remaining server instances and the Ancestor retains three outposts");
    await page.evaluate(() => location.hash = encodeURIComponent("NPC/Naokim"));
    await page.waitForFunction(() => selected?.name === "Naokim");
    await settled();
    assert(await page.evaluate(() => selected.places.length > 0 && selected.places.every(p => p[4] === "Dungeon")));
    assert.equal(await page.getByRole("button",{name:"Locate on map",exact:true}).count(),0);
    assert(await page.evaluate(() => markers.every(m => m[2] !== selected)));
    await page.evaluate(() => location.hash = encodeURIComponent("NPC/Gurog High Shaman"));
    await page.waitForFunction(() => selected?.name === "Gurog High Shaman");
    assert(await page.getByRole("button",{name:"Locate on map",exact:true}).isVisible());
    checks.push("Repeated citations of an indoor NPC stay off the map; the NPC outside New Mage Academy remains locatable");
    await page.selectOption("#kind", "NPC");
    await page.fill("#search", "Lost wish Explorer");
    await page.locator("#results button").first().click();
    await page.getByRole("button", {name:"Locate on map", exact:true}).click();
    await page.uncheck("#guideLayer");
    await page.uncheck("#spawnLayer");
    for (let i = 0; i < 4; i++) await page.click("#zoomIn");
    await settled();
    assert([...tiles].some(t => t.startsWith("6/")), "Regional geometry tiles load at high zoom");
    assert(await page.evaluate(() => [...terrain.images.values()].every(t => t.state === "ready")));
    await page.screenshot({path:path.join(output,"lost-wish-detail.png")});
    const physicalCoverage = await page.evaluate(() => ({
      scale:terrain.manifest.tileSize * 2**terrain.manifest.maxZoom,
      maxZoom:terrain.manifest.maxZoom
    }));
    assert(physicalCoverage.scale >= 16384);
    const spanAtDetail = await page.evaluate(() => span);
    await page.locator("#map").focus();
    const start = await page.evaluate(() => center.slice());
    await page.keyboard.press("ArrowRight");
    assert((await page.evaluate(() => center[1])) > start[1]);
    await page.keyboard.press("Home");
    assert((await page.evaluate(() => span)) > spanAtDetail);
    const box = await page.locator("#map").boundingBox();
    const ax = Math.round(box.x + box.width * .38), ay = Math.round(box.y + box.height * .58);
    const worldAtPointer = () => page.evaluate(([x,y]) => {
      const r=canvas.getBoundingClientRect();
      return [center[0]-(y-r.top-r.height/2)/scale(),center[1]+(x-r.left-r.width/2)/scale()];
    }, [ax,ay]);
    const before = await worldAtPointer();
    await page.mouse.move(ax, ay);
    await page.mouse.wheel(0, -230);
    await page.waitForFunction(() => span < fitSpan);
    const after = await worldAtPointer();
    assert(before.every((v,i) => Math.abs(v-after[i]) < 1e-7), JSON.stringify({before,after,ax,ay,box,span:await page.evaluate(()=>span)}));
    const beforePan = await page.evaluate(() => center.slice());
    await page.mouse.down();
    await page.mouse.move(ax + 70, ay + 40, {steps:4});
    await page.mouse.up();
    assert((await page.evaluate(() => center[1])) < beforePan[1]);
    checks.push("World to finest tile level, layer controls, pointer-anchored zoom, drag, keyboard pan and reset");
    // Exercise the clipboard boundary without overwriting the developer's system clipboard.
    await page.evaluate(()=>Object.defineProperty(navigator,"clipboard",{configurable:true,value:{
      writeText:async value=>{window.copiedCoordinates=value;}
    }}));
    const [north,east]=await worldAtPointer();
    const compass=Math.abs(north).toFixed(1)+(north<0?"S":"N")+", "+Math.abs(east).toFixed(1)+(east<0?"W":"E");
    await page.mouse.click(ax,ay,{button:"right"});
    assert.equal(await page.locator("#coordinateValue").inputValue(),compass);
    await page.getByRole("button",{name:"Copy coordinates to clipboard"}).click();
    assert.equal(await page.evaluate(()=>window.copiedCoordinates),compass);
    await page.keyboard.press("Escape");
    await page.locator("#map").focus();
    await page.keyboard.press("Shift+F10");
    assert(await page.getByRole("button",{name:"Copy coordinates to clipboard"}).isVisible());
    await page.keyboard.press("Escape");
    checks.push("Pointer coordinates after zoom/pan use in-game compass formatting; keyboard context menu works");
    for (const [width,height] of [[1440,900],[1100,650],[800,600],[390,780],[600,360]]) {
      await page.setViewportSize({width,height});
      if (width <= 880) {
        await page.click('[data-show="catalog"]');
        await page.selectOption("#kind", "");
        await page.fill("#search", "Lost wish Explorer");
        await page.locator("#results button").filter({hasText:"NPC"}).click();
        assert(await page.locator("#detail").isVisible());
        await page.getByRole("button", {name:"Locate on map", exact:true}).click();
        assert(await page.locator("#map").isVisible());
      }
      await withinViewport();
    }
    await page.screenshot({path:path.join(output,"compact.png")});
    const touch = await page.context().newCDPSession(page);
    await touch.send("Emulation.setTouchEmulationEnabled", {enabled:true,maxTouchPoints:2});
    await page.click("#reset");
    const rect = await page.locator("#map").boundingBox();
    const cx = Math.round(rect.x+rect.width/2), cy = Math.round(rect.y+rect.height/2);
    const twoFingers = radius => [1,2].map(id=>({id,x:cx+(id===1?-radius:radius),y:cy}));
    await touch.send("Input.dispatchTouchEvent", {type:"touchStart",touchPoints:twoFingers(30)});
    for (const radius of [40,60,85]) {
      await touch.send("Input.dispatchTouchEvent", {type:"touchMove",touchPoints:twoFingers(radius)});
    }
    await touch.send("Input.dispatchTouchEvent", {type:"touchEnd",touchPoints:[]});
    await touch.send("Emulation.setTouchEmulationEnabled", {enabled:false});
    assert(await page.evaluate(() => span < fitSpan/2 && !pointers.size));
    await withinViewport();
    checks.push("Touch pinch loads a finer level without moving the browser viewport");
    checks.push("Desktop, tablet, mobile and landscape viewports fit, with working panel navigation");
    await page.setViewportSize({width:1440,height:900});
    await page.selectOption("#kind","Quest");
    await page.fill("#search","");
    await page.selectOption("#coverage",{label:"Played through UDP"});
    assert(await page.locator("#results button").count() > 0);
    await page.locator("#results button").first().click();
    await page.waitForFunction(() => document.querySelector("#detail").textContent.includes("Played through UDP"));
    await page.locator("#detail summary").filter({hasText:"Test evidence"}).click();
    await page.locator("#detail details[open] li").first().waitFor();
    assert.match(await page.locator("#detail details[open] li a").first().getAttribute("href"), /^\.\.\/\.\.\/Network\/test_/);
    checks.push("Quest status filter, selected record and traceable test evidence");
    await page.selectOption("#kind","Dungeon");
    await page.selectOption("#coverage","");
    await page.fill("#search","Water Temple");
    await page.locator("#results button").first().click();
    await page.locator("#detail summary").filter({hasText:"Interior room map"}).click();
    await page.getByLabel("Interior location",{exact:true}).selectOption({label:"Exterior doorway room · b8800100"});
    const extent = page.getByLabel("Map extent",{exact:true});
    const interior = page.locator("#detail .room-map");
    const countRooms = async () => parseInt(await interior.locator("..").locator("p").innerText(),10);
    const allRooms = await countRooms();
    await extent.selectOption("near");
    assert((await countRooms()) < allRooms);
    await extent.selectOption("level");
    assert(await countRooms() > 0);
    await extent.selectOption("all");
    const inhabitant = interior.getByRole("link",{name:"Chief Benamu",exact:true}).first();
    const destination = await inhabitant.getAttribute("aria-label");
    await inhabitant.focus();
    await page.keyboard.press("Shift+F10");
    assert.equal(await page.locator("#coordinateValue").inputValue(), "b88001db -135.058319 129.428726 156.158859");
    await page.getByRole("button",{name:"Copy coordinates to clipboard"}).click();
    assert.equal(await page.evaluate(()=>window.copiedCoordinates), await inhabitant.getAttribute("data-coordinates"));
    await page.keyboard.press("Escape");
    await inhabitant.focus();
    await page.keyboard.press("Enter");
    await page.waitForFunction(name=>document.querySelector("#detail h2").textContent===name, destination);
    assert.match(await page.locator("#detail dl").innerText(),/Dungeon/);
    assert.equal(await page.getByRole("button",{name:"Locate on map",exact:true}).count(),0);
    assert(await page.evaluate(()=>markers.every(m=>m[2]!==selected)));
    await withinViewport();
    checks.push("Interior doorway, floor and occupant navigation; dungeon occupants stay off the world map");
    const retina = await browser.newPage({viewport:{width:1200,height:800},deviceScaleFactor:2});
    retina.on("pageerror", error=>failures.push(error.message));
    retina.on("requestfailed", request=>failures.push(request.url()+" "+request.failure().errorText));
    await retina.goto(pathToFileURL(path.join(root,"Docs/Atlas/index.html")).href);
    for (let i=0;i<7;i++) await retina.click("#zoomIn");
    await retina.waitForFunction(()=>!terrain.loading && !pendingFrame);
    const displayedPixels=await retina.evaluate(()=>worldSize*scale()*devicePixelRatio);
    assert(Math.abs(displayedPixels-2*physicalCoverage.scale)<1e-6,"High-DPI zoom magnifies the finest map tiles up to 2×");
    checks.push("High-DPI inspection zoom reaches twice the finest native map scale");
    await retina.close();
    assert.deepEqual(failures, []);
    const report = {result:"PASS",checks,overviewTileCount,totalRequestedTiles:tiles.size,physicalCoverage,npcCoverage};
    fs.writeFileSync(path.join(output,"result.json"), JSON.stringify(report,null,2));
    console.log(output,report);
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode=1; });
