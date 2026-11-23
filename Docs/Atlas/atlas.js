"use strict";
const atlas = window.AC2Atlas;
const records = atlas.records;
const el = id => document.getElementById(id);
const fmt = n => n.toLocaleString();
const recordKey = r => r.kind + "/" + r.name;
const byName = new Map(records.map(r => [recordKey(r), r]));
for (const r of records) {
  for (const alias of r.facts["Guide name"] || []) byName.set(r.kind+"/"+alias, r);
}
const kinds = [...new Set(records.map(r => r.kind))];
const perPage = 50;
let matches = records, page = 0, selected, matchingSites = [], markers = [];
const fitSpan = worldSize * 1.035;
const worldMid = worldMin + worldSize / 2;
let center = [worldMid, worldMid], span = fitSpan, relatedPoints = [];
let pendingFrame = 0;
function requestMapDraw() {
  if (!pendingFrame) pendingFrame = requestAnimationFrame(() => { pendingFrame = 0; draw(); });
}
function showPane(id) {
  document.body.dataset.pane = id;
  for (const button of el("panes").children) {
    button.setAttribute("aria-pressed", String(button.dataset.show === id));
  }
}
for (const button of el("panes").children) button.onclick = () => showPane(button.dataset.show);
const review = window.AC2QuestReview?.records || {};
const canvas = el("map");
const ctx = canvas.getContext("2d");
const terrain = new AtlasTerrain(window.AC2TerrainTiles, requestMapDraw);
const dungeonBlocks = new Set(records.flatMap(r => r.interiorBlocks || []));
const onWorldGuide = p => p[4] !== "Dungeon" && p[4] !== "Disputed" && terrain.mappedCoordinates(p[1], p[2]);
const onWorldSpawn = s => s.length === 8 && terrain.mappedCell(s[2]);
const siteDistance = (a,b) => Math.hypot(a[0]-b[0],a[1]-b[1]);
// Guide coordinates are usually given to one decimal place. Allow up to half a
// compass degree (~102 m) between reports of the same outdoor NPC.
const npcGuideRadius = 0.5;
function worldSites(record) {
  const sites = (record.spawns || []).filter(onWorldSpawn).map(s => ({
    spawn:[s[6],s[7],record,false]
  }));
  const reports = record.places.filter(onWorldGuide);
  if (record.kind !== "NPC") {
    return sites.concat(reports.map(p => ({guide:[p[1],p[2],record,true]})));
  }
  const byCoordinate = new Map();
  for (const [label,n,e,source] of reports) {
    const key = n+","+e;
    if (!byCoordinate.has(key)) byCoordinate.set(key,{point:[n,e,record,true], sources:new Set(), patrol:false});
    const report = byCoordinate.get(key);
    report.sources.add(source);
    if (label === "Documented patrol location") report.patrol = true;
  }
  // Match to the nearest actual instance first. Published patrol stops each
  // keep their own site even if a single captured position is close to both.
  const candidates = [...byCoordinate.values()].sort((a,b) =>
    Number(b.patrol)-Number(a.patrol) || b.sources.size-a.sources.size ||
    a.point[0]-b.point[0] || a.point[1]-b.point[1]);
  const guideOnly = [];
  for (const report of candidates) {
    const p = report.point;
    let nearest, distance = npcGuideRadius;
    for (const site of sites) {
      if (report.patrol && site.patrol) continue;
      const d = siteDistance(p,site.spawn);
      if (d <= distance) { nearest=site; distance=d; }
    }
    if (nearest) {
      if (report.patrol) nearest.patrol = true;
      if (!nearest.guide || distance < siteDistance(nearest.guide,nearest.spawn)) nearest.guide=p;
      continue;
    }
    // Require agreement with every coordinate in the group; a chain of
    // intermediate reports must not join two distant locations.
    const group = guideOnly.find(g => !(report.patrol && g.patrol) &&
      g.members.every(m => siteDistance(m,p) <= npcGuideRadius));
    if (group) group.members.push(p);
    else guideOnly.push({guide:p,patrol:report.patrol,members:[p]});
  }
  return sites.concat(guideOnly);
}
const sitesByRecord = new Map(records.map(r => [r,worldSites(r)]));
function spawnRegion(s) {
  if (s.length === 8) return onWorldSpawn(s) ? "Overworld" : "Off the world map";
  return dungeonBlocks.has(s[2].slice(0, 4)) || !terrain.mappedCell(s[2]) ? "Dungeon" : "Interior";
}
for (const record of records) {
  const regions = new Set((record.spawns || []).filter(s => s.length === 6).map(spawnRegion));
  if (record.kind === "Dungeon" || record.places.some(p => p[4] === "Dungeon")) regions.add("Dungeon");
  for (const region of regions) {
    const values = record.facts.Region ??= [];
    if (!values.includes(region)) values.push(region);
  }
}
const node = (tag, value) => {
  const result = document.createElement(tag);
  if (value !== undefined) result.textContent = value;
  return result;
};
function initMarkerSettings() {
  const storageKey = "ac2e.atlas.hiddenMarkerKinds.v1";
  const mapKinds = [...new Set(records
    .filter(r => r.places.some(onWorldGuide) || r.spawns?.some(onWorldSpawn))
    .map(r => r.kind))].sort();
  let saved;
  try { saved = JSON.parse(localStorage.getItem(storageKey)); } catch { /* Storage is optional. */ }
  const hidden = new Set(Array.isArray(saved) ? mapKinds.filter(kind => saved.includes(kind)) : []);
  const all = el("allMarkerKinds");
  const container = el("markerKinds");
  function syncAll() {
    all.checked = hidden.size === 0;
    all.indeterminate = hidden.size > 0 && hidden.size < mapKinds.length;
    all.disabled = mapKinds.length === 0;
  }
  function apply() {
    syncAll();
    try { localStorage.setItem(storageKey, JSON.stringify([...hidden])); } catch { /* Keep session settings. */ }
    requestMapDraw();
  }
  for (const kind of mapKinds) {
    const input = node("input");
    input.type = "checkbox";
    input.value = kind;
    input.checked = !hidden.has(kind);
    const label = node("label");
    label.append(input, kind);
    container.append(label);
  }
  container.onchange = ({target}) => {
    if (target.checked) hidden.delete(target.value);
    else hidden.add(target.value);
    apply();
  };
  all.onchange = () => {
    hidden.clear();
    for (const input of container.querySelectorAll("input")) {
      input.checked = all.checked;
      if (!input.checked) hidden.add(input.value);
    }
    apply();
  };
  syncAll();
  el("mapSettings").onclick = () => el("markerSettings").showModal();
  return hidden;
}
const hiddenMarkerKinds = initMarkerSettings();
const coords = (n,e) => Math.abs(n).toFixed(2)+(n<0?" S":" N")+", "+Math.abs(e).toFixed(2)+(e<0?" W":" E");
function guideLink(path,label) {
  const a = node("a",label || path);
  const url = new URL(path,atlas.source);
  if (url.origin === new URL(atlas.source).origin) a.href = url.href;
  a.target="_blank"; a.rel="noopener noreferrer";
  return a;
}
function detailGroup(parent, title, items, render) {
  if (!items.length) return;
  const group = node("details"), list = node("ul");
  group.append(node("summary",title+" ("+fmt(items.length)+")"));
  group.addEventListener("toggle", () => {
    if (!group.open || list.childElementCount) return;
    for (const item of items) {
      const entry=node("li"); entry.append(render(item)); list.append(entry);
    }
  });
  group.append(list); parent.append(group);
}
function testEvidence(value) {
  const match = /^(Network\/test_[a-z0-9_]+\.py) \(([-\w]+)\)$/.exec(value);
  if (!match) return node("span",value);
  const entry = node("span"), link = node("a",match[1].slice("Network/".length));
  link.href = "../../" + match[1];
  entry.append(link,document.createElement("br"),node("code",match[2]));
  return entry;
}
function readSelection() {
  let key;
  try { key = decodeURIComponent(location.hash.slice(1)); } catch { key = ""; }
  selected = byName.get(key);
  relatedPoints = [];
  const panel=el("detail");panel.replaceChildren();panel.scrollTop=0;
  if (!selected) {
    panel.append(node("h2","Quest review"));
    const counts = window.AC2QuestReview?.indexedSummary;
    if (counts) {
      const total = Object.values(counts).reduce((sum,n)=>sum+n,0);
      panel.append(node("p",fmt(total)+" quests in the AC2Hero guide index."));
      const list=node("dl");
      for (const [status,n] of Object.entries(counts).sort(([a],[b])=>a.localeCompare(b))) {
        const term=node("dt"), button=node("button",status);
        button.className="cross";
        button.onclick=()=>{
          el("kind").value="Quest";
          el("search").value="";
          el("coverage").value="indexed:"+status;
          filter();showPane("catalog");
        };
        term.append(button);list.append(term,node("dd",fmt(n)));
      }
      panel.append(list,node("p",window.AC2QuestReview.scope));
    }
    panel.append(node("p","Select a record or a point on the map to see its sources and locations."));
  } else {
    const r=selected, facts=node("dl");
    panel.append(node("small",r.kind),node("h2",r.name));
    if (r.ids?.length) {
      facts.append(node("dt","Native IDs"),node("dd",r.ids.join(", ")));
    }
    for (const [label, values] of Object.entries(r.facts)) {
      const value=node("dd");
      if (label==="Map image") {
        for (const path of values) value.append(guideLink(path,"Open floor plan"),document.createElement("br"));
      } else value.textContent=values.join(" · ");
      facts.append(node("dt",label),value);
    }
    const audit = review[recordKey(r)];
    if (audit) {
      facts.append(node("dt","Gameplay review"),node("dd",audit.status));
    }
    panel.append(facts);
    if (audit) {
      detailGroup(panel,"Checks performed",audit.checks || [],s=>node("span",s));
      detailGroup(panel,"Remaining steps",audit.blockers || [],s=>node("span",s));
      detailGroup(panel,"Test evidence",audit.tests || [],testEvidence);
    }
    addRoomMap(panel, r, byName);
    detailGroup(panel,"Guide pages",r.sources,p=>guideLink(p));
    detailGroup(panel,"Guide locations",r.places,p=>{
      const entry=node("span",[p[0],p[4],coords(p[1],p[2])].filter(Boolean).join(" · "));
      if (p[3]) entry.append(" · ",guideLink(p[3],"Guide page"));
      return entry;
    });
    detailGroup(panel,"Prepared placements",r.spawns || [],p=>{
      const entry=node("span",(onWorldSpawn(p)?coords(p[6],p[7]):spawnRegion(p))+
        " · cell "+p[2]+" · local ("+p.slice(3,6).join(", ")+") m · instance "+p[0]);
      const evidence=r.placementEvidence?.[p[0]];
      if (evidence) {
        const [pack,basis,guide]=evidence, source=node("a","Placement source");
        source.href="../../AC2E/emulator/content/"+encodeURIComponent(pack);
        entry.append(document.createElement("br"),basis+" ",source);
        if (guide) {
          const link=node("a","Guide");
          link.href=guide;
          entry.append(" · ",link);
        }
      }
      return entry;
    });
    detailGroup(panel,"Observed corpse bundles",r.bundles || [],([id,count,items,areas])=>
      node("span",items.join(", ")+" · "+fmt(count)+" observations for "+id+
        (areas && areas!=="-"?" · landblock or room counts "+areas:"")));
    detailGroup(panel,"Native generic treasure limits",r.lootLimits || [],([id,min,max,level])=>
      node("span",min+"–"+max+" slots · treasure level "+level+" · "+id));
    detailGroup(panel,"Native trophy rolls",r.trophies || [],([id,slot,item,chance,source])=>
      node("span",item+" · "+chance+"% · slot "+slot+" on "+id+" · "+source));
    detailGroup(panel,"Native store offers",r.offers || [],([store,ordinal,item,price,currency])=>
      node("span",item+" · "+price.toLocaleString()+" "+currency+" per unit · "+store+" #"+ordinal));
    detailGroup(panel,"Related records",r.links,([role,kind,name])=>{
      const wrap=node("span"), a=node("button",name);
      a.className="cross";a.onclick=()=>choose(byName.get(kind+"/"+name));wrap.append(a," · "+role);return wrap;
    });
    const sites=sitesByRecord.get(r);
    const worldPosition=r.kind==="NPC"
      ? sites.find(s=>s.spawn)?.spawn || sites[0]?.guide
      : r.places.find(onWorldGuide)?.slice(1,3) || r.spawns?.find(onWorldSpawn)?.slice(6);
    if (worldPosition) {
      const focus=node("button","Locate on map");
      focus.onclick=()=>{
        showPane("geography");center=worldPosition.slice(0,2);span=16;requestMapDraw();
      };
      panel.insertBefore(focus, facts);
    }
    const mapRoles = {
      Item: ["Observed loot", "Native trophy", "2017 DAT trophy", "Glyph source", "Guide trophy source", "Quest-only drop", "Quest item source", "Native store offer", "Commodity trade", "Butchery material"],
      Quest: ["Quest giver", "Quest starter", "Quest target", "Quest objective", "Quest recipient"],
      Recipe: ["Required workshop"],
      Dungeon: ["Server portal"]
    }[r.kind];
    if (mapRoles) {
      const locations = new Set();
      const actorLinks = r.links.slice();
      if (r.kind==="Quest") {
        for (const [role,kind,name] of r.links) {
          if (role==="Server quest identity") actorLinks.push(...(byName.get(kind+"/"+name)?.links || []));
        }
      }
      for (const [role,kind,name] of actorLinks) {
        if (!mapRoles.includes(role)) continue;
        const other = byName.get(kind+"/"+name);
        for (const site of sitesByRecord.get(other) || []) {
          if (site.spawn) locations.add(site.spawn);
        }
      }
      if (locations.size) {
        const label = {Item:r.links.some(([role])=>["Native store offer","Commodity trade","Quest item source"].includes(role))
          ?"Map item sources":"Map carrier locations",Quest:"Map quest actors",Dungeon:"Map entrance locations",Recipe:"Map workshops"}[r.kind];
        const show=node("button",label);
        show.onclick=()=>{
          relatedPoints=[...locations];
          const ns=relatedPoints.map(p=>p[0]),ew=relatedPoints.map(p=>p[1]);
          const south=Math.min(...ns),north=Math.max(...ns),west=Math.min(...ew),east=Math.max(...ew);
          center=[(north+south)/2,(east+west)/2];
          showPane("geography");
          const {width:w,height:h}=canvas.getBoundingClientRect(),short=Math.min(w,h);
          span=Math.min(fitSpan,Math.max(3,1.2*Math.max((north-south)*short/h,(east-west)*short/w)));
          requestMapDraw();
        };
        panel.insertBefore(show,facts);
      }
    }
  }
  listResults();draw();
}
function choose(record) {
  if (!record) return;
  location.hash=encodeURIComponent(recordKey(record));
  showPane("detail");
}
function listResults() {
  el("count").textContent=fmt(matches.length)+(matches.length===1?" record":" records");
  const list = el("results");list.replaceChildren();
  for (const r of matches.slice(page*perPage,(page+1)*perPage)) {
    const b=node("button",r.name); b.append(node("small",r.kind));b.onclick=()=>choose(r);
    b.setAttribute("aria-current",String(r===selected));list.append(b);
  }
  el("page").textContent=matches.length?(page+1)+" / "+Math.ceil(matches.length/perPage):"0 / 0";
  el("prev").disabled=page===0;el("next").disabled=(page+1)*perPage>=matches.length;
}
function hasCoverage(r, coverage) {
  if (coverage.startsWith("indexed:")) return review[recordKey(r)]?.indexed && review[recordKey(r)].status === coverage.slice(8);
  if (coverage.startsWith("tested:")) return review[recordKey(r)]?.status === coverage.slice(7);
  switch (coverage) {
    case "": return true;
    case "quest": return !!r.facts["Quest policy"];
    case "questPoints": return !!r.facts["Documented quest points"] || !!r.facts["Guide quest points"];
    case "index": return r.kind==="Quest" && !!r.facts["Guide section"];
    case "questSteps": return !!r.facts["Quest route gap"] || !!r.facts["Partial quest support"];
    case "questGap": return r.kind==="Quest" && r.sources.length>0 && !r.facts["Quest policy"];
    case "unplaced": return !r.spawns?.length;
    case "unplacedActor": return ["NPC","Creature"].includes(r.kind) && r.sources.length>0 && !r.spawns?.length;
    default: return !!r[coverage]?.length;
  }
}
function filter() {
  const query=el("search").value.trim().toLowerCase(), kind=el("kind").value, coverage=el("coverage").value;
  matches=records.filter(r=>(!kind || r.kind===kind) &&
    (!query || r.name.toLowerCase().includes(query) || r.ids?.some(i=>i.includes(query.replace(/^0x/,""))) ||
      ["Habitat","Region","Continent","Guide name"].some(k=>r.facts[k]?.some(value=>value.toLowerCase().includes(query)))) &&
    hasCoverage(r,coverage));
  matchingSites=matches.flatMap(r=>sitesByRecord.get(r));
  page=0;listResults();draw();
}
function draw() {
  const {width:w,height:h}=canvas.getBoundingClientRect();
  if (!w || !h) return;
  const dpr=window.devicePixelRatio || 1;
  span=clampSpan(span,Math.min(w,h),dpr);
  if (canvas.width!==Math.round(w*dpr) || canvas.height!==Math.round(h*dpr)) {
    canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);
  }
  ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
  const scale=Math.min(w,h)/span;
  const x=e=>w/2+(e-center[1])*scale, y=n=>h/2-(n-center[0])*scale;
  terrain.draw(ctx,x(worldMin),y(worldMax),worldSize*scale,w,h,dpr);
  const step=[.1,.25,.5,1,2,5,10,20,50,100].find(n=>n*scale>=74) || 100;
  ctx.font="11px sans-serif";ctx.fillStyle="#c0cfc9";ctx.strokeStyle="rgba(37,60,62,.55)";ctx.lineWidth=1;
  for (let i=Math.ceil(worldMin/step);i<=Math.floor(worldMax/step);i++) {
    const c=Number((i*step).toFixed(2));
    if (x(c)>=0 && x(c)<=w) {ctx.beginPath();ctx.moveTo(x(c),0);ctx.lineTo(x(c),h);ctx.stroke();ctx.fillText(Math.abs(c)+(c<0?"W":"E"),x(c)+2,h-5);}
    if (y(c)>=0 && y(c)<=h) {ctx.beginPath();ctx.moveTo(0,y(c));ctx.lineTo(w,y(c));ctx.stroke();ctx.fillText(Math.abs(c)+(c<0?"S":"N"),3,y(c)-2);}
  }
  markers=[];
  // At continental scales many independent spawns project onto the same few pixels.
  const overviewCells = new Set();
  function point(p,selectedPoint,related=false) {
    if (hiddenMarkerKinds.has(p[2].kind)) return;
    const px=x(p[1]),py=y(p[0]);
    if (px<0 || px>w || py<0 || py>h) return;
    if (!selectedPoint && span > 50) {
      const key = Math.floor(px/5) + "," + Math.floor(py/5) + "," + p[3];
      if (overviewCells.has(key)) return;
      overviewCells.add(key);
    }
    markers.push([px,py,p[2]]);
    ctx.fillStyle=related?"#f88d73":selectedPoint?"#ffd385":p[3]?"#c494d9":"#7cc2ae";
    ctx.globalAlpha=selectedPoint || related ? 1 : p[3] ? 0.68 : 0.42;
    ctx.beginPath();ctx.arc(px,py,selectedPoint?4.8:span>50?1.3:p[3]?2.4:1.7,0,Math.PI*2);ctx.fill();
    if (selectedPoint) {ctx.strokeStyle="#1e2628";ctx.stroke();}
  }
  const showSpawns=el("spawnLayer").checked, showGuide=el("guideLayer").checked;
  const visiblePoint = site => (showSpawns && site.spawn) || (showGuide && site.guide);
  const highlighted = new Set(showSpawns ? relatedPoints : []);
  for (const site of matchingSites) {
    const p=visiblePoint(site);
    if (p && p[2]!==selected && !highlighted.has(p)) point(p,false);
  }
  for (const p of highlighted) point(p,true,true);
  for (const site of sitesByRecord.get(selected) || []) {
    const p=visiblePoint(site);
    if (p && !highlighted.has(p)) point(p,true);
  }
  ctx.globalAlpha=1;
}
function scale() {
  const {width,height}=canvas.getBoundingClientRect();
  return Math.min(width,height)/span;
}
function clampCenter(n,e) {
  center=[n,e].map(value=>Math.max(worldMin,Math.min(worldMax,value)));
}
function clampSpan(value,shortEdge,dpr) {
  const pixelsAcrossWorld=terrain.manifest.tileSize*2**terrain.manifest.maxZoom;
  const nativeSpan=shortEdge*dpr*worldSize/pixelsAcrossWorld;
  // Allow one final 2x inspection step beyond the finest raster. Markers and the grid stay vector-sharp.
  return Math.min(fitSpan,Math.max(nativeSpan/2,value));
}
function zoom(multiplier,px,py) {
  const {width,height}=canvas.getBoundingClientRect();
  const size=Math.min(width,height);
  px ??= width/2;
  py ??= height/2;
  if (!size) return;
  const next=clampSpan(span*multiplier,size,window.devicePixelRatio || 1);
  clampCenter(center[0]-(py-height/2)*(span-next)/size,
              center[1]+(px-width/2)*(span-next)/size);
  span=next;requestMapDraw();
}
function resetMap() {
  center=[worldMid,worldMid];span=fitSpan;relatedPoints=[];requestMapDraw();
}
el("zoomIn").onclick=()=>zoom(.5);el("zoomOut").onclick=()=>zoom(2);el("reset").onclick=resetMap;
for (const id of ["spawnLayer","guideLayer"]) el(id).onchange=requestMapDraw;
canvas.addEventListener("wheel",e=>{
  e.preventDefault();
  const r=canvas.getBoundingClientRect(), delta=e.deltaY*(e.deltaMode===1?16:e.deltaMode===2?canvas.getBoundingClientRect().height:1);
  zoom(2**(Math.max(-300,Math.min(300,delta))/300),e.clientX-r.left,e.clientY-r.top);
},{passive:false});
canvas.onkeydown=e=>{
  if (e.key==="ContextMenu" || (e.shiftKey && e.key==="F10")) {
    e.preventDefault();
    const rect=canvas.getBoundingClientRect();
    worldCoordinatesAt(rect.left+rect.width/2,rect.top+rect.height/2);
    return;
  }
  const shift=span*.1;
  switch (e.key) {
    case "+":case "=":zoom(.5);break;
    case "-":zoom(2);break;
    case "Home":resetMap();break;
    case "ArrowUp":clampCenter(center[0]+shift,center[1]);break;
    case "ArrowDown":clampCenter(center[0]-shift,center[1]);break;
    case "ArrowRight":clampCenter(center[0],center[1]+shift);break;
    case "ArrowLeft":clampCenter(center[0],center[1]-shift);break;
    default:return;
  }
  e.preventDefault();requestMapDraw();
};
const pointers=new Map();
let gestureMoved=false;
function pointerPosition(e) {
  const rect=canvas.getBoundingClientRect();return [e.clientX-rect.left,e.clientY-rect.top];
}
const coordinateMenu=el("coordinateMenu"), coordinateValue=el("coordinateValue");
function placeCoordinateMenu(clientX,clientY) {
  const bounds=coordinateMenu.getBoundingClientRect();
  coordinateMenu.style.left=Math.max(4,Math.min(clientX,innerWidth-bounds.width-4))+"px";
  coordinateMenu.style.top=Math.max(4,Math.min(clientY,innerHeight-bounds.height-4))+"px";
}
function openCoordinates(clientX,clientY,value) {
  coordinateValue.value=value;
  el("copyStatus").textContent="";
  coordinateMenu.showPopover();
  placeCoordinateMenu(clientX,clientY);
  el("copyCoordinates").focus({preventScroll:true});
}
el("copyCoordinates").onclick=async () => {
  let copied=false;
  try {
    await navigator.clipboard.writeText(coordinateValue.value);
    copied=true;
  } catch {
    coordinateValue.focus();
    coordinateValue.select();
    try { copied=document.execCommand("copy"); } catch {}
  }
  el("copyStatus").textContent=copied ? "Copied" : "Press Ctrl+C or ⌘C to copy the selected coordinates.";
  const bounds=coordinateMenu.getBoundingClientRect();
  placeCoordinateMenu(bounds.left,bounds.top);
};
function worldCoordinatesAt(px,py) {
  const rect=canvas.getBoundingClientRect();
  const n=center[0]-(py-rect.top-rect.height/2)/scale();
  const e=center[1]+(px-rect.left-rect.width/2)/scale();
  if ([n,e].some(v=>v<worldMin || v>worldMax)) return;
  // Match FAC2MapData::Coordinates, including precision and axis order.
  openCoordinates(px,py,Math.abs(n).toFixed(1)+(n<0?"S":"N")+", "+Math.abs(e).toFixed(1)+(e<0?"W":"E"));
}
canvas.addEventListener("contextmenu",event=>{
  event.preventDefault();
  worldCoordinatesAt(event.clientX,event.clientY);
});
canvas.onpointerdown=e=>{
  if (e.button!==0) return;
  const pos=pointerPosition(e);
  gestureMoved=pointers.size>0;
  pointers.set(e.pointerId,{pos,start:pos});
  canvas.setPointerCapture(e.pointerId);
};
canvas.onpointermove=e=>{
  const current=pointers.get(e.pointerId);
  if (!current) return;
  const pos=pointerPosition(e), old=current.pos;
  const other=[...pointers.values()].find(p=>p!==current);
  if (other) {
    const midpoint=other.pos.map((v,i)=>(v+old[i])/2);
    const newDistance=Math.hypot(pos[0]-other.pos[0],pos[1]-other.pos[1]);
    const oldDistance=Math.hypot(old[0]-other.pos[0],old[1]-other.pos[1]);
    if (newDistance && oldDistance) zoom(oldDistance/newDistance,...midpoint);
  } else if (Math.hypot(pos[0]-current.start[0],pos[1]-current.start[1])>3) gestureMoved=true;
  if (gestureMoved) {
    const rate=scale()*(other?2:1);
    clampCenter(center[0]+(pos[1]-old[1])/rate,center[1]-(pos[0]-old[0])/rate);
    requestMapDraw();
  }
  current.pos=pos;
};
canvas.onlostpointercapture=e=>{pointers.delete(e.pointerId);};
canvas.onpointerup=e=>{
  if (!pointers.delete(e.pointerId) || gestureMoved) return;
  const [px,py]=pointerPosition(e);let nearest,distance=64;
  for (const [x,y,record] of markers) {
    const d=(x-px)**2+(y-py)**2;if(d<=distance){nearest=record;distance=d;}
  }
  choose(nearest);
};
el("prev").onclick=()=>{page--;listResults();el("results").scrollTop=0;};
el("next").onclick=()=>{page++;listResults();el("results").scrollTop=0;};
el("summary").textContent=fmt(records.length)+" records · "+fmt(atlas.pages.pages.length)+" guide pages · "+
 fmt(records.reduce((n,r)=>n+(r.spawns?.length || 0),0))+" prepared placements";
for (const status of [...new Set(Object.values(review).map(r=>r.status))].sort())
  el("coverage").add(new Option(status, "tested:"+status));
if (window.AC2QuestReview?.indexedSummary) {
  const group = node("optgroup");
  group.label = "Guide index";
  for (const status of Object.keys(window.AC2QuestReview.indexedSummary).sort())
    group.append(new Option("Guide · "+status,"indexed:"+status));
  el("coverage").append(group);
}
for(const kind of kinds.sort()) el("kind").add(new Option(kind,kind));
for(const id of ["search","coverage","kind"]) el(id).addEventListener("input",filter);
window.addEventListener("hashchange",readSelection);
new ResizeObserver(draw).observe(canvas);
filter(); readSelection();
if (selected) showPane("detail");
