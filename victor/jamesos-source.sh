#!/usr/bin/env bash
# JamesOS source publisher (GPL-3.0: whoever gets the JamesOS OTA must be able to get its source).
#
#   ./jamesos-source.sh          publish the source of your newest PUBLIC OTA (the one ./jamesos.sh ota made)
#                                to github.com/jamesos-project/jamesos
#   ./jamesos-source.sh check    make it and check it, but don't publish (look in ~/jamesos-source-preview)
#
# What gets published: only the files JamesOS changed or added on top of WireOS, exactly as they were
# built into the OTA, plus a README that says which WireOS versions they go on top of.
# Private parts are taken out first (everything inside "#if JAMESOS_PRIVATE", your robots' names,
# your name). If anything private is still found, NOTHING is published.
# No git history is published - one clean commit per release, author "JamesOS".

cd "$(dirname "$0")" || exit 1
WOS="${WIREOS_DIR:-$HOME/wire-os}"
VB="$WOS/anki/victor"
OUT="${JAMESOS_SOURCE_DIR:-$HOME/jamesos-source}"
REPO="${JAMESOS_SOURCE_REPO:-https://github.com/jamesos-project/jamesos.git}"
MODE="${1:-publish}"

# Words that must never be in the public source are kept in jamesos/private-words.txt (one per line).
# That folder is never put in git, so the list itself stays private.
WORDS=jamesos/private-words.txt

die() { echo "$*"; exit 1; }

[[ -d "$VB/.git" || -f "$VB/.git" ]] || die "No WireOS builder in $WOS."
[[ "$(git -C "$VB" rev-parse --abbrev-ref HEAD 2>/dev/null)" == jamesos-ota ]] || die "Build the public OTA first:  ./jamesos.sh ota"
DR="$VB/cannedAnimLib/proceduralFace/proceduralFaceDrawer.cpp"
grep -q '^#define JAMESOS_PRIVATE 0' "$DR" || die "The last OTA build doesn't have private extras OFF - run ./jamesos.sh ota again first."
OTA=$(ls -t "$WOS"/_build/vicos-*.ota 2>/dev/null | head -1)
[[ -f "$OTA" ]] || die "No OTA file found. Build one first: ./jamesos.sh ota"
VER=$(basename "$OTA" .ota); VER=${VER#vicos-}
REL=$(sed -n 's/^JAMESOS_RELEASE="\(.*\)"/\1/p' jamesos.sh | head -1)
BASE=$(git -C "$WOS" ls-tree HEAD anki/victor | awk '{print $3}')
WOSBASE=$(git -C "$WOS" rev-parse HEAD)
[[ -n "$BASE" && -n "$WOSBASE" ]] || die "Couldn't tell which WireOS version the OTA was built on."

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/victor" "$STAGE/builder"

[[ -s "$WORDS" ]] || die "First make the list of private words in $WORDS (never published, one per line):
  your name as it appears, your GitHub name, your city and country, your PC user name, your robots' names, the PVT's ESN.
  Then run this again."

echo "== 1/4 Collecting the JamesOS files that went into $VER =="
skip() {  # files that are never published
  case "$1" in
    _build/*|*/_build/*|generated/*|EXTERNALS/*) return 0 ;;
    jamesos/private.on|jamesos/robot-names.txt|jamesos/ota-host.txt|jamesos/ota-link.txt|robot_ip.txt) return 0 ;;
    *.key|*sshkey*|*.pem|*.ota|*.patch|fix-*.py) return 0 ;;
  esac
  return 1
}
n=0
while IFS= read -r f; do
  [[ -z "$f" ]] && continue
  skip "$f" && continue
  [[ -f "$VB/$f" ]] || continue
  mkdir -p "$STAGE/victor/$(dirname "$f")"
  cp -p "$VB/$f" "$STAGE/victor/$f"; n=$((n+1))
done < <( { git -C "$VB" diff --name-only "$BASE"; git -C "$VB" ls-files --others --exclude-standard; } | sort -u )
git -C "$VB" diff --name-only --diff-filter=D "$BASE" > "$STAGE/victor-deleted.txt"
echo "   $n files from WireOS's victor code"

# the builder (~/wire-os) files jamesos.sh ota changed: startup picture, welcome message, JamesOS keep
m=0
while IFS= read -r sub; do
  dir="$WOS/$sub"; [[ "$sub" == "." ]] && dir="$WOS"
  [[ "$sub" == anki/victor ]] && continue
  while IFS= read -r line; do
    f="${line:3}"; f="${f#\"}"; f="${f%\"}"
    path="$f"; [[ "$sub" != "." ]] && path="$sub/$f"
    case "$path" in anki/victor|anki/victor/*|_build/*|poky/build/*|*/build/tmp*) continue ;; esac
    if [[ -d "$dir/$f" ]]; then
      while IFS= read -r g; do
        mkdir -p "$STAGE/builder/$(dirname "$path/$g")"; cp -p "$dir/$f/$g" "$STAGE/builder/$path/$g"; m=$((m+1))
      done < <(cd "$dir/$f" && find . -type f | sed 's#^\./##')
    elif [[ -f "$dir/$f" ]]; then
      mkdir -p "$STAGE/builder/$(dirname "$path")"; cp -p "$dir/$f" "$STAGE/builder/$path"; m=$((m+1))
    fi
  done < <(git -C "$dir" status --porcelain --untracked-files=normal --ignore-submodules=all 2>/dev/null | grep -v '^ D\|^D ')
done < <( echo "."; git -C "$WOS" submodule foreach --recursive --quiet 'echo "$displaypath"' 2>/dev/null )
echo "   $m files from the WireOS builder"
[[ $m -lt 200 ]] || die "That's too many builder files ($m) - something is off. Nothing was published. Send Claude a screenshot."

echo "== 2/4 Taking out private parts =="
python3 - "$STAGE" <<'PY' || exit 1
import os, re, sys
root = sys.argv[1]
IF_PRIV = re.compile(r'^\s*#\s*if\s+JAMESOS_PRIVATE\s*(//.*)?$')
IF_ANY = re.compile(r'^\s*#\s*if(n?def)?\b')
ELSE = re.compile(r'^\s*#\s*else\b')
ELIF = re.compile(r'^\s*#\s*elif\b')
ENDIF = re.compile(r'^\s*#\s*endif\b')
def strip(text, name):
    out, stack = [], []   # stack items: ["priv", keeping] or ["other", None]
    def keeping():
        return all(k for kind, k in stack if kind == "priv")
    for line in text.split("\n"):
        if IF_PRIV.match(line):
            stack.append(["priv", False]); continue
        if IF_ANY.match(line):
            stack.append(["other", None])
            if keeping(): out.append(line)
            continue
        if stack and stack[-1][0] == "priv" and ELIF.match(line):
            sys.exit("#elif inside a JAMESOS_PRIVATE block in %s - can't strip it safely" % name)
        if stack and stack[-1][0] == "priv" and ELSE.match(line):
            stack[-1][1] = True; continue
        if ENDIF.match(line) and stack:
            kind, _ = stack.pop()
            if kind == "priv": continue
            if keeping(): out.append(line)
            continue
        if keeping(): out.append(line)
    if stack: sys.exit("unbalanced #if in %s - can't strip it safely" % name)
    return "\n".join(out)
changed = 0
for dp, dn, fn in os.walk(root):
    for f in fn:
        p = os.path.join(dp, f)
        try: s = open(p, encoding="utf-8").read()
        except (UnicodeDecodeError, IsADirectoryError): continue
        t = s
        if f.endswith((".cpp", ".h", ".hpp", ".cc", ".txt")) and "JAMESOS_PRIVATE" in s:
            t = strip(s, p[len(root)+1:])
        if t != s:
            open(p, "w", encoding="utf-8").write(t); changed += 1
print("   private parts taken out of %d files" % changed)
PY

echo "== 3/4 Checking nothing private is left =="
BAD=0
while IFS= read -r w; do
  [[ -z "$w" ]] && continue
  hits=$(grep -rIlwF -- "$w" "$STAGE" 2>/dev/null | sed "s#^$STAGE/##")
  if [[ -n "$hits" ]]; then echo "   FOUND \"$w\" in:"; echo "$hits" | sed 's/^/      /'; BAD=1; fi
done < "$WORDS"
[[ $BAD == 0 ]] || die "Private words are still in the source above - NOTHING was published. Send Claude a screenshot."
echo "   clean"

cp "$VB/LICENSE" "$STAGE/LICENSE" 2>/dev/null
cat > "$STAGE/README.md" <<EOF
# JamesOS $REL ($VER)

Custom firmware for Anki Vector, built on [WireOS](https://github.com/os-vector).
This is the complete source of the public JamesOS $REL OTA (\`jamesos-$VER.ota\`), published under the
GPL-3.0 like WireOS itself.

JamesOS is a set of changes on top of WireOS. This repository holds every file JamesOS changed or added,
exactly as built into the OTA:

| Folder | Goes on top of |
|---|---|
| \`victor/\` | [os-vector/wire-os-victor](https://github.com/os-vector/wire-os-victor) at commit \`$BASE\` |
| \`builder/\` | [os-vector/wire-os](https://github.com/os-vector/wire-os) at commit \`$WOSBASE\` |

Files listed in \`victor-deleted.txt\` (if any) were removed.

## Build it yourself

1. \`git clone --recurse-submodules https://github.com/os-vector/wire-os && cd wire-os && git checkout $WOSBASE && git submodule update --init --recursive\`
2. \`cd anki/victor && git checkout $BASE && cd ../..\`
3. Copy \`builder/\` over \`wire-os/\`, and \`victor/\` over \`wire-os/anki/victor/\`.
4. Build the OTA the WireOS way (\`./build/build.sh\`, see the WireOS README).

## License

GPL-3.0 (see LICENSE). Vector, Anki and Digital Dream Labs are trademarks of their owners.
EOF

if [[ "$MODE" == check ]]; then
  rm -rf "$HOME/jamesos-source-preview"; cp -r "$STAGE" "$HOME/jamesos-source-preview"
  echo "== Check only: nothing was published. Look at it in ~/jamesos-source-preview =="
  exit 0
fi

echo "== 4/4 Publishing to $REPO =="
if [[ ! -d "$OUT/.git" ]]; then
  git clone -q "$REPO" "$OUT" 2>/dev/null || die "Couldn't get $REPO. Make it on GitHub first: organization jamesos-project -> New repository -> name: jamesos -> Public -> Create (leave it empty)."
fi
find "$OUT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -r "$STAGE"/. "$OUT"/
git -C "$OUT" add -A
git -C "$OUT" -c user.name="JamesOS" -c user.email="jamesos@users.noreply.github.com" \
  commit -q -m "JamesOS $REL ($VER)" || echo "   (no changes since the last time)"
git -C "$OUT" tag -f "$VER" >/dev/null
git -C "$OUT" push -q origin HEAD 2>/dev/null || git -C "$OUT" push -q -u origin HEAD:main || die "Push failed - send Claude a screenshot."
git -C "$OUT" push -q -f origin "$VER" || true
echo
echo "Done. Source of JamesOS $REL ($VER):  ${REPO%.git}"
