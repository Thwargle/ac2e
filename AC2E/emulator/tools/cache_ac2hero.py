"""Cache linked AC2Hero guide pages for offline catalog generation (requires lxml)."""
import argparse
import re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from time import sleep
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urldefrag, urljoin, urlsplit
from urllib.request import Request, urlopen

from lxml import html

BASE = "http://ac2hero.e5i5o.com/"
SEEDS = ["index.html", "items.html", "quest.html", "vaults.html", "maps/maps.html",
         "maps/dungeons.html", "resource.html", "butcher.html", "guides.html",
         "search_list.html", "workshops.html", "cartomancer.html", "begining.html",
         "survey.html", "skills.html", "titles.html", "refining.html", "spellbinding.html",
         "traits.html", "essence.html", "ileveltoarcanelore.html", "spelleffects.html"]
# Editorial guides also link to standalone pages and to quests with spaces
# in their filenames. The archived message boards have their own crawl space.
ALLOWED = re.compile(r"(?:(?:quest|items|maps|guides|search)/(?:[\w .%-]+/)*)?[\w .%-]+\.html$", re.I)


def linked_pages(path, body):
    doc = html.fromstring(body)
    for ref in doc.xpath("//a[@href]/@href"):
        url = urlsplit(urldefrag(urljoin(BASE + path, ref))[0])
        name = unquote(url.path).lstrip("/")
        if url.hostname == "ac2hero.e5i5o.com" and ALLOWED.fullmatch(name) and ".." not in Path(name).parts:
            # HTTrack writes the query result to a distinct hash-suffixed file.
            yield name


def cache_page(cache, path):
    target = cache / path
    if target.is_file():
        return target.read_bytes(), None
    for attempt in range(3):
        try:
            req = Request(BASE + quote(path), headers={"User-Agent": "AC2E-content-review/1.0"})
            with urlopen(req, timeout=15) as res:
                body = res.read()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            return body, None
        except (HTTPError, URLError, TimeoutError, OSError) as err:
            if isinstance(err, HTTPError) and err.code in (403, 404, 410):
                break
            if attempt < 2:
                sleep(attempt + 1)
    return None, path


def crawl(cache):
    seen = set()
    failed = []
    queue = set(SEEDS)
    with ThreadPoolExecutor(4) as pool:
        while queue:
            paths = sorted(queue)
            seen.update(paths)
            queue = set()
            for path, (body, error) in zip(paths, pool.map(lambda p: cache_page(cache, p), paths)):
                if error:
                    failed.append(error)
                else:
                    queue.update(linked_pages(path, body))
            queue -= seen
            print(f"Scanned {len(seen)} paths; {len(queue)} more links; {len(failed)} unavailable.", flush=True)
    import json
    (cache / "crawl.json").write_text(json.dumps({"pages":sorted(seen-set(failed)), "unavailable":sorted(failed)},indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cache", type=Path)
    crawl(ap.parse_args().cache)
