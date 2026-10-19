"""Merge the translations into the catalogs the web page loads, checking them as it goes.

    python tools/i18n/build_catalogs.py

Reads tools/i18n/keys.json (the English) and tools/i18n/tr/<lang>*.json (English -> translation,
in as many files as convenient), and writes web/i18n/<lang>.json. Every translation must keep its
key's {#} and {*} blanks -- the same count of each, in any order, or numbered ({#2}) to reorder.
Reports keys still untranslated, and translations of text that is no longer a key.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).parent
WEB = HERE.parent.parent / "web" / "i18n"
LANGUAGES = ["fr", "it", "es", "de", "pt", "pl", "uk", "zh", "ja", "hi", "he"]
BLANK = re.compile(r"\{([#*])\d*\}")


def blanks(text):
    found = BLANK.findall(text)
    return found.count("#"), found.count("*")


def main():
    keys = json.load(open(HERE / "keys.json", encoding="utf8"))
    known = set(keys)
    WEB.mkdir(parents=True, exist_ok=True)
    problems = 0
    for lang in LANGUAGES:
        merged = {}
        for path in sorted((HERE / "tr").glob(f"{lang}*.json")):
            merged.update(json.load(open(path, encoding="utf8")))
        catalog, stale, bad = {}, [], []
        for key, translation in merged.items():
            if key not in known:
                stale.append(key)
                continue
            if not translation or blanks(translation) != blanks(key):
                bad.append(key)
                continue
            catalog[key] = translation
        missing = [key for key in keys if key not in catalog]
        out = WEB / f"{lang}.json"
        json.dump({key: catalog[key] for key in keys if key in catalog}, open(out, "w", encoding="utf8"),
                  ensure_ascii=False, indent=0, sort_keys=False)
        print(f"{lang}: {len(catalog)}/{len(keys)}  missing {len(missing)}  bad {len(bad)}  stale {len(stale)}")
        for key in bad:
            print(f"   bad blanks: {key!r} -> {merged[key]!r}")
        for key in stale[:10]:
            print(f"   stale: {key!r}")
        if "--missing" in sys.argv:
            for key in missing:
                print(f"   missing: {key}")
        problems += len(bad)
    return 1 if problems else 0


sys.exit(main())
