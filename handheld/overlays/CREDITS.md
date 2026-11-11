# Screen overlays

Offered as extra values of the options screen's GRID (`handheld/la_bezel.c`).
Both come from [ourigen/perfect_overlays](https://github.com/ourigen/perfect_overlays)
("RetroArch Perfect Overlays for 640x480 Handhelds"), derived from the work of
[1playerinsertcoin](https://www.reddit.com/user/1playerinsertcoin/submitted/):

| File | Source | Bezel by |
|---|---|---|
| `GBC_Mugwomp.png` | `GBC/mugwomp93/Perfect_GBC.png` | [mugwomp93](https://www.reddit.com/r/RG35XX/comments/1ai93ee/perfect_overlays_adapted_for_rg35xx/) |
| `GBC_Drkhrse.png` | `GBC/drkhrse/Perfect_GBC.png` (commit `46eaa5f`) | [drkhrse](https://github.com/drkhrse/drkhrse_miyoo_bezels) |

They are not in this repository: that one has no license, so we don't
redistribute them. `scripts/fetch_overlays.sh` (run by `build-miaui.sh`)
downloads them from there at commit `46eaa5f`, checks their SHA-256 and puts
them here, unchanged, only renamed (both are `Perfect_GBC.png` upstream).
"Game Boy Color" is a trademark of Nintendo.

Your own: put any PNG the size of the panel (an opaque bezel around a
transparent hole) in `Saves/LinksAwakening/overlays/`. The game finds the
hole by itself and puts the picture there; no `.cfg` is needed.
