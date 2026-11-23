"""Build the offline AC2Hero evidence atlas from cache_ac2hero.py and a prepared AC2E world."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
from urllib.parse import parse_qs, unquote, urljoin, urlsplit

from lxml import html
from hero_atlas_names import ITEM_FAMILIES, LUDWARD, LUDWARD_RECIPES, PORTAL_CLASSES, QUALITY_NAMES, journal_label
from hero_policy_aliases import link_quest_policies
from hero_quest_routes import audit_routes
from hero_trade_guides import index_trade_guide

BASE = "http://ac2hero.e5i5o.com/"
COORD = re.compile(r"(?<![.\d])(\d{1,3}(?:\.\d+)?)\s*([NS])[\s,;_/]+(\d{1,3}(?:\.\d+)?)\s*([EW])\b"
                   r"|(?<![.\d])(\d{1,3}(?:\.\d+)?)\s*([EW])[\s,;_/]+(\d{1,3}(?:\.\d+)?)\s*([NS])\b", re.I)


def clean(s):
    return " ".join((s or "").replace("\ufeff", "").split())


def text(node):
    return clean(node.text_content())


def guide_reward_text(label):
    # A numeric reward can be in the heading paragraph or the following paragraph.
    # Preserve line-break boundaries between reward currencies.
    paragraph = label.getparent()
    reward = clean(" ".join(paragraph.itertext()))[len(text(label)):].strip(": ")
    if not reward:
        paragraph = paragraph.getnext()
        if paragraph is None or paragraph.tag != "p" or paragraph.xpath("./strong[contains(., ':')]"):
            return None
        reward = clean(" ".join(paragraph.itertext()))
    return reward


def guide_quest_points(reward):
    values = re.findall(r"(?<![\d.,])(\d+(?:,\d{3})*)\s*(?:QP|quest points?)\b", reward, re.I)
    return int(values[0].replace(",", "")) if len(values) == 1 else None


def guide_fixed_xp(reward):
    # A percentage/cap, a list of alternatives or a per-kill payment is not a fixed turn-in award.
    match = re.match(r"^(\d+(?:,\d{3})*)\s*(?:XP|experience points?)\b", reward, re.I)
    if match and not re.search(r"%|\b(?:cap|maximum|per|each|depending|levels?)\b|\d\s*(?:XP|experience)", reward[match.end():], re.I):
        return int(match[1].replace(",", ""))
    return None


def key(name):
    return clean(re.sub(r"\s*\[(?:e|v)\]\s*$", "", name, flags=re.I)).replace("Ã¢â‚¬â„¢", "'").casefold()


def workbook_name(name):
    return clean(re.sub(r"(?:\s*\[[efimnpsv]+\])+$", "", name, flags=re.I)).replace(r"\q", '"')


def positions(s):
    # Consume each pair from the left. A slash can also separate consecutive locations.
    for pair in COORD.finditer(s):
        if pair[1] is None:
            x, ew, y, ns = pair.groups()[4:]
        else:
            y, ns, x, ew = pair.groups()[:4]
        if max(float(y), float(x)) > 100:
            continue
        yield [round(float(y) * (1 if ns.upper() == "N" else -1), 3),
               round(float(x) * (1 if ew.upper() == "E" else -1), 3)]


def add_unique(items, value):
    if value not in items:
        items.append(value)


def relative(page, ref):
    u = urlsplit(urljoin(BASE + page, ref))
    return unquote(u.path).lstrip("/") if u.hostname == "ac2hero.e5i5o.com" else ""


def data_rows(file):
    with file.open(encoding="utf-8-sig") as handle:
        next(handle)
        for line in handle:
            if line.strip() and not line.startswith("#"):
                yield line.rstrip("\r\n").split("\t")


def portal_routes(content, world):
    routes = {}
    sidecar = world.with_suffix(".routes.tsv")
    paths = [content / "portal-routes.tsv", content / "starter-routes-v1.tsv"]
    if sidecar.is_file():
        paths.append(sidecar)
    for path in paths:
        for row in data_rows(path):
            if row[0] in routes:
                raise ValueError(f"Duplicate portal route {row[0]} in {path}")
            routes[row[0]] = row
    return routes


def dat_templates(directory):
    from import_loot_tables import Reader, templates

    profile, local, rows = templates(directory)
    try:
        reader = Reader(profile.portal.read(0x34000000))
        if reader.word() != 0x34000000:
            raise ValueError("Unexpected native property dictionary")
        reader.take(8)
        labels = {reader.word(): reader.string() for _ in range(reader.word())}
        for row in rows.values():
            row["props"] = {labels.get(k, str(k)): v for k, v in row["props"].items()}
        return rows
    finally:
        local.file.close()
        profile.close()


# Scope spelling corrections to the kind. Many distinct variants share similar names.
GUIDE_NAMES = {
    "Quest": {
        "clense the burial mound": "Cleanse the Burial Mound",
    },
    "Item": {
        "hugeobsidian stone": "Huge Obsidian Stone",
        "largeopal": "Large Opal",
    },
    "NPC": {
        "digger dillo mama": "Digger 'Dillo Mama",
        "onara": "Onara, Warder Tracker",
        "onara warder tracker": "Onara, Warder Tracker",
        "iridar vel kaid": "Iridar Vel'Kaid",
        "justicar maronak": "Justiciar Maronak",
        "sontar risoya": "Sontar Trisoya",
        "eshivon ugin": "Eshivon Ugim",
        "hand of asheron": "The Hand of Asheron",
        "mistress of shadows": "The Mistress of Shadows",
        "servant of the empire": "The Servant of The Empire",
        "the dark agent": "Dark Agent",
        "the frostbound one": "Frostbound One",
        "lost wish explorer": "Lost Wish Explorer",
        "rashan twoblades": "Spectre of Rashan Twoblades",
        "tamar realian": "Tamar",
        "miraua": "Miraua, Warder Tracker",
        "miraua warder tracker": "Miraua, Warder Tracker",
        "darvain geddon master armorsmith": "Darvain Geddon",
        "karu geddon apprentice armorsmith": "Karu Geddon",
        "kiraly geddon apprentice armorsmith": "Kiraly Geddon",
        "tyke geddon apprentice armorsmith": "Tyke Geddon",
        # Temple Search uses the shrine's dedication; the DAT uses the whole name.
        "black mother earth": "Shrine to Black Mother Earth",
        "shrine of black mother earth": "Shrine to Black Mother Earth",
        "blue mother heaven": "Shrine to Blue Mother Heaven",
        "great father sea": "Shrine to Ezheret, Great Father Sea",
        "ezereth great father sea": "Shrine to Ezheret, Great Father Sea",
        "shrine of ezheret": "Shrine to Ezheret, Great Father Sea",
        "great sister rain": "Shrine to Great Sister Rain",
        "great sister wind": "Shrine to Great Sister Wind",
        "hazahtu the blind eye": "Shrine to Hazahtu, the Blind Eye",
        "the blind eye": "Shrine to Hazahtu, the Blind Eye",
        "mother aundetanga": "Shrine to Mother Audetanga",
        "palenqual": "Shrine to Palenqual",
        "volkama": "Shrine to Brother Volkama",
        # The three outposts host the same quest giver at distinct positions.
        "armoredillo ancestor head": "Armoredillo Doom Ancestor",
        "armoredillo ancestor head - arwic": "Armoredillo Doom Ancestor",
        "armoredillo ancestor head - esper": "Armoredillo Doom Ancestor",
        "armoredillo ancestor head - lost wish": "Armoredillo Doom Ancestor",
    },
    "Creature": {
        "sandy armoredillio": "Sandy Armoredillo",
        "fierce sandy armoredillio": "Fierce Sandy Armoredillo",
        "parfal nephane": "Parfal Nefane",
    },
}


# Each key identifies one reviewed source statement. A newly recovered location
# for the same NPC is independent evidence, even if its coordinates look similar.
DISPUTED_NPC_LOCATIONS = {
    "Arwic Explorer": (
        {(44.9, -7.1, "search/npcsearchf9ae.html"), (44.9, -7.1, "quest/osteth_arwicobelisksearch.html")},
        "The Arwic Obelisk Search guide gives 45.6N, 7.8W. The recovered Arwic Explorer is at 45.664N, 7.775W."),
    "Dron Apfel": (
        {(-30.5, 34.3, "search/npcsearch9a82.html"), (-30.5, 34.3, "quest/knorr_apprehendseritorsanatos.html")},
        "The index and quest cite the Upper Level of Caerlin Skyport. The recovered placement of Dron Apfel there is at 30.692S, 33.789E."),
    "Akhri the Egg Collector": (
        {(-6.9, 43.9, "search/npcsearchec27.html"),
         (-5.8, 44.3, "quest/omishan_chthonicdarkenfowlegghunt.html")},
        "The Mysterious Egg and both Darkenfowl Egg Hunt walkthroughs put Akhri at 6.9N, 43.9E "
        "at Eastern Kydi Outpost, beside the recovered placement. The index and one start heading disagree."),
    "Komeua": (
        {(29.1, 55.9, "search/npcsearch91a2.html")},
        "Fetch Dried Flowers and Improve the Honey Crop locate Komeua at 29.1N, 54.9E in Rakani."),
    "Shi Jepauri": (
        {(16.5, 73.6, "search/npcsearch176f.html")},
        "Ero-Bal Remains and both Revoke the Rea-Bal walkthroughs locate Shi Jepauri at 16.0N, 73.7E in Zu."),
    "Keranua": (
        {(8.6, 76.2, "search/npcsearch4c9d.html")},
        "Burun Interdiction, Burun Orders, Burun Punishment and Placate the Moarsmen place Keranua at 8.4N, 76.1E in Hakata."),
    "Polauri": (
        {(8.3, 76.2, "search/npcsearchc22e.html")},
        "Six Hakata walkthroughs, including Ancient Amulet and Moarsman Mania, locate Polauri at 8.4N, 75.9E."),
    "Matto Olvidan": (
        {(-18.2, -18.0, "quest/linvak_greenthumb.html")},
        "The index, Follow the Bouncing Ball, Wild Hare and the recovered farm placement agree on 17.8S, 17.9W."),
    "Venedeta's Servant": (
        {(12.7, -49.5, "search/npcsearchb420.html")},
        "Slay Uran the Black locates the servant near the Star Beacon at 12.1N, 49.4W, beside the recovered placement."),
    "Mara Sereda": (
        {(12.0, -49.5, "search/npcsearch6ace.html")},
        "Slay Veran places Mara at 11.6N, 49.5W, beside her recovered placement."),
    "Voice of the Singularity": (
        {(12.2, -49.1, "search/npcsearch7606.html")},
        "Slay Rymeras places the Voice at 11.9N, 49.1W, beside the recovered placement."),
    "Stubby": (
        {(-8.8, -52.6, "search/npcsearch50df.html")},
        "All six linked walkthroughs place Stubby by the Water Walk near 26.0S, 37.7W. The recovered placement is there."),
    "Shrine to Palenqual": (
        {(18.5, -53.4, "search/npcsearch8228.html")},
        "Temple Search places the shrine in Northeast Ariaki at 18.5N, 53.4E, beside the recovered shrine. The index prints W."),
    "Armoredillo Doom Ancestor": (
        {(42.2, -12.9, "quest/osteth_armoredillodoom.html")},
        "The NPC index lists three outposts. The Lost Wish head is at 42.2N, 12.9E near Kehan. "
        "The quest walkthrough names Kehan but prints W; the Miller of Kehan and Factotum Messenger routes both place Kehan east."),
    "Lost Wish Explorer": (
        {(43.0, 11.5, "search/npcsearchace8.html"), (43.0, 11.5, "quest/osteth_lostwishobelisksearch.html")},
        "The historical guide locates the Explorer at 43.0N, 11.5E. AC2E uses the recovered position beside the active Ringway at 44.0N, 11.3E."),
}

# These guides give the coordinate for reaching the dungeon in which the NPC lives.
NPC_DUNGEON_ENTRANCES = {
    "Eshivon Ugim": (38.6, -25.4, "quest/osteth_arcanumslibrary.html",
                     "The guide directs players into the Arcanum Vault. Eshivon is inside in cell 09f10106."),
    "Master of the Gauntlet": (32.4, -7.2, "quest/osteth_championgauntlet.html",
                               "The guide directs players to the Gauntlet Arena east of Cragstone. The Master is inside in cell 049e010a."),
}


class Atlas:
    def __init__(self, cache):
        self.cache = cache
        self.pages = json.loads((cache / "crawl.json").read_text(encoding="utf8"))
        self.records = {}
        self.page_quests = defaultdict(list)
        self.item_families = set()

    def record(self, kind, name, page=None):
        name = clean(name)
        canonical = GUIDE_NAMES.get(kind, {}).get(key(name), name)
        ident = (kind, key(canonical))
        r = self.records.setdefault(ident, {"kind": kind, "name": canonical, "sources": [],
                                            "facts": {}, "places": [], "links": []})
        if canonical != name:
            self.fact(r, "Guide name", name)
        if page:
            add_unique(r["sources"], page)
        return r

    def link(self, a, b, role, reverse_role=None):
        for src, dest, label in [(a, b, role), (b, a, reverse_role or role)]:
            if src is not dest:
                add_unique(src["links"], [label, dest["kind"], dest["name"]])

    def fact(self, r, label, value):
        if value:
            add_unique(r["facts"].setdefault(label, []), value)

    def read(self, page):
        raw = (self.cache / page).read_bytes().decode("utf-8", "surrogateescape")
        decoded = re.sub(r"[\udc80-\udcff]+", lambda m: m[0].encode(
            "utf-8", "surrogateescape").decode("cp1252", "replace"), raw)
        return html.fromstring(decoded.encode(), parser=html.HTMLParser(encoding="utf-8"))

    def quest_index(self):
        for row in self.read("quest.html").xpath("//tr[not(.//tr)]"):
            cells = row.xpath("./td")
            if len(cells) != 3:
                continue
            for a in cells[-1].xpath(".//a[@href]"):
                page = relative("quest.html", a.get("href"))
                if not page.startswith("quest/") or not text(a):
                    continue
                r = self.record("Quest", text(a), page)
                if r not in self.page_quests[page]:
                    self.page_quests[page].append(r)
                self.fact(r, "Guide level", text(cells[0]))
                self.fact(r, "Region", clean(" ".join(cells[-1].xpath("./text()"))))
                region = row.xpath("preceding::h2[1]")
                if region:
                    self.fact(r, "Guide section", text(region[0]))

    def references(self, doc, page, parent):
        for a in doc.xpath(".//a[@href]"):
            ref = a.get("href")
            target = relative(page, ref)
            term = clean(parse_qs(urlsplit(ref).query).get("search", [""])[0])
            if term and "http" not in term.lower() and any(ch.isalpha() for ch in term) and target.startswith(("search/mobsearch", "search/npcsearch")):
                kind = "Creature" if "mobsearch" in target else "NPC"
                self.link(parent, self.record(kind, term, target), "Guide reference")
            for q in self.page_quests.get(target, []):
                self.link(parent, q, "Guide reference")

    def items(self, doc, page, quests=()):
        for row in doc.xpath(".//tr[not(.//tr)]"):
            cells = row.xpath("./td")
            if len(cells) < 2:
                continue
            icons = cells[0].xpath(".//img/@src")
            if not icons or not any("items/" in relative(page, icon) for icon in icons):
                continue
            name = text(cells[1]).strip(" \xa0:-")
            if not name or len(name) > 80 or name.lower() in ("name", "icon", "item") or name.lower().startswith("one time use") or re.search(r"\s-\sLevel\s+\d", name, re.I):
                continue
            quantity = re.match(r"^[\d,]+(?:\s*[xÃ—])?\s+(.+)$", name, re.I)
            if quantity:
                name = quantity[1]
                if " or " in name:
                    for option in name.split(" or "):
                        alternative = self.record("Item", option, page)
                        for q in quests:
                            self.link(q, alternative, "Alternative quest item")
                    continue
            if key(name) in ITEM_FAMILIES.get(page, {}):
                self.item_families.add((page, key(name)))
                for q in quests:
                    self.fact(q, "Guide item family", name)
                continue
            r = self.record("Item", name, page)
            for q in quests:
                self.link(q, r, "Quest item reference")
            self.references(row, page, r)
            if page.startswith("items/"):
                self.fact(r, "Guide category", Path(page).stem)
                columns = {"dye": [(2, "Dye plant"), (3, "Crafting cost")], "reckoning": [(2, "Guide level"), (3, "Cache location")],
                           "ort": [(2, "Used on"), (4, "Arcane lore requirement"), (5, "Guide level requirement")],
                           "potions": [(2, "Guide effect")]}
                for column, heading in columns.get(Path(page).stem, []):
                    if column < len(cells) and len(text(cells[column])) < 100:
                        self.fact(r, heading, text(cells[column]))
            if page == "items/glyphs.html" and len(cells) >= 7:
                self.fact(r, "Vault level", text(cells[2]))
                for path in cells[5].xpath(".//a/@href"):
                    mapped = self.maps.get(relative(page, path))
                    if mapped:
                        self.link(r, mapped, "Vault glyph")
                        for point in positions(text(cells[4])):
                            add_unique(mapped["places"], ["Glyph guide entrance", *point, page])
                for a in cells[6].xpath(".//a[@href]"):
                    self.link(r, self.record("Creature", text(a)), "Glyph source")

    def guides(self):
        self.quest_index()
        self.maps = {}
        # The vault and map indexes occasionally label the same floor plan differently.
        vaults = self.read("vaults.html")
        vault_names = {relative("vaults.html", a.get("href")): text(a)
                       for a in vaults.xpath('//tr[td]/td[1]/a[contains(@href,"dungeonimg/")]')}
        for page in sorted(self.pages["pages"], key=lambda p: p != "maps/dungeons.html"):
            doc = self.read(page)
            index_trade_guide(self, doc, page, positions)
            if page.startswith("quest/"):
                bodies = doc.xpath('//div[contains(concat(" ",normalize-space(@class)," ")," tripper ")]')
                qs = self.page_quests[page]
                if not qs:
                    for header in doc.xpath("//h3"):
                        title = re.match(r"Quest Name\s*:\s*(.+)", text(header), re.I)
                        if title:
                            qs.append(self.record("Quest", title[1], page))
                for body in bodies:
                    self.items(body, page, qs)
                    for q in qs:
                        self.references(body, page, q)
                        for ref in body.xpath(".//img/@src | .//a/@href"):
                            mapped = self.maps.get(relative(page, ref))
                            if mapped:
                                self.link(q, mapped, "Quest map")
                        for label in body.xpath(".//p/strong"):
                            heading = text(label).strip(": ").lower()
                            value = clean(label.tail)
                            if heading in ("time limit", "reset timer") and len(value) < 80:
                                self.fact(q, heading.title(), value)
                            if heading in ("reward", "rewards") and len(qs) == 1 and len(bodies) == 1:
                                reward = guide_reward_text(label) or ""
                                xp = guide_fixed_xp(reward)
                                if xp is not None:
                                    self.fact(q, "Guide fixed XP", f"{xp:,} XP ({page})")
                                points = guide_quest_points(reward)
                                if points is not None:
                                    self.fact(q, "Guide quest points", f"{points:,} ({page})")
                            if heading == "requirements":
                                requirement = clean(text(label.getparent())[len(text(label)):].strip(": "))
                                if len(requirement) < 150:
                                    self.fact(q, "Guide requirements", requirement)
                            if heading == "start location":
                                for point in positions(text(label.getparent())):
                                    add_unique(q["places"], ["Guide start" if len(qs) == 1 else "Shared guide start", *point, page])
                        for point in positions(text(body)):
                            if not any(p[1:3] == point for p in q["places"]):
                                add_unique(q["places"], ["Guide waypoint" if len(qs) == 1 else "Shared guide waypoint", *point, page])
            elif page.startswith("items/"):
                self.items(doc, page)
            elif page == "essence2.html":
                for row in doc.xpath('//tr[td[1]//img[contains(@src,"items/trophy/")]]'):
                    cells = row.xpath("./td")
                    if len(cells) != 5:
                        continue
                    trophy = self.record("Item", text(cells[1]), page)
                    for cell in cells[2:4]:
                        self.fact(trophy, "Crafting essence", text(cell))
                    for carrier in text(cells[4]).split(","):
                        name = clean(carrier).replace("Gearknight ", "Gear Knight ")
                        if name:
                            self.link(trophy, self.record("Creature", name, page), "Guide trophy source")
            elif page == "resource.html":
                for row in doc.xpath("//tr[td][not(.//tr)]"):
                    cells = [text(cell) for cell in row.xpath("./td")]
                    if len(cells) != 7 or not list(positions(cells[2])):
                        continue
                    trader = self.record("NPC", cells[0], page)
                    self.fact(trader, "Guide town", cells[1])
                    for point in positions(cells[2]):
                        add_unique(trader["places"], ["Guide trader", *point, page])
                    self.link(trader, self.record("Item", cells[3], page), "Commodity trade")
                    self.fact(trader, "Guide exchange", cells[3] + ": " + ", ".join(cells[4:]))
            elif page.startswith("search/npcsearch"):
                for cell in doc.xpath("//tr/td[1][strong][span]"):
                    name = text(cell.xpath("./span")[0])
                    if not name:
                        continue
                    r = self.record("NPC", name, page)
                    locality = cell.xpath("./strong[normalize-space()='Local:']")
                    local_name = key(locality[0].tail) if locality else ""
                    # The index also uses dungeon names for nearby outdoor camps. In particular,
                    # both Gurog quest givers are north of the New Mage Academy portal.
                    guide_dungeon = local_name in {
                        "warders hideout", "ikeras - shadow antechamber", "chaos forge", "vothardun",
                        "skyport mines", "skyport mansion", "empyrean intro",
                    }
                    if guide_dungeon:
                        self.fact(r, "Region", "Dungeon")
                    for label in cell.xpath("./strong"):
                        heading, value = text(label).rstrip(":"), clean(label.tail)
                        if heading == "Coords":
                            for point in positions(value):
                                place = ["Guide NPC", *point, page]
                                if guide_dungeon:
                                    place.append("Dungeon")
                                add_unique(r["places"], place)
                        elif heading in ("Local", "Region", "Continent"):
                            self.fact(r, heading, value)
                    self.references(cell.getparent(), page, r)
            elif page.startswith("search/mobsearch"):
                for row in doc.xpath('//tr[td]'):
                    cells = [text(c) for c in row.xpath("./td")]
                    if len(cells) < 8 or not cells[1].isdigit():
                        continue
                    r = self.record("Creature", cells[0], page)
                    self.fact(r, "Guide level", cells[1])
                    for heading, value in zip(("Guide XP", "Guide group required", "Guide hitpoints", "Guide vigor",
                                               "Guide minimum damage", "Guide maximum damage"), cells[2:8]):
                        self.fact(r, heading, value)
                    if len(cells) >= 9:
                        self.fact(r, "Continent", cells[8])
            elif page == "maps/dungeons.html":
                for a in doc.xpath('//a[contains(@href,"dungeonimg/")]'):
                    name = text(a)
                    if not name:
                        continue
                    image = relative(page, a.get("href"))
                    r = self.record("Dungeon", vault_names.get(image, name), page)
                    if name != r["name"]:
                        self.fact(r, "Floor-plan title", name)
                    self.maps[image] = r
                    self.fact(r, "Map image", image)
                    heading = a.xpath("preceding::h3[1]")
                    if heading:
                        self.fact(r, "Continent", text(heading[0]))
            elif page == "vaults.html":
                for row in doc.xpath("//tr[td]"):
                    cells = row.xpath("./td")
                    if len(cells) != 6 or not list(positions(text(cells[2]))):
                        continue
                    r = self.record("Dungeon", text(cells[0]), page)
                    self.fact(r, "Vault level", text(cells[1]))
                    self.fact(r, "Region", text(cells[4]))
                    self.fact(r, "Continent", text(cells[3]))
                    for point in positions(text(cells[2])):
                        add_unique(r["places"], ["Vault entrance", *point, page])
                    for mob in text(cells[5]).split(","):
                        if clean(mob):
                            self.link(r, self.record("Creature", clean(mob)), "Glyph source")
            else:
                self.items(doc, page)

    def world(self, content, world, templates):
        def catalog(filename):
            return json.loads((content / filename).read_bytes())

        native = {f'{int(entity):08x}': t for entity, t in templates.items()}
        names = {entity: workbook_name(t["name"]) for entity, t in native.items() if t.get("name")}
        names.update({entity: workbook_name(name) for entity, _, name, *_ in data_rows(content / "definitions.tsv")
                      if name and name != "-"})
        recovered_actors = {}
        for entity, _, table, name in data_rows(content / "recovered-actor-names.tsv"):
            names[entity] = name
            recovered_actors[table] = entity
        for entity, (name, _) in QUALITY_NAMES.items():
            names.setdefault(entity, name)
        for entity in PORTAL_CLASSES:
            names.setdefault(entity, "Portal")
        by_name = defaultdict(set)
        for entity, name in names.items():
            by_name[key(name)].add(entity)

        def definition(kind, entity, name=None):
            r = self.record(kind, name or names.get(entity, entity))
            add_unique(r.setdefault("ids", []), entity)
            if entity in QUALITY_NAMES and r["name"] == QUALITY_NAMES[entity][0]:
                table = QUALITY_NAMES[entity][1]
                source = f"English table {table}, key a8607cec" if table else "inline English name"
                self.fact(r, "Retail name evidence", f"{entity} · gmQualities 81{entity[2:]} · {source}")
            if entity in PORTAL_CLASSES and r["name"] == "Portal":
                self.fact(r, "Retail name evidence", f"{entity} · portal visual {PORTAL_CLASSES[entity]}; "
                          "named captures share this class. These instances have no recovered destination name.")
            return r

        for page, family in sorted(self.item_families):
            pattern, role = ITEM_FAMILIES[page][family]
            if not pattern:
                continue
            for entity in sorted(names):
                item_name = names[entity]
                if not re.fullmatch(pattern, item_name):
                    continue
                if family == "elegant atlan <weapontype>" and "Weapon_Damage" not in native.get(entity, {}).get("props", {}):
                    continue
                item = definition("Item", entity)
                for q in self.page_quests[page]:
                    if page == "quest/shapershoardkeys.html" and not q["name"].endswith(item_name.split()[0]):
                        continue
                    self.link(q, item, role)

        # Both the localized item and the guide retain the question marks as part of the badge's name.
        badge_page = "quest/knorr_battlefieldofdreams.html"
        badge = definition("Item", "47003efe")
        if badge["name"] == "Gear Badge #?????":
            add_unique(badge["sources"], badge_page)
            for q in self.page_quests[badge_page]:
                self.link(q, badge, "Quest starter")

        professions = catalog("professions-v1.json")
        for entity, _, name, *_ in data_rows(content / "loot-items.tsv"):
            self.fact(definition("Item", entity, workbook_name(name)), "Server catalog", "Loot item")
        for tag, rows, id_field in [
            ("Treasure weapon", catalog("treasure-weapons.json")["weapons"], "definition"),
            ("Recovered apparel", catalog("recovered-apparel-evidence.json"), "definition"),
            ("Crafting item", professions["items"], "id"),
        ]:
            for row in rows:
                self.fact(definition("Item", f'{row[id_field]:08x}', workbook_name(row["name"])), "Server catalog", tag)

        craft_items = {row["id"]: definition("Item", f'{row["id"]:08x}', workbook_name(row["name"]))
                       for row in professions["items"]}
        skill_names = {row["id"]: row["name"] for row in professions["skills"]}
        workshops = defaultdict(list)
        for row in professions["workshops"]:
            workshops[row["effect"]].append(definition("World object", f'{row["id"]:08x}'))
        for recipe in professions["recipes"]:
            profession = skill_names[recipe["skill"]]
            r = self.record("Recipe", f'{recipe["name"]} ({profession})')
            add_unique(r.setdefault("ids", []), f'{recipe["id"]:08x}')
            self.fact(r, "Profession", profession)
            self.fact(r, "Recipe action", recipe["kind"])
            if LUDWARD_RECIPES.fullmatch(recipe["name"]):
                for q in self.page_quests[LUDWARD]:
                    self.link(q, r, "Quest crafting recipe")
            for station in workshops.get(recipe["workshop"], []):
                self.link(r, station, "Required workshop")
            for ingredient in recipe["ingredients"]:
                role = f'Ingredient option for slot {ingredient["ordinal"]} ({ingredient["name"]})'
                for item in ingredient["choices"]:
                    self.link(r, craft_items[item], role)
            for output in recipe["outputs"]:
                self.link(r, craft_items[output["item"]], "Possible recipe output")
        for row in professions["butcher"]:
            creature = definition("Creature", f'{row["id"]:08x}')
            self.link(creature, craft_items[row["material"]], "Butchery material")
            if row["profile"] == 0x6f0028a8:  # Shreth, see harvesting::Butcher::yield_material
                self.link(creature, craft_items[0x470027a7], "Butchery material")
        for row in professions["mines"]:
            mine = definition("World object", f'{row["id"]:08x}')
            self.link(mine, craft_items[row["material"]], "Harvested material")
        for row in professions["maps"]:
            survey = craft_items[row["id"]]
            self.link(survey, definition("World object", f'{row["mine"]:08x}'), "Survey deposit")
            west, south, east, north = row["bounds"]
            self.fact(survey, "Search bounds (world meters)", f'({west}, {south}) to ({east}, {north})')
        for rule in catalog("native-loot-limits.json")["rules"]:
            entity = f'{rule["entity"]:08x}'
            creature = definition("Creature", entity)
            add_unique(creature.setdefault("lootLimits", []), [entity, rule["minimum"], rule["maximum"], rule["level"]])

        cameron_id = recovered_actors["NPCTable_OfficerCameron"]
        npc_ids = {cameron_id}
        cameron = definition("NPC", cameron_id)
        cameron_page = "quest/osteth_busted.html"
        add_unique(cameron["sources"], cameron_page)
        self.fact(cameron, "Identification", "The retail actor uses NPCTable_OfficerCameron. "
                  "The Busted! guide describes attacking him on patrol in Cragstone, Ikeras and Linvak Tukal.")
        for quest in self.page_quests[cameron_page]:
            self.link(quest, cameron, "Quest target", "Appears in quest")
            for label, lat, lon, page in quest["places"]:
                if page == cameron_page and label == "Guide start":
                    add_unique(cameron["places"], ["Documented patrol location", lat, lon, page])

        creatures = set()
        for pattern in ("*creature-ai*.tsv", "*creature-combat*.tsv", "ac2hero-raid-ai*.tsv"):
            for file in sorted(content.glob(pattern)):
                creatures.update(f[0] for f in data_rows(file))
        for entity in sorted(creatures):
            definition("NPC" if entity in npc_ids else "Creature", entity)
        for region in catalog("wilderness-content-v1.json")["regions"]:
            for monster in region["creatures"]:
                for variant in monster["variants"]:
                    self.fact(definition("Creature", f"{variant:08x}"), "Habitat", region["name"])
        for filename in ("npc-dialogue-v1.json", "ac2hero-settlements-v1.json"):
            for profile in catalog(filename)["profiles"]:
                entity = f'{profile["entity"]:08x}'
                npc_ids.add(entity)
                self.fact(definition("NPC", entity, profile["name"]), "Server catalog", "NPC conversation")
        for npc in catalog("ac2hero-settlements-v1.json")["npcs"]:
            npc_ids.add(f'{npc["entity"]:08x}')

        policy_titles = {}
        active_policies = {}
        for filename, field in (("windmill-v1.json", "native_fields"), ("esper-camp-v1.json", "native_fields"),
                                ("warder-notebook-v1.json", "fields")):
            for entry in catalog(filename)[field]:
                if entry["field"] == "name":
                    title = workbook_name(entry["text_raw"])
                    policy_titles[entry["quest"]] = title
                    self.fact(self.record("Quest", title), "Quest policy", filename)
        windmill = self.record("Quest", policy_titles["400000c7"])
        self.fact(windmill, "Server completion XP", "200 XP (guide overview; the transcript lists 150 XP).")
        self.fact(windmill, "Renewed Focus", "+40 maximum vigor for three hours in both installed DATs; the guides describe +10.")
        self.link(windmill, definition("Item", "470010a0"), "Quest reward")
        mines = catalog("arwic-mines-v1.json")
        policy_titles["400000c1"] = mines["title"]
        self.fact(self.record("Quest", mines["title"]), "Quest policy", "arwic-mines-v1.json")
        shelter = self.record("Quest", "Rescue the Shelter Hold", "quest/osteth_rescuetheshelterhold.html")
        self.fact(shelter, "Quest policy", "shelter.rs")
        self.fact(shelter, "AC2E journal ID", "4000ff05")
        add_unique(shelter.setdefault("ids", []), "4000ff05")
        for kind, role, entities in (("NPC", "Quest giver", (0x4700157d,)),
                                     ("NPC", "Quest recipient", (0x4700157e, 0x47001581, 0x47001580, 0x4700157f)),
                                     ("Creature", "Quest target", (0x4700158b, 0x4700158c)),
                                     ("Item", "Quest item", (0x47001587, 0x47001582))):
            for entity in entities:
                self.link(shelter, definition(kind, f'{entity:08x}'), role)
        shreth = self.record("Quest", "Shreth-Hide Leather", "quest/omishan_shrethhideleather.html")
        self.fact(shreth, "Quest policy", "omishan_collection.rs")
        self.fact(shreth, "AC2E journal ID", "4000fe03")
        self.fact(shreth, "Rewards", "120,000 XP, 1,250 gold and 600 quest points.")
        add_unique(shreth.setdefault("ids", []), "4000fe03")
        for kind, entity, role in (("NPC", "470021b7", "Quest giver"), ("NPC", "470021b8", "Quest recipient"),
                                   ("Creature", "470021c1", "Quest target"), ("Item", "470021b1", "Quest item"),
                                   ("Item", "470021b0", "Quest item"), ("Item", "470027a7", "Quest item")):
            self.link(shreth, definition(kind, entity), role)
        self.link(definition("Item", "470021b1"), definition("Creature", "470021c1"), "Quest-only drop")
        self.link(definition("Item", "470021b0"), definition("NPC", "470021b7"), "Quest item source", "Provides quest item")
        required_quests = []
        for file in ("exploration-content-v1.json", "hunting-content-v1.json", "hunting-content-v2.json", "hunting-content-v3.json", "hunting-content-v4.json", "hunting-content-v5.json", "hunting-content-v6.json",
                     "inferred-content-v1.json", "warder-referrals-v1.json", "drudgerton-referral-v1.json", "ac2hero-hunts-v2.json", "npc-bounties-v1.json"):
            policy = catalog(file)
            quests = list(policy["quests"])
            if "referral" in policy:
                quests.append(policy["referral"])
            for q in quests:
                r = self.record("Quest", q["title"])
                quest_id = f'{q["id"]:08x}'
                policy_titles[quest_id] = q["title"]
                active_policies[quest_id] = q
                add_unique(r.setdefault("ids", []), quest_id)
                self.fact(r, "Quest policy", file)
                if reward := q.get("key"):
                    self.link(r, definition("Item", f'{reward["item"]:08x}', reward["name"]), "Quest reward")
                if q.get("delivery") and q.get("item") and q.get("giver"):
                    item = definition("Item", f'{q["item"]:08x}')
                    self.link(r, item, "Delivery item")
                    self.link(item, definition("NPC", f'{q["giver"]:08x}'), "Quest item source", "Provides quest item")
                for prior in q.get("prerequisites", []):
                    required_quests.append((r, f'{prior:08x}'))
                evidence_rows = q.get("evidence", [])
                for evidence in [evidence_rows] if isinstance(evidence_rows, dict) else evidence_rows:
                    url = (evidence.get("url") or evidence.get("guide") or "") if isinstance(evidence, dict) else ""
                    if url.startswith(BASE+"quest/"):
                        page = relative("", url)
                        if page in self.page_quests:
                            add_unique(r["sources"], page)
                            for other in self.page_quests[page]:
                                self.link(r, other, "Same quest guide")
                for field, kind, role in (("targets", "Creature", "Quest target"), ("kill_starters", "Creature", "Quest starter"),
                                          ("use_starters", "Item", "Quest starter"), ("objectives", None, "Quest objective"),
                                          ("npc", "NPC", "Quest giver"), ("giver", "NPC", "Quest giver"), ("recipient", "NPC", "Quest recipient")):
                    ids = q.get(field, [])
                    for id_ in ids if isinstance(ids, list) else [ids]:
                        entity = f'{id_:08x}'
                        inferred_kind = kind or ("NPC" if entity in npc_ids else "World object")
                        self.link(r, definition(inferred_kind, entity), role)

        for journal, title, page, xp, actors in (
            ("4000002b", "The Slavetaker's Head", "osteth_theslavetakershead", 4000,
             [("NPC", "4700088a", "Quest giver"), ("Creature", "4700127e", "Quest target"),
              ("Item", "4700109d", "Quest item"), ("Item", "470011b8", "Quest reward")]),
            ("400000d6", "Seek the Drudge Counselors", "osteth_seekthedrudgecounselors", 500,
             [("NPC", "470015ca", "Quest giver"), ("NPC", "4700088a", "Quest objective")]),
        ):
            record = self.record("Quest", title, f"quest/{page}.html")
            policy_titles[journal] = title
            self.fact(record, "Quest policy", "slavetaker.rs")
            self.fact(record, "AC2E journal ID", journal)
            policy = active_policies[journal] = {"title": title, "xp": xp}
            for kind, did, role in actors:
                self.link(record, definition(kind, did), role)
                if role == "Quest giver":
                    policy["giver"] = int(did, 16)
                elif role in ("Quest objective", "Quest target"):
                    policy.setdefault("objectives", []).append(int(did, 16))
        self.link(self.record("Quest", "Seek the Drudge Counselors"), self.record("Quest", "The Slavetaker's Head"),
                  "Requires quest", "Advances quest")
        self.link(definition("Item", "4700109d"), definition("Creature", "4700127e"), "Quest-only drop")

        for update in catalog("incursion-rewards-v3.json"):
            active_policies[update["id"]]["xp"] = update["xp"]

        slimelord = catalog("slimelord-hunt-v1.json")
        did = f'{slimelord["id"]:08x}'
        active_policies[did].update((key, slimelord[key]) for key in ("level", "xp", "limit_seconds"))
        row = self.record("Quest", policy_titles[did], relative("", slimelord["source"]))
        self.fact(row, "Updated hunt rules", f'Level {slimelord["level"]}, {slimelord["xp"]:,} XP, {slimelord["limit_seconds"] // 60}-minute limit.')
        self.fact(row, "Reset evidence", slimelord["reset_evidence"])
        self.fact(row, "Route evidence", slimelord["route_evidence"])

        revised_hunts = catalog("hunting-updates-v7.json")
        for correction in revised_hunts["quests"]:
            did = f'{correction["id"]:08x}'
            active_policies[did].update(level=correction["level"], xp_cap=correction["xp"],
                                        limit_seconds=revised_hunts["limit_seconds"],
                                        repeat_seconds=revised_hunts["repeat_seconds"])
            row = self.record("Quest", policy_titles[did], relative("", correction["source"]))
            self.fact(row, "Updated hunt rules", f'Level {correction["level"]}, {correction["xp"]:,} XP, six-day reset.')

        for hunt in catalog("npc-bounties-v1.json")["starter_hunts"]:
            did = f'{hunt["id"]:08x}'
            if giver := hunt.get("giver"):
                # The Ancestor is an alternative to consuming the trophy.
                active_policies[did]["alternate_giver"] = giver["npc"]
                self.link(self.record("Quest", policy_titles[did]),
                          definition("NPC", f'{giver["npc"]:08x}', giver["speaker"]), "Alternative quest giver")

        for quest, prior in required_quests:
            self.link(quest, self.record("Quest", policy_titles[prior]), "Requires quest", "Unlocks quest")

        recovered_titles = {entry["quest"]: workbook_name(entry["text_raw"])
                            for entry in catalog("recovered-quest-text.json")["candidates"] if entry["field"] == "name"}
        for q in catalog("native-quests.json")["quests"]:
            quest_id = f'{q["id"]:08x}'
            title = workbook_name(q["name"]["text"] or recovered_titles.get(quest_id) or policy_titles.get(quest_id) or q["package"])
            package_title = title == q["package"]
            r = self.record("Quest", journal_label(title) if package_title else title)
            if package_title:
                basis = ("The English title repeats the package name." if q["name"]["text"] else
                         "No English title was recovered.")
                self.fact(r, "Display name", f"Descriptive label from the retail journal package. {basis}")
            self.fact(r, "Journal definition", quest_id)
            self.fact(r, "Journal package", q["package"])
            add_unique(r.setdefault("ids", []), quest_id)
            if not q["name"]["text"] and quest_id in recovered_titles:
                self.fact(r, "Recovered journal title", "Historical English archive (Pyreal)")
            if quest_id in policy_titles:
                implemented = self.record("Quest", policy_titles[quest_id])
                self.link(r, implemented, "Server quest identity")
                for filename in implemented["facts"].get("Quest policy", []):
                    self.fact(r, "Quest policy", filename)

        link_quest_policies(self, content, policy_titles)

        known_kinds = defaultdict(set)
        guide_kinds = defaultdict(set)
        for (kind, name), r in self.records.items():
            if kind in ("NPC", "Item", "Creature"):
                if r["sources"]:
                    guide_kinds[name].add(kind)
                for entity in r.get("ids", []):
                    known_kinds[entity].add(kind)
        for entity, template in native.items():
            role = template.get("role")
            if role == "monster":
                known_kinds[entity].add("Creature")
            elif role == "vendor" or entity in npc_ids:
                known_kinds[entity].add("NPC")
            elif "Item_Value" in template.get("props", {}):
                known_kinds[entity].add("Item")
        for (kind, name), r in list(self.records.items()):
            if kind in ("NPC", "Item", "Creature") and r["sources"]:
                for entity in sorted(by_name.get(name, set())):
                    matches = known_kinds.get(entity, guide_kinds[name])
                    if kind in matches and (entity in known_kinds or len(matches) == 1):
                        add_unique(r.setdefault("ids", []), entity)

        # Keep one placement per instance, with its authored instance name. Several
        # native definitions are reused for signs, portals and other named objects.
        npc_names = {name for kind, name in self.records if kind == "NPC"}
        item_names = {name for kind, name in self.records if kind == "Item"}
        routes = portal_routes(content, world)
        portal_labels = {f"814000000003{suffix:04x}": label
                         for suffixes, label in [([2, 4, 0x49], "Arwic Outland Portal"),
                            ([5, 0x22, 0x25], "Human Shelter Portal"), ([0x30], "Tumerok Shelter Portal"),
                            ([0x31], "Lugian Shelter Portal"), ([0x47], "Menhir Vault Portal")]
                         for suffix in suffixes}
        exits = {1: "Arwic Outland Portal", 2: "Esper Reaches Portal", 4: "Lost Wish Portal"}
        for area in catalog("starter-areas-v1.json")["areas"]:
            for actor in area["actors"]:
                if actor["role"] == "menhir-portal":
                    portal_labels[actor["id"]] = "Menhir Vault Portal"
                if actor["role"] == "shortcut-portal":
                    portal_labels[actor["id"]] = "Quick Exit to the Arwic Outlands"
            for actor in area["vault"]["actors"]:
                if actor["role"] == "exit":
                    portal_labels[actor["id"]] = exits[area["race"]]
        settlement = catalog("ac2hero-settlements-v1.json")
        placement_evidence = {}
        def note_at(iid, pack, note, url=None):
            placement_evidence[iid] = (pack, note, url)

        for n in settlement["npcs"]:
            note_at(n["id"], "ac2hero-settlements-v1.json",
                    "Terrain-validated placement near the guide coordinate.", n["evidence"]["url"])
        for pack in ["ac2hero-settlements-v1.json", "hero-wilderness-camps-v2.json"]:
            for camp in catalog(pack)["camps"]:
                for slot in range(len(camp["roster"])):
                    note_at(camp["id"] + slot, pack, camp["evidence"]["basis"], camp["evidence"]["url"])
        workshops = catalog("workshops-content-v1.json")
        for station in workshops["npcs"]:
            ev = station["evidence"]
            if "url" in ev:
                note = f'{ev["town"]} · guide {ev["compass"]}. Reconstructed on walkable terrain.'
                if "model_reference" in ev:
                    note += " " + ev["model_reference"]
                note_at(station["id"], "workshops-content-v1.json", note, ev["url"])
        for station in workshops["captured_stations"]:
            ev = station["evidence"]
            note_at(station["id"], "workshops-content-v1.json",
                    f'{ev["town"]} · guide {ev["compass"]}. Position and model confirmed by capture.', ev["url"])
        for npc in catalog("quest-encounters-v1.json")["npcs"]:
            evidence = npc["evidence"]
            note_at(npc["id"], "quest-encounters-v1.json",
                    f'Walkable placement at {evidence["town"]}, near the reported guide coordinate.',
                    evidence["source"])
        crypt_pack = catalog("fiery-crypt-map-v1.json")
        floor_plan = next(source["url"] for source in crypt_pack["evidence"] if "/dungeonimg/" in source["url"])
        crypt_ids = set()
        for marker in crypt_pack["interiors"]:
            crypt_ids.add(marker["id"])
            x, y = marker["map_pixel"]
            note_at(marker["id"], "fiery-crypt-map-v1.json", f"Floor-plan marker ({x}, {y}), fitted to a native room.",
                    floor_plan)
        for pack in ["illuminated-cave-v1.json", "warder-hideout-v1.json", "vault-world-v1.json",
                     "cragstone-excavation-v1.json", "the-ossuary-v1.json", "quest-encounters-v1.json",
                     "crystal-caves-v1.json", "golden-age-reliquary-v1.json", "drudgerton-referral-v1.json"]:
            contents = catalog(pack)
            report = catalog(pack.replace(".json", ".report.json"))
            repairs = {row["id"] for row in report.get("floor_repairs", [])}
            for group in report.get("groups", []):
                if group.get("roaming_homes"):
                    guide = next(url for url in contents["sources"] if url.startswith(BASE + "quest/"))
                    for iid in group["members"]:
                        note_at(iid, pack, f'One of {len(group["members"])} possible homes for one creature. '
                                "The guide identifies the area. AC2E chooses between terrain-validated positions.", guide)
            for actor in [*contents["interiors"], *contents.get("portals", [])]:
                note = f'Source instance {actor["capture_id"]}, template {actor["capture_entity"]}.'
                if captures := actor.get("captures"):
                    note += f' Seen in {len(captures)} retail capture{"s" if len(captures) != 1 else ""}.'
                if actor["id"] in repairs:
                    note += " Adjusted within the captured room for walkable ground in the Legions DAT."
                note_at(actor["id"], pack, note)
        crypt = self.record("Dungeon", "Fiery Crypt")
        captured_dungeons = {block: self.record("Dungeon", name) for block, name in (
            (0x07f3, "Warder Hideout"), (0x07f2, "Illuminated Drudge Cave"),
            (0x05f9, "Cragstone Excavation"), (0x07fb, "Ossuary"), (0xb880, "Water Temple"),
            (0x07f7, "Crystal Caves"), (0x07fa, "New Mage Academy"), (0x0afc, "Golden Age Reliquary"))}
        referral_actors = {a["id"]: a for a in catalog("drudgerton-referral-v1.json")["interiors"]}
        placed = set()
        item_sources = set()
        portal_transitions = set()
        for f in data_rows(world):
            iid, entity, _, cell, x, y, z = f[:7]
            if actor := referral_actors.get(int(iid, 16)):
                if (int(entity, 16), f[-1]) != (actor["entity"], actor["name"]):
                    raise ValueError(f"Incorrect Drudgerton actor at {iid}")
                entity = f'{actor["quest_entity"]:08x}'
            placed.add(int(entity, 16))
            name = workbook_name(f[-1]) if f[-1] != "-" else names.get(entity, entity)
            if f[-1] == "-" and entity == "4700084d":
                name = portal_labels.get(iid, "Portal")
            roles = known_kinds.get(entity, set())
            if entity in npc_ids or "NPC" in roles:
                kind = "NPC"
            elif entity in creatures or "Creature" in roles:
                kind = "Creature"
            elif "Item" in roles:
                kind = "Item"
            elif key(name) in npc_names:
                kind = "NPC"
            elif key(name) in item_names:
                kind = "Item"
            else:
                kind = "World object"
            r = definition(kind, entity, name)
            number = int(cell, 16)
            outdoor = 1 <= number % 65536 <= 64
            pos = [round(((number >> 16 & 255) * 8 + float(y)/20)/10.2-100, 3),
                   round(((number >> 24) * 8 + float(x)/20)/10.2-100, 3)] if outdoor else []
            r.setdefault("spawns", []).append([iid, entity, cell, float(x), float(y), float(z), *pos])
            instance = int(iid, 16)
            if instance in crypt_ids:
                add_unique(crypt.setdefault("interiorBlocks", []), cell[:4])
                self.link(crypt, r, "Floor-plan encounter")
            dungeon = captured_dungeons.get(number >> 16)
            if dungeon and kind in ("Creature", "NPC") and instance in placement_evidence:
                if 0x100 <= number & 0xffff <= 0xfffb:
                    add_unique(dungeon.setdefault("interiorBlocks", []), cell[:4])
                self.link(dungeon, r, "Captured dungeon encounter")
            if dungeon and iid in routes and 1 <= int(routes[iid][6], 16) % 65536 <= 64:
                self.link(dungeon, r, "Surface exit")
            if instance in placement_evidence:
                pack, basis, url = placement_evidence[instance]
                evidence = [pack, basis]
                if url:
                    evidence.append(url)
                r.setdefault("placementEvidence", {})[iid] = evidence
                if url and (page := relative("", url)):
                    add_unique(r["sources"], page)
            if iid in routes:
                dest = routes[iid]
                portal_transitions.add((number, int(dest[6], 16)))
                self.fact(r, "Portal route", f'{iid} Â· {cell} â†’ {dest[6]} ({", ".join(dest[7:10])} m)')
                if 0x100 <= int(dest[6], 16) & 0xffff <= 0xfffb:
                    add_unique(r.setdefault("arrivals", []), [dest[6], *map(float, dest[7:10])])
                dungeon = self.records.get(("Dungeon", key(name)))
                if not dungeon and outdoor:
                    dungeon = captured_dungeons.get(int(dest[6], 16) >> 16)
                if dungeon:
                    self.link(dungeon, r, "Server portal")
        for f in data_rows(content / "observed-loot.tsv"):
            creature = definition("Creature", f[0])
            bundle = [names.get(item, item) for item in f[3].split(",")]
            add_unique(creature.setdefault("bundles", []), [f[0], int(f[2]), bundle, f[4]])
            for item in f[3].split(","):
                if int(f[0], 16) in placed and int(f[2]) > 0:
                    item_sources.add(int(item, 16))
                if item in names:
                    self.link(creature, definition("Item", item), "Observed loot")
        excavation = captured_dungeons[0x05f9]
        for block, page in ((0x05f9, "quest/osteth_cragstoneexcavation.html"),
                            (0x07fb, "quest/linvak_killossein.html"),
                            (0xb880, "quest/omishan_killchiefbenamu.html"),
                            (0x07f7, "quest/linvak_killcloudburst.html"),
                            (0x07fa, "quest/linvak_killthegurogconjurer.html"),
                            (0x0afc, "quest/linvak_killthemasterguroghenchman.html")):
            dungeon = captured_dungeons[block]
            add_unique(dungeon["sources"], page)
            for quest in self.page_quests[page]:
                self.link(dungeon, quest, "Dungeon quest")
        library = catalog("cragstone-excavation-v1.json")["excluded_library_evidence"]
        self.fact(excavation, "Sealed library", f'The portal is recorded in {library["cell"]}, leading to '
                  f'{library["destination_cell"]}. The quest recipe and fellowship passage are still unimplemented.')
        for policy in catalog("hero-wilderness-camps-v2.json")["quest_evidence"].values():
            page = relative("", policy["guide"])
            temple = self.maps[relative("", policy["dungeon"]["map_url"])]
            for quest in self.page_quests[page]:
                self.link(temple, quest, "Dungeon quest")
                self.fact(quest, "AC2E encounter status", policy["status"])
                for stage in policy["fort_stages"]:
                    preceptor = definition("Creature", stage["preceptor"])
                    talisman = definition("Item", stage["talisman"])
                    self.link(quest, preceptor, "Quest target")
                    self.link(preceptor, talisman, "Documented quest drop (pending)")
            for door in policy["dungeon"]["door_captures"]:
                talisman = definition("Item", door["matching_talisman"])
                self.fact(talisman, "Temple door evidence",
                          f'Lock {door["lock_key"]} in room {door["native_fixture_cell"]}.')
        for rule in catalog("cragstone-excavation-loot-v1.json")["rules"]:
            creature = definition("Creature", f'{rule["entity"]:08x}')
            item = definition("Item", f'{rule["definition"]:08x}')
            self.link(creature, item, "Cragstone Excavation loot")
            self.link(excavation, item, "Dungeon loot")
            sample = f'{item["name"]} on {rule["matches"]} of {rule["sampled"]} sampled {creature["name"]} corpses.'
            basis = "The 2017 DAT supplies the trophy rate." if "rate_source" in rule else "AC2E estimates the drop chance from this sample."
            self.fact(item, "Cragstone Excavation loot evidence", f"{sample} {basis}")
        for filename, role in (("native-trophies.json", "Native trophy"),
                               ("hero-reference-trophies-v1.json", "2017 DAT trophy")):
            for rule in catalog(filename)["rules"]:
                entity = f'{rule["entity"]:08x}'
                # Named descendants already have their own resolved trophy slots.
                # Listing their unnamed, non-placeable parents invents a loot carrier.
                if (names.get(entity) == "???" and native.get(entity, {}).get("props", {}).get("Placeable") == 0
                        and rule["entity"] not in placed):
                    continue
                # Named template parents can carry inheritance evidence of their
                # own without representing another encounterable monster.
                template_parent = (names.get(entity, "").casefold().endswith(" template")
                                   and native.get(entity, {}).get("props", {}).get("Placeable") == 0
                                   and rule["entity"] not in placed)
                creature = definition("Native definition" if template_parent else "Creature", entity)
                for drop in rule["drops"]:
                    if rule["entity"] in placed and drop["probability"] > 0:
                        item_sources.add(drop["definition"])
                    item_record = definition("Item", f'{drop["definition"]:08x}', workbook_name(drop["name"]) if drop.get("name") else None)
                    self.link(creature, item_record, role)
                    chance = round(100 * drop["probability"], 6)
                    add_unique(creature.setdefault("trophies", []), [entity, drop["slot"], item_record["name"], chance, role])

        def store_definition(kind, id_):
            entity = f'{id_:08x}'
            return definition(kind, entity, workbook_name(native.get(entity, {}).get("name") or names.get(entity, entity)))

        for store in catalog("native-store-stock.json")["stores"]:
            for id_ in store["vendors"]:
                trader = store_definition("NPC", id_)
                for offer in store["offers"]:
                    if id_ in placed:
                        item_sources.add(offer["item"])
                    item = store_definition("Item", offer["item"])
                    self.link(trader, item, "Native store offer")
                    currency = native.get(f'{offer["currency"]:08x}', {}).get("name") or f'{offer["currency"]:08x}'
                    add_unique(trader.setdefault("offers", []), [f'{store["store"]:08x}', offer["ordinal"],
                                                                 item["name"], offer["unit_price"], currency])

        audit_routes(self, active_policies, placed, item_sources, catalog("quest-point-rewards-v1.json"),
                     native, portal_transitions, catalog("new-mage-academy-evidence-v1.json"))

        registered = defaultdict(list)
        for record in self.records.values():
            for entity in record.get("ids", []):
                registered[entity].append(record)
        for entity in sorted(set(names) | native.keys()):
            template = native.get(entity, {})
            props = template.get("props", {})
            if entity not in registered:
                display_name = workbook_name(template.get("name") or names.get(entity, entity))
                # A shared placeholder provides no identity. Captured or otherwise
                # identified instances were registered above with their evidence.
                if display_name == "???":
                    continue
                if template.get("role") == "monster":
                    kind = "Creature"
                elif template.get("role") == "vendor":
                    kind = "NPC"
                elif any(k in props for k in ("Item_Value", "Item_ArmorLevel", "Weapon_Damage", "Item_IsStackable")):
                    kind = "Item"
                else:
                    kind = "Native definition"
                registered[entity].append(definition(kind, entity, display_name))
            for r in registered[entity]:
                if template:
                    self.fact(r, "Native template", entity)

    def export(self):
        rows = sorted(self.records.values(), key=lambda r: (r["kind"], key(r["name"])))
        # Walkthroughs often repeat the same indoor NPC's compass coordinates. Keep
        # the attribution on each citation and on quest waypoints drawn from that page.
        waypoint_regions = {}
        for actor in rows:
            if actor["kind"] != "NPC":
                continue
            indoor = {(p[1], p[2]) for p in actor["places"] if p[4:] == ["Dungeon"]}
            if entrance := NPC_DUNGEON_ENTRANCES.get(actor["name"]):
                ns, ew, page, explanation = entrance
                indoor.add((ns, ew))
                waypoint_regions[(ns, ew, page)] = "Dungeon"
                self.fact(actor, "Region", "Dungeon")
                self.fact(actor, "Location evidence", explanation)
            disputed, evidence = DISPUTED_NPC_LOCATIONS.get(actor["name"], ((), None))
            for citation in disputed:
                waypoint_regions[citation] = "Disputed"
            for p in actor["places"]:
                citation = tuple(p[1:4])
                if (p[1], p[2]) in indoor:
                    p[4:] = ["Dungeon"]
                elif citation in disputed:
                    p[4:] = ["Disputed"]
                    self.fact(actor, "Location evidence", evidence)
                if p[4:]:
                    waypoint_regions[citation] = p[4]
        for quest in rows:
            if quest["kind"] == "Quest":
                for p in quest["places"]:
                    region = waypoint_regions.get(tuple(p[1:4]))
                    if region:
                        p[4:] = [region]
        return {"source": BASE, "pages": self.pages, "records": rows}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cache", type=Path)
    ap.add_argument("world", type=Path)
    ap.add_argument("output", type=Path)
    inputs = ap.add_mutually_exclusive_group()
    inputs.add_argument("--dat", type=Path, help="Read the native template catalog from a retail DAT directory")
    inputs.add_argument("--templates", type=Path, default=Path("Saved/current-native-templates.json"),
                        help="Reuse a previously exported native template catalog")
    args = ap.parse_args()
    atlas = Atlas(args.cache)
    atlas.guides()
    templates = dat_templates(args.dat) if args.dat else json.loads(args.templates.read_bytes())
    atlas.world(Path(__file__).resolve().parents[1]/"content", args.world, templates)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("window.AC2Atlas = " + json.dumps(atlas.export(), ensure_ascii=False, separators=(",", ":")) + ";\n", encoding="utf-8")
