#!/usr/bin/env python3
"""Regenerate sfx_picker.html from sfx_picker.template.html + sfx_map.csv."""
import csv, json, os
H = os.path.dirname(os.path.abspath(__file__))
rows = list(csv.DictReader(open(os.path.join(H, "sfx_map.csv"), newline="")))
t = open(os.path.join(H, "sfx_picker.template.html")).read()
open(os.path.join(H, "sfx_picker.html"), "w").write(t.replace("/*ROWS*/[]", json.dumps(rows)))
print("sfx_picker.html: %d sounds" % len(rows))
