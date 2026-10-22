/**
* File: faceInfoScreen.cpp
*
* Author: Kevin Yoon
* Created: 03/02/2018
*
* Description: Face screen object for Customer Support Info / Debug screens that
*              defines menu text, menu item selection behavior, and screen timeouts.
*              Except for menu text, which is drawn with DrawMenu() to the given image,
*              other screen content is draw separately and is handled primarily
*              by FaceInfoScreenManager
*
* Copyright: Anki, Inc. 2018
*
*/
#include "anki/cozmo/shared/cozmoConfig.h"
#include "cozmoAnim/faceDisplay/faceInfoScreen.h"
#include "coretech/common/shared/math/rect.h"
#include "coretech/common/engine/utils/timer.h"
#include "coretech/vision/engine/image.h"
#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS badges
#include <utility>
#include <string>
#include <map>
#include <algorithm>
#include <vector>

namespace Anki {
namespace Vector {


FaceInfoScreen::FaceInfoScreen(ScreenName name,
                               ScreenName buttonGotoScreen)
:FaceInfoScreen(name, buttonGotoScreen, {})
{
}

  
FaceInfoScreen::FaceInfoScreen(ScreenName name,
                               ScreenName buttonGotoScreen,
                               const std::vector<std::string>& staticText)
: _name(name)
, _buttonScreen(buttonGotoScreen)
, _timeoutScreen(ScreenName::None)
, _timeoutDuration_s(kDefaultScreenTimeoutDuration_s)
, _timeout_s(0.f)
, _menuCursor(0)
, _staticText(staticText)
{

}

// JamesOS: defined with the list style below
size_t JamesRestoreCursor(ScreenName name, size_t count);
void JamesRememberCursor(ScreenName name, size_t cursor);

void FaceInfoScreen::EnterScreen()
{
  _menuCursor = 0;
  _menuCursor = static_cast<decltype(_menuCursor)>(JamesRestoreCursor(_name, _menu.size())); // JamesOS
  
  // Only set timeout if the timeout screen is different from the current screen.
  if (_timeoutScreen != _name) {
    RestartTimeout();
  }

  if (_enterAction) {
    _enterAction();
  }
}

void FaceInfoScreen::ExitScreen()
{
  JamesRememberCursor(_name, _menuCursor); // JamesOS
  _menuCursor = 0;
  
  if (_exitAction) {
    _exitAction();
  }
}

bool FaceInfoScreen::IsTimedOut()
{
  if (_timeout_s > 0.f) {
    const auto currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    return currTime_s > _timeout_s;
  }
  return false;
}
  
void FaceInfoScreen::SetTimeout(f32 seconds, ScreenName gotoScreen)
{
  _timeoutDuration_s = seconds;
  _timeoutScreen = gotoScreen;
}
  
void FaceInfoScreen::RestartTimeout()
{
  if (_timeoutDuration_s > 0.f) {
    const auto currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    _timeout_s = currTime_s + _timeoutDuration_s;
  }
}
  
void FaceInfoScreen::AppendMenuItem(const std::string& text, ScreenName gotoScreen)
{
  AppendMenuItem(text, [gotoScreen](){ return gotoScreen;});
}
  
void FaceInfoScreen::AppendMenuItem(const std::string& text, MenuItemAction action)
{
  _menu.emplace_back(text, action);
}

void FaceInfoScreen::DrawMenuVertical(Vision::ImageRGB565& img) const
{
  const ColorRGBA& menuBgColor = NamedColors::BLACK;
  const ColorRGBA& menuItemColor = NamedColors::WHITE;
  const f32 locX = 10;
  const f32 stepY = 11;
  const f32 textScale = 0.4f;

  f32 locY = stepY;
  for (auto& text : _staticText) {
    img.DrawText({0,locY}, text, menuItemColor, textScale);
    locY += stepY;
  }
  
  if (HasMenu()) {
    // Draw menu items (bottom-aligned)
    locY = FACE_DISPLAY_HEIGHT-1;
    for (s32 i = static_cast<s32>(_menu.size()) - 1; i >= 0; --i) {
      
      if (_menuCursor == i) {
        // Draw cursor
        img.DrawText({0,locY}, ">", menuItemColor, textScale);
      } else {
        const Rectangle<f32> rect( 0.f, locY-stepY, FACE_DISPLAY_WIDTH, stepY);
        img.DrawFilledRect(rect, menuBgColor);
      }

      // Draw menu item text
      img.DrawText({locX, locY}, _menu[i].text, menuItemColor, textScale);
      locY -= stepY;
    }
  }
}

void FaceInfoScreen::DrawMenuHorizontal(Vision::ImageRGB565& img) const
{
  const ColorRGBA& menuBgColor = NamedColors::BLACK;
  const ColorRGBA& menuItemColor = NamedColors::WHITE;
  const f32 stepY = 11;
  const f32 textScale = 0.4f;

  f32 locY = stepY;
  for (auto& text : _staticText) {
    img.DrawText({0,locY}, text, menuItemColor, textScale);
    locY += stepY;
  }
  
  if (HasMenu()) {
    // Draw menu items (bottom)
    locY = FACE_DISPLAY_HEIGHT-1;
    s32 menus = static_cast<s32>(_menu.size());

    ASSERT_NAMED(menus <= 4, "More than 4 menus in same row will not fit");
    
    s32 blocks = menus;
    f32 blockSize = FACE_DISPLAY_WIDTH / blocks;
    s32 blockDrawn = 1;

    // Clear background
    const Rectangle<f32> rect(0.f, FACE_DISPLAY_HEIGHT -stepY, FACE_DISPLAY_WIDTH, stepY);
    img.DrawFilledRect(rect, menuBgColor);

    if (menus >= 3) {
      // JamesOS: 3 or 4 buttons in one row (v24: 3 too, so EXTRAS fits on the 2.0). Size each button by its text and spread
      // them evenly; if they don't fit, use slightly smaller text for this row.
      const f32 rightMargin = 2.f;
      f32 scale = textScale;
      f32 cursorW = 8.f;
      std::vector<f32> widths;
      f32 total = 0.f;
      for (int pass = 0; pass < 2; ++pass) {
        widths.clear();
        total = 0.f;
        for (const auto& item : _menu) {
          const f32 w = cursorW + Vision::Image::GetTextSize(item.text, scale, 1).x();
          widths.push_back(w);
          total += w;
        }
        if (total <= FACE_DISPLAY_WIDTH - rightMargin) {
          break;
        }
        scale = 0.35f;
        cursorW = 7.f;
      }
      const f32 gap = std::max(0.f, (FACE_DISPLAY_WIDTH - rightMargin - total) / (f32)(menus - 1));
      f32 x = 0.f;
      for (s32 i = 0; i < menus; ++i) {
        if (_menuCursor == i) img.DrawText({x, locY}, ">", menuItemColor, scale);
        img.DrawText({x + cursorW, locY}, _menu[i].text, menuItemColor, scale);
        x += widths[i] + gap;
      }
      return;
    }
    // JamesOS: end 4 buttons

    for (s32 i = menus - 1; i >= 0; --i) {
      f32 x = (FACE_DISPLAY_WIDTH - (blockSize * blockDrawn));
      f32 y = locY;

      // Draw menu item text
      if (_menuCursor == i) img.DrawText({x, y}, ">", menuItemColor, textScale);
      img.DrawText({x + 10, y}, _menu[i].text, menuItemColor, textScale);
      blockDrawn++;
    }
  }
}

// >>> JamesOS list style (begin)
// JamesOS: the EXTRAS / CONFIGURATION screens are drawn in the classic style:
//   "CONFIGURATION PAGE 1", then a plain list with ">" on the selected line,
//   settings' values in green on the right, info lines under the title.
static bool JamesIsListScreen(ScreenName name)
{
  switch (name) {
    case ScreenName::JamesOS:
    case ScreenName::JamesStars:
    case ScreenName::JamesEffects:
    case ScreenName::JamesTheme:
    case ScreenName::JamesSleep:
    case ScreenName::JamesLights:
    case ScreenName::JamesStatus:
    case ScreenName::JamesBadges:
    case ScreenName::JamesConf:
    case ScreenName::JamesAbout:
    case ScreenName::JamesRestart:
    case ScreenName::JamesSecret:
    case ScreenName::JamesProof:
    case ScreenName::JamesNews:
    case ScreenName::JamesFamily:
    case ScreenName::JamesGame:
      return true;
    default:
      return false;
  }
}

static const char* JamesListTitle(ScreenName name)
{
  switch (name) {
    case ScreenName::JamesOS:      return "JAMESOS EXTRAS";
    case ScreenName::JamesStars:   return "STARS";
    case ScreenName::JamesEffects: return "EFFECTS";
    case ScreenName::JamesTheme:   return "THEME";
    case ScreenName::JamesSleep:   return "SLEEP SCREEN";
    case ScreenName::JamesLights:  return "BACK LIGHTS";
    case ScreenName::JamesStatus:  return "STATUS";
    case ScreenName::JamesBadges:  return "BADGES";
    case ScreenName::JamesConf:    return "CONFIGURATION";
    case ScreenName::JamesAbout:   return "ABOUT JAMESOS";
    case ScreenName::JamesRestart: return "RESTART";
    case ScreenName::JamesSecret:  return "SECRET ROOM";
    case ScreenName::JamesProof:   return "MASTER PROOF";
    case ScreenName::JamesNews:    return "WHAT'S NEW";
    case ScreenName::JamesFamily:  return "MY FAMILY";
    case ScreenName::JamesGame:    return "TIC-TAC-TOE";
    default:                       return "JAMESOS";
  }
}

static std::map<ScreenName, std::vector<std::pair<std::string, int>>>& JamesListInfo()
{
  static std::map<ScreenName, std::vector<std::pair<std::string, int>>> info;
  return info;
}

static std::map<ScreenName, std::vector<std::string>>& JamesListValues()
{
  static std::map<ScreenName, std::vector<std::string>> values;
  return values;
}

void JamesSetListInfo(ScreenName screen, const std::vector<std::pair<std::string, int>>& lines)
{
  JamesListInfo()[screen] = lines;
}

void JamesSetListValues(ScreenName screen, const std::vector<std::string>& values)
{
  JamesListValues()[screen] = values;
}

// JamesOS: coming back to a menu puts the cursor where you left it
// (EXTRAS, CONF and the main screen). Leaving the menus forgets it.
static std::map<ScreenName, size_t>& JamesCursorMemory()
{
  static std::map<ScreenName, size_t> mem;
  return mem;
}
void JamesRememberCursor(ScreenName name, size_t cursor)
{
  if (name == ScreenName::JamesOS || name == ScreenName::JamesConf || name == ScreenName::Main) {
    JamesCursorMemory()[name] = cursor;
  }
}
size_t JamesRestoreCursor(ScreenName name, size_t count)
{
  if (name == ScreenName::None) {
    JamesCursorMemory().clear();
    return 0;
  }
  const auto it = JamesCursorMemory().find(name);
  if (it != JamesCursorMemory().end() && it->second < count) {
    return it->second;
  }
  return 0;
}

// v26: TIC-TAC-TOE (EXTRAS -> TIC-TAC-TOE). The game itself lives in the menu code; it hands the
// board over as list values: [0..8] squares ("X" you, "O" Vector, "" empty), [9] status line,
// [10] your wins, [11] Vector's wins, [12] the winning line ("a b c" square numbers, or "").
// Cursor 0..8 = a square (wheels move it, lift places your X), 9 = BACK.
static void JamesDrawTicTacToe(Vision::ImageRGB565& img, size_t cursor)
{
  static const ColorRGBA kBg(u8(0), u8(0), u8(0));
  static const ColorRGBA kFg(u8(255), u8(255), u8(255));
  static const ColorRGBA kGreen(u8(80), u8(255), u8(80));
  static const ColorRGBA kDim(u8(130), u8(130), u8(130));
  static const ColorRGBA kGrid(u8(90), u8(90), u8(90));
  static const ColorRGBA kSel(u8(30), u8(80), u8(40));
  static const ColorRGBA kO(u8(255), u8(160), u8(40));
  static const ColorRGBA kGold(u8(255), u8(200), u8(40));

  const s32 W = img.GetNumCols();
  const s32 H = img.GetNumRows();
  const bool small = (H < 96);
  img.DrawFilledRect(Rectangle<f32>(0.f, 0.f, (f32)W, (f32)H), kBg);

  std::vector<std::string> v(13);
  const auto it = JamesListValues().find(ScreenName::JamesGame);
  if (it != JamesListValues().end()) {
    for (size_t i = 0; i < v.size() && i < it->second.size(); ++i) { v[i] = it->second[i]; }
  }

  const s32 cell = (H - (small ? 14 : 16)) / 3;
  const s32 B = cell * 3;
  const s32 x0 = (W - B) / 2;
  const s32 y0 = 2;
  auto centerOf = [&](s32 k) {
    return Point2f((f32)(x0 + (k % 3) * cell) + cell * 0.5f, (f32)(y0 + (k / 3) * cell) + cell * 0.5f);
  };

  // the square you're on
  if (cursor < 9) {
    const s32 cx = x0 + ((s32)cursor % 3) * cell, cy = y0 + ((s32)cursor / 3) * cell;
    img.DrawFilledRect(Rectangle<f32>((f32)cx + 1.f, (f32)cy + 1.f, (f32)cell - 1.f, (f32)cell - 1.f), kSel);
  }
  // the grid
  for (s32 i = 1; i <= 2; ++i) {
    img.DrawLine(Point2f((f32)(x0 + i * cell), (f32)y0), Point2f((f32)(x0 + i * cell), (f32)(y0 + B)), kGrid, 1);
    img.DrawLine(Point2f((f32)x0, (f32)(y0 + i * cell)), Point2f((f32)(x0 + B), (f32)(y0 + i * cell)), kGrid, 1);
  }
  // X and O
  const f32 m = cell * 0.25f;
  for (s32 k = 0; k < 9; ++k) {
    const Point2f c = centerOf(k);
    const f32 r = cell * 0.5f - m;
    if (v[k] == "X") {
      img.DrawLine(Point2f(c.x() - r, c.y() - r), Point2f(c.x() + r, c.y() + r), kFg, 2);
      img.DrawLine(Point2f(c.x() - r, c.y() + r), Point2f(c.x() + r, c.y() - r), kFg, 2);
    } else if (v[k] == "O") {
      img.DrawCircle(c, kO, (s32)r, 2);
    }
  }
  // the winning line
  {
    const std::string& wl = v[12];   // "a b c", one digit each
    if (wl.size() == 5 && wl[0] >= '0' && wl[0] <= '8' && wl[4] >= '0' && wl[4] <= '8') {
      img.DrawLine(centerOf(wl[0] - '0'), centerOf(wl[4] - '0'), kGreen, 2);
    }
  }

  // side columns: YOU (X) on the left, VECTOR (O) on the right, wins under each
  const f32 ts = small ? 0.3f : 0.34f;
  auto drawCentered = [&](const std::string& t, f32 cxp, f32 baseline, const ColorRGBA& col, f32 scale) {
    f32 s = scale;
    f32 w = img.GetTextSize(t, s, 1).x();
    while (w > (f32)x0 - 4.f && s > 0.22f) { s -= 0.02f; w = img.GetTextSize(t, s, 1).x(); }
    img.DrawText({cxp - w * 0.5f, baseline}, t, col, s);
  };
  const f32 lx = x0 * 0.5f, rx = (f32)(x0 + B) + (f32)(W - x0 - B) * 0.5f;
  drawCentered("YOU", lx, small ? 11.f : 13.f, kFg, ts);
  drawCentered("VECTOR", rx, small ? 11.f : 13.f, kFg, ts);
  const f32 my = small ? 26.f : 31.f, mr = small ? 7.f : 8.f;
  img.DrawLine(Point2f(lx - mr, my - mr), Point2f(lx + mr, my + mr), kFg, 2);
  img.DrawLine(Point2f(lx - mr, my + mr), Point2f(lx + mr, my - mr), kFg, 2);
  img.DrawCircle(Point2f(rx, my), kO, (s32)mr, 2);
  drawCentered(v[10].empty() ? std::string("0") : v[10], lx, small ? 50.f : 58.f, kGold, ts + 0.06f);
  drawCentered(v[11].empty() ? std::string("0") : v[11], rx, small ? 50.f : 58.f, kGold, ts + 0.06f);
  drawCentered(cursor == 9 ? "> BACK" : "BACK", lx, (f32)(y0 + B) - 1.f, cursor == 9 ? kGreen : kDim, ts);

  // status line under the board
  {
    const std::string& st = v[9];
    f32 s = ts;
    f32 w = img.GetTextSize(st, s, 1).x();
    while (w > (f32)W - 4.f && s > 0.22f) { s -= 0.02f; w = img.GetTextSize(st, s, 1).x(); }
    img.DrawText({((f32)W - w) * 0.5f, (f32)H - 2.f}, st, (st.find("WIN") != std::string::npos || st == "DRAW") ? kGold : kFg, s);
  }
}

static void JamesDrawList(Vision::ImageRGB565& img, ScreenName name,
                          const std::vector<std::string>& items, size_t cursor)
{
  if (name == ScreenName::JamesGame) {   // v26: the tic-tac-toe board instead of a list
    JamesDrawTicTacToe(img, cursor);
    return;
  }
  static const ColorRGBA kBg(u8(0), u8(0), u8(0));
  static const ColorRGBA kFg(u8(255), u8(255), u8(255));
  static const ColorRGBA kGreen(u8(80), u8(255), u8(80));
  static const ColorRGBA kRed(u8(255), u8(90), u8(90));
  static const ColorRGBA kDim(u8(150), u8(150), u8(150));
  static const ColorRGBA kGold(u8(255), u8(200), u8(40));

  const s32 W = img.GetNumCols();
  const s32 H = img.GetNumRows();
  const bool small = (H < 96);                 // Vector 2.0 screen
  const f32 kScale = small ? 0.36f : 0.4f;
  const s32 kRow = small ? 11 : 14;            // height of one line in the list
  const s32 kInfo = small ? 10 : 12;           // height of one info line
  const s32 kMaxPerPage = 5;

  img.DrawFilledRect(Rectangle<f32>(0.f, 0.f, (f32)W, (f32)H), kBg);

  // info lines (color: 0 white, 1 green, 2 red, 3 gray, 4 gold)
  std::vector<std::pair<std::string, int>> info;
  const auto it = JamesListInfo().find(name);
  if (it != JamesListInfo().end()) { info = it->second; }

  // how many lines fit on a page
  const s32 titleY = small ? 12 : 14;
  s32 y = titleY + (small ? 4 : 6);
  const s32 infoTop = y;
  y += (s32)info.size() * kInfo + (info.empty() ? 0 : 3);
  const s32 itemsTop = y;
  const s32 n = (s32)items.size();
  const s32 perPage = std::max<s32>(1, std::min<s32>(kMaxPerPage, (H - itemsTop) / kRow));
  const s32 pages = std::max<s32>(1, (n + perPage - 1) / perPage);
  const s32 cur = std::max<s32>(0, std::min<s32>((s32)cursor, n - 1));
  const s32 page = cur / perPage;

  // title: "CONFIGURATION PAGE 1"
  std::string title = JamesListTitle(name);
  if (pages > 1) { title += " PAGE " + std::to_string(page + 1); }
  f32 ts = kScale;
  if (img.GetTextSize(title, ts, 1).x() > (f32)(W - 10)) { ts = 0.32f; }
  img.DrawText({6.f, (f32)titleY}, title, kFg, ts);

  for (size_t k = 0; k < info.size(); ++k) {
    const auto& line = info[k];
    const ColorRGBA& c = (line.second == 1) ? kGreen : (line.second == 2) ? kRed : (line.second == 3) ? kDim :
                         (line.second == 4) ? kGold : kFg;
    f32 is = kScale;
    if (img.GetTextSize(line.first, is, 1).x() > (f32)(W - 10)) { is = 0.3f; }
    img.DrawText({6.f, (f32)(infoTop + (s32)k * kInfo + kInfo - 3)}, line.first, c, is);
  }

  const auto vit = JamesListValues().find(name);
  const bool badges = (name == ScreenName::JamesBadges);
  const s32 badgeOffset = (badges && !items.empty() && items[0] == "BACK") ? 1 : 0;

  for (s32 k = 0; k < perPage; ++k) {
    const s32 i = page * perPage + k;
    if (i >= n) { break; }
    const f32 baseline = (f32)(itemsTop + k * kRow + kRow - 3);
    if (i == cur) {
      img.DrawText({6.f, baseline}, ">", kFg, kScale);
    }
    // secret badges show as "???" until they are found
    const s32 bi = i - badgeOffset;
    const bool hidden = badges && bi >= 0 && bi < ProceduralFaceDrawer::JamesBadgeCount() &&
                        ProceduralFaceDrawer::JamesBadgeIsSecret(bi) && ProceduralFaceDrawer::JamesBadgeDate(bi) <= 0;
    img.DrawText({16.f, baseline}, hidden ? std::string("???") : items[i], kFg, kScale);

    // the setting's value in green on the right (gray when OFF); smaller if it would bump into the name
    if (vit != JamesListValues().end() && i < (s32)vit->second.size() && !vit->second[i].empty()) {
      const std::string& v = vit->second[i];
      const f32 nameEnd = 16.f + img.GetTextSize(items[i], kScale, 1).x();
      f32 vs = kScale;
      f32 vw = img.GetTextSize(v, vs, 1).x();
      if ((f32)W - 6.f - vw < nameEnd + 6.f) {
        vs = 0.3f;
        vw = img.GetTextSize(v, vs, 1).x();
      }
      img.DrawText({(f32)W - 6.f - vw, baseline}, v, (v == "OFF") ? kDim : kGreen, vs);
    }

    // BADGES: a box on the right, filled green when that badge is unlocked
    const s32 b = i - badgeOffset;
    if (badges && b >= 0 && b < ProceduralFaceDrawer::JamesBadgeCount()) {
      const f32 bx = (f32)W - 13.f, by = baseline - 7.f;
      if (ProceduralFaceDrawer::JamesBadgeDate(b) > 0) {
        img.DrawFilledRect(Rectangle<f32>(bx, by, 7.f, 7.f), kGreen);
      } else {
        img.DrawFilledRect(Rectangle<f32>(bx, by, 7.f, 1.f), kDim);
        img.DrawFilledRect(Rectangle<f32>(bx, by + 6.f, 7.f, 1.f), kDim);
        img.DrawFilledRect(Rectangle<f32>(bx, by, 1.f, 7.f), kDim);
        img.DrawFilledRect(Rectangle<f32>(bx + 6.f, by, 1.f, 7.f), kDim);
      }
    }
  }
}
// <<< JamesOS list style (end)

void FaceInfoScreen::DrawMenu(Vision::ImageRGB565& img) const 
{
  if (JamesIsListScreen(_name)) { // JamesOS styled list
    std::vector<std::string> items;
    for (const auto& item : _menu) { items.push_back(item.text); }
    JamesDrawList(img, _name, items, _menuCursor);
    return;
  }
  if (IsXray() && !JamesIsListScreen(_name)) {
    DrawMenuHorizontal(img);
  } else {
    DrawMenuVertical(img);
  }
}



bool FaceInfoScreen::HasMenu() const
{
  return !_menu.empty();
}
  
void FaceInfoScreen::MoveMenuCursorUp()
{
  if (HasMenu()) {
    if (_menuCursor == 0) {
      _menuCursor = _menu.size()-1;
    } else {
      --_menuCursor;
    }
  }
}
void FaceInfoScreen::MoveMenuCursorDown()
{
  if (HasMenu()) {
    ++_menuCursor;
    if (_menuCursor == _menu.size()) {
      _menuCursor = 0;
    }
  }
}

ScreenName FaceInfoScreen::ConfirmMenuItemAndGetNextScreen()
{
  DEV_ASSERT(HasMenu(), "FaceInfoScreen.ConfirmMenuItemAndGetNextScreen.MustHaveMenu");
  
  auto& item = _menu[_menuCursor];
  return item.action();
}
  
} // namespace Vector
} // namespace Anki
