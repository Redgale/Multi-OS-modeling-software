#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a pseudo-translation from the translation template, for testing.

Every string becomes accented and about a third longer, wrapped in [ ]. Placeholders
(%1, %n, %L1), HTML tags, entities and & mnemonics are kept. Running the app in this
"language" shows at a glance which text is not translatable (it stays plain) and where
layouts break when translations are longer.

    python3 tools/pseudo_translate.py translations/jigsmith.ts translations/jigsmith_qps.ts
"""
import re
import sys
import xml.etree.ElementTree as ET

ACCENTS = str.maketrans(
    "aceinouyAEINOUCY",
    "áçéîñöüýÁÉÎÑÖÜÇÝ",
)
TOKEN = re.compile(r"(%L?\d+|%n|<[^>]+>|&[a-zA-Z#0-9]+;|&(?=\w))")


def pseudo(text: str) -> str:
    parts = TOKEN.split(text)
    out = []
    letters = 0
    for i, part in enumerate(parts):
        if i % 2 == 1:  # a token: keep as is
            out.append(part)
        else:
            out.append(part.translate(ACCENTS))
            letters += sum(c.isalpha() for c in part)
    pad = "~" * max(1, letters // 3)
    return "[" + "".join(out) + pad + "]"


def main(src: str, dst: str) -> None:
    tree = ET.parse(src)
    root = tree.getroot()
    root.set("language", "qps")
    for msg in root.iter("message"):
        source = msg.find("source")
        tr = msg.find("translation")
        if source is None or tr is None:
            continue
        if msg.get("numerus") == "yes":
            for nf in list(tr):
                tr.remove(nf)
            tr.text = None
            for _ in range(2):
                ET.SubElement(tr, "numerusform").text = pseudo(source.text or "")
        else:
            tr.text = pseudo(source.text or "")
        tr.attrib.pop("type", None)  # finished, not "unfinished"
    tree.write(dst, encoding="utf-8", xml_declaration=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
