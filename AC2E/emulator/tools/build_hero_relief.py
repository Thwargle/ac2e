"""Build the offline quest atlas terrain pyramid from the installed retail DATs.

Use: python build_hero_relief.py <DAT directory> Docs/Atlas/terrain.png
Requires NumPy and Pillow with WebP support. All inputs are opened read-only.
"""
import argparse
from collections import Counter, defaultdict
from contextlib import ExitStack
from functools import lru_cache
import json
import math
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

from preview_animation import decode_rig, transform_matrix
from preview_assets import Assets, setup_meshes
from preview_material import material_info
from preview_mesh import decode_mesh
from preview_terrain import landscape_defs, region_references, surface_types, terrain_types
from world_terrain import decode_block

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "Tools" / "AC2"), str(ROOT / "Network")]
from content_profile import ContentProfile
from dat_probe import Archive
from population_probe import Reader, entity


TILE = 256
MAX_ZOOM = 6
BLOCKS = 256  # Includes the unpopulated far north and east strips in the atlas extent.
OCEAN = (22, 48, 58)
OCEAN_HEX = "#16303a"
NO_TERRAIN = 30


def world_map_blocks(portal, refs):
    """Read the same detail sheets and x-major block index as FAC2MapData::At."""
    did = refs["detailmap"][0][0]
    sheet = Reader(portal.read(did))
    if sheet.word() != did:
        raise ValueError("World map descriptor identity mismatch")
    block_map = sheet.word()
    values = []
    for _ in range(sheet.word()):
        values.append(sheet.word())
        sheet.take(20)  # Image DID, origin and dimensions in landblocks.
        sheet.take(4 * sheet.word())  # Note tables.
    data = portal.read(block_map)
    ident, h, w = struct.unpack_from("<III", data)
    if (ident != block_map or not (0 < h <= BLOCKS and 0 < w <= BLOCKS)
            or len(data) < 12 + w*h or sheet.pos != len(sheet.data)):
        raise ValueError("Invalid world map block index")
    indices = np.frombuffer(data, np.uint8, w*h, 12).reshape(w, h)
    coverage = np.zeros((BLOCKS,BLOCKS), bool)
    coverage[:w,:h] = np.isin(indices, values)
    return coverage


def water_planes(portal, refs, height_table):
    """CWaterDesc and its two native x-major maps, as in FAC2WaterSource."""
    did = refs["water"][0][0]
    r = Reader(portal.read(did))
    r.word()
    if r.word() != did:
        raise ValueError("Water descriptor identity mismatch")

    def byte_map(did):
        data = portal.read(did)
        ident, h, w = struct.unpack_from("<III", data)
        if ident != did or not 0 < w <= BLOCKS or not 0 < h <= BLOCKS:
            raise ValueError("Invalid water map")
        return np.frombuffer(data, np.uint8, w * h, 12).reshape(w, h)

    levels, types = byte_map(r.word()), byte_map(r.word())

    def read_type():
        index, present = r.word(), r.word() == 0
        if present:
            r.take(24)
            for _ in range(r.word()):
                r.take(44)
                combine = r.word()
                r.take(8 * sum((combine >> (n * 3)) & 7 == 1 for n in range(7)))
        return index, present

    default = read_type()[1] if r.word() else False
    wet = np.full(256, default)
    for _ in range(r.word()):
        index, present = read_type()
        wet[index] = present
    if r.pos != len(r.data) or levels.shape != types.shape:
        raise ValueError("Incomplete water descriptor or inconsistent water maps")
    result = np.full((BLOCKS, BLOCKS), -np.inf, dtype=np.float32)
    w, h = levels.shape
    result[:w, :h] = np.where(wet[types], height_table[levels] + .95, -np.inf)
    return result


def terrain_materials(portal, refs, terrain_bits, metres_per_pixel):
    """Combine far-surface color with the native ground material at map pixel density."""
    terrain, default = terrain_types(portal.read(refs["terrain"][0][0]))
    descriptor = surface_types(portal.read(refs["surface"][0][0]))
    surfaces = descriptor["surfaces"]
    palette = np.empty((1 << terrain_bits, 256, 3), np.float32)
    palette[:] = (90, 86, 69)
    material_ids = np.zeros(palette.shape[:2], np.uint8)
    details = [np.zeros((8,8,3), np.float32)]

    @lru_cache(maxsize=None)
    def detail(did, tiling):
        info = material_info(portal, did, max_size=16)
        if "image" not in info:
            return 0, None
        source = info["image"]
        tile = Image.frombytes("RGB", (source["width"],source["height"]), source["rgb"])
        period = round(tiling / metres_per_pixel)
        if period < 1 or 8 % period or not math.isclose(period*metres_per_pixel, tiling):
            raise ValueError(f"Terrain material {did:08x} has unsupported map tiling {tiling:g} m")
        pixels = np.array(tile.resize((period,period), Image.Resampling.BOX), np.float32)
        mean = pixels.mean(axis=(0,1))
        details.append(np.tile(pixels-mean, (8//period,8//period,1)))
        return len(details)-1, mean

    for kind in range(len(palette)):
        typ = terrain.get(kind, default)
        groups = surfaces.get(typ["surface"], []) if typ else []
        # The first matching native slope interval wins. Several flat surfaces
        # also have an overlapping full-range catchall for the steeper terrain.
        for group in reversed(groups):
            if not group["colors"]:
                continue
            colors = [[(row["far"] >> shift) & 255 for shift in (16, 8, 0)]
                      for row in group["colors"]]
            weights = [struct.unpack("<f", struct.pack("<I", row["variation"][-1]))[0]
                       for row in group["colors"]]
            avg = np.average(colors, weights=weights if sum(weights) else None, axis=0)
            texture = 0
            if group["base"]:
                base = max(group["base"], key=lambda entry:entry["frequency"])
                texture, mean = detail(base["material"], base["tiling"])
                if mean is not None:
                    avg = avg*.68 + mean*.32
            start = round(max(0, group["low"]) * 255)
            end = min(255, round(group["high"] * 255))
            palette[kind, start:end + 1] = avg
            material_ids[kind, start:end + 1] = texture
    road_palette = np.zeros((16,3), np.float32)
    for kind, groups in descriptor["roads"].items():
        if groups and groups[0]["colors"]:
            far = groups[0]["colors"][0]["far"]
            road_palette[kind] = [(far>>shift)&255 for shift in (16,8,0)]
    return palette, material_ids, np.array(details), road_palette


def road_surface(corners, fx, fy, width):
    """CLandBlock::OnRoad corner strips and diagonal connections, in native quad units."""
    a,b,c,d = (np.asarray(v) != 0 for v in corners)
    mask = ((a & (fx+fy < width)) | (b & (1-fx+fy < width)) |
            (c & (fx+1-fy < width)) | (d & (2-fx-fy < width)) |
            (a & b & (fy < width)) | (a & c & (fx < width)) |
            (b & d & (1-fx < width)) | (c & d & (1-fy < width)) |
            (a & d & ~b & ~c & (abs(fx-fy) < width)) |
            (b & c & ~a & ~d & (abs(fx+fy-1) < width)) | (a & b & c & d))
    return mask, np.maximum.reduce(corners)


class World:
    def __init__(self, cells, profile):
        portal = profile.portal
        refs = region_references(portal.read(0x0f000000))
        defs = landscape_defs(portal.read(refs["land_defs"]))
        self.steps = defs["vertex_side"] - 1
        self.block_metres = defs["block_side"]
        self.metres_per_pixel = self.block_metres * BLOCKS / (TILE << MAX_ZOOM)
        self.vertices = self.steps * BLOCKS
        self.height_table = np.asarray(defs["heights"], np.float32)
        self.water = water_planes(portal, refs, self.height_table)
        coverage = world_map_blocks(portal, refs)
        self.present = np.zeros((BLOCKS,BLOCKS), bool)
        self.palette, self.materials, self.details, self.road_palette = terrain_materials(
            portal, refs, defs["terrain_bits"], self.metres_per_pixel)
        self.heights = np.zeros((self.vertices + 1,) * 2, np.uint8)
        self.kinds = np.full_like(self.heights, NO_TERRAIN)
        self.roads = np.zeros_like(self.heights)
        self.road_width = defs["road_width"] / (self.block_metres/self.steps)
        road_shift = sum(defs[name] for name in ("terrain_bits","scene_bits","encounter_bits","color_bits"))
        self.placements = []
        self.block_count = 0
        side = self.steps + 1
        mask = (1 << defs["terrain_bits"]) - 1
        for did, offset, size, _ in cells.entries():
            kind, bx, by = did & 0xffff, did >> 24, (did >> 16) & 255
            if kind != 0xffff:
                continue
            data = cells.chain(offset, size)
            block = decode_block(data, defs, did)
            self.present[bx,by] = coverage[bx,by]
            region = np.s_[bx*self.steps:bx*self.steps+side, by*self.steps:by*self.steps+side]
            self.heights[region] = np.frombuffer(block["heights"], np.uint8).reshape(side, side)
            packed = np.asarray(block["infos"], np.uint32).reshape(side, side)
            self.kinds[region] = packed & mask
            self.roads[region] = (packed >> road_shift) & ((1<<defs["road_bits"])-1)
            self.block_count += 1
            # CLandBlockData names its own LandBlockInfo. Older *FFFE records
            # still exist in blank ocean blocks after the terrain was removed.
            info = struct.unpack_from("<I", data, 4)[0]
            if not coverage[bx,by] or not info:
                continue
            r = Reader(cells.read(info))
            flags, ident = r.word(), r.word()
            if ident != info:
                raise ValueError("LandBlockInfo identity mismatch")
            if flags & 1:
                r.word()
            r.take(12)  # Cell count, light-info record, entity group header.
            for _ in range(r.word()):
                placed = entity(r, profile.types)
                x, y, z = placed["origin"]
                placed["origin"] = (bx*self.block_metres+x, by*self.block_metres+y, z)
                self.placements.append(placed)

    def tile(self, tx, ty):
        # Take a one-vertex apron for centered relief normals on shared tile edges.
        count = self.vertices >> MAX_ZOOM
        x0, y0 = tx*count, self.vertices - (ty+1)*count
        ix = np.clip(np.arange(x0-1, x0+count+2), 0, self.vertices)
        iy = np.clip(np.arange(y0-1, y0+count+2), 0, self.vertices)
        height = self.height_table[self.heights[np.ix_(ix, iy)]]
        kind = self.kinds[x0:x0+count+1, y0:y0+count+1]
        blocks = self.water[x0//self.steps:(x0+count)//self.steps, y0//self.steps:(y0+count)//self.steps]
        core = height[1:-1, 1:-1]
        if (kind.min() == NO_TERRAIN and kind.max() == NO_TERRAIN) or core.max() < blocks.min() - 8:
            return Image.new("RGB", (TILE, TILE), OCEAN)

        spacing = self.block_metres / self.steps
        dx = (height[2:, 1:-1] - height[:-2, 1:-1]) / (2*spacing)
        dy = (height[1:-1, 2:] - height[1:-1, :-2]) / (2*spacing)
        norm = np.sqrt(1+dx*dx+dy*dy)
        slope = np.rint(255*np.sqrt(dx*dx+dy*dy)/norm).astype(np.uint8)
        light = (.60 + .76 * np.maximum(0, (.46*dx-.48*dy+.75)/norm))
        rgb = np.clip((self.palette[kind, slope]*1.28+12)*light[:, :, None], 0, 255)
        material = self.materials[kind, slope]
        # Render the original DAT diagonal cut, so shorelines and ridges match
        # the ground triangles. Samples lie at pixel centers in world space.
        index = (np.arange(TILE, dtype=np.float32)+.5) * (count/TILE)
        base = index.astype(int)
        local = index-base
        x, y = base[:, None], base[None, :]
        fx, fy = local[:, None], local[None, :]
        gx = (x+x0).astype(np.uint32)
        gy = (y+y0).astype(np.uint32)
        cut = ((gy*gx*gx*0x39557fcf-0x4a6506db)*gy-gx*0x6b3c5f11-0x51dd9315) >= 0x80000000
        # Weight at the north-east vertex uniquely defines both triangle cuts.
        ne = np.where(cut, np.minimum(fx, fy), np.maximum(0, fx+fy-1))
        weights = (1-fx-fy+ne, fx-ne, fy-ne, ne)
        corners = ((x,y), (x+1,y), (x,y+1), (x+1,y+1))
        surface = sum(core[c]*w for c,w in zip(corners,weights))
        # Every 256px tile begins on a complete native texture repeat.
        texel = np.arange(TILE) % 8
        color = sum((rgb[c]+.8*self.details[material[c],texel[None,:],texel[:,None]])*w[:, :, None]
                    for c,w in zip(corners,weights))
        roads = self.roads[x0:x0+count+1, y0:y0+count+1]
        if roads.any():
            on_road, road_type = road_surface([roads[c] for c in corners], fx, fy, self.road_width)
            color[on_road] = color[on_road]*.20 + (self.road_palette[road_type[on_road]]*1.35+22)*.80
        depth = blocks[x//self.steps, y//self.steps] - surface
        shore = 1-np.clip(depth/8, 0, 1)
        wet_color = OCEAN + shore[:, :, None] * np.array([12, 23, 22])
        # Shallow water reveals the ground. Flat 70 m shelves in the Lost Wish
        # DAT sit just below a 70.95 m plane and otherwise look like solid blocks.
        opacity = (-np.expm1(-np.clip(depth,0,8)/2) / -math.expm1(-4))[:, :, None]
        color += opacity*(wet_color-color)
        holes = np.logical_or.reduce([kind[c] == NO_TERRAIN for c in corners])
        color[holes] = np.where((depth[holes] > 0)[:,None], wet_color[holes], (44,40,35))
        color[~self.present[(x+x0)//self.steps,(y+y0)//self.steps]] = OCEAN
        return Image.fromarray(np.clip(color,0,255).astype(np.uint8).transpose(1, 0, 2)[::-1])


class ObjectModels:
    """Flatten supported root setups in their native rest pose, preserving mesh materials."""
    def __init__(self, profile):
        self.profile = profile
        self.portal = profile.portal

    # Assets.visual merges parent descriptions using this object's .portal and .visual.
    visual = Assets.visual

    @lru_cache(maxsize=None)
    def material(self, did):
        info = material_info(self.portal, did, max_size=16)
        if not info["visible"]:
            return None
        if "image" in info:
            return np.array(info["average"]) * np.clip(info["diffuse"][:3], 0, 1)
        return np.array([125, 116, 92]) * np.clip(info["diffuse"][:3], 0, 1)

    @lru_cache(maxsize=128)
    def mesh(self, did):
        mesh = decode_mesh(self.portal.read(did))
        vertices = np.array(mesh["vertices"], np.float32)
        triangles, colors = [], []
        for group in mesh["groups"]:
            color = self.material(mesh["materials"][group["material"]])
            if color is None:
                continue
            start, count = group["start"], group["count"]
            triangles.extend(mesh["indices"][start:start+count])
            colors.extend([color] * (count//3))
        return vertices, np.asarray(triangles, int).reshape(-1,3), np.asarray(colors), bool(mesh["skin"])

    @lru_cache(maxsize=None)
    def shape(self, did):
        visual = self.profile.visual(did)
        if not visual:
            return None
        desc = self.visual(visual)
        roots = [g for g in desc["groups"].values() if not g.get("parent") and g.get("setup")]
        if not roots:
            return None
        size = np.asarray(desc.get("scale", (1,1,1)), np.float32)
        if did >> 24 == 0x47:
            size *= entity(Reader(self.portal.read(did)), self.profile.types)["scale"]
        faces, shades = [], []
        for group in roots:
            setup = group["setup"]
            raw = self.portal.read(setup)
            parts = setup_meshes(raw, setup)
            bones = struct.unpack_from("<I", raw, 12+4*len(parts))[0]
            bind = []
            if bones:
                rig = decode_rig(raw, setup, static_placement=True)
                bind = [np.linalg.inv(np.asarray(m).reshape(4,4)) for m in rig["inverse_bind"]]
            for i, part in enumerate(parts):
                points, triangles, colors, skinned = self.mesh(part)
                if not len(triangles):
                    continue
                if not skinned and i < len(bind):
                    points = points @ bind[i][:3,:3].T + bind[i][:3,3]
                faces.append((points*size)[triangles])
                shades.append(colors)
        if not faces:
            return None
        return np.concatenate(faces).astype(np.float32), np.concatenate(shades).astype(np.float32)


def place_objects(world, models):
    bins = defaultdict(list)
    counts = Counter()
    invalid = set()
    mpp = world.metres_per_pixel
    extent = world.block_metres * BLOCKS
    seeds = []
    for item in world.placements:
        did = item["data"]
        if did in invalid:
            counts["unsupported"] += 1
            continue
        try:
            model = models.shape(did)
        except (KeyError, ValueError) as error:
            invalid.add(did)
            counts[type(error).__name__] += 1
            counts["unsupported"] += 1
            continue
        if model is None:
            counts["withoutGeometry"] += 1
            continue
        faces, colors = model
        x, y, z = item["origin"]
        matrix = np.array(transform_matrix((0,0,0), item["scale"], item["rotation"]), np.float32).reshape(4,4)
        geometry = faces @ matrix[:3,:3].T
        # Most underground decoration shares horizontal space with the overworld.
        # Compare the top of the object to the authored ground at its origin.
        ix = int(np.clip(round(x*world.steps/world.block_metres), 0, world.vertices))
        iy = int(np.clip(round(y*world.steps/world.block_metres), 0, world.vertices))
        if z+geometry[:,:,2].max() < world.height_table[world.heights[ix,iy]]-2:
            counts["underground"] += 1
            continue
        # Rasterize visible top-down geometry. Keep holes in gates, bridge arches
        # and courtyards instead of using enclosing boxes as building outlines.
        normals = np.cross(geometry[:,1]-geometry[:,0], geometry[:,2]-geometry[:,0])
        area = np.abs(normals[:,2])
        keep = area > (.08*mpp*mpp)
        if not keep.any():
            counts["subpixel"] += 1
            continue
        normals = normals[keep]
        lengths = np.maximum(np.linalg.norm(normals, axis=1), .0001)
        normals *= np.where(normals[:,2] < 0, -1., 1.)[:,None]
        light = .58+.63*np.maximum(0, normals @ np.array([-.47,.49,.73])/lengths)
        colors = np.clip((colors[keep]*1.25+14)*light[:,None], 0,255).astype(np.uint8)
        geometry = geometry[keep]
        zorder = np.argsort(geometry[:,:,2].mean(axis=1), kind="stable")
        uv = geometry[zorder,:,:2].copy()
        uv[:,:,0] = (x+uv[:,:,0])/mpp
        uv[:,:,1] = (extent-y-uv[:,:,1])/mpp
        x0,y0 = np.floor(uv.min(axis=(0,1))).astype(int)
        x1,y1 = np.floor(uv.max(axis=(0,1))).astype(int)
        if x1 < 0 or y1 < 0 or x0 >= (TILE<<MAX_ZOOM) or y0 >= (TILE<<MAX_ZOOM):
            counts["outside"] += 1
            continue
        # Render once in an object-local image and clip across every touched tile.
        # Never repack a full-world 16k RGBA canvas in memory.
        stamp = Image.new("RGBA", (x1-x0+1, y1-y0+1))
        pen = ImageDraw.Draw(stamp)
        polygons = np.floor(uv).astype(int) - np.array([x0,y0])
        for points, color in zip(polygons.reshape(-1,6).tolist(), colors[zorder].tolist()):
            pen.polygon(points, tuple(color))
        top = z+geometry[:,:,2].max()
        seeds.append((top, stamp, int(x0), int(y0)))
        counts["mapped"] += 1
    for _, stamp, x0, y0 in sorted(seeds, key=lambda row:row[0]):
        for ty in range(max(0,y0//TILE), min(1<<MAX_ZOOM,(y0+stamp.height-1)//TILE+1)):
            for tx in range(max(0,x0//TILE), min(1<<MAX_ZOOM,(x0+stamp.width-1)//TILE+1)):
                bins[tx,ty].append((stamp, (x0-tx*TILE,y0-ty*TILE)))
    return bins, counts


def runs(xs):
    """Compact manifest coverage for one north-up tile row."""
    ranges = []
    for x in sorted(xs):
        if ranges and ranges[-1][1] == x-1:
            ranges[-1][1] = x
        else:
            ranges.append([x,x])
    return ",".join(str(a) if a == b else f"{a}-{b}" for a,b in ranges)


def render(dat_dir, output):
    with ExitStack() as stack:
        profile = ContentProfile(dat_dir)
        stack.callback(profile.close)
        cells = Archive(dat_dir / "cell_1.dat")
        stack.callback(cells.file.close)
        world = World(cells, profile)
        print(f"Decoded {world.block_count:,} outdoor blocks and {len(world.placements):,} static placements.", flush=True)
        objects, stats = place_objects(world, ObjectModels(profile))
    print(f"Object geometry: {dict(stats)}", flush=True)
    folder = output.parent / "tiles"
    coverage = [set() for _ in range(MAX_ZOOM+1)]
    for z in range(MAX_ZOOM+1):
        (folder / str(z)).mkdir(exist_ok=True, parents=True)
    empty_extrema = tuple((v,v) for v in OCEAN)

    def build(z, x, y):
        if z == MAX_ZOOM:
            image = world.tile(x,y)
            for stamp, position in objects.get((x,y), ()):
                image.paste(stamp, position, stamp)
            if image.getextrema() == empty_extrema:
                return None
        else:
            # The parent samples the uncompressed children, so WebP artifacts
            # do not accumulate with each successive level of zoom.
            full = None
            for dy in range(2):
                for dx in range(2):
                    child = build(z+1, 2*x+dx, 2*y+dy)
                    if child is not None:
                        if full is None:
                            full = Image.new("RGB", (2*TILE,2*TILE), OCEAN)
                        full.paste(child, (dx*TILE,dy*TILE))
            if full is None:
                return None
            if z == 0:
                full.save(output, optimize=True)
            image = full.resize((TILE,TILE), Image.Resampling.BOX)
        image.save(folder / str(z) / f"{x}-{y}.webp", quality=90, method=4)
        coverage[z].add((x,y))
        return image

    build(0,0,0)
    levels = []
    for z,present in enumerate(coverage):
        for previous in (folder / str(z)).glob("*.webp"):
            x, sep, y = previous.stem.partition("-")
            if sep and x.isdecimal() and y.isdecimal() and (int(x),int(y)) not in present:
                previous.unlink()
        levels.append([runs(x for x,y in present if y == row) for row in range(1<<z)])
        print(f"Zoom {z}: {len(present)} tiles", flush=True)
    # World-marker lookup uses native coordinate order, from south to north.
    world_blocks = [runs(np.flatnonzero(world.present[:,y])) for y in range(BLOCKS)]
    manifest = dict(tileSize=TILE, maxZoom=MAX_ZOOM, emptyColor=OCEAN_HEX, levels=levels,
                    worldBlocks=world_blocks,
                    blockMetres=world.block_metres, heightSampleMetres=world.block_metres/world.steps,
                    metresPerPixel=world.metres_per_pixel, landblocks=world.block_count,
                    staticPlacements=len(world.placements), renderedObjects=stats["mapped"],
                    unsupportedPlacements=stats["unsupported"])
    (folder.parent / "terrain-tiles.js").write_text("window.AC2TerrainTiles=" +
                                                   json.dumps(manifest, separators=(",", ":")) + ";\n",
                                                   encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dat_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(args.dat_dir, args.output)

