/*
 * File:          animEngine.cpp
 * Date:          6/26/2017
 *
 * Description:   A platform-independent container for spinning up all the pieces
 *                required to run Vector Animation Process.
 *
 * Author: Kevin Yoon
 *
 * Modifications:
 */

#include "cozmoAnim/animEngine.h"

#include "cozmoAnim/alexa/alexa.h"
#include "cozmoAnim/animComms.h"
#include "cozmoAnim/animContext.h"
#include "cozmoAnim/animProcessMessages.h"
#include "cozmoAnim/audio/cozmoAudioController.h"
#include "cozmoAnim/audio/microphoneAudioClient.h"
#include "cozmoAnim/audio/engineRobotAudioInput.h"
#include "cozmoAnim/audio/sdkAudioComponent.h"
#include "cozmoAnim/animation/animationStreamer.h"
#include "cozmoAnim/animation/streamingAnimationModifier.h"
#include "cozmoAnim/backpackLights/animBackpackLightComponent.h"
#include "cozmoAnim/faceDisplay/faceInfoScreenManager.h"
#include "cozmoAnim/micData/micDataSystem.h"
#include "cozmoAnim/perfMetricAnim.h"
#include "cozmoAnim/robotDataLoader.h"
#include "cozmoAnim/showAudioStreamStateManager.h"
#include "cozmoAnim/textToSpeech/textToSpeechComponent.h"

#include "coretech/common/engine/opencvThreading.h"
#include "coretech/common/engine/utils/timer.h"
#include "coretech/vision/shared/spriteCache/spriteCache.h"
#include "audioEngine/multiplexer/audioMultiplexer.h"

#include "webServerProcess/src/webService.h"
#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h" // JamesOS companion page
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "osState/osState.h"

#include "util/cpuProfiler/cpuProfiler.h"
#include "util/logging/logging.h"

#define LOG_CHANNEL    "AnimEngine"
#define NUM_ANIM_OPENCV_THREADS 0

namespace Anki {
namespace Vector {
namespace Anim {

#if ANKI_CPU_PROFILER_ENABLED
  CONSOLE_VAR_RANGED(float, kAnimEngine_TimeMax_ms,     ANKI_CPU_CONSOLEVARGROUP, 33, 2, 33);
  CONSOLE_VAR_ENUM(u8,      kAnimEngine_TimeLogging,    ANKI_CPU_CONSOLEVARGROUP, 0, Util::CpuProfiler::CpuProfilerLogging());
#endif

AnimEngine::AnimEngine(Util::Data::DataPlatform* dataPlatform)
  : _isInitialized(false)
  , _context(std::make_unique<AnimContext>(dataPlatform))
  , _animationStreamer(std::make_unique<AnimationStreamer>(_context.get()))
{
#if ANKI_CPU_PROFILER_ENABLED
  // Initialize CPU profiler early and put tracing file at known location with no dependencies on other systems
  Anki::Util::CpuProfiler::GetInstance();
  Anki::Util::CpuThreadProfiler::SetChromeTracingFile(
      dataPlatform->pathToResource(Util::Data::Scope::Cache, "vic-anim-tracing.json").c_str());
  Anki::Util::CpuThreadProfiler::SendToWebVizCallback([&](const Json::Value& json) { _context->GetWebService()->SendToWebViz("cpuprofile", json); });
#endif

  if (Anki::Util::gTickTimeProvider == nullptr) {
    Anki::Util::gTickTimeProvider = BaseStationTimer::getInstance();
  }

  _microphoneAudioClient.reset(new Audio::MicrophoneAudioClient(_context->GetAudioController()));

#if ANKI_PROFILE_ANIMCOMMS_SOCKET_BUFFER_STATS
  AnimComms::InitSocketBufferStats();
#endif

}

AnimEngine::~AnimEngine()
{
  _context->GetWebService()->Stop();

#if ANKI_PROFILE_ANIMCOMMS_SOCKET_BUFFER_STATS
  AnimComms::ReportSocketBufferStats();
#endif

  if (Anki::Util::gTickTimeProvider == BaseStationTimer::getInstance()) {
    Anki::Util::gTickTimeProvider = nullptr;
  }
  BaseStationTimer::removeInstance();
}

// >>> JamesOS companion page (begin)
// v28: the JamesOS page on the robot itself. A phone on the same Wi-Fi opens
//   http://<robot IP>:8889/jamesos/
// The page (resources/webserver/jamesos/index.html) is served by vic-anim's own web server, and
// these two handlers read and change the same small files the EXTRAS menu uses (/data/data/jamesos).
//   GET  /jamesos/api/state           -> JSON: robot card + every setting + the choices he has unlocked
//   GET  /jamesos/api/badges          -> every badge (secret ones stay ??? until found)
//   GET  /jamesos/api/whatsnew        -> the WHAT'S NEW list that came with this build
//   POST /jamesos/api/set  "key=value" -> changes one setting, answers with the new state
// Safety: changes need POST + the "X-JamesOS: 1" header (other websites can't send that without
// asking first, and we never say yes), and the address must be an IP or a vector name (DNS rebinding).
#ifndef JAMES_COMPANION_ROOT
#define JAMES_COMPANION_ROOT ""
#endif
namespace {
namespace JamesCompanion {

const std::string kDir        = JAMES_COMPANION_ROOT "/data/data/jamesos";
const std::string kVersion    = JAMES_COMPANION_ROOT "/anki/etc/jamesos-version";
const std::string kAnkiLights = JAMES_COMPANION_ROOT "/data/data/enableankilights";
const std::string kLightsSent = JAMES_COMPANION_ROOT "/data/data/customBackpackLights/off.json";
const std::string kWhatsNew   = JAMES_COMPANION_ROOT "/anki/data/assets/cozmo_resources/config/jamesos/whatsnew.txt";

bool Exists(const std::string& f) { struct stat st; return stat(f.c_str(), &st) == 0; }
std::string F(const char* name) { return kDir + "/" + name; }
std::string Word(const std::string& f)
{
  std::ifstream in(f);
  std::string w;
  if (in.is_open()) { std::getline(in, w); }
  while (!w.empty() && (w.back() == '\r' || w.back() == ' ')) { w.pop_back(); }
  return w;
}
bool Has(const std::string& text, const char* w) { return text.find(w) != std::string::npos; }
void Put(const std::string& f, const std::string& text)
{
  mkdir(kDir.c_str(), 0777);
  std::ofstream out(f, std::ios::trunc);
  out << text;
}
void Del(const std::string& f) { unlink(f.c_str()); }
void Flag(const std::string& f, bool on) { if (on) { Put(f, "1"); } else { Del(f); } }

const std::vector<std::string> kThemes = {"auto", "normal", "halloween", "christmas", "lunar", "newyear",
  "valentine", "stpatrick", "aprilfools", "easter", "mothersday", "fireworks", "thanksgiving"};
const std::vector<std::string> kSecretThemes = {"glitch", "retro", "galaxymax"};

bool GoldUnlocked()   { return ProceduralFaceDrawer::JamesHasBadge("STREAK_100"); }   // LOYAL FRIEND
bool SecretUnlocked() { return ProceduralFaceDrawer::JamesHasBadge("SECRET_ROOM"); }

// ---- current value of each setting: same words (and same rules) as the EXTRAS menu ----
std::string Stars()
{
  if (!Exists(F("starlight.conf")) || Exists(F("starlight.off"))) { return "off"; }
  const std::string w = Word(F("starlight.style"));
  if (Has(w, "rainbow"))  { return "rainbow"; }
  if (Has(w, "shooting")) { return "shooting"; }
  if (Has(w, "gold"))     { return "gold"; }
  return "white";
}
std::string Petting()
{
  if (Exists(F("galaxy.always"))) { return "always"; }
  return Exists(F("hearts.on")) ? "galaxy" : "off";
}
std::string Theme()
{
  const std::string w = Word(F("theme"));
  for (const auto& t : kSecretThemes) { if (Has(w, t.c_str())) { return t; } }
  for (const auto& t : kThemes) { if (t != "normal" && Has(w, t.c_str())) { return t; } }
  return Has(w, "normal") ? "normal" : "auto";
}
std::string Holidays()
{
  const std::string w = Word(F("holidays"));
  if (Has(w, "western")) { return "western"; }
  if (Has(w, "off"))     { return "off"; }
  return "all";
}
std::string HolidayTime() { return Has(Word(F("holidaytime")), "day") ? "day" : "season"; }
std::string FunEyes()
{
  if (Exists(F("funeyes.off")))  { return "off"; }
  if (Exists(F("funeyes.auto"))) { return "auto"; }
  return "on";
}
std::string Sleep()
{
  const std::string w = Word(F("sleepscreen"));
  if (Has(w, "stars")) { return "stars"; }
  if (Has(w, "clock")) { return "clock"; }
  if (Has(w, "both"))  { return "both"; }
  return "off";
}
std::string Lights()
{
  if (Exists(kAnkiLights)) { return "anki"; }
  if (!Exists(kLightsSent)) { return "notsent"; }
  return Exists(F("lights.off")) ? "normal" : "custom";
}

// v28: birthday = "MM-DD NAME" in /data/data/jamesos/birthday (the birthday animation uses it)
bool Birthday(int& month, int& day, std::string& name)
{
  const std::string w = Word(F("birthday"));
  if (w.size() < 5 || w[2] != '-') { return false; }
  for (const int i : {0, 1, 3, 4}) { if (w[(size_t)i] < '0' || w[(size_t)i] > '9') { return false; } }
  month = (w[0] - '0') * 10 + (w[1] - '0');
  day = (w[3] - '0') * 10 + (w[4] - '0');
  name = w.size() > 6 ? w.substr(6) : "";
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

std::string Esc(const std::string& s)
{
  std::string o;
  for (const char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) { o += ' '; }
    else { o += c; }
  }
  return o;
}
std::string List(const std::vector<std::string>& v)
{
  std::string o = "[";
  for (size_t i = 0; i < v.size(); ++i) { o += (i ? ",\"" : "\"") + Esc(v[i]) + "\""; }
  return o + "]";
}

std::string StateJson()
{
  f32 volts = 0.f; s32 tempC = 0; bool onCharger = false, charging = false;
  ProceduralFaceDrawer::JamesGetStatus(volts, tempC, onCharger, charging);
  int pct = -1;   // -1 = don't know yet
  if (onCharger && !charging) { pct = 100; }   // charging finished (the voltage isn't real then)
  else if (volts > 1.f) {
    const f32 p = (volts - 3.6f) / (4.1f - 3.6f) * 100.f;
    pct = (int)(p < 0.f ? 0.f : (p > 100.f ? 100.f : p));
  }
  char host[64] = {0};
  if (gethostname(host, sizeof(host) - 1) != 0) { host[0] = 0; }
  std::string label;
  {
    std::ifstream in(kVersion);
    std::string first;
    if (in.is_open()) { std::getline(in, first); std::getline(in, label); }
  }
  const s32 n = ProceduralFaceDrawer::JamesBadgeCount();
  s32 got = 0;
  for (s32 i = 0; i < n; ++i) { if (ProceduralFaceDrawer::JamesBadgeDate(i) > 0) { ++got; } }

  std::vector<std::string> stars = {"rainbow", "white", "shooting"};
  if (GoldUnlocked()) { stars.push_back("gold"); }
  stars.push_back("off");
  std::vector<std::string> themes = kThemes;
  if (SecretUnlocked()) { themes.insert(themes.end(), kSecretThemes.begin(), kSecretThemes.end()); }

  std::string j = "{";
  j += "\"host\":\"" + Esc(host) + "\",";
  j += "\"version\":\"" + Esc(label) + "\",";
  j += "\"battery\":" + std::to_string(pct) + ",";
  j += std::string("\"charger\":") + (onCharger ? "true" : "false") + ",";
  j += std::string("\"charging\":") + (charging ? "true" : "false") + ",";
  j += "\"tempC\":" + std::to_string(tempC) + ",";
  j += "\"petsToday\":" + std::to_string(ProceduralFaceDrawer::JamesPetsToday()) + ",";
  j += "\"badges\":" + std::to_string(got) + ",\"badgesTotal\":" + std::to_string(n) + ",";
  j += "\"holidayNow\":\"" + Esc(ProceduralFaceDrawer::JamesLightTheme()) + "\",";
  {
    int bm = 0, bd = 0;
    std::string bn;
    if (Birthday(bm, bd, bn)) {
      j += "\"birthday\":{\"month\":" + std::to_string(bm) + ",\"day\":" + std::to_string(bd) + ",\"name\":\"" + Esc(bn) + "\"},";
    } else {
      j += "\"birthday\":null,";
    }
  }
  j += "\"settings\":{";
  j += "\"theme\":\"" + Theme() + "\",\"holidaytime\":\"" + HolidayTime() + "\",\"holidays\":\"" + Holidays() + "\",";
  j += "\"stars\":\"" + Stars() + "\",\"petting\":\"" + Petting() + "\",";
  j += std::string("\"mood\":\"") + (Exists(F("mood.on")) ? "on" : "off") + "\",";
  j += std::string("\"sleepy\":\"") + (Exists(F("sleepy.off")) ? "off" : "on") + "\",";
  j += "\"funeyes\":\"" + FunEyes() + "\",\"sleep\":\"" + Sleep() + "\",\"lights\":\"" + Lights() + "\"},";
  j += "\"choices\":{\"stars\":" + List(stars) + ",\"theme\":" + List(themes) + "}";
  j += "}";
  return j;
}

std::string BadgesJson()
{
  std::string j = "{\"badges\":[";
  const s32 n = ProceduralFaceDrawer::JamesBadgeCount();
  for (s32 i = 0; i < n; ++i) {
    const s32 date = ProceduralFaceDrawer::JamesBadgeDate(i);
    const bool secret = ProceduralFaceDrawer::JamesBadgeIsSecret(i);
    const bool hide = secret && date <= 0;   // a secret stays a secret until he finds it
    j += i ? ",{" : "{";
    j += "\"name\":\"" + (hide ? std::string("???") : Esc(ProceduralFaceDrawer::JamesBadgeName(i))) + "\",";
    j += "\"hint\":\"" + (hide ? std::string("A secret. Keep looking") : Esc(ProceduralFaceDrawer::JamesBadgeHint(i))) + "\",";
    j += std::string("\"secret\":") + (secret ? "true" : "false") + ",";
    j += "\"date\":" + std::to_string(date > 0 ? date : 0) + "}";
  }
  return j + "]}";
}

std::string WhatsNewJson()
{
  std::ifstream in(kWhatsNew);
  std::string line, version, items;
  int count = 0;
  while (in.is_open() && std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) { line.pop_back(); }
    if (line.empty() || line[0] == '#') { continue; }
    if (version.empty()) { version = line; continue; }
    const size_t bar = line.find('|');
    std::string name = line.substr(0, bar), text = bar == std::string::npos ? "" : line.substr(bar + 1);
    for (auto& c : text) { if (c == '/') { c = ' '; } }
    items += (count++ ? ",{" : "{");
    items += "\"name\":\"" + Esc(name) + "\",\"text\":\"" + Esc(text) + "\"}";
  }
  return "{\"version\":\"" + Esc(version) + "\",\"items\":[" + items + "]}";
}

bool In(const std::string& v, const std::vector<std::string>& list)
{
  for (const auto& x : list) { if (x == v) { return true; } }
  return false;
}

// Changes one setting the same way the EXTRAS / LIGHTS menus do.
// Returns "" when done, "restart" when he restarts to apply it, or why it didn't work.
std::string Set(const std::string& key, const std::string& v)
{
  if (key == "stars") {
    if (!In(v, {"rainbow", "white", "shooting", "off"}) && !(v == "gold" && GoldUnlocked())) { return "bad value"; }
    if (v == "off") { Put(F("starlight.off"), "1"); return ""; }
    Put(F("starlight.style"), v + "\n");
    Del(F("starlight.off"));
    if (!Exists(F("starlight.conf"))) {
      Put(F("starlight.conf"), "enabled=1\nstars=3\nchance=0.04\nlife=24\nsize=2\ncolor=255,255,255\n");
    }
    return "";
  }
  if (key == "petting") {
    if (!In(v, {"galaxy", "always", "off"})) { return "bad value"; }
    if (v == "off") { Del(F("hearts.on")); Del(F("galaxy.always")); return ""; }
    Put(F("hearts.on"), "1");
    Put(F("petfx"), "galaxy\n");
    Flag(F("galaxy.always"), v == "always");
    return "";
  }
  if (key == "theme") {
    if (!In(v, kThemes) && !(In(v, kSecretThemes) && SecretUnlocked())) { return "bad value"; }
    Put(F("theme"), v + "\n");
    return "";
  }
  if (key == "holidays") {
    if (!In(v, {"all", "western", "off"})) { return "bad value"; }
    Put(F("holidays"), v + "\n");
    return "";
  }
  if (key == "holidaytime") {
    if (!In(v, {"season", "day"})) { return "bad value"; }
    if (v == "day") { Put(F("holidaytime"), "day\n"); } else { Del(F("holidaytime")); }
    return "";
  }
  if (key == "mood")   { if (!In(v, {"on", "off"})) { return "bad value"; } Flag(F("mood.on"), v == "on"); return ""; }
  if (key == "sleepy") { if (!In(v, {"on", "off"})) { return "bad value"; } Flag(F("sleepy.off"), v == "off"); return ""; }
  if (key == "funeyes") {
    if (!In(v, {"auto", "on", "off"})) { return "bad value"; }
    Flag(F("funeyes.off"), v == "off");
    Flag(F("funeyes.auto"), v == "auto");
    return "";
  }
  if (key == "sleep") {
    if (!In(v, {"stars", "clock", "both", "off"})) { return "bad value"; }
    if (v == "off") { Del(F("sleepscreen")); } else { Put(F("sleepscreen"), v + "\n"); }
    return "";
  }
  if (key == "birthday") {   // "off" or "MM-DD" or "MM-DD NAME" (A-Z, space, - and ' only, up to 16)
    if (v == "off") { Del(F("birthday")); return ""; }
    if (v.size() < 5 || v[2] != '-') { return "bad value"; }
    const int m = (v[0] >= '0' && v[0] <= '9' && v[1] >= '0' && v[1] <= '9') ? (v[0] - '0') * 10 + (v[1] - '0') : 0;
    const int d = (v[3] >= '0' && v[3] <= '9' && v[4] >= '0' && v[4] <= '9') ? (v[3] - '0') * 10 + (v[4] - '0') : 0;
    static const int kDays[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12 || d < 1 || d > kDays[m]) { return "that date doesn't exist"; }
    std::string name;
    if (v.size() > 5) {
      if (v[5] != ' ') { return "bad value"; }
      for (size_t i = 6; i < v.size(); ++i) {
        char c = v[i];
        if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); }
        if (!((c >= 'A' && c <= 'Z') || c == ' ' || c == '-' || c == '\'')) { return "letters only in the name"; }
        name += c;
      }
      while (!name.empty() && name.back() == ' ') { name.pop_back(); }
      if (name.size() > 16) { return "name is too long (16 letters)"; }
    }
    Put(F("birthday"), v.substr(0, 5) + (name.empty() ? "" : " " + name) + "\n");
    return "";
  }
  if (key == "lights") {   // the back lights load when he starts, so a change restarts him (like the menu)
    if (!In(v, {"custom", "normal", "anki"})) { return "bad value"; }
    const std::string now = Lights();
    if (v == now) { return ""; }
    if (v == "anki") {
      Put(kAnkiLights, "1");
    } else {
      if (!Exists(kLightsSent)) { return "his JamesOS lights aren't on this robot yet"; }
      Del(kAnkiLights);
      Flag(F("lights.off"), v == "normal");
    }
    return "restart";
  }
  return "unknown setting";
}

// ---- web ----
bool HostOk(const char* host)
{
  if (host == nullptr || host[0] == 0) { return false; }
  std::string h(host);
  const size_t colon = h.rfind(':');
  if (colon != std::string::npos && h.find(']') == std::string::npos) { h = h.substr(0, colon); }
  bool ip = !h.empty();
  for (const char c : h) { if (!((c >= '0' && c <= '9') || c == '.')) { ip = false; } }
  if (ip || h == "localhost") { return true; }
  std::string low;
  for (const char c : h) { low += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c); }
  return low.rfind("vector-", 0) == 0 || (low.size() > 6 && low.compare(low.size() - 6, 6, ".local") == 0);
}

void Reply(struct mg_connection* conn, int code, const std::string& body)
{
  mg_printf(conn,
            "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
            "Content-Length: %u\r\nConnection: close\r\n\r\n",
            code, code == 200 ? "OK" : (code == 403 ? "Forbidden" : (code == 404 ? "Not Found" : "Bad Request")),
            (unsigned)body.size());
  mg_write(conn, body.data(), body.size());
}

int Handle(struct mg_connection* conn, void* /*cbdata*/)
{
  const struct mg_request_info* info = mg_get_request_info(conn);
  if (info == nullptr || !HostOk(mg_get_header(conn, "Host"))) {
    Reply(conn, 403, "{\"error\":\"not allowed\"}");
    return 1;
  }
  const std::string method = info->request_method ? info->request_method : "";
  const std::string uri = info->local_uri ? info->local_uri : "";
  if (uri == "/jamesos/api/state" && method == "GET") {
    Reply(conn, 200, StateJson());
    return 1;
  }
  if (uri == "/jamesos/api/badges" && method == "GET") {
    Reply(conn, 200, BadgesJson());
    return 1;
  }
  if (uri == "/jamesos/api/whatsnew" && method == "GET") {
    Reply(conn, 200, WhatsNewJson());
    return 1;
  }
  if (uri == "/jamesos/api/set" && method == "POST") {
    const char* mark = mg_get_header(conn, "X-JamesOS");
    const char* origin = mg_get_header(conn, "Origin");
    const char* host = mg_get_header(conn, "Host");
    if (mark == nullptr || std::string(mark) != "1" ||
        (origin != nullptr && std::string(origin) != std::string("http://") + host)) {
      Reply(conn, 403, "{\"error\":\"not allowed\"}");
      return 1;
    }
    char buf[128] = {0};
    int len = 0;
    while (len < (int)sizeof(buf) - 1) {
      const int r = mg_read(conn, buf + len, sizeof(buf) - 1 - (size_t)len);
      if (r <= 0) { break; }
      len += r;
    }
    const std::string body(buf, (size_t)len);
    const size_t eq = body.find('=');
    if (eq == std::string::npos) { Reply(conn, 400, "{\"error\":\"bad request\"}"); return 1; }
    std::string key = body.substr(0, eq), value = body.substr(eq + 1);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) { value.pop_back(); }
    const std::string result = Set(key, value);
    if (!result.empty() && result != "restart") {
      Reply(conn, 400, "{\"error\":\"" + Esc(result) + "\"}");
      return 1;
    }
    LOG_INFO("JamesOS.Companion.Set", "%s=%s", key.c_str(), value.c_str());
    sync();
    const std::string state = StateJson();
    Reply(conn, 200, "{\"restart\":" + std::string(result == "restart" ? "true" : "false") + ",\"state\":" + state + "}");
    if (result == "restart") {
      // answer the phone first, then restart (same as LIGHTS on the robot)
      const int rc = std::system("(sleep 2; sync; systemctl reboot || /sbin/reboot) >/dev/null 2>&1 &");
      (void)rc;
    }
    return 1;
  }
  Reply(conn, 404, "{\"error\":\"not found\"}");
  return 1;
}

}  // namespace JamesCompanion
}  // namespace

static void JamesCompanionStart(WebService::WebService* web)
{
  if (web != nullptr) {
    web->RegisterRequestHandler("/jamesos/api", &JamesCompanion::Handle, nullptr);
    LOG_INFO("JamesOS.Companion", "page at http://<robot IP>:8889/jamesos/");
  }
}
// <<< JamesOS companion page (end)

Result AnimEngine::Init()
{
  if (_isInitialized) {
    LOG_INFO("AnimEngine.Init.ReInit", "Reinitializing already-initialized CozmoEngineImpl with new config.");
  }

  uint32_t seed = 0; // will choose random seed
# ifdef ANKI_PLATFORM_OSX
  {
    seed = 1; // Setting to non-zero value for now for repeatable testing.
  }
# endif
  _context->SetRandomSeed(seed);

  OSState::getInstance()->SetUpdatePeriod(1000);

  RobotDataLoader * dataLoader = _context->GetDataLoader();
  dataLoader->LoadConfigData();
  dataLoader->LoadNonConfigData();

  _ttsComponent = std::make_unique<TextToSpeechComponent>(_context.get());
  _context->GetMicDataSystem()->Init(*dataLoader);

  // animation streamer must be initialized after loading non config data (otherwise there are no animations loaded)
  _animationStreamer->Init(_ttsComponent.get());
  _context->GetBackpackLightComponent()->Init();

  // Create and set up EngineRobotAudioInput to receive Engine->Robot messages and broadcast Robot->Engine
  auto* audioMux = _context->GetAudioMultiplexer();
  auto regId = audioMux->RegisterInput( new Audio::EngineRobotAudioInput() );
  // Easy access to Audio Controller
  _audioControllerPtr = _context->GetAudioController();

  // Set up message handler
  auto * audioInput = static_cast<Audio::EngineRobotAudioInput*>(audioMux->GetInput(regId));
  _streamingAnimationModifier = std::make_unique<StreamingAnimationModifier>(_animationStreamer.get(), audioInput, _ttsComponent.get());

  // set up audio stream state manager
  {
    _context->GetShowAudioStreamStateManager()->SetAnimationStreamer(_animationStreamer.get());
  }


  AnimProcessMessages::Init(this, _animationStreamer.get(), _streamingAnimationModifier.get(), audioInput, _context.get());

  _context->GetWebService()->Start(_context->GetDataPlatform(),
                                   _context->GetDataLoader()->GetWebServerAnimConfig());
  JamesCompanionStart(_context->GetWebService()); // JamesOS companion page
  FaceInfoScreenManager::getInstance()->Init(_context.get(), _animationStreamer.get());

  _context->GetAlexa()->Init(_context.get());

  const auto pm = _context->GetPerfMetric();
  pm->Init(_context->GetDataPlatform(), _context->GetWebService());
  pm->SetAnimationStreamer(_animationStreamer.get());
  if (pm->GetAutoRecord())
  {
    pm->Start();
  }

  // Make sure OpenCV isn't threading
  Result cvResult = SetNumOpencvThreads( NUM_ANIM_OPENCV_THREADS, "AnimEngine.Init" );
  if( RESULT_OK != cvResult )
  {
    return cvResult;
  }

  _sdkAudioComponent = std::make_unique<SdkAudioComponent>(_context.get());

  LOG_INFO("AnimEngine.Init.Success","Success");
  _isInitialized = true;

  return RESULT_OK;
}

Result AnimEngine::Update(const BaseStationTime_t currTime_nanosec)
{
  ANKI_CPU_TICK("AnimEngine::Update", kAnimEngine_TimeMax_ms, Util::CpuProfiler::CpuProfilerLoggingTime(kAnimEngine_TimeLogging));
  if (!_isInitialized) {
    LOG_ERROR("AnimEngine.Update", "Cannot update AnimEngine before it is initialized.");
    return RESULT_FAIL;
  }

  //
  // Declare some invariants. These components are always present after successful initialization.
  //
  DEV_ASSERT(_context, "AnimEngine.Update.InvalidContext");
  DEV_ASSERT(_ttsComponent, "AnimEngine.Update.InvalidTTSComponent");
  DEV_ASSERT(_animationStreamer, "AnimEngine.Update.InvalidAnimationStreamer");
  DEV_ASSERT(_streamingAnimationModifier, "AnimEngine.Update.InvalidStreamingAnimationModifier");
  DEV_ASSERT(_sdkAudioComponent, "AnimEngine.Update.InvalidSdkComponent");

#if ANKI_PROFILE_ANIMCOMMS_SOCKET_BUFFER_STATS
  {
    // Update socket buffer counters
    AnimComms::UpdateSocketBufferStats();
  }
#endif

  BaseStationTime_t tick_ns = currTime_nanosec;
#ifdef STANDALONE_SIM
  {
    static BaseStationTime_t simClockOffset_ns = 0;
    static bool simClockAnchored = false;
    const uint32_t bodyNow_ms = AnimProcessMessages::GetLastRobotStateTimestamp_ms();
    if (bodyNow_ms > 0) {
      const BaseStationTime_t bodyNow_ns = (BaseStationTime_t)bodyNow_ms * 1000000ull;
      if (!simClockAnchored) {
        simClockOffset_ns = currTime_nanosec - bodyNow_ns;
        simClockAnchored = true;
      }
      tick_ns = bodyNow_ns + simClockOffset_ns;
    }
  }
#endif

  BaseStationTimer::getInstance()->UpdateTime(tick_ns);

  _context->GetWebService()->Update();

  Result result = AnimProcessMessages::Update(tick_ns);
  if (RESULT_OK != result) {
    LOG_WARNING("AnimEngine.Update", "Unable to process messages (result %d)", result);
    return result;
  }

  OSState::getInstance()->Update(tick_ns);

  _ttsComponent->Update();

  // Clear out sprites that have passed their cache time
  _context->GetDataLoader()->GetSpriteCache()->Update(tick_ns);

  // Update animations
  _streamingAnimationModifier->ApplyAlterationsBeforeUpdate(_animationStreamer.get());

  _animationStreamer->Update();

  _streamingAnimationModifier->ApplyAlterationsAfterUpdate(_animationStreamer.get());

  // Update audio controller
  if (_audioControllerPtr != nullptr) {
    // Update mic info in Audio Engine
    const auto& micDirectionMsg = _context->GetMicDataSystem()->GetLatestMicDirectionMsg();
    _microphoneAudioClient->ProcessMessage(micDirectionMsg);
    // Tick the Audio Engine at the end of each anim frame
    _audioControllerPtr->Update();
  }

  // Update backpack lights
  _context->GetBackpackLightComponent()->Update();

#if ANKI_PROFILE_ANIMCOMMS_SOCKET_BUFFER_STATS
  {
    // Update socket buffer counters
    AnimComms::UpdateSocketBufferStats();
  }
#endif

  return RESULT_OK;
}

void AnimEngine::RegisterTickPerformance(const float tickDuration_ms,
                                         const float tickFrequency_ms,
                                         const float sleepDurationIntended_ms,
                                         const float sleepDurationActual_ms) const
{
  _context->GetPerfMetric()->Update(tickDuration_ms, tickFrequency_ms,
                                    sleepDurationIntended_ms, sleepDurationActual_ms);
}

void AnimEngine::HandleMessage(const RobotInterface::TextToSpeechPrepare & msg)
{
  DEV_ASSERT(_ttsComponent, "AnimEngine.TextToSpeechPrepare.InvalidTTSComponent");
  _ttsComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::TextToSpeechPlay & msg)
{
  DEV_ASSERT(_ttsComponent, "AnimEngine.TextToSpeechPlay.InvalidTTSComponent");
  _ttsComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::TextToSpeechCancel & msg)
{
  DEV_ASSERT(_ttsComponent, "AnimEngine.TextToSpeechCancel.InvalidTTSComponent");
  _ttsComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::SetLocale & msg)
{
  const std::string locale(msg.locale, msg.locale_length);

  LOG_INFO("AnimEngine.SetLocale", "Set locale to %s", locale.c_str());

  if (_context != nullptr) {
    _context->SetLocale(locale);
  }

  if (_ttsComponent != nullptr) {
    _ttsComponent->SetLocale(locale);
  }
}

void AnimEngine::HandleMessage(const RobotInterface::ExternalAudioChunk & msg)
{
  DEV_ASSERT(_sdkAudioComponent, "AnimEngine.ExternalAudioChunk.InvalidSDKAudioComponent");
  _sdkAudioComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::ExternalAudioComplete & msg)
{
  DEV_ASSERT(_sdkAudioComponent, "AnimEngine.ExternalAudioComplete.InvalidSDKAudioComponent");
  _sdkAudioComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::ExternalAudioCancel & msg)
{
  DEV_ASSERT(_sdkAudioComponent, "AnimEngine.ExternalAudioCancel.InvalidSDKAudioComponent");
  _sdkAudioComponent->HandleMessage(msg);
}

void AnimEngine::HandleMessage(const RobotInterface::ExternalAudioPrepare & msg)
{
  DEV_ASSERT(_sdkAudioComponent, "AnimEngine.ExternalAudioPrepare.InvalidSDKAudioComponent");
  _sdkAudioComponent->HandleMessage(msg);
}


} // namespace Anim
} // namespace Vector
} // namespace Anki
