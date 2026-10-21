#!/usr/bin/env python3
"""
JamesOS feature installer (v22). Run from the root of wire-os-victor:

    python3 jamesos-features/apply_jamesos_features.py

Safe to run more than once, and it upgrades anything an older version installed.
Works for Vector 2.0 and Vector 1.0 (one build runs on both).

What it adds:
  [1] Info screen      "JamesOS v21" - the real version (+ build info lines)
  [2] Eye effects      starlight sparkles (white / rainbow / shooting), galaxy eyes when petted, mood colors
  [3] Light theme      jamesos/backpackLights/
  [4] Sparkle settings jamesos/starlight.conf
  [5] Menus            main screen: EXIT / EXTRAS / CONF (classic "CONFIGURATION PAGE 1" style)
                       EXTRAS: one list, each line shows its setting in green, lift = next choice
                       CONF: SELF TEST, RESTART, BACKPACK LIGHTS, DATA, CLEAR OUT SOUL, ABOUT JAMESOS
  [6] Lights switch    lets the menu switch custom/normal lights
  [7] Menu drawing     4 buttons in one row + COOL FEATURES pages: white bar on the selected line,
                       values on the right, "L=DOWN R=UP LIFT=OK" hint
 [18] Smooth wheel     keep turning a wheel and the cursor keeps moving (no stop-and-go)
  [8] Petting signal   tells the eyes when Vector is being petted (for hearts)
  [9] Mood signal      vic-engine shares Vector's mood with the eyes (for mood colors)
 [10] One lift = one press (fixes choices running by themselves)
 [11] Status signal  battery / charger / clock shared with the face (STATUS screen, night clock)
 [12] Halloween lights loader (uses jamesos/backpackLights-halloween when the theme is Halloween)
      Themes: normal / halloween / christmas (snow + lights) / lunar (Chinese New Year lanterns) / auto
 [16] Sleepy eyes on low battery + 19 BADGES (achievements, incl. New Year, Chinese New Year, Valentine's) with a pop-up and a BADGES menu
 [19] Easter eggs: tickle mode, shy mode, secret room (3 hidden themes), LOYAL FRIEND gold sparkles,
      secret ??? badges and a MASTER PROOF screen when every badge is found
 [20] Name fix: "my name is James" heard twice no longer saves the name "james my name is james"
 [21] CLEAR OUT SOUL screen says what it erases (and that badges are kept)
 [22] JamesOS survives CLEAR OUT SOUL: lights, sparkles, galaxy eyes, updater and badges come back by themselves
 [23] Holiday back lights: petting / charging / listening / thinking lights match the holiday, live
 v22: battery shows FULL when charging is done (2.0 fix), ABOUT shows the real version + "ON WIREOS. NOT BY DDL"
 v22: AUTO HOLIDAYS (default): 8 new holiday themes (New Year, Valentine's, St Patrick's, April Fools, Easter,
      Mother's Day, July fireworks, Thanksgiving), HOLIDAYS switch (all/western/asian/off), 7-day countdown banner
 [17] Power off: say it / click it / ./jamesos.sh poweroff (vic-robot asks the body board to cut power, like the button)
 [13] Voice eye colors REMOVED (Vector's own eye color code is put back; JamesOS colors taken out of wire-pod)
 [14] Halloween light theme files (jamesos/backpackLights-halloween/)
 [15] Auto-update signal: the face shares "on charger" with the JamesOS updater (/dev/shm)
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

INFO_CPP   = "animProcess/src/cozmoAnim/faceDisplay/faceInfoScreenManager.cpp"
SCREEN_CPP = "animProcess/src/cozmoAnim/faceDisplay/faceInfoScreen.cpp"
TYPES_H    = "animProcess/src/cozmoAnim/faceDisplay/faceInfoScreenTypes.h"
DRAWER_CPP = "cannedAnimLib/proceduralFace/proceduralFaceDrawer.cpp"
DRAWER_H   = "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h"
LIGHTS_H   = "engine/components/lightsConfig.h"
BPL_CPP    = "animProcess/src/cozmoAnim/backpackLights/animBackpackLightComponent.cpp"
LOADER_CPP = "animProcess/src/cozmoAnim/robotDataLoader.cpp"
EYE_CPP    = "engine/aiComponent/behaviorComponent/behaviors/eyeColor/behaviorEyeColorVoiceCommand.cpp"
MOOD_CPP   = "engine/moodSystem/moodManager.cpp"
WIREOS_LIGHTS = "resources/config/engine/lights/backpackLightsWireOS"

START_TAG = "// ============================================================================\n// JamesOS starlight eyes"
END_TAG   = "// ============================== end JamesOS starlight ==============================\n"

LIST_SCREENS = ["JamesOS", "JamesStars", "JamesEffects", "JamesTheme", "JamesSleep", "JamesLights", "JamesStatus", "JamesBadges",
                "JamesConf", "JamesAbout", "JamesRestart", "JamesSecret", "JamesProof", "JamesNews", "JamesFamily"]


def die(msg):
    print("ERROR: " + msg)
    print("That file was not changed. Send this message to Claude.")
    sys.exit(1)


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write(p, s):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def piece(name):
    return read(os.path.join(HERE, name))


def must_have(text, anchor, where):
    if anchor not in text:
        die("Couldn't find the right spot in " + where)


for p in (INFO_CPP, SCREEN_CPP, TYPES_H, DRAWER_CPP, DRAWER_H, LIGHTS_H, BPL_CPP, MOOD_CPP, LOADER_CPP, WIREOS_LIGHTS):
    if not os.path.exists(p):
        die("Can't find %s. Run this from the wire-os-victor folder (cd ~/wire-os-victor)." % p)

# ------------------------------------------------------------------ [1] info screen
s = read(INFO_CPP)
# v21: the OS line shows the real JamesOS version (stamped into /anki/etc/jamesos-version by jamesos.sh),
# e.g. "OS: JamesOS v21" or "OS: JamesOS v21 TEST" for a build sent with go/deploy
OSNAME_FN = (
    'static std::string JamesOSName()  // JamesOS: real version from /anki/etc/jamesos-version\n'
    '{\n'
    '  std::ifstream f("/anki/etc/jamesos-version");\n'
    '  std::string id, label;\n'
    '  std::getline(f, id);\n'
    '  std::getline(f, label);\n'
    '  if (label.empty()) { return "JamesOS v1.0"; }\n'
    '  return "JamesOS " + label + (id.compare(0, 5, "test-") == 0 ? " TEST" : "");\n'
    '}\n'
    '#define OSProject JamesOSName()  // JamesOS: read every time the screen is drawn (deploy writes it after he starts)')
if "#define OSProject JamesOSName()" in s:
    new = s
elif "const std::string OSProject = JamesOSName();" in s:   # v21 first try: read only once at start
    new = s.replace("const std::string OSProject = JamesOSName();",
                    "#define OSProject JamesOSName()  // JamesOS: read every time the screen is drawn (deploy writes it after he starts)", 1)
else:
    new = re.sub(r'const std::string OSProject = ".*?";', lambda m: OSNAME_FN, s, count=1)
# v24: James's name and GitHub only on his own robots (private builds). Everyone else sees WireOS credit.
# The words themselves live in the face code inside "#if JAMESOS_PRIVATE", so a public build doesn't even contain them.
CREDITS = ('#define Creator (ProceduralFaceDrawer::JamesCreator())  // JamesOS: name only in private builds\n'
           '#define CreatorWebsite (ProceduralFaceDrawer::JamesCreatorWebsite())\n')
new = re.sub(r'^(const std::string Creator = .*|#define Creator .*)\n', '', new, flags=re.M)
new = re.sub(r'^(const std::string CreatorWebsite = .*|#define CreatorWebsite .*)\n', '', new, flags=re.M)
anchor = re.search(r'^#define OSProject JamesOSName\(\).*\n', new, re.M)
if anchor is None:
    die("couldn't find the OSProject line in " + INFO_CPP)
new = new[:anchor.end()] + CREDITS + new[anchor.end():]
if new != s:
    write(INFO_CPP, new)
    print("[1] Info screen updated")
else:
    print("[1] Info screen already done")

# ------------------------------------------------------------------ [2] eye effects
h = read(DRAWER_H)
h0 = h
overlay_decl = "    static bool ApplyCustomOverlay(const ProceduralFace& faceData, Vision::ImageRGB565& output, bool dirty);\n"
drawface_decl = "    static void DrawFace(const ProceduralFace& faceData, const Util::RandomGenerator& rng, Vision::ImageRGB565& output);\n"
must_have(h, overlay_decl, DRAWER_H)
must_have(h, drawface_decl, DRAWER_H)
if "ApplyStarlight(" not in h:
    h = h.replace(overlay_decl, overlay_decl +
                  "    // JamesOS starlight eyes (settings: /data/data/jamesos/)\n"
                  "    static bool ApplyStarlight(Vision::ImageRGB565& output, bool dirty);\n", 1)
if "ApplyJamesHearts(" not in h:
    anchor = "    static bool ApplyStarlight(Vision::ImageRGB565& output, bool dirty);\n"
    h = h.replace(anchor, anchor +
                  "    static bool ApplyJamesHearts(Vision::ImageRGB565& output, bool dirty); // JamesOS hearts\n"
                  "    static f32  JamesMoodHue(f32 baseHue); // JamesOS mood colors\n", 1)
if "JamesSetPetting(" not in h:
    h = h.replace(drawface_decl, drawface_decl +
                  "\n    // JamesOS: told by the backpack light component when Vector is being petted\n"
                  "    static void JamesSetPetting(bool petting);\n", 1)
if "JamesSetStatus(" not in h:
    anchor = "    static void JamesSetPetting(bool petting);\n"
    h = h.replace(anchor, anchor +
                  "    // JamesOS: battery / charger / local time from the robot state (STATUS screen, night clock)\n"
                  "    static void JamesSetStatus(f32 battVolts, s32 battTempC, bool onCharger, bool charging,\n"
                  "                               s32 hour, s32 minute, s32 dateKey);\n"
                  "    static void JamesGetStatus(f32& battVolts, s32& battTempC, bool& onCharger, bool& charging);\n"
                  "    static s32  JamesPetsToday();\n", 1)
if "JamesBadgeCount(" not in h:
    anchor = "    static s32  JamesPetsToday();\n"
    must_have(h, anchor, DRAWER_H)
    h = h.replace(anchor, anchor +
                  "    // JamesOS badges (achievements), for the BADGES menu\n"
                  "    static s32  JamesBadgeCount();\n"
                  "    static std::string JamesBadgeName(s32 i);\n"
                  "    static std::string JamesBadgeHint(s32 i);\n"
                  "    static s32  JamesBadgeDate(s32 i); // 0 = not unlocked yet, else yyyymmdd\n", 1)
if "#include <string>\n" not in h:
    first_ns = h.index("namespace Anki")
    h = h[:first_ns] + "#include <string>\n" + h[first_ns:]
if "ApplyJamesExtras(" not in h:
    anchor = "    static f32  JamesMoodHue(f32 baseHue); // JamesOS mood colors\n"
    h = h.replace(anchor, anchor +
                  "    static f32  JamesThemeSat(f32 saturation); // JamesOS themes\n"
                  "    static bool ApplyJamesExtras(Vision::ImageRGB565& output, bool dirty); // JamesOS theme, sleep screen, confetti\n", 1)
if "JamesUnlockBadge(" not in h:
    anchor = "    static s32  JamesBadgeDate(s32 i); // 0 = not unlocked yet, else yyyymmdd\n"
    must_have(h, anchor, DRAWER_H)
    h = h.replace(anchor, anchor +
                  "    static bool JamesBadgeIsSecret(s32 i);              // JamesOS easter eggs: shown as ??? until found\n"
                  "    static bool JamesHasBadge(const std::string& key);\n"
                  "    static void JamesUnlockBadge(const std::string& key);\n", 1)

c = read(DRAWER_CPP)
c0 = c
must_have(c, "#include <mutex>\n", DRAWER_CPP)
for inc in ("algorithm", "atomic", "chrono", "cmath", "cstdio", "cstdlib", "cstring", "ctime", "deque", "fstream", "functional", "map", "random", "string", "vector", "sys/stat.h"):
    line = "#include <%s>\n" % inc
    if line not in c:
        c = c.replace("#include <mutex>\n", "#include <mutex>\n" + line, 1)
# v24: the shared holiday calendar (also used by the boot animation)
CAL_INC = '#include "util/jamesos/jamesosSeasonal.h" // JamesOS: one holiday calendar\n'
if CAL_INC not in c:
    c = c.replace("#include <mutex>\n", CAL_INC + "#include <mutex>\n", 1)

overlay_call = "    dirty = ApplyCustomOverlay(faceData, output, dirty);\n"
must_have(c, overlay_call, DRAWER_CPP)
if "dirty = ApplyStarlight(output, dirty);" not in c:
    c = c.replace(overlay_call, overlay_call + "    dirty = ApplyStarlight(output, dirty); // JamesOS starlight\n", 1)
if "dirty = ApplyJamesHearts(output, dirty);" not in c:
    c = re.sub(r"(    dirty = ApplyStarlight\(output, dirty\);[^\n]*\n)",
               lambda mm: mm.group(1) + "    dirty = ApplyJamesHearts(output, dirty); // JamesOS hearts\n", c, count=1)

if "dirty = ApplyJamesExtras(output, dirty);" not in c:
    c = re.sub(r"(    dirty = ApplyJamesHearts\(output, dirty\);[^\n]*\n)",
               lambda mm: mm.group(1) + "    dirty = ApplyJamesExtras(output, dirty); // JamesOS theme, sleep screen, confetti\n", c, count=1)
sat_line = "    const u8 drawSat = ROUND(255.f*satF);\n"
if "JamesThemeSat(satF)" not in c:
    must_have(c, sat_line, DRAWER_CPP)
    c = c.replace(sat_line, "    const u8 drawSat = ROUND(255.f*JamesThemeSat(satF)); // JamesOS themes\n", 1)

hue_line = "    const u8 drawHue = ROUND(255.f*faceData.GetHue());\n"
if "JamesMoodHue(faceData.GetHue())" not in c:
    must_have(c, hue_line, DRAWER_CPP)
    c = c.replace(hue_line, "    const u8 drawHue = ROUND(255.f*JamesMoodHue(faceData.GetHue())); // JamesOS mood colors\n", 1)

effects = piece("effects_code.cpp.txt")
if "ApplyJamesExtras" not in effects or "JamesRefreshRare" not in effects:
    die("effects_code.cpp.txt is from an older version")
# v23: James's private extras (Vector family, prototype) only when jamesos/private.on exists.
# jamesos/ never goes to GitHub, so anyone else's build doesn't have them.
JAMESOS_PRIVATE = os.path.exists("jamesos/private.on")
if JAMESOS_PRIVATE:
    effects = effects.replace("#define JAMESOS_PRIVATE 0", "#define JAMESOS_PRIVATE 1", 1)
if START_TAG in c and END_TAG in c:
    a = c.index(START_TAG)
    b = c.index(END_TAG) + len(END_TAG)
    c = c[:a] + effects.rstrip("\n") + "\n" + c[b:]
else:
    func_anchor = "bool ProceduralFaceDrawer::ApplyCustomOverlay("
    must_have(c, func_anchor, DRAWER_CPP)
    c = c.replace(func_anchor, effects + func_anchor, 1)

if c != c0 or h != h0:
    write(DRAWER_H, h)
    write(DRAWER_CPP, c)
    print("[2] Eye effects ready: sparkles, galaxy eyes, mood, Halloween, sleep screen, clock, confetti, sleepy eyes, badges")
else:
    print("[2] Eye effects already up to date")

# ------------------------------------------------------------------ [5] COOL FEATURES menu
t = read(TYPES_H)
t0 = t
must_have(t, "  Count\n", TYPES_H)
for name in LIST_SCREENS:
    if re.search(r"^  %s,\s" % name, t, re.M) is None:
        t = t.replace("  Count\n", "  %s, // JamesOS COOL FEATURES\n  \n  Count\n" % name, 1)
if "JamesSetListInfo" not in t:
    decl = ("// JamesOS: info lines shown under the header of the COOL FEATURES screens\n"
            "// (color: 0 = white, 1 = green, 2 = red). Defined in faceInfoScreen.cpp.\n"
            "void JamesSetListInfo(ScreenName screen, const std::vector<std::pair<std::string, int>>& lines);\n\n")
    anchor = "constexpr f32 kDefaultScreenTimeoutDuration_s"
    must_have(t, anchor, TYPES_H)
    t = t.replace(anchor, decl + anchor, 1)
    for inc in ("#include <string>\n", "#include <utility>\n", "#include <vector>\n"):
        if inc not in t:
            first_ns = t.index("namespace Anki")
            t = t[:first_ns] + inc + t[first_ns:]
if "JamesSetListValues" not in t:
    decl = ("// JamesOS: values shown on the right of each COOL FEATURES line (same order as the menu).\n"
            "void JamesSetListValues(ScreenName screen, const std::vector<std::string>& values);\n\n")
    anchor = "constexpr f32 kDefaultScreenTimeoutDuration_s"
    must_have(t, anchor, TYPES_H)
    t = t.replace(anchor, decl + anchor, 1)
if t != t0:
    write(TYPES_H, t)

NEW_MAIN_MENU = """  ADD_MENU_ITEM(Main, "EXIT", None);
  ADD_MENU_ITEM(Main, "EXTRAS", JamesOS); // JamesOS (v24: was COOL)
  ADD_MENU_ITEM(Main, "CONF", JamesConf); // JamesOS: self test, clear out soul... are in here now
"""
# v23 and older called it COOL
OLD_COOL_MAIN = '  ADD_MENU_ITEM(Main, "COOL", JamesOS);   // JamesOS\n'
MAIN_BLOCK = re.compile(r'(  ADD_MENU_ITEM\(Main, "COOL FEATURES", JamesOS\);[^\n]*\n)?  ADD_MENU_ITEM\(Main, "EXIT", None\);\n.*?  ADD_MENU_ITEM\(Main, IsXray\(\) \? "CLEAR" : "CLEAR OUT SOUL", ClearUserData\);\n', re.S)
OLD_MENU = re.compile(r'\n  // === JamesOS settings screen.*?ADD_MENU_ITEM_WITH_ACTION\(JamesOS, "LIGHTS", toggleLights\);\n  \}\n', re.S)
NEW_BEGIN = "\n  // >>> JamesOS MODS menu (begin)"
NEW_END = "  // <<< JamesOS MODS menu (end)\n"

s = read(INFO_CPP)
s0 = s
s = s.replace(OLD_COOL_MAIN, '  ADD_MENU_ITEM(Main, "EXTRAS", JamesOS); // JamesOS (v24: was COOL)\n', 1)
if NEW_MAIN_MENU not in s and not MAIN_BLOCK.search(s):
    die("Couldn't find the main menu in " + INFO_CPP)
screens_anchor = "  ADD_SCREEN(BuildInfo, Main); // Last screen cycles back to Main\n"
must_have(s, screens_anchor, INFO_CPP)
# drop older JamesOS screen lines, then add the current list once
s = re.sub(r"  ADD_SCREEN\(James\w*, Main\);[^\n]*\n", "", s)
s = s.replace(screens_anchor, screens_anchor +
              "".join("  ADD_SCREEN(%s, Main); // JamesOS COOL FEATURES\n" % n for n in LIST_SCREENS), 1)

menu = piece("menu_code.cpp.txt")
if NEW_MAIN_MENU not in s:
    s = MAIN_BLOCK.sub(lambda _: NEW_MAIN_MENU, s, count=1)
# SELF TEST and CLEAR OUT SOUL now open from CONF, so their EXIT goes back there
s = s.replace('  ADD_MENU_ITEM(SelfTest, "EXIT", Main);\n',
              '  ADD_MENU_ITEM(SelfTest, "EXIT", JamesConf); // JamesOS\n', 1)
s = s.replace('  ADD_MENU_ITEM(ClearUserData, "EXIT", Main);\n',
              '  ADD_MENU_ITEM(ClearUserData, "EXIT", JamesConf); // JamesOS\n', 1)
if NEW_BEGIN in s and NEW_END in s:
    a = s.index(NEW_BEGIN)
    b = s.index(NEW_END) + len(NEW_END)
    s = s[:a] + menu.rstrip("\n") + "\n" + s[b:]
elif OLD_MENU.search(s):
    s = OLD_MENU.sub(lambda _: menu, s, count=1)
else:
    s = s.replace(NEW_MAIN_MENU, NEW_MAIN_MENU + menu, 1)
if "#include <functional>\n" not in s:
    s = s.replace("#include <fstream>\n", "#include <fstream>\n#include <functional>\n", 1)

# [10] one lift = one press: forget the old lift range after every press,
# otherwise staying on the same screen makes the press repeat by itself
confirm = "        SetScreen(_currScreen->ConfirmMenuItemAndGetNextScreen());\n"
must_have(s, confirm, INFO_CPP)
if "JamesOS: one lift = one press" not in s:
    s = s.replace(confirm, confirm + "        ResetObservedHeadAndLiftAngles(); // JamesOS: one lift = one press\n", 1)

if s != s0:
    write(INFO_CPP, s)
    print("[5] Menus ready: main screen EXIT / EXTRAS / CONF")
    print("[10] One lift = one press")
else:
    print("[5] Menus already up to date")

# ------------------------------------------------------------------ [6] lights switch
l = read(LIGHTS_H)
if "lights.off" not in l:
    a = '      value = (stat("/data/data/customBackpackLights/off.json", &buffer) == 0);\n'
    must_have(l, a, LIGHTS_H)
    l = l.replace(a, '      value = (stat("/data/data/customBackpackLights/off.json", &buffer) == 0) &&\n'
                     '              (stat("/data/data/jamesos/lights.off", &buffer) != 0); // JamesOS menu switch\n', 1)
    write(LIGHTS_H, l)
    print("[6] Lights switch added")
else:
    print("[6] Lights switch already added")

# ------------------------------------------------------------------ [7] menu drawing
m = read(SCREEN_CPP)
m0 = m
# undo an older "smaller text" 4-button version, if present
m = m.replace('    ASSERT_NAMED(menus <= 4, "More than 4 menus in same row is probably a bad idea"); // JamesOS: 4 buttons\n'
              '    const f32 itemScale = (menus >= 4) ? 0.35f : textScale; // smaller text so 4 fit\n'
              '    const f32 itemOffsetX = (menus >= 4) ? 7.f : 10.f;\n',
              '    ASSERT_NAMED(menus <= 3, "More than 3 menus in same row is probably a bad idea");\n')
m = m.replace('      if (_menuCursor == i) img.DrawText({x, y}, ">", menuItemColor, itemScale);\n',
              '      if (_menuCursor == i) img.DrawText({x, y}, ">", menuItemColor, textScale);\n')
m = m.replace('      img.DrawText({x + itemOffsetX, y}, _menu[i].text, menuItemColor, itemScale);\n',
              '      img.DrawText({x + 10, y}, _menu[i].text, menuItemColor, textScale);\n')
# remove an older version of my 4-button block (so the new one goes in clean)
m = re.sub(r"\n    if \(menus >=? 3\) \{\n      // JamesOS: (?:3 or )?4 buttons in one row.*?      return;\n    \}\n(    // JamesOS: end 4 buttons\n)?",
           "", m, count=1, flags=re.S)
a_assert_old = '    ASSERT_NAMED(menus <= 3, "More than 3 menus in same row is probably a bad idea");\n'
a_assert_new = '    ASSERT_NAMED(menus <= 4, "More than 4 menus in same row will not fit");\n'
a_bg = "    const Rectangle<f32> rect(0.f, FACE_DISPLAY_HEIGHT -stepY, FACE_DISPLAY_WIDTH, stepY);\n    img.DrawFilledRect(rect, menuBgColor);\n"
if m.count(a_bg) != 1 or (a_assert_old not in m and a_assert_new not in m):
    die("Menu drawing code in " + SCREEN_CPP + " is not what I expected")
m = m.replace(a_assert_old, a_assert_new, 1)
m = m.replace(a_bg, a_bg + piece("menu4_code.cpp.txt"), 1)
inc = '#include "coretech/vision/engine/image.h"\n'
if "#include <vector>" not in m and inc in m:
    m = m.replace(inc, inc + "#include <algorithm>\n#include <vector>\n", 1)

# COOL FEATURES screens are drawn as lists (one choice per line), also on 2.0
draw_menu = "void FaceInfoScreen::DrawMenu(Vision::ImageRGB565& img) const \n{\n  if (IsXray()) {\n"
# styled, scrolling COOL FEATURES lists (v9): replace any older helper with the list-style block
style = piece("list_style.cpp.txt").replace("@@LIST_CASES@@\n", "".join("    case ScreenName::%s:\n" % n for n in LIST_SCREENS))
old_helper_re = re.compile(r"// JamesOS: the COOL FEATURES screens are lists, drawn one choice per line\nstatic bool JamesIsListScreen\(ScreenName name\)\n\{.*?\n\}\n\n", re.S)
style_re = re.compile(r"// >>> JamesOS list style \(begin\).*?// <<< JamesOS list style \(end\)\n\n", re.S)
if style_re.search(m):
    m = style_re.sub(lambda _: style, m, count=1)
elif old_helper_re.search(m):
    m = old_helper_re.sub(lambda _: style, m, count=1)
else:
    must_have(m, draw_menu, SCREEN_CPP)
    m = m.replace(draw_menu, style + "void FaceInfoScreen::DrawMenu(Vision::ImageRGB565& img) const \n{\n"
                  "  if (IsXray() && !JamesIsListScreen(_name)) {\n", 1)
# DrawMenu: James screens use the styled list
dm_head = "void FaceInfoScreen::DrawMenu(Vision::ImageRGB565& img) const \n{\n"
if "JamesDrawList(img, _name" not in m:
    must_have(m, dm_head, SCREEN_CPP)
    m = m.replace(dm_head, dm_head +
                  "  if (JamesIsListScreen(_name)) { // JamesOS styled list\n"
                  "    std::vector<std::string> items;\n"
                  "    for (const auto& item : _menu) { items.push_back(item.text); }\n"
                  "    JamesDrawList(img, _name, items, _menuCursor);\n"
                  "    return;\n"
                  "  }\n", 1)
inc = '#include "coretech/vision/engine/image.h"\n'
drawer_inc = '#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS badges\n'
if "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" not in m and inc in m:
    m = m.replace(inc, inc + drawer_inc, 1)
for extra in ("#include <map>\n", "#include <string>\n", "#include <utility>\n", "#include <vector>\n"):
    if extra not in m and inc in m:
        m = m.replace(inc, inc + extra, 1)
# COOL FEATURES remembers the cursor when you come back from LIGHTS / STATUS / BADGES
enter_a = "void FaceInfoScreen::EnterScreen()\n{\n  _menuCursor = 0;\n"
exit_a = "void FaceInfoScreen::ExitScreen()\n{\n  _menuCursor = 0;\n"
if "JamesRestoreCursor" not in m.split("// >>> JamesOS list style (begin)")[0]:
    must_have(m, enter_a, SCREEN_CPP)
    must_have(m, exit_a, SCREEN_CPP)
    m = m.replace(enter_a,
                  "// JamesOS: defined with the list style below\n"
                  "size_t JamesRestoreCursor(ScreenName name, size_t count);\n"
                  "void JamesRememberCursor(ScreenName name, size_t cursor);\n\n"
                  "void FaceInfoScreen::EnterScreen()\n{\n  _menuCursor = 0;\n"
                  "  _menuCursor = static_cast<decltype(_menuCursor)>(JamesRestoreCursor(_name, _menu.size())); // JamesOS\n", 1)
    m = m.replace(exit_a,
                  "void FaceInfoScreen::ExitScreen()\n{\n"
                  "  JamesRememberCursor(_name, _menuCursor); // JamesOS\n"
                  "  _menuCursor = 0;\n", 1)
if m != m0:
    write(SCREEN_CPP, m)
    print("[7] Menu drawing ready (classic PAGE 1 / PAGE 2 lists, values in green)")
else:
    print("[7] Menu drawing already up to date")

# ------------------------------------------------------------------ [18] smooth wheel
w = read(INFO_CPP)
if "JamesWheelMove" not in w:
    a_const = "  const f32 kWheelMotionThresh_mmps = 3.f;\n"
    a_fwd = "      if (_wheelMovingForwardsCount == kMenuCursorMoveCountThresh) {\n"
    a_bwd = "      if (_wheelMovingBackwardsCount == kMenuCursorMoveCountThresh) {\n"
    for a in (a_const, a_fwd, a_bwd):
        must_have(w, a, INFO_CPP)
    w = w.replace(a_const, a_const + piece("wheel_code.cpp.txt"), 1)
    w = w.replace(a_fwd, "      if (JamesWheelMove(_wheelMovingForwardsCount)) { // JamesOS: keeps moving while you turn\n", 1)
    w = w.replace(a_bwd, "      if (JamesWheelMove(_wheelMovingBackwardsCount)) { // JamesOS: keeps moving while you turn\n", 1)
    if "#include <chrono>\n" not in w:
        w = w.replace("#include <fstream>\n", "#include <chrono>\n#include <fstream>\n", 1)
    write(INFO_CPP, w)
    print("[18] Smooth wheel: keep turning and the cursor keeps moving")
else:
    print("[18] Smooth wheel already added")

# ------------------------------------------------------------------ [8] petting signal
bp = read(BPL_CPP)
if "JamesSetPetting" not in bp:
    trig = "  _mostRecentTrigger = trigger;\n"
    manual = "  _mostRecentTrigger = BackpackAnimationTrigger::Off;\n"
    must_have(bp, trig, BPL_CPP)
    must_have(bp, manual, BPL_CPP)
    bp = bp.replace(trig, trig + "  ProceduralFaceDrawer::JamesSetPetting(trigger == BackpackAnimationTrigger::Petting); // JamesOS hearts\n", 1)
    bp = bp.replace(manual, manual + "  ProceduralFaceDrawer::JamesSetPetting(false); // JamesOS hearts\n", 1)
    first_inc = bp.index("#include")
    bp = bp[:first_inc] + '#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS hearts\n' + bp[first_inc:]
    write(BPL_CPP, bp)
    print("[8] Petting signal added")
else:
    print("[8] Petting signal already added")

# ------------------------------------------------------------------ [9] mood signal
md = read(MOOD_CPP)
if "jamesos_mood" not in md:
    anchor = "  const bool hasAudioComp = dependentComps.HasComponent<Audio::EngineRobotAudioClient>();\n"
    must_have(md, anchor, MOOD_CPP)
    md = md.replace(anchor, piece("mood_code.cpp.txt") + anchor, 1)
    for inc in ("#include <cstdio>\n", "#include <fstream>\n", "#include <sys/stat.h>\n"):
        if inc not in md:
            md = md.replace("#include <assert.h>\n", "#include <assert.h>\n" + inc, 1)
    write(MOOD_CPP, md)
    print("[9] Mood signal added")
else:
    print("[9] Mood signal already added")

# ------------------------------------------------------------------ [11] status signal
s = read(INFO_CPP)
s0 = s
inc_anchor = '#include "cozmoAnim/robotDataLoader.h"\n'
if "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" not in s:
    must_have(s, inc_anchor, INFO_CPP)
    s = s.replace(inc_anchor, inc_anchor + '#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS\n', 1)
upd = "void FaceInfoScreenManager::Update(const RobotState& state)\n{\n"
if "JamesSetStatus(" not in s:
    must_have(s, upd, INFO_CPP)
    s = s.replace(upd, upd + piece("status_hook.cpp.txt"), 1)
if s != s0:
    write(INFO_CPP, s)
    print("[11] Status signal added (battery, charger, clock)")
else:
    print("[11] Status signal already added")

# ------------------------------------------------------------------ [12] old Halloween lights loader (retired in v23)
# Halloween is now one of the holiday back light sets in [23], which switch live.
# Take the old Halloween-only loader out (the build stops on unused code).
ld = read(LOADER_CPP)
ld12 = ld.replace("    } else if(JamesHalloweenLights()) { // JamesOS Halloween theme\n"
                  "      const auto& fileInfo = animLoader.CollectAnimFiles({kPathToJamesHalloweenLights});\n"
                  "      LoadBackpackLightAnimations(fileInfo);\n", "", 1)
old_piece = piece("halloween_loader.cpp.txt")
ld12 = ld12.replace(old_piece, "", 1)
if "JamesHalloweenLights" in ld12:
    # a slightly different older copy: cut from its first line to the end of the function
    a = ld12.find("// JamesOS: Halloween backpack lights")
    b = ld12.find("\n}\n", ld12.find("bool JamesHalloweenLights()"))
    if a != -1 and b != -1:
        ld12 = ld12[:a] + ld12[b + 3:]
if "JamesHalloweenLights" in ld12:
    print("ERROR: couldn't take out the old Halloween lights loader in " + LOADER_CPP)
    print("That file was not changed. Send this message to Claude.")
    sys.exit(1)
if ld12 != ld:
    write(LOADER_CPP, ld12)
    print("[12] Old Halloween-only lights loader taken out (holiday lights replace it)")
else:
    print("[12] Old Halloween-only lights loader already gone")

# ------------------------------------------------------------------ [13] remove voice eye colors
# JamesOS voice eye colors are turned off for now: put Vector's own eye color code back
# (the version from before JamesOS changed it), and take the JamesOS colors out of wire-pod.
import glob, json, subprocess, time
EYE_H = EYE_CPP[:-4] + ".h"
EYE_JSON = "resources/config/engine/jamesos_eye_colors.json"

def original_from_git(path):
    """Newest version of `path` in git history that has no JamesOS eye color code."""
    try:
        revs = subprocess.run(["git", "rev-list", "HEAD", "--", path], capture_output=True, text=True, check=True).stdout.split()
        for rev in revs:
            r = subprocess.run(["git", "show", "%s:%s" % (rev, path)], capture_output=True, text=True)
            if r.returncode == 0 and "TrySetJamesEyeColor" not in r.stdout and "JamesEyeColorDebug" not in r.stdout:
                return r.stdout
    except Exception:
        pass
    return None

removed = []
for path, bundled in ((EYE_CPP, "behaviorEyeColorVoiceCommand.cpp"), (EYE_H, "behaviorEyeColorVoiceCommand.h")):
    if os.path.exists(path) and "TrySetJamesEyeColor" in read(path):
        orig = original_from_git(path) or piece(os.path.join("eyecolor_original", bundled))
        write(path, orig)
        removed.append(os.path.basename(path))
if os.path.exists(EYE_JSON):
    os.remove(EYE_JSON)
    removed.append("jamesos_eye_colors.json")
print("[13] Voice eye colors removed (%s)" % ", ".join(removed) if removed else "[13] Voice eye colors already removed")

# wire-pod: take out the "JamesOS eye ..." voice commands (keeps everything else, makes a backup)
for intents_path in glob.glob("/mnt/c/Users/*/AppData/Roaming/wire-pod/customIntents.json"):
    try:
        txt = read(intents_path).strip()
        intents = json.loads(txt) if txt else []
        keep = [i for i in intents if not str(i.get("name", "")).startswith("JamesOS eye ")]
        if len(keep) != len(intents):
            backup = intents_path + ".backup-" + time.strftime("%Y%m%d-%H%M%S")
            shutil.copy(intents_path, backup)
            with open(intents_path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(keep, f, indent=2)
            print("     wire-pod: removed %d JamesOS eye color commands (backup: %s)" % (len(intents) - len(keep), os.path.basename(backup)))
            print("     >>> Restart wire-pod so it stops using them <<<")
        else:
            print("     wire-pod: no JamesOS eye color commands (nothing to remove)")
    except Exception as ex:
        print("     wire-pod: couldn't clean %s (%s) - send this line to Claude" % (intents_path, ex))

# ------------------------------------------------------------------ [17] power off without the button
BOT_CPP = "robot/supervisor/src/cozmoBot.cpp"
if os.path.exists(BOT_CPP):
    b = read(BOT_CPP)
    b0 = b
    code = piece("poweroff_code.cpp.txt")
    pw_re = re.compile(r"      // >>> JamesOS power off \(begin\).*?      // <<< JamesOS power off \(end\)\n\n", re.S)
    func_anchor = "      void CheckForOverheatingBatteryShutdown()\n"
    call_anchor = "          CheckForOverheatingBatteryShutdown();\n"
    if pw_re.search(b):
        b = pw_re.sub(lambda _: code, b, count=1)
    elif func_anchor in b and call_anchor in b:
        b = b.replace(func_anchor, code + func_anchor, 1)
    else:
        print("[17] SKIPPED: couldn't find the right spot in " + BOT_CPP + " (send this line to Claude)")
    if "CheckForJamesPowerOff();" not in b and call_anchor in b and "void CheckForJamesPowerOff()" in b:
        b = b.replace(call_anchor, call_anchor + "          CheckForJamesPowerOff(); // JamesOS\n", 1)
    if b != b0:
        write(BOT_CPP, b)
        print("[17] Power off command ready")
    else:
        print("[17] Power off command already added")
else:
    print("[17] SKIPPED: " + BOT_CPP + " not found (send this line to Claude)")

# ------------------------------------------------------------------ [20] name fix
# "Hey Vector, my name is James" sometimes became the name "james my name is james"
# (the voice server heard the sentence twice). Clean the name before Vector saves it.
ENROLL_CPP = "engine/aiComponent/behaviorComponent/behaviors/meetCozmo/behaviorEnrollFace.cpp"
NF_START, NF_END = "// >>> JamesOS name fix (begin)", "// <<< JamesOS name fix (end)"
e = read(ENROLL_CPP)
e0 = e
namefix = piece("name_fix.cpp.txt")
if NF_START in e and NF_END in e:
    a = e.index(NF_START)
    b = e.index(NF_END) + len(NF_END)
    e = e[:a] + namefix.strip("\n") + e[b:]
else:
    anchor = "void BehaviorEnrollFace::CheckForIntentData()\n"
    must_have(e, anchor, ENROLL_CPP)
    e = e.replace(anchor, namefix + anchor, 1)
old_line = "_dVars->persistent.settings.name = meetVictor.username;"
if "JamesCleanSpokenName(meetVictor.username)" not in e:
    must_have(e, old_line, ENROLL_CPP)
    e = e.replace(old_line, "_dVars->persistent.settings.name = JamesCleanSpokenName(meetVictor.username); // JamesOS name fix", 1)
if e != e0:
    write(ENROLL_CPP, e)
    print("[20] Name fix added (\"james my name is james\" -> \"james\")")
else:
    print("[20] Name fix already added")

# ------------------------------------------------------------------ [14] Halloween light theme files
os.makedirs("jamesos", exist_ok=True)
hdst = "jamesos/backpackLights-halloween"
if os.path.exists(hdst):
    print("[14] jamesos/backpackLights-halloween already exists (kept your version)")
else:
    shutil.copytree(WIREOS_LIGHTS, hdst)
    for f in os.listdir(os.path.join(HERE, "lights-halloween")):
        shutil.copy(os.path.join(HERE, "lights-halloween", f), os.path.join(hdst, f))
    print("[14] Halloween light theme created in jamesos/backpackLights-halloween/")

# ------------------------------------------------------------------ [3] light theme
os.makedirs("jamesos", exist_ok=True)
dst = "jamesos/backpackLights"
if os.path.exists(dst):
    print("[3] jamesos/backpackLights already exists (kept your version)")
else:
    shutil.copytree(WIREOS_LIGHTS, dst)
    for f in os.listdir(os.path.join(HERE, "lights")):
        shutil.copy(os.path.join(HERE, "lights", f), os.path.join(dst, f))
    print("[3] Light theme created in jamesos/backpackLights/")

# ------------------------------------------------------------------ [4] sparkle settings
if os.path.exists("jamesos/starlight.conf"):
    print("[4] jamesos/starlight.conf already exists (kept your version)")
else:
    shutil.copy(os.path.join(HERE, "starlight.conf"), "jamesos/starlight.conf")
    print("[4] jamesos/starlight.conf created")

# ------------------------------------------------------------------ [21] CLEAR OUT SOUL warning
c = read(INFO_CPP)
old_txt = 'ADD_SCREEN_WITH_TEXT(ClearUserData, Main, {"CLEAR OUT SOUL?"});'
# (the list is in ( ) so the macro doesn't split it at the commas)
new_txt = ('ADD_SCREEN_WITH_TEXT(ClearUserData, Main, (std::vector<std::string>{"CLEAR OUT SOUL?", '
           '"FACES+SETTINGS GO", "BADGES ARE KEPT"})); // JamesOS')
if new_txt in c:
    print("[21] CLEAR OUT SOUL warning already added")
else:
    must_have(c, old_txt, INFO_CPP)
    write(INFO_CPP, c.replace(old_txt, new_txt, 1))
    print("[21] CLEAR OUT SOUL now says what it erases (and that badges are kept)")

# ------------------------------------------------------------------ [22] JamesOS survives CLEAR OUT SOUL
# Put JamesOS's own defaults INSIDE the build (resources/config/jamesos -> /anki/.../config/jamesos on the robot):
#   jamesos-keep.sh   runs at every start/shutdown: after CLEAR OUT SOUL it puts the lights, sparkles,
#                     galaxy eyes, updater and badges back; at shutdown it saves the badges to /persist
#   defaults.tar      your backpack lights (+ Halloween lights), starlight.conf, galaxy eyes on
#   jamesos-update    the updater (+ its .service), so it comes back too
import io, tarfile
KEEP_DIR = "resources/config/jamesos"
os.makedirs(KEEP_DIR, exist_ok=True)
changed = []

def put(dst, data, mode=0o644):
    path = os.path.join(KEEP_DIR, dst)
    old = open(path, "rb").read() if os.path.exists(path) else None
    if old != data:
        with open(path, "wb") as f:
            f.write(data)
        changed.append(dst)
    os.chmod(path, mode)

def piece_bytes(rel):
    with open(os.path.join(HERE, rel), "rb") as f:
        return f.read()

put("jamesos-keep.sh", piece_bytes("keep/jamesos-keep.sh"), 0o755)
put("jamesos-update", piece_bytes("updater/jamesos-update"), 0o755)
put("jamesos-update.service", piece_bytes("updater/jamesos-update.service"))
put("whatsnew.txt", piece_bytes("whatsnew.txt"))   # v23h: CONF -> WHAT'S NEW

# defaults.tar (same bytes every time, so the build only changes when your files change)
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as t:
    def add_dir(name):
        ti = tarfile.TarInfo(name); ti.type = tarfile.DIRTYPE; ti.mode = 0o777; ti.mtime = 0
        t.addfile(ti)
    def add_file(name, data, mode=0o666):
        ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = mode; ti.mtime = 0
        t.addfile(ti, io.BytesIO(data))
    for src, dst in (("jamesos/backpackLights", "customBackpackLights"),
                     ("jamesos/backpackLights-halloween", "jamesos/lights-halloween")):
        if os.path.isdir(src):
            add_dir(dst)
            for fn in sorted(os.listdir(src)):
                fp = os.path.join(src, fn)
                if os.path.isfile(fp):
                    with open(fp, "rb") as f:
                        add_file(dst + "/" + fn, f.read(), 0o644)
    add_dir("jamesos")
    if os.path.exists("jamesos/starlight.conf"):
        with open("jamesos/starlight.conf", "rb") as f:
            add_file("jamesos/starlight.conf", f.read())
    add_file("jamesos/hearts.on", b"")
    add_file("jamesos/petfx", b"galaxy\n")
put("defaults.tar", buf.getvalue())
if changed:
    print("[22] JamesOS defaults put inside the build (%s) - he keeps JamesOS after CLEAR OUT SOUL" % ", ".join(changed))
else:
    print("[22] JamesOS defaults already inside the build")

# ------------------------------------------------------------------ [23] holiday back lights (v23)
# Every holiday gets its own petting / charging / listening / thinking lights, and they follow the
# theme LIVE: all sets are loaded at start as "<holiday>~<light>", and the back lights pick the one
# for the current theme each time they start. No restart when the theme changes.
CONT_CPP = "animProcess/src/cozmoAnim/backpackLights/backpackLightAnimationContainer.cpp"
h23 = read(DRAWER_H)
if "JamesLightTheme(" not in h23:
    anchor = "    static s32  JamesBadgeCount();\n"
    must_have(h23, anchor, DRAWER_H)
    h23 = h23.replace(anchor, anchor + "    static std::string JamesLightTheme(); // JamesOS: holiday back lights (\"\" = normal)\n", 1)
    write(DRAWER_H, h23)
h23 = read(DRAWER_H)
if "JamesIsPrototype(" not in h23:
    anchor = "    static s32  JamesBadgeCount();\n"
    must_have(h23, anchor, DRAWER_H)
    h23 = h23.replace(anchor, anchor + "    static bool JamesIsPrototype();       // JamesOS private: this robot is a prototype\n", 1)
    write(DRAWER_H, h23)
h23 = read(DRAWER_H)
if "JamesCreator(" not in h23:
    anchor = "    static s32  JamesBadgeCount();\n"
    must_have(h23, anchor, DRAWER_H)
    h23 = h23.replace(anchor, anchor +
                      "    static std::string JamesCreator();        // JamesOS: credit line on the About page\n"
                      "    static std::string JamesCreatorWebsite();\n", 1)
    write(DRAWER_H, h23)
h23 = read(DRAWER_H)
if "JamesFamilyLines(" not in h23:
    anchor = "    static s32  JamesBadgeCount();\n"
    must_have(h23, anchor, DRAWER_H)
    h23 = h23.replace(anchor, anchor +
                      "    static bool JamesHasFamily();                    // JamesOS private: COOL -> FAMILY\n"
                      "    static std::vector<std::string> JamesFamilyLines();\n"
                      "    static std::string JamesFamilyOnlineText();\n", 1)
    if "#include <vector>\n" not in h23:
        first_ns = h23.index("namespace Anki")
        h23 = h23[:first_ns] + "#include <vector>\n" + h23[first_ns:]
    write(DRAWER_H, h23)

ld = read(LOADER_CPP)
old_branch = ("    } else if(JamesHalloweenLights()) { // JamesOS Halloween theme\n"
              "      const auto& fileInfo = animLoader.CollectAnimFiles({kPathToJamesHalloweenLights});\n"
              "      LoadBackpackLightAnimations(fileInfo);\n")
ld = ld.replace(old_branch, "", 1)   # Halloween is now one of the holiday sets below
if "JamesOS holiday back lights" not in ld:
    anchor = "  {\n    LoadBackpackAnimationTriggerMap();\n  }\n"
    must_have(ld, anchor, LOADER_CPP)
    ld = ld.replace(anchor, r"""  // JamesOS holiday back lights: every holiday's set is loaded now as "<holiday>~<light>"; the back
  // lights pick the one for the current theme each time they start (see backpackLightAnimationContainer.cpp).
  // BACK LIGHTS -> NORMAL (lights.off) or Anki lights: no holiday lights.
  {
    struct stat jamesSt;
    if (!_ankilights() && stat("/data/data/jamesos/lights.off", &jamesSt) != 0) {
      static const char* const kJamesHolidayLights[] = {"halloween", "christmas", "lunar", "newyear", "newyear-midnight",
                                                        "valentine", "mothersday", "stpatrick", "easter", "fireworks",
                                                        "thanksgiving", "shiny"};
      static const char* const kJamesLightNames[] = {"charging", "petting", "streaming", "workingOnIt"};
      int jamesLoaded = 0;
      for (const char* holiday : kJamesHolidayLights) {
        for (const char* light : kJamesLightNames) {
          const std::string rel = std::string("config/jamesos/holiday-lights/") + holiday + "/" + light + ".json";
          Json::Value def;
          if (_platform->readAsJson(Util::Data::Scope::Resources, rel, def) && !def.empty()) {
            std::lock_guard<std::mutex> guard(_backpackLoadingMutex);
            _backpackLightAnimations.emplace(std::string("jamesos/") + holiday + "~" + light + ".json", def);
            ++jamesLoaded;
          }
        }
      }
      PRINT_CH_INFO("Animations", "RobotDataLoader.JamesHolidayLights", "Loaded %d holiday back lights", jamesLoaded);
    }
  }

""" + anchor, 1)
ld = ld.replace('"thanksgiving"};\n', '"thanksgiving", "shiny"};\n', 1)   # v23: shiny day lights (older loader)
write(LOADER_CPP, ld)

ct = read(CONT_CPP)
if "JamesLightTheme" not in ct:
    inc = '#include "cozmoAnim/backpackLights/animBackpackLightComponent.h"\n'
    must_have(ct, inc, CONT_CPP)
    ct = ct.replace(inc, inc + '#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS holiday lights\n', 1)
    anchor = "  BackpackLightAnimation::BackpackAnimation* animPtr = nullptr;\n  auto retVal = _animations.find(name);\n"
    must_have(ct, anchor, CONT_CPP)
    ct = ct.replace(anchor, """  // JamesOS: use the holiday version of this light if there is one (e.g. "christmas~charging")
  {
    const std::string jamesTheme = ProceduralFaceDrawer::JamesLightTheme();
    if (!jamesTheme.empty()) {
      const auto jamesIt = _animations.find(jamesTheme + "~" + name);
      if (jamesIt != _animations.end()) {
        return const_cast<BackpackLightAnimation::BackpackAnimation*>(&jamesIt->second);
      }
    }
  }
""" + anchor, 1)
    write(CONT_CPP, ct)

bp = read(BPL_CPP)
if "jamesLightTheme" not in bp:
    anchor = "  if(trigger != _internalCriticalLightsTrigger)\n"
    must_have(bp, anchor, BPL_CPP)
    bp = bp.replace(anchor, """  // JamesOS: the holiday changed -> start the light again so it shows the new holiday's colors
  {
    static std::string jamesLightTheme = ProceduralFaceDrawer::JamesLightTheme();
    const std::string jamesNow = ProceduralFaceDrawer::JamesLightTheme();
    if (jamesNow != jamesLightTheme) {
      jamesLightTheme = jamesNow;
      if (trigger != BackpackAnimationTrigger::Off) { _internalCriticalLightsTrigger = BackpackAnimationTrigger::Off; }
    }
  }
""" + anchor, 1)
    write(BPL_CPP, bp)

# the light files go inside the build: resources/config/jamesos/holiday-lights/<holiday>/<light>.json
HL_SRC = os.path.join(HERE, "holiday-lights")
HL_DST = "resources/config/jamesos/holiday-lights"
hl_changed = 0
want = set()
for holiday in sorted(os.listdir(HL_SRC)):
    for fn in sorted(os.listdir(os.path.join(HL_SRC, holiday))):
        want.add((holiday, fn))
        os.makedirs(os.path.join(HL_DST, holiday), exist_ok=True)
        data = open(os.path.join(HL_SRC, holiday, fn), "rb").read()
        dst = os.path.join(HL_DST, holiday, fn)
        if not os.path.exists(dst) or open(dst, "rb").read() != data:
            with open(dst, "wb") as f:
                f.write(data)
            hl_changed += 1
for holiday in os.listdir(HL_DST):
    for fn in os.listdir(os.path.join(HL_DST, holiday)):
        if (holiday, fn) not in want:
            os.remove(os.path.join(HL_DST, holiday, fn)); hl_changed += 1
print("[23] Holiday back lights: %d sets, they follow the theme live%s"
      % (len(os.listdir(HL_SRC)), " (updated)" if hl_changed else ""))
print("[24] Rare things: shiny days (1 in 4096), blue moon nights" +
      ("; PRIVATE extras ON (Vector family, prototype)" if JAMESOS_PRIVATE else ""))

print("\nAll done. Next: ./jamesos.sh go   (builds + deploys)")
