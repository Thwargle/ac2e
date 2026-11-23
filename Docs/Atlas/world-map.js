"use strict";

// The image grid is north-up. Its west/south edges match the origin used
// when the world manifest converts positions in metres to AC2 compass units.
const worldMin = -100;
const worldSize = 256 * 8 / 10.2;
const worldMax = worldMin + worldSize;

function decodeRuns(row, width) {
  const present = new Uint8Array(width);
  if (row) for (const range of row.split(",")) {
    const [first, last = first] = range.split("-").map(Number);
    present.fill(1, first, last + 1);
  }
  return present;
}

class AtlasTerrain {
  constructor(manifest, redraw) {
    this.manifest = manifest;
    this.redraw = redraw;
    this.images = new Map();
    this.loading = 0;
    this.levels = manifest.levels.map((rows, z) => rows.map(row => decodeRuns(row, 2 ** z)));
    this.worldBlocks = manifest.worldBlocks.map(row => decodeRuns(row, 256));
  }

  mappedCell(cell) {
    const block = parseInt(cell.slice(0, 4), 16);
    return !!this.worldBlocks[block & 255][block >>> 8];
  }

  mappedCoordinates(north, east) {
    const block = v => Math.floor((v - worldMin) * 256 / worldSize);
    return !!this.worldBlocks[block(north)]?.[block(east)];
  }

  tile(z,x,y) {
    const key = z + "/" + x + "-" + y;
    const cached = this.images.get(key);
    if (cached) {
      // Map iteration follows insertion order and provides least-recently-used eviction.
      this.images.delete(key);
      this.images.set(key, cached);
      return cached;
    }
    if (this.loading >= 8) return;
    const image = new Image();
    const tile = {image, state:"loading"};
    this.images.set(key, tile);
    this.loading++;
    const finish = state => {
      tile.state = state;
      this.loading--;
      this.redraw();
    };
    image.onload = () => finish("ready");
    image.onerror = () => finish("failed");
    image.src = "tiles/" + key + ".webp";
    return tile;
  }

  draw(ctx, left, top, size, width, height, dpr) {
    const {tileSize, maxZoom, emptyColor} = this.manifest;
    const target = Math.max(0, Math.min(maxZoom, Math.ceil(Math.log2(size * dpr / tileSize))));
    ctx.fillStyle = emptyColor;
    ctx.fillRect(left, top, size, size);
    // Draw parent tiles first so a cold zoom or a pan always has geographic context.
    let visible = 0;
    for (let z = 0; z <= target; z++) {
      const count = 2 ** z, side = size / count;
      const x0 = Math.max(0, Math.floor(-left / side)), y0 = Math.max(0, Math.floor(-top / side));
      const x1 = Math.min(count, Math.ceil((width-left) / side)), y1 = Math.min(count, Math.ceil((height-top) / side));
      for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
        // Share destination edges in device pixels to prevent translucent hairlines at tile borders.
        const xPos = n => Math.round((left + n * side) * dpr) / dpr;
        const yPos = n => Math.round((top + n * side) * dpr) / dpr;
        const px = xPos(x), py = yPos(y), w = xPos(x+1)-px, h = yPos(y+1)-py;
        if (!this.levels[z][y][x]) {
          ctx.fillRect(px, py, w, h);
          continue;
        }
        visible++;
        const tile = this.tile(z,x,y);
        if (tile?.state === "ready") ctx.drawImage(tile.image, px, py, w, h);
      }
    }
    const limit = Math.max(128, visible + 32);
    if (this.images.size > limit) for (const [key,tile] of this.images) {
      if (this.images.size <= limit) break;
      if (tile.state !== "loading") this.images.delete(key);
    }
  }
}

