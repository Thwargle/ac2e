"""Map native indoor doorways reachable from prepared placements and portal arrivals."""
import argparse
from collections import deque
import json
from pathlib import Path
import sys

from build_hero_atlas import data_rows, portal_routes
from world_dungeon import parse_cell

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "Tools" / "AC2"))
from dat_probe import Archive


def connected_rooms(dat_dir, seeds):
    seeds = {i for i in seeds if 0x100 <= i & 0xffff <= 0xfffb}
    archive = Archive(dat_dir / "cell_1.dat")
    rooms = {}
    try:
        index = {i: (offset, size) for i, offset, size, _ in archive.entries()}
        queue = deque(sorted(seeds))
        while queue:
            cell = queue.popleft()
            if cell in rooms:
                continue
            room = parse_cell(archive.chain(*index[cell]))
            if room["cell"] != cell:
                raise ValueError(f"Room ID mismatch at {cell:08x}")
            doors = set()
            for door in room["portals"]:
                other = door["other_cell"]
                if door["flags"] & 2 or not 0x100 <= other <= 0xfffb:
                    continue
                next_cell = cell & 0xffff0000 | other
                if next_cell != cell and next_cell in index:
                    doors.add((next_cell, door["index"], door["other_index"]))
            opens_outside = any(door["flags"] & 2 for door in room["portals"])
            rooms[cell] = (room["position"], doors, opens_outside)
            queue.extend(sorted({n for n, _, _ in doors} - rooms.keys()))
    finally:
        archive.file.close()
    # Keep only reciprocal doorway connections. Visibility lists can span solid walls.
    result = {}
    for cell, (pos, doors, opens_outside) in sorted(rooms.items()):
        neighbors = sorted({n for n, local, remote in doors if (cell, remote, local) in rooms[n][1]})
        entry = [*[round(v, 3) for v in pos], [f"{n:08x}" for n in neighbors]]
        if opens_outside:
            entry.append(True)
        result[f"{cell:08x}"] = entry
    return result


def build(dat_dir, world, content):
    seeds = {int(row[3], 16) for row in data_rows(world)}
    seeds.update(int(row[6], 16) for row in portal_routes(content, world).values())
    return connected_rooms(dat_dir, seeds)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dat_dir", type=Path)
    parser.add_argument("world", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    content_dir = Path(__file__).resolve().parents[1] / "content"
    rooms = build(args.dat_dir, args.world, content_dir)
    args.output.write_text("window.AC2Rooms = " + json.dumps(rooms, separators=(",", ":")) + ";\n", encoding="utf-8")
    print(f"Mapped {len(rooms):,} native rooms and {sum(len(r[3]) for r in rooms.values())//2:,} reciprocal doorways.")
