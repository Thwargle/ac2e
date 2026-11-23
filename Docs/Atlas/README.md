# AC2Hero world atlas

Open [the atlas](index.html) in a browser. Retail terrain rasters and tiles are generated locally from your DATs. See the terrain preparation commands below. The catalog, terrain, search and map work directly from disk. Select a record to see its evidence, related records, native IDs and locations. The source pages and original dungeon floor plans open on AC2Hero. [AC2Hero indexes](http://ac2hero.e5i5o.com/index.html)

## Navigating the atlas

The map, catalog and selected record share the browser window. Scroll inside the catalog or record to read long entries. Narrow screens have buttons to switch between the three views. Drag the world map to pan, use the wheel or pinch to zoom at a point, or focus the map and use the arrow keys and **+ / −**. **Fit world** and the **Home** key restore the full extent. **Locate on map** jumps to a record's coordinates.

The gear beside **+ / −** opens **Map settings**. Choose which kinds of records appear as dots. The browser remembers these choices. The **Prepared spawns** and **Guide coordinates** checkboxes below the map control their respective layers. Right-click the world map and choose **Copy coordinates to clipboard** to copy an in-game compass position such as `44.2N, 11.3E`.

The outdoor map has 1,605 tiles across seven levels. Each tile is 256 pixels square. The browser loads the visible tiles and draws coarser ones while they arrive. The finest level spans 16,384 pixels across Dereth at 2.5 metres per pixel. One further 2× zoom step magnifies that level to make nearby markers easier to distinguish.

### What the outdoor map covers

The generator reads 65,025 landblocks and uses the retail map's detail-sheet index to select the 24,256 blocks on its outdoor map. It reads 10-metre elevation samples, terrain materials, roads and water levels from the DATs. Of the 19,926 static placements referenced by these blocks, 15,019 have visible overhead geometry at the finest level. Another 3,703 fall below a pixel, 216 are underground, seven have no drawable geometry and 981 use unsupported geometry.

The nine island shapes formerly visible near the north edge are repeated patches in the terrain archive. They have matching elevations and scenery layouts, and none has an entry in the retail world map's detail sheets. Their gameplay purpose is unknown. The isolated shapes northwest of Osteth have a different explanation. For example, blocks `66CE`, `6BCA` and `6DCC` contain ocean floor, and their landblock headers reference no scenery records. An earlier atlas drew objects from detached scenery records at those coordinates. The generator now follows each landblock's own scenery reference.

## Reading the map

- **Prepared placements** are the positions in the AC2E world manifest, including validated reconstructions. Outdoor coordinates use the game's compass convention. Dungeon placements carry the region **Dungeon** and appear in the interior viewer. Outdoor entrances still appear on the world map. A placement source can identify the recorded instance, a floor adjustment, a marker fitted from a floor plan, or a reconstructed camp. A roaming spawn can have several listed homes with only one creature present.
- **Interior room map** shows room centers joined by reciprocal doorways in the retail DAT. Select a portal arrival, exterior doorway or inhabitant to explore its connected rooms. Use the two-doorway view or the floor filter where rooms overlap. Select a green inhabitant marker to open its record. Right-click a room, arrival or inhabitant and choose **Copy coordinates to clipboard** for its eight-digit cell ID followed by local X, Y and Z in metres. The guide's illustrated floor plans are linked separately.
- **Guide locations** come from descriptions of vault entrances, NPC locations, quest starts and waypoints. A waypoint can mark a destination, a landmark or part of a multi-quest walkthrough. Related records identify shared guides and floor plans.
- Each outdoor NPC site has one dot. With both layers enabled the dot uses the prepared spawn. With only **Guide coordinates** enabled it uses a published coordinate. For the same NPC, the atlas matches reports to the nearest prepared position within half a compass degree (about 102 m). Guide-only positions within that distance of each other share one dot. Separate prepared posts and documented patrol stops keep their own dots. The record's **Guide locations** list links every citation, including conflicting reports.
- **Observed loot** links a creature to items recorded together in corpse captures. **Native trophy** and **2017 DAT trophy** identify the archive supplying each slot probability. **Native generic treasure limits** give the number of generic slots allowed for a creature and the treasure level. AC2E reconstructs ordinary item weights and quantity sampling.
- Use **Map carrier locations** on an item to find its prepared outdoor carriers. Quests and dungeons offer the same lookup for their actors and entrances. Creature records show individual corpse bundles, sample counts and any recovered trophy-slot probabilities.
- **Quest policy** names an AC2E implementation. **Journal definition** identifies a retail journal record. **Documented quest points** ties a sourced amount to a specific journal ID. **Guide quest points** preserves a published reward even when the corresponding server quest has yet to be identified. Use **Partial quests and missing route steps** to find known missing prerequisites, NPCs, targets and dungeon gates. “Required actor templates present” checks the prepared world for those actors. Open **Gameplay review** and **Checks performed** on a quest record for the results of its server tests.
- **Native store offers** show prices per unit from the retail store graph. The six resource traders in AC2E sell the bundle sizes documented in the guide. Recipe records link to the required workshop, options for each ingredient slot and possible outputs. Butchery materials link back to the creature, and survey maps give the search bounds for their deposits.
- Records group identical normalized names within a category and list the native IDs where known. Creature variants can share a name. The **Native definition** category retains named templates whose role is unclear. The atlas omits anonymous base templates and unresolved workbook rows labeled `???`. Trophy rolls inherited from those templates appear under the named creatures that drop them.

The current snapshot covers 1,222 linked pages, 8,232 catalog records and all 12,754 positions in the prepared world. It displays all 7,673 named entity templates from the retail template export and all 582 native journal IDs. Each of the 113 mapped dungeons and vaults links to its guide floor plan. The interior viewer uses 14,552 native rooms and 24,772 reciprocal doorways. When the vault and glyph guides disagree on an entrance coordinate, both positions retain their attribution.

Eleven additional definitions get their English names from the retail base-description data, including the Imbued Digging Tool, Lost Wish Glyph and Virindi Death Trap. Two further definitions identify portals. Their 45 unnamed placements have no documented destination. For 59 journal packages whose English title is missing or repeats the package name, the atlas supplies a readable label ending in **(journal entry)**. Their records retain the original package and definition ID.

Eight guide entries use variable item descriptions. Their quests now link to 67 specific item variants, including each continent's five Shaper's Hoard keys, and Ludward's Dream links to the crafting recipes with their ingredient options. The question marks in **Gear Badge #?????** are part of its retail name. The badge starts Battlefield of Dreams. [Battlefield of Dreams guide](http://ac2hero.e5i5o.com/quest/knorr_battlefieldofdreams.html)

Search for `47002e27` or **Officer Cameron** to find the patrol NPC from **Busted!**. His retail AI table identifies him, and the captured Cragstone and Ikeras placements match the guide. Linvak Tukal appears as a guide location. [Busted! quest guide](http://ac2hero.e5i5o.com/quest/osteth_busted.html)

The guide quest index names 450 distinct entries. The **Quest review** on the initial screen shows how many have a completed route, partial test evidence, missing route steps or no linked server policy. Select a status to list its guide entries. Quest records include the gameplay assertions, remaining steps, test script and run ID when available. The catalog also covers journal IDs, aliases and related phases outside the guide index. There are 427 guide creature or NPC records without a prepared placement.

The tests run the AC2E server over loopback UDP. They exercise actions such as accepting a quest, killing its targets, looting, turning in, receiving rewards and reloading character state. The record lists the checks actually performed. Some runs accelerate travel, seed the test character's level or supply encounters. Full walking routes, retail drop probabilities and Icefire behavior require separate verification.

The atlas shows numeric quest-point awards on 50 guide records and fixed XP awards on 207. The server reward catalog matches 46 quest IDs to a documented point amount. A guide can describe several quests or phases. Compare **Guide fixed XP** and **Server completion XP** where sources disagree, including Find the Warder Scout, Kill Captain Sneer and Kehan Windmill. “To Drudgerton!” uses captured NPC locations for Jrent and Jellio and a server-local journal ID.

The Water Temple room map connects the exterior doorway to Chief Benamu. Crystal Caves and the Golden Age Reliquary have captured entrances and return routes, with Cloudburst and the Master Gurog Henchman respectively reachable through connected rooms. The New Mage Academy record identifies its recorded telepads and seal drop, and explains the outstanding group admission and inner Sanctum gates.

Cragstone Excavation links its two exits, recorded encounters and dungeon-specific seal fragments. The Ossuary links Ossein and the route back to Linvak. The Kill Ossein quest map shows three possible homes for the Servant of the Bones outside the entrance. The four Sclavus fort preceptors link to their documented talismans and temple locks, with the unimplemented quest gates marked on the quest record. Guide spellings such as Onara and the Digger 'Dillo Mama resolve to their existing actors.

Warder Hideout links its entrance, both exits and its Lasher encounters. The outdoor records include the Scarred Shreth Hunter in Naderu and the Aerie hunters. The Shreth-Hide Leather record links Lupe, the Hunter, its quest drop and Gallus. Crafting, refining and spellbinding records include the guide-listed stations in six towns.

## Rebuilding

Run these commands from the repository root with Python, lxml, NumPy and Pillow with WebP support installed. The DAT directory must contain the matching Legions archives. The crawler reuses its local page cache.

```text
python AC2E/emulator/tools/cache_ac2hero.py Saved/AC2Hero
python Network/prepare_content.py "Asheron's Call 2 - Legions" AC2E/data/world/legions-world.tsv
python AC2E/emulator/tools/import_store_stock.py "Asheron's Call 2 - Legions" AC2E/emulator/content/native-store-stock.json
python AC2E/emulator/tools/build_hero_atlas.py Saved/AC2Hero AC2E/data/world/legions-world.tsv Docs/Atlas/atlas-data.js --dat "Asheron's Call 2 - Legions"
python AC2E/emulator/tools/build_hero_relief.py "Asheron's Call 2 - Legions" Docs/Atlas/terrain.png
python AC2E/emulator/tools/build_hero_interiors.py "Asheron's Call 2 - Legions" AC2E/data/world/legions-world.tsv Docs/Atlas/rooms-data.js
```

The catalog builder can also use the recovery tools' native-template snapshot through `--templates`. The terrain image uses elevations from the retail archives. Authored spawner positions, floor-plan mappings, withheld candidates and quest evidence live alongside the [server content manifests](../../AC2E/emulator/content/).

To refresh the gameplay evidence, run `python AC2E/emulator/tools/build_quest_review.py` with the saved run directories present in `Saved/AC2NetworkTests/`. To select different evidence, pass one `--run <directory-name>` for each run to include. The generated review travels with the offline atlas.

Run `node AC2E/emulator/tools/test_hero_atlas_browser.cjs` with Playwright and Chromium installed. Set `CHROME_PATH` to use an existing Chrome executable. The test opens the atlas from disk and writes screenshots and a result file under `Saved/AtlasTests/`.
