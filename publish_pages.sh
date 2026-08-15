#!/usr/bin/env bash
#
# publish_pages.sh — assemble every performance + image-equivalence HTML report into a
# static site and force-push it to the gh-pages branch (GitHub Pages).
#
# Performance reports are self-contained HTML. Image-equivalence reports reference their
# baseline / candidate / diff PNGs relative to the report file, so each published report
# is copied alongside exactly the directories it references (grepped from its <img> tags).
# Machine-local artifacts (baselines, candidates) are published here even though they are
# not committed on master — the site is a snapshot, regenerate by re-running this script.
#
# Usage: bash publish_pages.sh
set -euo pipefail
cd "$(dirname "$0")"

REMOTE=$(git remote get-url origin)
SITE=$(mktemp -d)
trap 'rm -rf "$SITE"' EXIT

# ---- performance: every committed report ----
mkdir -p "$SITE/performance"
cp performance/report_*.html "$SITE/performance/"

# ---- image equivalence: reports + the exact asset dirs they reference ----
mkdir -p "$SITE/image_equivalence"
IMG_REPORTS=$(ls image_equivalence/report_*.html 2>/dev/null | grep -vE "_one\.html|glglcheck|selfcheck" || true)
for r in $IMG_REPORTS; do
    cp "$r" "$SITE/image_equivalence/"
    for dir in $(grep -o 'src="[^"]*"' "$r" | sed 's/src="//;s/"$//' | cut -d/ -f1 | sort -u); do
        if [ -d "image_equivalence/$dir" ] && [ ! -d "$SITE/image_equivalence/$dir" ]; then
            cp -R "image_equivalence/$dir" "$SITE/image_equivalence/$dir"
        fi
    done
done

# ---- index ----
{
cat <<'HDR'
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>raylib_tests reports</title>
<style>
:root{color-scheme:light dark}body{font-family:-apple-system,system-ui,sans-serif;max-width:900px;margin:2rem auto;padding:0 1rem;line-height:1.5}
h1{border-bottom:2px solid #888;padding-bottom:.3rem}h2{margin-top:2rem;color:#c66}
a{text-decoration:none}a:hover{text-decoration:underline}li{margin:.2rem 0}
.sub{color:#888;font-size:.85rem}
</style></head><body>
<h1>raylib_tests — reports</h1>
<p class=sub>Backend test reports for raylib's interchangeable graphics backends (rlgl OpenGL,
rlvk Vulkan, rlmtl Metal, rlsw software). Performance = uncapped frame-time benchmarking;
image equivalence = pixel comparison against the GL baseline. Republished by publish_pages.sh.</p>
HDR

section () { echo "<h2>$1</h2><ul>"; }
endsection () { echo "</ul>"; }
row () { local f=$1; local name; name=$(basename "$f" .html); echo "<li><a href=\"$f\">$name</a></li>"; }

section "Performance — cross-backend comparisons"
for f in $(ls "$SITE"/performance/report_comparison_*.html | sort); do row "performance/$(basename "$f")"; done
endsection

section "Performance — per-backend detail"
for f in $(ls "$SITE"/performance/report_*.html | grep -v comparison | sort); do row "performance/$(basename "$f")"; done
endsection

section "Image equivalence — pixel gates and regressions"
for f in $(ls "$SITE"/image_equivalence/report_*.html 2>/dev/null | sort); do row "image_equivalence/$(basename "$f")"; done
endsection

echo "<p class=sub>Generated $(date '+%Y-%m-%d %H:%M %Z') on $(hostname -s)</p></body></html>"
} > "$SITE/index.html"

touch "$SITE/.nojekyll"    # serve directories starting with underscores etc. verbatim

# ---- push as gh-pages ----
git -C "$SITE" init -q -b gh-pages
git -C "$SITE" add -A
git -C "$SITE" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
    commit -q -m "Publish reports $(date '+%Y-%m-%d %H:%M')"
git -C "$SITE" push -f "$REMOTE" gh-pages:gh-pages
echo "Published: $(du -sh "$SITE" | cut -f1) site -> gh-pages"
