"""Extract native AC2 merchant offers and unit prices from retail Legions StoreTemplate graphs."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

from ac2_dat import Dat
from build_hero_atlas import dat_templates
from craft_catalog import graph


def extract(directory, templates):
    vendors = defaultdict(list)
    for entity, template in templates.items():
        if store := template["props"].get("StoreTemplate"):
            vendors[store].append(int(entity))

    stores = []
    with Dat(directory / "portal.dat") as archive:
        for store, actors in sorted(vendors.items()):
            raw = archive.read(store)
            nodes = graph(raw, store)
            root = nodes[0]
            if root.get("package") != 0x3810:
                raise ValueError(f"Unknown store class {store:08x}")
            stock = nodes[root["slots"][5]]
            if stock.get("native") != 0x61 or len(stock["list"]) != root["slots"][4]:
                raise ValueError(f"Invalid stock list {store:08x}")
            offers = []
            for index in stock["list"]:
                entry = nodes[index]
                tags = entry["tags"]
                if (entry.get("package") != 0x380f or len(tags) != 7 or tags[:4] != [4, 4, 1, 4]
                        or tags[4] not in (2, 4) or tags[5] != 4 or tags[6] not in (2, 4)):
                    raise ValueError(f"Unknown offer class in {store:08x}")
                ordinal, item, attrs, price_bits, qualifier, currency, final_field = entry["slots"]
                price = struct.unpack("<f", struct.pack("<I", price_bits))[0]
                if not math.isfinite(price) or price < 0 or item >> 24 != 0x47 or currency >> 24 != 0x47:
                    raise ValueError(f"Invalid merchandise or price in {store:08x}")
                offers.append(dict(ordinal=ordinal, item=item, unit_price=price, currency=currency,
                                   attribute_node=attrs, qualifier=qualifier, final_field=final_field))
            stores.append(dict(store=store, vendors=sorted(actors), sha256=hashlib.sha256(raw).hexdigest(), offers=offers))
    return dict(schema=1, source="Legions portal.dat StoreTemplate", stores=stores)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    catalog = extract(args.directory, dat_templates(args.directory))
    args.output.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
