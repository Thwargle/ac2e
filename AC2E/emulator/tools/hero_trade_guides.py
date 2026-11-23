"""Index AC2Hero trade tables and directly attributed gathering/NPC locations."""
import re
from urllib.parse import parse_qs, urlsplit


def content(node):
    return " ".join(node.text_content().split())


def rows(body):
    for row in body.xpath(".//tr[not(.//tr)]"):
        yield [content(cell) for cell in row.xpath("./td|./th")]


def place(record, coordinates, label, page):
    for point in coordinates:
        location = [label, *point, page]
        if location not in record["places"]:
            record["places"].append(location)


def quest_locations(atlas, body, page, positions):
    for link in body.xpath('.//a[contains(@href,"npcsearch") and contains(@href,"search=")]'):
        name = parse_qs(urlsplit(link.get("href")).query).get("search", [""])[0]
        if not name or content(link).casefold() != name.casefold():
            continue
        # Only attribute a coordinate written immediately after the linked NPC.
        following = re.match(r"\s*(?:at\s+)?\(([^()]{3,40})\)", link.tail or "", re.I)
        if following and (coords := list(positions(following[1]))):
            place(atlas.record("NPC", name, page), coords, "Guide NPC", page)
    if page == "quest/osteth_sparkselements.html":
        for paragraph in body.xpath(".//p"):
            match = re.match(r"Retrieve 3 (Barium|Stronium) elements from (\w+)[’']s mine, travel to \(([^()]+)\)", content(paragraph))
            if match:
                place(atlas.record("Item", match[1], page), positions(match[3]),
                      f"Mine access · {match[2]}'s mine", page)
    if page == "quest/linvak_greenthumb.html":
        match = re.search(r"found within the Mucor Mushroom Grotto\s*\(([^)]+)\)", content(body), re.I)
        if match:
            place(atlas.record("Item", "Mucor Mushroom", page), positions(match[1]),
                  "Gather inside · Mucor Mushroom Grotto entrance", page)


def dye_garden(atlas, body, page, positions):
    match = re.search(r"homestead of Regani\s*\(([^)]+)\)", content(body))
    if not match:
        return
    coords = list(positions(match[1]))
    vendor = atlas.record("NPC", "Regani", page)
    place(vendor, coords, "Guide NPC · east of Cragstone", page)
    for name in ("Watering Can", "Bag of Fertilizer", "Regani's Guide to Dye Plant Cultivation"):
        atlas.link(vendor, atlas.record("Item", name, page), "Garden supplies")
    dye_page = "items/dye.html"
    if dye_page not in atlas.pages["pages"]:
        return
    plants = {}
    for cells in rows(atlas.read(dye_page)):
        if len(cells) < 3 or not cells[1].endswith(" Dye Plant") or not cells[2].endswith(" Plant"):
            continue
        color = cells[1].removesuffix(" Dye Plant")
        plants[color] = cells[2].removesuffix(" Plant")
        plant = atlas.record("Item", cells[2], dye_page)
        atlas.record("Item", cells[2], page)
        atlas.link(plant, atlas.record("Item", cells[1], dye_page), "Dye color")
        place(plant, coords, "Cultivation · Regani's fenced garden", page)
    for cells in rows(body):
        if len(cells) != 3 or not cells[0].endswith(" Seed") or not cells[1].endswith(" Pollen"):
            continue
        result = re.fullmatch(r"(\d+) (.+) Seeds", cells[2])
        colors = [cells[0].removesuffix(" Seed"), cells[1].removesuffix(" Pollen"), result[2] if result else ""]
        if not all(color in plants for color in colors):
            continue
        seed, pollen, output = (atlas.record("Item", plants[color] + suffix, page)
                                for color, suffix in zip(colors, (" Seed", " Pollen", " Seed")))
        atlas.fact(output, "Hybrid recipe", f"{seed['name']} + {pollen['name']} → {result[1]} seeds")
        atlas.link(output, seed, "Hybrid seed ingredient")
        atlas.link(output, pollen, "Hybrid pollen ingredient")


def index_trade_guide(atlas, doc, page, positions):
    supported = {"cartomancer.html", "workshops.html", "essence.html", "traits.html", "refining.html",
                 "survey.html", "spellbinding.html", "guides/dyeplantguide_spaz.html"}
    if page not in supported and not page.startswith("quest/"):
        return
    for body in doc.xpath('//div[contains(concat(" ",normalize-space(@class)," ")," tripper ")]'):
        if page.startswith("quest/"):
            quest_locations(atlas, body, page, positions)
        elif page == "guides/dyeplantguide_spaz.html":
            dye_garden(atlas, body, page, positions)
        elif page == "cartomancer.html":
            guide = atlas.record("Guide", "Surveying and Mining", page)
            for cells in rows(body):
                if len(cells) == 4 and cells[0].endswith(" the Cartomancer"):
                    npc = atlas.record("NPC", cells[0], page)
                    place(npc, positions(cells[3]), f"Guide NPC · {cells[2]} ({cells[1]})", page)
                    atlas.link(guide, npc, "Survey map travel")
        elif page == "workshops.html":
            headers = []
            for cells in rows(body):
                if cells[:2] == ["Continent", "Town"]:
                    headers = cells[2:]
                elif headers and len(cells) == len(headers) + 2:
                    for workshop, field in zip(headers, cells[2:]):
                        place(atlas.record("World object", workshop, page), positions(field),
                              f"Guide workshop · {cells[1]} ({cells[0]})", page)
        elif page == "essence.html":
            group = None
            for cells in rows(body):
                if len(cells) == 9 and cells[1] == "Trophy":
                    group = {"Anger": "Minor essence", "Wrath": "Major essence"}.get(cells[2])
                elif group and len(cells) == 9 and cells[1]:
                    item = atlas.record("Item", cells[1], page)
                    for essence in filter(None, cells[2:]):
                        atlas.fact(item, group, essence)
        elif page == "spellbinding.html":
            guide = atlas.record("Guide", "Spellbinding", page)
            for cells in rows(body):
                if len(cells) == 3 and cells[0] != "Class":
                    atlas.fact(guide, cells[0], f"Minor: {cells[1]} · Major: {cells[2]}")
        elif page == "traits.html":
            for cells in rows(body):
                if len(cells) != 6 or cells[0] == "Creature":
                    continue
                for material in cells[1:]:
                    if material in {"Bone", "Chitin", "Hide", "Sinew", "Tallow"}:
                        atlas.fact(atlas.record("Item", material, page), "Butchering source family", cells[0])
        elif page == "refining.html":
            workshop = atlas.record("World object", "Refining Workshop", page)
            for result, first, second in re.findall(r"\b(\w+)\s*=\s*(\w+)\s*\+\s*(\w+)\b", content(body)):
                material = atlas.record("Item", result, page)
                atlas.link(material, workshop, "Refined at")
                for name in (first, second):
                    atlas.link(material, atlas.record("Item", name, page), "Refining ingredient")
        elif page == "survey.html":
            guide = atlas.record("Guide", "Surveying and Mining", page)
            for paragraph in body.xpath(".//p"):
                match = re.fullmatch(r"(Red|Orange|Yellow|Green) Diamond\s*-\s*(.+)", content(paragraph), re.I)
                if match:
                    atlas.fact(guide, "Map marker", f"{match[1]} · {match[2].replace('Not to Far', 'Not too far')}")
            atlas.fact(guide, "Map use", "Hand a survey map to a cartomancer to travel to its search area. Use the map again while moving to locate the resource.")
