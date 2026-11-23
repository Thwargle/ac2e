"use strict";

// Room centers and doorways are in native landblock-local coordinates.
const occupantsByRoom = new Map();
const exteriorRoomsByBlock = new Map();
for (const [cell, room] of Object.entries(window.AC2Rooms)) {
  if (!room[4]) continue;
  const block=cell.slice(0,4);
  if (!exteriorRoomsByBlock.has(block)) exteriorRoomsByBlock.set(block, []);
  exteriorRoomsByBlock.get(block).push(cell);
}
for (const record of window.AC2Atlas.records) {
  for (const s of record.spawns || []) {
    if (!window.AC2Rooms[s[2]]) continue;
    if (!occupantsByRoom.has(s[2])) occupantsByRoom.set(s[2], []);
    occupantsByRoom.get(s[2]).push({record, cell:s[2], x:s[3], y:s[4], z:s[5]});
  }
}

function addRoomMap(parent, record, byName) {
  const rooms = window.AC2Rooms;
  const entries = new Map();
  function addRecord(other, useSpawns=true, blocks=null) {
    for (const [cell,x,y,z] of other.arrivals || []) {
      if (rooms[cell] && (!blocks || blocks.has(cell.slice(0,4)))) entries.set(cell, {x,y,z,label:"Portal arrival · "+cell});
    }
    if (useSpawns) for (const s of other.spawns || []) {
      const cell=s[2];
      if (rooms[cell] && !entries.has(cell) && (!blocks || blocks.has(cell.slice(0,4)))) {
        entries.set(cell, {x:s[3], y:s[4], z:s[5], label:other.name+" · "+cell});
      }
    }
  }
  addRecord(record);
  for (const [role, kind, name] of record.links) {
    if (record.kind==="Dungeon" && role==="Server portal") {
      const other=byName.get(kind+"/"+name);
      if (other) addRecord(other,false);
    }
  }
  // Walk-in dungeons have captured interiors even when there is no teleport arrival.
  if (record.kind==="Dungeon") {
    const blocks=new Set([...(record.interiorBlocks || []), ...[...entries.keys()].map(cell=>cell.slice(0,4))]);
    for (const [role, kind, name] of record.links) {
      if (!["Floor-plan encounter","Captured dungeon encounter","Surface exit"].includes(role)) continue;
      const other=byName.get(kind+"/"+name);
      if (other) addRecord(other,true,blocks);
    }
    const outside=[...blocks].flatMap(block=>exteriorRoomsByBlock.get(block) || []);
    if (outside.length) {
      const connected=new Set(entries.keys());
      for (const cell of connected) for (const next of rooms[cell][3]) connected.add(next);
      for (const cell of outside) {
        if (!connected.has(cell) || entries.has(cell)) continue;
        const [x,y,z]=rooms[cell];
        entries.set(cell,{x,y,z,label:"Exterior doorway room · "+cell});
      }
    }
  }
  if (!entries.size) return;
  const details=node("details"), picker=node("select"), scope=node("select"), view=node("div");
  details.append(node("summary","Interior room map"));
  picker.setAttribute("aria-label","Interior location");
  for (const [cell, place] of entries) picker.add(new Option(place.label,cell));
  scope.setAttribute("aria-label","Map extent");
  scope.add(new Option("Connected interior","all"));
  scope.add(new Option("Within two doorways","near"));
  scope.add(new Option("Within 4 m of selected height","level"));
  details.append(picker,scope,view);
  const svgTag = (name,attributes) => {
    const e=document.createElementNS("http://www.w3.org/2000/svg",name);
    for (const [key,value] of Object.entries(attributes)) e.setAttribute(key,String(value));
    return e;
  };
  function render() {
    const start=picker.value, origin=entries.get(start), visited=new Set([start]);
    let front=[start], depth=0;
    while (front.length && (scope.value!=="near" || depth<2)) {
      const next=[];
      for (const id of front) for (const neighbour of rooms[id][3]) {
        if (!visited.has(neighbour)) {visited.add(neighbour);next.push(neighbour);}
      }
      front=next;depth++;
    }
    if (scope.value==="level") for (const id of visited) {
      if (Math.abs(rooms[id][2]-rooms[start][2])>4) visited.delete(id);
    }
    const points=[...visited].map(id=>rooms[id]);
    const occupants=[...visited].flatMap(id=>occupantsByRoom.get(id) || []);
    const xs=[origin.x,...points.map(p=>p[0]),...occupants.map(p=>p.x)];
    const ys=[origin.y,...points.map(p=>p[1]),...occupants.map(p=>p.y)];
    const left=Math.min(...xs), top=Math.max(...ys);
    const scale=280/Math.max(16,Math.max(...xs)-left,top-Math.min(...ys));
    const x=v=>10+(v-left)*scale, y=v=>10+(top-v)*scale;
    const svg=svgTag("svg",{viewBox:"0 0 300 300",class:"room-map",role:"group",tabindex:0,
      "aria-label":"Interior map, north up. Left and right arrows browse rooms. Shift+F10 opens coordinates."});
    const roomMarkers=[];
    let doors="";
    for (const id of visited) {
      const [a,b,z,links]=rooms[id];
      roomMarkers.push(svgTag("circle",{cx:x(a),cy:y(b),r:1.4,fill:"#d0d7cd",stroke:"transparent","stroke-width":2,
        tabindex:-1,"aria-label":"Room "+id,"data-coordinates":[id,a,b,z].join(" ")}));
      for (const n of links) if (n>id && visited.has(n)) doors+="M"+x(a)+" "+y(b)+"L"+x(rooms[n][0])+" "+y(rooms[n][1]);
    }
    svg.append(svgTag("path",{d:doors,fill:"none",stroke:"#718c8c","stroke-width":1}),...roomMarkers);
    for (const p of occupants) {
      const a=svgTag("a",{href:"#"+encodeURIComponent(p.record.kind+"/"+p.record.name),"aria-label":p.record.name,
        "data-coordinates":[p.cell,p.x,p.y,p.z].join(" ")});
      const title=svgTag("title",{});title.textContent=p.record.name+" · "+p.z+" m";
      a.append(title,svgTag("circle",{cx:x(p.x),cy:y(p.y),r:2.8,fill:"#7cc2ae"}));svg.append(a);
    }
    const target=svgTag("circle",{cx:x(origin.x),cy:y(origin.y),r:5,fill:"none",stroke:"#ffd385","stroke-width":2,
      "aria-label":origin.label,"data-coordinates":[start,origin.x,origin.y,origin.z].join(" ")});
    svg.append(target);
    svg.oncontextmenu=event=>{
      let marker=event.target.closest("[data-coordinates]");
      // The gold ring is hollow so its center can also hold a clickable inhabitant.
      if (!marker) {
        const box=target.getBoundingClientRect();
        if (Math.hypot(event.clientX-box.left-box.width/2,event.clientY-box.top-box.height/2)<=box.width/2) marker=target;
      }
      if (!marker) return;
      event.preventDefault();
      openCoordinates(event.clientX,event.clientY,marker.dataset.coordinates);
    };
    svg.onkeydown=event=>{
      if (event.key==="ContextMenu" || (event.shiftKey && event.key==="F10")) {
        event.preventDefault();
        const marker=event.target.closest("[data-coordinates]") || target;
        const box=marker.getBoundingClientRect();
        openCoordinates(box.left+box.width/2,box.top+box.height/2,marker.dataset.coordinates);
      } else if (event.key==="ArrowRight" || event.key==="ArrowLeft") {
        const current=roomMarkers.indexOf(event.target);
        if (current<0 && event.target!==svg) return;
        event.preventDefault();
        const next=event.key==="ArrowRight" ? current+1 : (current<0 ? -1 : current-1);
        roomMarkers[(next+roomMarkers.length)%roomMarkers.length].focus({preventScroll:true});
      }
    };
    view.replaceChildren(svg,node("p",visited.size+" rooms · "+occupants.length+" prepared placements. Gold marks the selected location. Green points open their records. Right-click a marker to copy its cell and local coordinates."));
  }
  details.ontoggle=()=>{if (details.open && !view.childElementCount) render();};
  picker.onchange=scope.onchange=render;
  parent.append(details);
}
