#!/usr/bin/env python3
"""Build the custom music + sound effects pack for the handheld port.

    tools/sound-pack/make_pack.py --switch <extracted-oggs-from-switch> \
        [--ost "<Link's Awakening Original Soundtrack>"] --out <folder>

Reads music_map.csv and sfx_map.csv (next to this script) and writes

    <out>/XX.ogg          music, track XX (see docs/MUSIC.md)
    <out>/sfx/j_XX.ogg    jingle XX   (JINGLE_*)
    <out>/sfx/w_XX.ogg    wave sfx XX (WAVE_SFX_*)
    <out>/sfx/n_XX.ogg    noise sfx XX (NOISE_SFX_*)

Copy <out> to "Ports/Links Awakening DX.pak/music/" and set MUSIC: CUSTOM.

Sources in the maps:
    sw:NAME          <switch>/music/NAME.ogg (music map) - loops from loops.csv
    music:NAME       <switch>/music/NAME.ogg (sfx map)
    sfx:BANK/NNNN    <switch>/sfx/BANK/NNNN.ogg
    ost:CD x/file    <ost>/CD x/file (mp3)

Music: stereo Switch files with no cut are copied as they are (no
re-encoding), only the loop tags change. Cuts, files with more than two
channels (the Switch's layered tracks: the layers are summed) and the OST
are re-encoded with sox (Vorbis q6). Sound effects are re-encoded with the
leading silence trimmed, so they start on the frame the game asks for them.

Needs ffmpeg, ffprobe and sox (with Vorbis) on the PATH.
"""
import argparse, csv, json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("failed: %s\n%s" % (" ".join(cmd), r.stderr[-2000:]))
    return r


def probe(path):
    r = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
             "stream=channels,sample_rate", "-of", "json", path])
    s = json.loads(r.stdout)["streams"][0]
    return int(s["channels"]), int(s["sample_rate"])


def peak_db(wav):
    r = subprocess.run(["ffmpeg", "-nostats", "-i", wav, "-af", "volumedetect", "-f", "null", "-"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for line in r.stderr.splitlines():
        if "max_volume:" in line:
            return float(line.split("max_volume:")[1].split()[0])
    return 0.0


def downmix_filter(ch):
    if ch == 4:   # two stereo layers, both meant to be heard
        return "pan=stereo|c0=c0+c2|c1=c1+c3"
    if ch > 2:
        return None  # ffmpeg's own downmix (-ac 2)
    return ""


def encode(src, out, start=None, end=None, tags=None, trim_silence=False, tmp=None):
    """Decode src (cut, folded to stereo), peak-limit to -1 dB, encode with sox."""
    ch, rate = probe(src)
    wav = os.path.join(tmp, "x.wav")
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", src]
    af = []
    if start is not None or end is not None:
        trim = "atrim="
        parts = []
        if start is not None: parts.append("start=%s" % start)
        if end is not None: parts.append("end=%s" % end)
        af.append(trim + ":".join(parts))
        af.append("asetpts=PTS-STARTPTS")
    dm = downmix_filter(ch)
    if dm: af.append(dm)
    if trim_silence:
        af.append("silenceremove=start_periods=1:start_threshold=-60dB:start_silence=0.005")
    if af: cmd += ["-af", ",".join(af)]
    if dm is None: cmd += ["-ac", "2"]
    cmd += ["-c:a", "pcm_s16le", wav]
    run(cmd)
    pk = peak_db(wav)
    gain = []
    if pk > -1.0:
        gain = ["gain", "%.2f" % (-1.0 - pk)]
    sox = ["sox", wav]
    if tags:
        first = True
        for k, v in tags.items():
            sox += ["--comment" if first else "--add-comment", "%s=%s" % (k, v)]
            first = False
    sox += ["-C", "6", out] + gain
    run(sox)


def copy_with_tags(src, out, tags):
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", src, "-map", "0:a", "-c", "copy", "-map_metadata", "-1"]
    for k, v in (tags or {}).items():
        cmd += ["-metadata:s:a:0", "%s=%s" % (k, v)]
    run(cmd + [out])


def resolve(source, args):
    kind, _, name = source.partition(":")
    if kind in ("sw", "music"):
        return os.path.join(args.switch, "music", name + ".ogg")
    if kind == "sfx":
        return os.path.join(args.switch, "sfx", name + ".ogg")
    if kind == "ost":
        if not args.ost:
            sys.exit("%s needs --ost" % source)
        return os.path.join(args.ost, name)
    sys.exit("unknown source: " + source)


def load_loops(args):
    loops = {}
    p = os.path.join(args.switch, "music", "loops.csv")
    with open(p, newline="") as f:
        for row in csv.DictReader(f):
            loops[row["track"]] = (int(row["loop_start"]), int(row["loop_end"]))
    return loops


def build_music(args, tmp):
    loops = load_loops(args)
    n = 0
    with open(os.path.join(HERE, "music_map.csv"), newline="") as f:
        for row in csv.DictReader(f):
            track = row["track"].strip().upper()
            src = resolve(row["source"], args)
            out = os.path.join(args.out, track + ".ogg")
            start = float(row["start"]) if row["start"].strip() else None
            end = float(row["end"]) if row["end"].strip() else None
            tags = None
            if row["loop"] == "csv":
                name = row["source"].partition(":")[2]
                if name not in loops:
                    sys.exit("%s: no loop for %s in loops.csv" % (track, name))
                ls, le = loops[name]
                if start:   # loop points are in the source's samples
                    ls -= int(round(start * probe(src)[1]))
                    le -= int(round(start * probe(src)[1]))
                tags = {"LOOPSTART": ls, "LOOPLENGTH": le - ls}
            ch, _ = probe(src)
            if row["source"].startswith("sw:") and start is None and end is None and ch <= 2:
                copy_with_tags(src, out, tags)
                how = "copy"
            else:
                encode(src, out, start, end, tags, tmp=tmp)
                how = "encode"
            n += 1
            print("  %s.ogg  <- %-40s %s%s" % (track, os.path.basename(src)[:40], how,
                                               " loop %s+%s" % (tags["LOOPSTART"], tags["LOOPLENGTH"]) if tags else ""))
    return n


def build_sfx(args, tmp):
    p = os.path.join(HERE, "sfx_map.csv")
    os.makedirs(os.path.join(args.out, "sfx"), exist_ok=True)
    prefix = {"jingle": "j", "wave": "w", "noise": "n"}
    n = 0
    with open(p, newline="") as f:
        for row in csv.DictReader(f):
            if not row["source"].strip():
                continue
            k = row["kind"].strip().lower()
            sid = int(row["id"], 16)
            src = resolve(row["source"].strip(), args)
            out = os.path.join(args.out, "sfx", "%s_%02X.ogg" % (prefix[k], sid))
            encode(src, out, trim_silence=True, tmp=tmp)
            n += 1
            print("  sfx/%s_%02X.ogg <- %s" % (prefix[k], sid, row["source"]))
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--switch", required=True, help="folder with music/ and sfx/ from the Switch version")
    ap.add_argument("--ost", help="the Original Soundtrack folder (holds 'CD 1', 'CD 2')")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", choices=["music", "sfx"])
    args = ap.parse_args()
    for tool in ("ffmpeg", "ffprobe", "sox"):
        if not shutil.which(tool):
            sys.exit(tool + " not found")
    os.makedirs(args.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        if args.only != "sfx":
            print("music:")
            print("  %d tracks" % build_music(args, tmp))
        if args.only != "music":
            print("sound effects:")
            print("  %d effects" % build_sfx(args, tmp))


if __name__ == "__main__":
    main()
