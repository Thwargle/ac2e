"""Atlas projection, cross-tile shading, shallow banks and sparse ocean coverage."""
import struct
import unittest
from unittest.mock import patch

import numpy as np

from build_hero_relief import MAX_ZOOM, OCEAN, World, road_surface, runs, world_map_blocks


class TerrainTiles(unittest.TestCase):
    def setUp(self):
        # One synthetic landblock per detail tile. Native metres and axis order
        # are preserved, without requiring the retail archives to run this test.
        world = self.world = World.__new__(World)
        side = 1 << MAX_ZOOM
        world.steps = 4
        world.block_metres = 40
        world.vertices = world.steps * side
        shape = (world.vertices + 1,) * 2
        world.height_table = np.arange(256, dtype=np.float32)
        world.heights = np.full(shape, 10, np.uint8)
        world.kinds = np.zeros(shape, np.uint8)
        world.roads = np.zeros(shape, np.uint8)
        world.palette = np.empty((32,256,3), np.float32)
        world.palette[:] = (100,95,70)
        world.palette[1] = (60,130,75)
        world.materials = np.zeros((32,256), np.uint8)
        world.details = np.zeros((1,8,8,3), np.float32)
        world.water = np.full((side,side), -np.inf, np.float32)
        world.present = np.ones((side,side), bool)

    def test_north_is_up_and_material_color_continues_across_tiles(self):
        self.world.kinds[:,42:] = 1
        western = np.asarray(self.world.tile(19,53))
        eastern = np.asarray(self.world.tile(20,53))
        self.assertTrue(np.array_equal(western[:,-1], eastern[:,0]))
        self.assertGreater(int(western[0,128,1]), int(western[-1,128,1]))

    def test_shallow_shelf_shows_ground_and_padded_block_is_ocean(self):
        dry = np.asarray(self.world.tile(19,53))[128,128]
        self.world.water[19,10] = 10.95
        shallow = np.asarray(self.world.tile(19,53))[128,128]
        self.assertGreater(int(dry[0]), int(shallow[0]))
        self.assertGreater(int(shallow[0]), OCEAN[0]+20)
        self.world.present[19,10] = False
        self.assertTrue(np.all(np.asarray(self.world.tile(19,53)) == OCEAN))

    def test_diagonal_road_leaves_opposing_corners_open(self):
        centerline, kind = road_surface([2,0,0,2], np.array([.2,.2,.8]), np.array([.2,.8,.8]), .15)
        self.assertEqual(centerline.tolist(), [True,False,True])
        self.assertEqual(kind, 2)

    def test_sparse_rows_round_trip_disconnected_coasts(self):
        self.assertEqual(runs([7,3,1,2,9,10]), "1-3,7,9-10")
        self.assertEqual(runs([]), "")

    def test_only_declared_retail_sheets_cover_world_blocks(self):
        # A 3x2 x-major map with two sheets, an empty block, and an unknown index.
        descriptor, indices = 0x59000000, 0x19000009
        records = {
            descriptor: struct.pack("<3I", descriptor, indices, 2) +
                        struct.pack("<8I", 2, 123, 0, 0, 1, 1, 1, 999) +
                        struct.pack("<7I", 7, 432, 2, 0, 1, 1, 0),
            indices: struct.pack("<3I", indices, 2, 3) + bytes([0,2,7,0,7,9]),
        }
        class Portal:
            read = staticmethod(records.__getitem__)

        blocks = world_map_blocks(Portal(), {"detailmap":[(descriptor, 0)]})
        self.assertEqual(blocks[:3,:2].tolist(), [[False,True],[True,False],[True,False]])
        self.assertEqual(int(blocks.sum()), 3)

    def test_orphan_and_unmapped_landblock_info_do_not_draw_objects(self):
        # Ocean block zero still has an obsolete *FFFE entry in the archive.
        # Block one references its scenery, as does block two beyond the map.
        class Cells:
            records = {}
            def entries(self):
                return ((did, did, len(data), 0) for did,data in self.records.items())
            def read(self, did):
                return self.records[did]
            def chain(self, offset, size):
                return self.read(offset)

        cells = Cells()
        for bx in range(3):
            terrain, info = bx<<24 | 0xffff, bx<<24 | 0xfffe
            cells.records[terrain] = struct.pack("<4I4BI4I", terrain, info if bx else 0, 0, 4,
                                                 0,0,0,0,4,0,0,0,0)
            cells.records[info] = struct.pack("<8I", 0,info,0,0,0,1,8,123+bx)
        class Profile:
            types = {}
            portal = cells
        cells.records[0x0f000000] = cells.records[0x36000000] = b""
        defs = dict(vertex_side=2,block_side=160,heights=[0]*256,road_width=0,terrain_bits=6,
                    scene_bits=1,encounter_bits=1,color_bits=1,road_bits=4)
        coverage = np.zeros((256,256), bool)
        coverage[:2,0] = True
        with patch.multiple("build_hero_relief",
                            region_references=lambda _: {"land_defs":0x36000000},
                            landscape_defs=lambda _: defs, world_map_blocks=lambda *_: coverage,
                            terrain_materials=lambda *_: (None,)*4, water_planes=lambda *_: None):
            world = World(cells, Profile())
        self.assertEqual([p["data"] for p in world.placements], [124])
        self.assertEqual(world.placements[0]["origin"], (160,0,0))


if __name__ == "__main__":
    unittest.main()
