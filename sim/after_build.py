# Runs after every build of the simulator (`pio run -e sim`): takes every
# picture again and puts them on one page, sim/shots/index.html, so the
# pictures are never older than the code. PlatformIO only rebuilds, and so
# only runs this, when a source file has changed.
import base64
import glob
import os
import subprocess

Import("env")  # noqa: F821  (PlatformIO provides it)

FOLDER = os.path.join(env.subst("$PROJECT_DIR"), "sim", "shots")  # noqa: F821

HEAD = """<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>m5drums screens</title>
<style>
:root { color-scheme: dark; --bg:#111210; --line:#34352f; --fg:#f0f0e6; --dim:#9a9a90; --accent:#ffa000; }
body { margin:0; background:var(--bg); color:var(--fg); font:15px/1.5 system-ui,-apple-system,sans-serif;
  padding:32px 16px 64px; }
main { max-width:1040px; margin:0 auto; }
h1 { font-size:24px; margin:0 0 4px; }
header p { margin:0 0 8px; color:var(--dim); max-width:65ch; }
h2 { font-size:13px; letter-spacing:.08em; text-transform:uppercase; color:var(--accent); margin:40px 0 12px; }
.grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(300px,1fr)); gap:20px 16px; }
figure { margin:0; }
img { display:block; width:100%; height:auto; image-rendering:pixelated; background:#000;
  border:1px solid var(--line); border-radius:8px; }
figcaption { margin-top:6px; color:var(--dim); font-size:13px; }
</style>
<main><header><h1>m5drums screens</h1>
<p>Every state of every screen, drawn by the firmware's own screen code running on a computer.
Made again each time that code changes. Colours on the real panel differ a little.</p></header>
"""

GROUPS = [("main", "Main screen"), ("drum", "A drum, open"), ("menu", "MENU")]


def take_pictures(source, target, env):
    os.makedirs(FOLDER, exist_ok=True)
    for old in glob.glob(os.path.join(FOLDER, "*.png")):
        os.remove(old)  # so a scene that was removed does not linger
    program = env.subst("$BUILD_DIR/program")
    subprocess.run([program, "shots", FOLDER], check=True, stdout=subprocess.DEVNULL)

    names = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(FOLDER, "*.png")))
    page = HEAD
    for prefix, title in GROUPS:
        page += "<h2>%s</h2><div class=grid>" % title
        # In the order the simulator lists them, which is the order they were written.
        group = [n for n in names if n.split("-")[0] == prefix]
        group.sort(key=lambda n: os.path.getmtime(os.path.join(FOLDER, n + ".png")))
        for name in group:
            with open(os.path.join(FOLDER, name + ".png"), "rb") as f:
                data = base64.b64encode(f.read()).decode()
            caption = name[len(prefix) + 1:].replace("-", " ") or "as it usually is"
            page += ('<figure><img src="data:image/png;base64,%s" alt="%s: %s" width=640 height=480>'
                     "<figcaption>%s</figcaption></figure>") % (data, title, caption, caption)
        page += "</div>"
    # A scene whose name starts with none of the above would otherwise be
    # left off the page without a word.
    known = [prefix for prefix, _ in GROUPS]
    missed = [n for n in names if n.split("-")[0] not in known]
    if missed:
        raise SystemExit("no group in sim/after_build.py for: " + ", ".join(missed))
    page += "</main>"
    with open(os.path.join(FOLDER, "index.html"), "w") as f:
        f.write(page)
    print("%d pictures and sim/shots/index.html updated" % len(names))


env.AddPostAction("$PROGPATH", take_pictures)  # noqa: F821
