/**
 * File: sdkComponent
 *
 * Author: Michelle Sintov
 * Created: 05/25/2018
 *
 * Description: Component that serves as a mediator between external SDK requests and any instances of SDK behaviors,
 * such as SDKDefault.
 *
 * The sdkComponent does the following, in this order:
 *     - The sdkComponent will receive a message from the external SDK, requesting that the SDK behavior be activated,
 *       so that the SDK can make the robot do something (which could be a behavior, action or low level motor control)
 *     - The SDK behavior will check the sdkComponent regularly to see if the SDK wants control.
 *       If so, the SDK behavior will say it wants to be activated.
 *     - When the SDK behavior is activated, it will inform sdkComponent that the SDK has control.
 *     - The sdkComponent sends a message to the external SDK to inform that it has control.
 *     - Any other state changes from the SDK behavior (e.g., lost control, etc.) are also communicated
 *       through the sdkComponent.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "coretech/common/engine/robotTimeStamp.h"

#include "engine/ankiEventUtil.h"
#include "engine/components/sdkComponent.h"
#include "engine/components/settingsManager.h"
#include "engine/components/animationComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/faceWorld.h"                    // JamesOS v32.1 face scan
#include "coretech/vision/engine/trackedFace.h"
#include "engine/components/visionScheduleMediator/visionScheduleMediator.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"
#include "util/jamesos/jamesosPin.h" // JamesOS v31: phone page code
#include "engine/externalInterface/externalInterface.h" // JamesOS explorer
#include "clad/externalInterface/messageGameToEngine.h"
#include "webServerProcess/src/webService.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include "engine/robotManager.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/robotEventHandler.h"
#include "engine/externalInterface/cladProtoTypeTranslator.h"
#include "engine/externalInterface/gatewayInterface.h"
#include "engine/externalInterface/externalMessageRouter.h"
#include "proto/external_interface/shared.pb.h"

#include "util/logging/logging.h"

#define LOG_CHANNEL "SdkComponent"

namespace Anki {
namespace Vector {

// >>> JamesOS explorer (begin)
// v31 Explorer: drive him from the phone with his camera on screen.
//   page:  http://<robot IP>:8888/jamesos/explore.html   (served by vic-engine's own web server)
//   GET  /jamesos/drive/frame   -> newest camera picture (JPEG); also tells him the phone is still there
//   GET  /jamesos/drive/state   -> {"on":..,"active":..,"busy":..}
//   POST /jamesos/drive/cmd     "start" | "stop" | "move L R H T"  (wheels mm/s, head -1..1, lift -1..1)
// He takes control the same way the SDK does, at the DEFAULT level: cliff and pick-up reactions stay on.
// Safety: motors stop 0.5 s after the last "move", and explorer ends 5 s after the phone stops asking.
namespace {
namespace JamesExplore {

struct State {
  std::mutex m;
  bool want = false;
  long long lastSeenMs = 0, lastMoveMs = 0;
  float l = 0.f, r = 0.f, head = 0.f, lift = 0.f;
  std::vector<u8> jpeg;
  std::string sayText;              // v32 Say: waiting to be spoken
  bool sayVector = true, sayHush = false;
  bool mirror = false;               // v32.1 Mirror: his face shows his camera
  std::string teachName;             // v32.1 Face scan: meet this person next
  int forgetId = 0;
  long long teachAskMs = 0, facesAskMs = -100000;
  std::string facesJson = "[]", seeJson = "[]";
  int unknownSeen = 0;
};
State gState;
std::atomic<bool> gActive{false};   // engine thread: control taken and the SDK behavior is running
std::atomic<bool> gSaying{false};   // v32 Say: he's talking
std::atomic<bool> gBusy{false};     // an SDK program already has control

long long NowMs()
{
  return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now().time_since_epoch()).count();
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

void Reply(struct mg_connection* conn, int code, const std::string& type, const std::string& body)
{
  mg_printf(conn,
            "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\n"
            "Content-Length: %u\r\nConnection: close\r\n\r\n",
            code, code == 200 ? "OK" : (code == 401 ? "Unauthorized" : (code == 403 ? "Forbidden" : (code == 404 ? "Not Found" : "Bad Request"))),
            type.c_str(), (unsigned)body.size());
  mg_write(conn, body.data(), body.size());
}

std::string StateJson()
{
  bool want, mirror;
  std::string see;
  int unknown;
  {
    std::lock_guard<std::mutex> lk(gState.m);
    want = gState.want;
    mirror = gState.mirror;
    see = gState.seeJson;
    unknown = gState.unknownSeen;
  }
  return std::string("{\"on\":") + (want ? "true" : "false") + ",\"active\":" + (gActive ? "true" : "false") +
         ",\"busy\":" + (gBusy ? "true" : "false") + ",\"saying\":" + (gSaying ? "true" : "false") +
         ",\"mirror\":" + (mirror ? "true" : "false") + ",\"see\":" + see + ",\"unknown\":" + std::to_string(unknown) + "}";
}

int Handle(struct mg_connection* conn, void* /*cbdata*/)
{
  const struct mg_request_info* info = mg_get_request_info(conn);
  if (info == nullptr || !HostOk(mg_get_header(conn, "Host"))) {
    Reply(conn, 403, "application/json", "{\"error\":\"not allowed\"}");
    return 1;
  }
  const std::string method = info->request_method ? info->request_method : "";
  const std::string uri = info->local_uri ? info->local_uri : "";
  const long long now = NowMs();
  if (!jamesos::pin::Unlocked(mg_get_header(conn, "Cookie"))) {   // v31: needs the phone page code
    Reply(conn, 401, "application/json", "{\"error\":\"locked\"}");
    return 1;
  }

  if (uri == "/jamesos/drive/frame" && method == "GET") {
    std::vector<u8> img;
    {
      std::lock_guard<std::mutex> lk(gState.m);
      gState.lastSeenMs = now;
      img = gState.jpeg;
    }
    if (img.empty()) { Reply(conn, 404, "application/json", "{\"error\":\"no picture yet\"}"); return 1; }
    Reply(conn, 200, "image/jpeg", std::string(img.begin(), img.end()));
    return 1;
  }
  if (uri == "/jamesos/drive/state" && method == "GET") {
    {
      std::lock_guard<std::mutex> lk(gState.m);
      gState.lastSeenMs = now;
    }
    Reply(conn, 200, "application/json", StateJson());
    return 1;
  }
  if (uri == "/jamesos/drive/cmd" && method == "POST") {
    const char* mark = mg_get_header(conn, "X-JamesOS");
    const char* origin = mg_get_header(conn, "Origin");
    const char* host = mg_get_header(conn, "Host");
    if (mark == nullptr || std::string(mark) != "1" ||
        (origin != nullptr && std::string(origin) != std::string("http://") + host)) {
      Reply(conn, 403, "application/json", "{\"error\":\"not allowed\"}");
      return 1;
    }
    char buf[96] = {0};
    int len = 0;
    while (len < (int)sizeof(buf) - 1) {
      const int rd = mg_read(conn, buf + len, sizeof(buf) - 1 - (size_t)len);
      if (rd <= 0) { break; }
      len += rd;
    }
    const std::string body(buf, (size_t)len);
    {
      std::lock_guard<std::mutex> lk(gState.m);
      gState.lastSeenMs = now;
      if (body.rfind("start", 0) == 0) {
        gState.want = true;
      } else if (body.rfind("stop", 0) == 0) {
        gState.want = false;
        gState.mirror = false;
        gState.l = gState.r = gState.head = gState.lift = 0.f;
      } else if (body.rfind("mirror ", 0) == 0) {   // v32.1: "mirror 1" | "mirror 0"
        gState.mirror = body.size() > 7 && body[7] == '1';
        if (gState.mirror) { gState.want = true; }
      } else if (body.rfind("move ", 0) == 0) {
        float l = 0.f, r = 0.f, h = 0.f, t = 0.f;
        if (std::sscanf(body.c_str() + 5, "%f %f %f %f", &l, &r, &h, &t) != 4) {
          Reply(conn, 400, "application/json", "{\"error\":\"bad move\"}");
          return 1;
        }
        gState.l = Clamp(l, -160.f, 160.f);
        gState.r = Clamp(r, -160.f, 160.f);
        gState.head = Clamp(h, -1.f, 1.f);
        gState.lift = Clamp(t, -1.f, 1.f);
        gState.lastMoveMs = now;
      } else {
        Reply(conn, 400, "application/json", "{\"error\":\"unknown command\"}");
        return 1;
      }
    }
    Reply(conn, 200, "application/json", StateJson());
    return 1;
  }
  if (uri == "/jamesos/drive/faces" && method == "GET") {   // v32.1: the faces he knows
    std::string list;
    bool teaching;
    {
      std::lock_guard<std::mutex> lk(gState.m);
      gState.facesAskMs = now;
      list = gState.facesJson;
      teaching = !gState.teachName.empty();
    }
    Reply(conn, 200, "application/json", "{\"faces\":" + list + ",\"teaching\":" + (teaching ? "true" : "false") + "}");
    return 1;
  }
  if (uri == "/jamesos/drive/face" && method == "POST") {   // v32.1: "teach NAME" | "forget ID"
    const char* mark = mg_get_header(conn, "X-JamesOS");
    const char* origin = mg_get_header(conn, "Origin");
    const char* host = mg_get_header(conn, "Host");
    if (mark == nullptr || std::string(mark) != "1" ||
        (origin != nullptr && std::string(origin) != std::string("http://") + host)) {
      Reply(conn, 403, "application/json", "{\"error\":\"not allowed\"}");
      return 1;
    }
    std::string body;
    char fb[128];
    while (body.size() < 200) {
      const int rd = mg_read(conn, fb, sizeof(fb));
      if (rd <= 0) { break; }
      body.append(fb, (size_t)rd);
    }
    std::lock_guard<std::mutex> lk(gState.m);
    if (body.rfind("teach ", 0) == 0) {
      std::string n;   // letters (any language), numbers, spaces, - and '
      for (const char ch : body.substr(6)) {
        const unsigned char c = (unsigned char)ch;
        if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '\'') { n += ch; }
        else if (c == ' ' && !n.empty() && n.back() != ' ') { n += ' '; }
      }
      while (!n.empty() && n.back() == ' ') { n.pop_back(); }
      if (n.empty() || n.size() > 40) { Reply(conn, 400, "application/json", "{\"error\":\"Use a name with letters, up to 20 letters.\"}"); return 1; }
      if (!gState.teachName.empty()) { Reply(conn, 400, "application/json", "{\"error\":\"He's already getting ready to meet someone.\"}"); return 1; }
      gState.teachName = n;
      gState.teachAskMs = now;
    } else if (body.rfind("forget ", 0) == 0) {
      int id = 0;
      if (std::sscanf(body.c_str() + 7, "%d", &id) != 1 || id <= 0) { Reply(conn, 400, "application/json", "{\"error\":\"bad face\"}"); return 1; }
      gState.forgetId = id;
    } else {
      Reply(conn, 400, "application/json", "{\"error\":\"bad request\"}");
      return 1;
    }
    Reply(conn, 200, "application/json", "{\"ok\":true}");
    return 1;
  }
  if (uri == "/jamesos/drive/say" && method == "POST") {   // v32: "say V <text>" | "hush"   (V: 1 Vector voice, 0 clear)
    const char* mark = mg_get_header(conn, "X-JamesOS");
    const char* origin = mg_get_header(conn, "Origin");
    const char* host = mg_get_header(conn, "Host");
    if (mark == nullptr || std::string(mark) != "1" ||
        (origin != nullptr && std::string(origin) != std::string("http://") + host)) {
      Reply(conn, 403, "application/json", "{\"error\":\"not allowed\"}");
      return 1;
    }
    std::string body;
    char sb[256];
    while (body.size() < 700) {
      const int rd = mg_read(conn, sb, sizeof(sb));
      if (rd <= 0) { break; }
      body.append(sb, (size_t)rd);
    }
    std::lock_guard<std::mutex> lk(gState.m);
    if (body == "hush") {
      gState.sayHush = true;
    } else if (body.rfind("say ", 0) == 0 && body.size() > 6) {
      if (gSaying || !gState.sayText.empty()) { Reply(conn, 400, "application/json", "{\"error\":\"He's still talking.\"}"); return 1; }
      std::string t;   // control characters become spaces; UTF-8 letters stay
      for (const char ch : body.substr(6)) { t += ((unsigned char)ch < 32 || ch == 127) ? ' ' : ch; }
      while (!t.empty() && t.back() == ' ') { t.pop_back(); }
      size_t a = 0;
      while (a < t.size() && t[a] == ' ') { ++a; }
      t = t.substr(a);
      if (t.empty()) { Reply(conn, 400, "application/json", "{\"error\":\"Type something first.\"}"); return 1; }
      if (t.size() > 300) {   // cut at a whole UTF-8 letter
        size_t cut = 300;
        while (cut > 0 && ((unsigned char)t[cut] & 0xC0) == 0x80) { --cut; }
        t = t.substr(0, cut);
      }
      gState.sayText = t;
      gState.sayVector = body[4] != '0';
    } else {
      Reply(conn, 400, "application/json", "{\"error\":\"bad request\"}");
      return 1;
    }
    Reply(conn, 200, "application/json", "{\"ok\":true}");
    return 1;
  }
  Reply(conn, 404, "application/json", "{\"error\":\"not found\"}");
  return 1;
}

}  // namespace JamesExplore
}  // namespace

// called by VisionComponent with every camera picture (only kept while explorer is on)
void JamesExploreStoreFrame(const std::vector<u8>& jpeg)
{
  std::lock_guard<std::mutex> lk(JamesExplore::gState.m);
  if (JamesExplore::gState.want && !jpeg.empty()) { JamesExplore::gState.jpeg = jpeg; }
}

void SDKComponent::JamesExploreUpdate()
{
  using namespace JamesExplore;
  const long long now = NowMs();
  bool want;
  float l, r, h, t;
  long long lastMove;
  {
    std::lock_guard<std::mutex> lk(gState.m);
    if (gState.want && now - gState.lastSeenMs > 5000) { gState.want = false; }   // the phone went away
    want = gState.want;
    l = gState.l; r = gState.r; h = gState.head; t = gState.lift;
    lastMove = gState.lastMoveMs;
    if (!want) { gState.jpeg.clear(); }
  }
  auto* ei = _robot->GetContext()->GetExternalInterface();

  // v32.1 Mirror + Face scan (engine thread)
  {
    static bool mirrorOn = false, facesOn = false;
    static long long lastSeeMs = 0, lastListMs = -100000;
    bool mirrorWant = false, teachNow = false;
    std::string teach;
    int forget = 0;
    long long facesAsk = 0;
    {
      std::lock_guard<std::mutex> lk(gState.m);
      if (!gState.want) { gState.mirror = false; }   // Mirror ends with Explore
      mirrorWant = gState.mirror;
      forget = gState.forgetId;
      gState.forgetId = 0;
      facesAsk = gState.facesAskMs;
      if (!gState.teachName.empty()) {
        if (_jamesExploreOn) {
          gState.want = false;    // he can't be driven and meet someone at the same time
          gState.mirror = false;
        } else if (!_sdkBehaviorActivated) {
          teach.swap(gState.teachName);
          teachNow = true;
        } else if (now - gState.teachAskMs > 15000) {
          gState.teachName.clear();
        }
      }
    }
    const bool wantFaces = _jamesExploreOn;   // look for faces while you drive ("I see James")
    if (wantFaces != facesOn) {
      SubscribeToVisionMode(wantFaces, VisionMode::Faces, false);
      facesOn = wantFaces;
    }
    const bool wantMirror = _jamesExploreOn && _sdkBehaviorActivated && mirrorWant;
    if (wantMirror != mirrorOn) {
      SubscribeToVisionMode(wantMirror, VisionMode::MirrorMode, false);
      mirrorOn = wantMirror;
      LOG_INFO("SDKComponent.JamesMirror", "Mirror %s", wantMirror ? "on" : "off");
    }
    if (forget > 0) {
      const Result res = _robot->GetVisionComponent().EraseFace((Vision::FaceID_t)forget);
      LOG_INFO("SDKComponent.JamesFaces", "forget face %d from the phone: %s", forget, res == RESULT_OK ? "done" : "failed");
      lastListMs = -100000;
    }
    if (teachNow && ei != nullptr) {
      ExternalInterface::AppIntent intent;
      intent.intent = "intent_meet_victor";   // the same "meet" the Vector app starts
      intent.param = teach;
      ei->Broadcast(ExternalInterface::MessageGameToEngine(std::move(intent)));
      LOG_INFO("SDKComponent.JamesFaces", "meet a new face from the phone (%zu letters)", teach.size());
    }
    if (now - facesAsk < 15000 && now - lastListMs > 2000) {
      lastListMs = now;
      auto esc = [](const std::string& in) {
        std::string o;
        for (const char ch : in) {
          if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
          else if ((unsigned char)ch < 32) { o += ' '; }
          else { o += ch; }
        }
        return o;
      };
      std::string js = "[";
      for (const auto& f : _robot->GetVisionComponent().JamesEnrolledNames()) {
        if (js.size() > 1) { js += ","; }
        js += "{\"id\":" + std::to_string(f.faceID) + ",\"name\":\"" + esc(f.name) + "\",\"seen\":" +
              std::to_string((long long)f.secondsSinceLastSeen) + "}";
      }
      js += "]";
      std::lock_guard<std::mutex> lk(gState.m);
      gState.facesJson = js;
    }
    if (_jamesExploreOn && now - lastSeeMs > 300) {
      lastSeeMs = now;
      const RobotTimeStamp_t imgT = _robot->GetLastImageTimeStamp();
      const auto ids = _robot->GetFaceWorld().GetFaceIDs(imgT > 1500 ? imgT - 1500 : 0);
      std::vector<std::string> names;
      int unknown = 0;
      for (const auto id : ids) {
        const Vision::TrackedFace* f = _robot->GetFaceWorld().GetFace(id);
        if (f == nullptr) { continue; }
        if (f->HasName()) { names.push_back(f->GetName()); } else { ++unknown; }
      }
      std::string js = "[";
      for (const auto& n : names) {
        if (js.size() > 1) { js += ","; }
        js += "\"";
        for (const char ch : n) { if (ch != '"' && ch != '\\' && (unsigned char)ch >= 32) { js += ch; } }
        js += "\"";
      }
      js += "]";
      std::lock_guard<std::mutex> lk(gState.m);
      gState.seeJson = js;
      gState.unknownSeen = unknown;
    } else if (!_jamesExploreOn) {
      std::lock_guard<std::mutex> lk(gState.m);
      gState.seeJson = "[]";
      gState.unknownSeen = 0;
    }
  }

  // v32 Say: the phone's text is spoken here, on the engine thread (same way as the SDK's say_text)
  {
    std::string sayText;
    bool sayVector = true, hush = false;
    {
      std::lock_guard<std::mutex> lk(gState.m);
      sayText.swap(gState.sayText);
      sayVector = gState.sayVector;
      hush = gState.sayHush;
      gState.sayHush = false;
    }
    auto& tts = _robot->GetTextToSpeechCoordinator();
    static uint8_t sayId = kInvalidUtteranceID;
    static bool hushWaiting = false;
    static long long sayStartMs = 0;
    if (hush && gSaying) { hushWaiting = true; }
    if (sayId != kInvalidUtteranceID) {
      const UtteranceState st = tts.GetUtteranceState(sayId);
      if (hushWaiting && (st == UtteranceState::Ready || st == UtteranceState::Playing)) {
        tts.CancelUtterance(sayId);   // (not while Generating: that would make the engine wait)
        LOG_INFO("SDKComponent.JamesSay", "stopped from the phone");
      }
      if (st == UtteranceState::Invalid || st == UtteranceState::Finished ||
          (hushWaiting && st != UtteranceState::Generating) || now - sayStartMs > 60000) {
        sayId = kInvalidUtteranceID;
        hushWaiting = false;
        gSaying = false;
      }
    }
    if (!sayText.empty() && sayId == kInvalidUtteranceID) {
      using Style = AudioMetaData::SwitchState::Robot_Vic_External_Processing;
      sayId = tts.CreateUtterance(sayText, UtteranceTriggerType::Immediate,
                                  sayVector ? Style::Default_Processed : Style::Unprocessed);
      gSaying = sayId != kInvalidUtteranceID;
      sayStartMs = now;
      LOG_INFO("SDKComponent.JamesSay", "saying %zu letters from the phone%s", sayText.size(), gSaying ? "" : " (failed)");
    }
  }

  if (want && !_jamesExploreOn) {
    if (_sdkWantsControl || _sdkWantsLock) {   // an SDK program is already driving him: don't fight it
      gBusy = true;
      std::lock_guard<std::mutex> lk(gState.m);
      gState.want = false;
      return;
    }
    gBusy = false;
    _jamesExploreOn = true;
    _sdkWantsControl = true;
    _sdkControlLevel = external_interface::ControlRequest_Priority_DEFAULT;
    _sdkControlConnId = 0;
    SubscribeToVisionMode(true, VisionMode::Viz, false);
    LOG_INFO("SDKComponent.JamesExplore", "Explorer on");
  } else if (!want && _jamesExploreOn) {
    if (_sdkBehaviorActivated && ei != nullptr) {
      ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::DriveWheels(0.f, 0.f, 0.f, 0.f)));
      ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::MoveHead(0.f)));
      ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::MoveLift(0.f)));
    }
    _jamesExploreOn = false;
    _jamesSent = false;
    _sdkWantsControl = false;
    SubscribeToVisionMode(false, VisionMode::Viz, false);
    LOG_INFO("SDKComponent.JamesExplore", "Explorer off");
  }
  gActive = _jamesExploreOn && _sdkBehaviorActivated;
  if (!gActive || ei == nullptr) { return; }

  // no "move" for half a second = stop (phone froze, finger lifted, Wi-Fi dropped)
  if (now - lastMove > 500) { l = r = h = t = 0.f; }
  const bool changed = l != _jamesL || r != _jamesR || h != _jamesH || t != _jamesT;
  const bool moving = l != 0.f || r != 0.f || h != 0.f || t != 0.f;
  if (!changed && !(moving && now - _jamesLastSendMs > 300) && _jamesSent) { return; }
  ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::DriveWheels(l, r, 0.f, 0.f)));
  ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::MoveHead(h * 1.5f)));    // rad/s
  ei->Broadcast(ExternalInterface::MessageGameToEngine(ExternalInterface::MoveLift(t * 2.0f)));    // rad/s
  _jamesL = l; _jamesR = r; _jamesH = h; _jamesT = t;
  _jamesLastSendMs = now;
  _jamesSent = true;
}

void SDKComponent::JamesExploreRegister()
{
  auto* ws = _robot->GetContext()->GetWebService();
  if (ws != nullptr) { ws->RegisterRequestHandler("/jamesos/drive/", &JamesExplore::Handle, nullptr); }
}
// <<< JamesOS explorer (end)

SDKComponent::SDKComponent()
: IDependencyManagedComponent<RobotComponentID>(this, RobotComponentID::SDK)
{
  _sdkControlLevel = external_interface::ControlRequest_Priority_UNKNOWN;
}


SDKComponent::~SDKComponent()
{
}


void SDKComponent::GetInitDependencies( RobotCompIDSet& dependencies ) const
{
  // TODO
}


void SDKComponent::InitDependent(Vector::Robot* robot, const RobotCompMap& dependentComps)
{
  // TODO It's preferred, where possible, to use dependentComps rather than caching robot
  // directly (makes it much easier to write unit tests)
  _robot = robot;
  JamesExploreRegister(); // JamesOS explorer
  auto* gi = robot->GetGatewayInterface();
  if (gi != nullptr)
  {
    auto callback = std::bind(&SDKComponent::HandleProtoMessage, this, std::placeholders::_1);

    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kControlRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kControlRelease, callback));

    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kGoToPoseRequest,      callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kDockWithCubeRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kDriveStraightRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kTurnInPlaceRequest,   callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kSetLiftHeightRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kSetHeadAngleRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kTurnTowardsFaceRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kGoToObjectRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kRollObjectRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kPopAWheelieRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kPickupObjectRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kPlaceObjectOnGroundHereRequest,  callback));

    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kAudioSendModeRequest, callback));

    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kEnableMarkerDetectionRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kEnableFaceDetectionRequest,   callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kEnableMotionDetectionRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kEnableMirrorModeRequest,      callback));
    // _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kCaptureSingleImageRequest,    callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kEnableImageStreamingRequest,  callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kIsImageStreamingEnabledRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kSayTextRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kSetEyeColorRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kListAnimationTriggersRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kExternalAudioStreamPrepare, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kExternalAudioStreamChunk, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kExternalAudioStreamComplete, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kExternalAudioStreamCancel, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kMasterVolumeRequest, callback));
    _signalHandles.push_back(gi->Subscribe(external_interface::GatewayWrapperTag::kSetCameraSettingsRequest, callback));
  }

  auto* context = _robot->GetContext();
  if(context != nullptr && context->GetExternalInterface() != nullptr)
  {
    using namespace ExternalInterface;
    auto helper = MakeAnkiEventUtil(*context->GetExternalInterface(), *this, _signalHandles);

    helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotProcessedImage>();
    helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotObservedMotion>();
    helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotErasedEnrolledFace>();
    helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotRenamedEnrolledFace>();
    helper.SubscribeEngineToGame<MessageEngineToGameTag::UnexpectedMovement>();
  }

  // Disable/Unsubscribe from MirrorMode when we enter pairing.
  // This is to prevent SDK requested MirrorMode from drawing over the pairing screens
  auto setConnectionStatusCallback = [this](const GameToEngineEvent& event) {
    const auto& msg = event.GetData().Get_SetConnectionStatus();
    if (msg.status != SwitchboardInterface::ConnectionStatus::NONE &&
        msg.status != SwitchboardInterface::ConnectionStatus::COUNT &&
        msg.status != SwitchboardInterface::ConnectionStatus::END_PAIRING)
    {
      DisableMirrorMode();
    }
  };

  _signalHandles.push_back(_robot->GetExternalInterface()->Subscribe(GameToEngineTag::SetConnectionStatus,
                                                                     setConnectionStatusCallback));

  //subscribe to messages back from audio playback
  auto* messageHandler = robot->GetRobotMessageHandler();
  _signalHandles.push_back(messageHandler->Subscribe(RobotInterface::RobotToEngineTag::audioStreamStatusEvent, [this](const AnkiEvent<RobotInterface::RobotToEngine>& event) {
      auto streamEvent = event.GetData().Get_audioStreamStatusEvent();
      HandleStreamStatusEvent(streamEvent.streamResultID, streamEvent.audioReceived,
                  streamEvent.audioPlayed);
  }));
}

template<>
void SDKComponent::HandleMessage(const ExternalInterface::RobotObservedMotion& msg)
{
  auto* gi = _robot->GetGatewayInterface();
  auto* observedMsg = new external_interface::RobotObservedMotion();
  observedMsg->set_timestamp(msg.timestamp);
  observedMsg->set_img_area(msg.img_area);
  observedMsg->set_img_x(msg.img_x);
  observedMsg->set_img_y(msg.img_y);
  observedMsg->set_ground_area(msg.ground_area);
  observedMsg->set_ground_x(msg.ground_x);
  observedMsg->set_ground_y(msg.ground_y);
  observedMsg->set_top_img_area(msg.top_img_area);
  observedMsg->set_top_img_x(msg.top_img_x);
  observedMsg->set_top_img_y(msg.top_img_y);
  observedMsg->set_bottom_img_area(msg.bottom_img_area);
  observedMsg->set_bottom_img_x(msg.bottom_img_x);
  observedMsg->set_bottom_img_y(msg.bottom_img_y);
  observedMsg->set_left_img_area(msg.left_img_area);
  observedMsg->set_left_img_x(msg.left_img_x);
  observedMsg->set_left_img_y(msg.left_img_y);
  observedMsg->set_right_img_area(msg.right_img_area);
  observedMsg->set_right_img_x(msg.right_img_x);
  observedMsg->set_right_img_y(msg.right_img_y);

  gi->Broadcast(ExternalMessageRouter::Wrap(observedMsg));
}

template<>
void SDKComponent::HandleMessage(const ExternalInterface::UnexpectedMovement& msg)
{
  auto* gi = _robot->GetGatewayInterface();
  auto* unexpMsg = new external_interface::UnexpectedMovement(msg.timestamp,
                                                              CladProtoTypeTranslator::ToProtoEnum(msg.movementType),
                                                              CladProtoTypeTranslator::ToProtoEnum(msg.movementSide));
  ANKI_VERIFY(gi != nullptr, "SDKComponent.HandleMessage.NullGatewayInterface", "");
  gi->Broadcast(ExternalMessageRouter::Wrap(unexpMsg));
}

template<>
void SDKComponent::HandleMessage(const ExternalInterface::RobotErasedEnrolledFace& msg)
{
  auto* gi = _robot->GetGatewayInterface();
  auto* erasedMsg = new external_interface::RobotErasedEnrolledFace(msg.faceID, msg.name);
  gi->Broadcast(ExternalMessageRouter::Wrap(erasedMsg));
}

template<>
void SDKComponent::HandleMessage(const Vision::RobotRenamedEnrolledFace& msg)
{
  auto* gi = _robot->GetGatewayInterface();
  auto* renamedMsg = new external_interface::RobotRenamedEnrolledFace(msg.faceID, msg.name);
  gi->Broadcast(ExternalMessageRouter::Wrap(renamedMsg));
}

void SDKComponent::HandleStreamStatusEvent(SDKAudioStreamingState streamStatusId, int audioFramesReceived, int audioFramesPlayed) {
  auto* gi = _robot->GetGatewayInterface();
  LOG_INFO("SDKComponent::HandleStreamStatusEvent","Received audio playback stream state %u received %u, played %u",
            (int)streamStatusId, audioFramesReceived, audioFramesPlayed);
  switch (streamStatusId) {
    case SDKAudioStreamingState::Completed:
    {
      //we're done, send message back to Gateway
      auto* msg = new external_interface::ExternalAudioStreamResponse(new external_interface::ExternalAudioStreamPlaybackComplete());
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
    }
    break;

    case SDKAudioStreamingState::BufferOverflow:
    {
      //failed due to potential buffer overrun situation (sending audio too fast) send message back to Gateway
      auto* msg = new external_interface::ExternalAudioStreamResponse(new external_interface::ExternalAudioStreamBufferOverrun());
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
    }
    break;

    case SDKAudioStreamingState::PrepareFailed:
    case SDKAudioStreamingState::PostFailed:
    case SDKAudioStreamingState::AddAudioFailed:
    {
      //failed due to potential buffer overrun situation (sending audio too fast) send message back to Gateway
      auto* msg = new external_interface::ExternalAudioStreamResponse(new external_interface::ExternalAudioStreamPlaybackFailure());
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
    }
    break;

    default:
      break;
  }
}

// @TODO: JMRivas - Delete this static and replace with a better way to store whether the audio processing mode on the
// robot/animProcess is considered to be under SDK control.
static bool _SDK_WANTS_AUDIO = false;
void SDKComponent::UpdateDependent(const RobotCompMap& dependentComps)
{
  JamesExploreUpdate(); // JamesOS explorer
  // @TODO: JMRivas - Remove all of this code and replace with a subscription to the true audio feed.
  //        Should be similarly formatted into chunks of SAMPLE_COUNTS_PER_SDK_MESSAGE samples, with
  //        audio metadata redundantly submitted with each packet.
  //        (Note: only the final message's metadata will be used)

  static RobotTimeStamp_t _SDK_AUDIO_BROADCAST_TIME = 0;
  static u32 _SDK_SAMPLE_GROUP_ID = 0;

  RobotTimeStamp_t now = _robot->GetLastMsgTimestamp();

  // The robot will send audio data in chunks of 1600 samples, and the animProcess operates
  // at a sampling rate of 16000.  The rate at which the robot generates these chunks (and the
  // minimum latency of the audiostream) is 100ms on the golden path.
  if( _SDK_WANTS_AUDIO && now > _SDK_AUDIO_BROADCAST_TIME + 100 )
  {
    auto* gi = _robot->GetGatewayInterface();
    if (gi == nullptr) return;

    _SDK_AUDIO_BROADCAST_TIME = now;
    // temporary 1 kHz sine wave generator to test over the wire transfer
    std::vector<u8> power_array(external_interface::SAMPLE_COUNTS_PER_SDK_MESSAGE*2);
    std::vector<u8> direction_array(external_interface::MIC_DETECTION_DIRECTIONS, 0);
    for( u32 i = 0; i < external_interface::SAMPLE_COUNTS_PER_SDK_MESSAGE; i++ ) {
      // sin(pi*i/8) will cycle every 16 samples, the robot's sample rate is 16,000
      // arbitrarily using amplitude of '1000' rather than max_int16 because its less painful to listen to.
      //
      // the robot's audio data is in the form of 16 bit signed integers, but the proto
      // stores it as a byte array
      f32 theta = (f32)i * (3.1415f/8.0f);
      s16 powerLevel = (s16)(sin(theta) * 1000.0);
      power_array[i*2+0] = reinterpret_cast<const u8* const>(&powerLevel)[0];
      power_array[i*2+1] = reinterpret_cast<const u8* const>(&powerLevel)[1];
    }

    std::string direction_data;
    direction_data.assign(direction_array.begin(), direction_array.begin() + external_interface::MIC_DETECTION_DIRECTIONS);

    // Break the audio group into chunks small enough to pass through to gateway.
    u32 chunks = external_interface::SAMPLE_COUNTS_PER_SDK_MESSAGE / external_interface::SAMPLE_COUNTS_PER_ENGINE_MESSAGE;
    for( u32 i=0; i<chunks; ++i ) {
      std::string power_data;
      u32 chunkSize = external_interface::SAMPLE_COUNTS_PER_ENGINE_MESSAGE*2;
      power_data.assign(power_array.begin() + (chunkSize * i), power_array.begin() + (chunkSize*(i+1)));

      // Send audio chunk to gateway
      external_interface::AudioChunk* audioChunk = new external_interface::AudioChunk();
      audioChunk->set_signal_power(std::move(power_data));
      audioChunk->set_direction_strengths(std::move(direction_data));
      audioChunk->set_group_id(_SDK_SAMPLE_GROUP_ID);
      audioChunk->set_chunk_id(i);
      audioChunk->set_audio_chunk_count(chunks);
      audioChunk->set_robot_time_stamp((::google::protobuf::uint32)now);
      audioChunk->set_source_direction(12);
      audioChunk->set_source_confidence(0);
      audioChunk->set_noise_floor_power(0);

      external_interface::GatewayWrapper wrapper;

      wrapper.set_allocated_audio_chunk(audioChunk);
      gi->Broadcast(wrapper);
      audioChunk = wrapper.release_audio_chunk();
      Util::SafeDelete(audioChunk);
    }
    ++_SDK_SAMPLE_GROUP_ID;
  }
}

// Determine whether or not image streaming is enabled on the robot
void SDKComponent::IsImageStreamingEnabledRequest(
  const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  auto* gi = _robot->GetGatewayInterface();
  if (gi == nullptr) {
    return;
  }

  const bool is_enabled = _robot->GetVisionComponent().IsSendingSDKImageChunks();
  external_interface::IsImageStreamingEnabledResponse* isEnabledResponse =
    new external_interface::IsImageStreamingEnabledResponse();
  isEnabledResponse->set_is_image_streaming_enabled(is_enabled);

  gi->Broadcast(ExternalMessageRouter::WrapResponse(isEnabledResponse));
}

void SDKComponent::OnSendAudioModeRequest(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  // @TODO: JMRivas - Replace this functionality with a call or message to whichever process controls
  //        the robot's audio processing mode, and lock it into an "SDK OWNED" state until receiving
  //        the "AUDIO_OFF" id.
  //
  //        Any internal change in the audioProcessing state should be broadcast in an AudioSendModeChanged
  //        message, whether a direct response to a request or incidental from underlying behavior.

  auto* gi = _robot->GetGatewayInterface();
  if (gi == nullptr) return;

  external_interface::AudioProcessingMode mode = event.GetData().audio_send_mode_request().mode();

  _SDK_WANTS_AUDIO = (mode != external_interface::AUDIO_OFF);

  // Signal audio mode changed to gateway
  external_interface::AudioSendModeChanged* changedEvent = new external_interface::AudioSendModeChanged();
  changedEvent->set_mode(mode);

  gi->Broadcast(ExternalMessageRouter::WrapResponse(changedEvent));
}

//keep ID for multiple exclusive user attempts
void SDKComponent::SetBehaviorLock(uint64_t controlId)
{
  if (_sdkWantsLock && (_sdkLockConnId != controlId)) {
    //grabbing control from another connection
    DispatchBehaviorLockLostResult();
    LOG_INFO("SDKComponent.SetBehaviorLock","Connection_id %llu control reservation LOST", _sdkLockConnId);
  }

  LOG_INFO("SDKComponent.SetBehaviorLock","Connection_id %llu reserving control", controlId);
  _sdkLockConnId = controlId;
  _sdkWantsLock = true;
  DispatchSDKActivationResult(true, controlId);
}

void SDKComponent::HandleProtoMessage(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  using namespace external_interface;
  auto* gi = _robot->GetGatewayInterface();
  const auto id = event.GetData().connection_id();

  switch(event.GetData().GetTag()) {
    // Receives a message that external SDK wants an SDK behavior to be activated.
    case external_interface::GatewayWrapperTag::kControlRequest:
      {
        auto & control_req = event.GetData().control_request();
        _sdkControlLevel = control_req.priority();
        LOG_INFO("SDKComponent.HandleProtoMessage", "SDK requested control connection_id %llu", id);
        if (!ANKI_VERIFY(_sdkControlLevel, "SDKComponent::HandleProtoMessage", "Invalid _sdkControlLevel 0 (UNKNOWN)")) {
          return;
        }

        if (event.GetData().control_request().priority() == event.GetData().control_request().RESERVE_CONTROL) {
          //user wants long-running control
          SetBehaviorLock(id);
          return;
        }

        _sdkWantsControl = true;
        _sdkControlConnId =  id;

        LOG_INFO("SDKComponent.HandleProtoMessage","SDK requested control priority %s (%u)",
                  control_req.Priority_Name(control_req.priority()).c_str(), _sdkControlLevel);

        if (_sdkBehaviorActivated) {
          LOG_INFO("SDKComponent.HandleMessageBehaviorActivated", "SDK already has control");
          // SDK wants control and and the SDK Behavior is already running. Send response that SDK behavior is activated.
          DispatchSDKActivationResult(_sdkBehaviorActivated, _sdkControlConnId);
          return;
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kControlRelease:
      LOG_INFO("SDKComponent.HandleProtoMessage", "Releasing SDK control connection_id %llu", id);
      if (id == _sdkLockConnId) {
        DispatchSDKActivationResult(false, _sdkLockConnId);
        LOG_INFO("SDKComponent.HandleProtoMessage", "ControlRelease Releasing control");
        _sdkWantsLock = false;
        _sdkLockConnId = 0;
      }
      _sdkWantsControl = false;
      _sdkControlConnId = 0;
      break;

    case external_interface::GatewayWrapperTag::kAudioSendModeRequest:
      LOG_INFO("SDKComponent.HandleMessageRelease", "Changing audio mode");
      OnSendAudioModeRequest(event);
      break;

    // All of the vision mode requests are gated behind the sdk behavior being activated
    // to prevent enabling of unnecessary modes that may have performance impact.
    // The modes are automatically removed when the behavior is deactivated.
    // Except for Viz since the SDK still wants to receive images while the robot is
    // doing his normal things
    #define SEND_FORBIDDEN(RESPONSE_TYPE) {                                     \
        ResponseStatus* status = new ResponseStatus(ResponseStatus::FORBIDDEN); \
        auto* msg = new RESPONSE_TYPE();                                        \
        msg->set_allocated_status(status);                                      \
        gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));                \
      }

    case external_interface::GatewayWrapperTag::kEnableMarkerDetectionRequest:
      {
        if(_sdkBehaviorActivated)
        {
          const auto& enable = event.GetData().enable_marker_detection_request().enable();
          SubscribeToVisionMode(enable, VisionMode::Markers);
          SubscribeToVisionMode(enable, VisionMode::Markers_FullFrame);
        }
        else
        {
          SEND_FORBIDDEN(EnableMarkerDetectionResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kEnableFaceDetectionRequest:
      {
        if(_sdkBehaviorActivated)
        {
          const auto& msg = event.GetData().enable_face_detection_request();
          SubscribeToVisionMode(msg.enable(), VisionMode::Faces);
          SubscribeToVisionMode(msg.enable_smile_detection(), VisionMode::Faces_Smile);
          SubscribeToVisionMode(msg.enable_expression_estimation(), VisionMode::Faces_Expression);
          SubscribeToVisionMode(msg.enable_blink_detection(), VisionMode::Faces_Blink);
          SubscribeToVisionMode(msg.enable_gaze_detection(), VisionMode::Faces_Gaze);
        }
        else
        {
          SEND_FORBIDDEN(EnableFaceDetectionResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kEnableMotionDetectionRequest:
      {
        if(_sdkBehaviorActivated)
        {
          const auto& enable = event.GetData().enable_motion_detection_request().enable();
          SubscribeToVisionMode(enable, VisionMode::Motion);
        }
        else
        {
          SEND_FORBIDDEN(EnableMotionDetectionResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kEnableMirrorModeRequest:
      {
        if(_sdkBehaviorActivated)
        {
          const auto& enable = event.GetData().enable_mirror_mode_request().enable();
          SubscribeToVisionMode(enable, VisionMode::MirrorMode);
        }
        else
        {
          SEND_FORBIDDEN(EnableMirrorModeResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kEnableImageStreamingRequest:
      {
        // Allowed to be controlled even when the behavior is not active
        const auto& enableImageStreaming = event.GetData().enable_image_streaming_request().enable();
        const auto& enableHighResolutionImages = event.GetData().enable_image_streaming_request().enable_high_resolution();
        SubscribeToVisionMode(enableImageStreaming, VisionMode::Viz);
        _robot->GetVisionComponent().EnableSendingSDKImageChunks(enableImageStreaming, enableHighResolutionImages);
      }
      break;

    case external_interface::GatewayWrapperTag::kIsImageStreamingEnabledRequest:
      {
        // Determine if the image streaming is enabled on the robot
        IsImageStreamingEnabledRequest(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kSayTextRequest:
      {
        if (_sdkBehaviorActivated)
        {
          SayText(event);
        }
        else
        {
          SEND_FORBIDDEN(SayTextResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kSetCameraSettingsRequest:
      {
        if (_sdkBehaviorActivated)
        {
          SetCameraSettings(event);
        }
        else
        {
          SEND_FORBIDDEN(SetCameraSettingsResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kSetEyeColorRequest:
      {
        if (_sdkBehaviorActivated)
        {
          SetEyeColor(event);
        }
        else
        {
          SEND_FORBIDDEN(SetEyeColorResponse);
        }
      }
      break;

    case external_interface::GatewayWrapperTag::kListAnimationTriggersRequest:
      {
        ListAnimationTriggers(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kExternalAudioStreamPrepare:
      {
        HandleAudioStreamPrepareRequest(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kExternalAudioStreamChunk:
      {
        HandleAudioStreamChunkRequest(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kExternalAudioStreamComplete:
      {
        HandleAudioStreamCompleteRequest(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kExternalAudioStreamCancel:
      {
        HandleAudioStreamCancelRequest(event);
      }
      break;

    case external_interface::GatewayWrapperTag::kMasterVolumeRequest:
      {
        SetMasterVolume(event);
      }
      break;

    default:
      _robot->GetRobotEventHandler().HandleMessage(event);
      break;
  }

  #undef SEND_FORBIDDEN
}

template<>
void SDKComponent::HandleMessage(const ExternalInterface::RobotProcessedImage& msg)
{
  // For all of the vision modes we are waiting to change, check if they have appeared or disappeared
  // from the RobotProcessedImage result. If they did what we were expecting then send a Response message
  // for that mode
  // Note: There is one frame of lag between when a VisionMode Request is made, when the detection is sent, and when
  // the Response is sent. The RobotProcessedImage message comes from VisionComponent after any messages
  // for things actually detected in that frame. So if an SDK program is waiting for a Response message,
  // they could miss the detections from the first frame.
  auto waitingIter = _visionModesWaitingToChange.begin();
  while(waitingIter != _visionModesWaitingToChange.end())
  {
    const auto& iter = std::find(msg.visionModes.begin(), msg.visionModes.end(), waitingIter->first);
    const bool modeWasProcessed = (iter != msg.visionModes.end());

    if(modeWasProcessed == waitingIter->second)
    {
      using namespace external_interface;
      auto* gi = _robot->GetGatewayInterface();

      bool eraseFromSet = true;
      ResponseStatus* status = new ResponseStatus(ResponseStatus::OK);
      switch(waitingIter->first)
      {
        case VisionMode::Markers:
          {
            auto* msg = new EnableMarkerDetectionResponse();
            msg->set_allocated_status(status);
            gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
          }
          break;
        case VisionMode::Faces:
          {
            auto* msg = new EnableFaceDetectionResponse();
            msg->set_allocated_status(status);
            gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
          }
          break;
        case VisionMode::Motion:
          {
            auto* msg = new EnableMotionDetectionResponse();
            msg->set_allocated_status(status);
            gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
          }
          break;
        case VisionMode::MirrorMode:
          {
            auto* msg = new EnableMirrorModeResponse();
            msg->set_allocated_status(status);
            gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
          }
          break;
        case VisionMode::Viz:
          {
            auto* msg = new EnableImageStreamingResponse();
            msg->set_allocated_status(status);
            gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
          }
          break;

        default:
          eraseFromSet = false;
          Util::SafeDelete(status);
          break;
      }

      if(eraseFromSet)
      {
        waitingIter = _visionModesWaitingToChange.erase(waitingIter);
      }
      else
      {
        waitingIter++;
      }
    }
    else
    {
      waitingIter++;
    }
  }
}

int SDKComponent::SDKControlLevel()
{
  ANKI_VERIFY(_sdkWantsControl, "SDKComponent::SDKControlLevel", "_sdkWantsControl not set when accessing _sdkControlLevel");

  return _sdkControlLevel;
}

bool SDKComponent::SDKWantsControl()
{
  return _sdkWantsControl;
}

bool SDKComponent::SDKWantsBehaviorLock()
{
  return _sdkWantsLock;
}

void SDKComponent::SDKBehaviorActivation(bool enabled)
{
  _sdkBehaviorActivated = enabled;
  DispatchSDKActivationResult(_sdkBehaviorActivated, _sdkControlConnId);

  // If sdk behavior is being deactivated...
  if(!_sdkBehaviorActivated)
  {
    // ...unsubscribe from MirrorMode. This is to make sure the robot
    // will display eyes when the sdk no longer has control.
    DisableMirrorMode();

    // Remove all other vision modes except for Viz in order
    // to allow SDK users to still receive images when the SDK does not have
    // control.
    VisionModeSet modes;
    modes.InsertAllModes();
    modes.Remove(VisionMode::Viz);
    _robot->GetVisionScheduleMediator().RemoveVisionModeSubscriptions(this, modes.GetSet());

    auto* gi = _robot->GetGatewayInterface();
    auto* msg = new external_interface::Event(new external_interface::VisionModesAutoDisabled());
    gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
  }
}

void SDKComponent::HandleAudioStreamPrepareRequest(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  u32 rate, volume;
  RobotInterface::ExternalAudioPrepare msg;

  rate = event.GetData().external_audio_stream_prepare().audio_frame_rate();
  volume = event.GetData().external_audio_stream_prepare().audio_volume();

  msg.audio_volume = (u16) volume;
  msg.audio_rate = (u16) rate;
  LOG_INFO("SDKComponent.HandleAudioStreamPrepareRequest", "SDK Passing prepare audio stream frame rate %d volume %d", rate, volume);

  // Send request to animation process
  const Result result = _robot->SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  if (RESULT_OK != result) {
    LOG_ERROR("SDKComponent.HandleAudioStreamPrepareRequest", "SDK Send Prepare Audio Streaming Message to Robot failed");
    return;
  }
}

void SDKComponent::HandleAudioStreamChunkRequest(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  const std::string samplesStr = event.GetData().external_audio_stream_chunk().audio_chunk_samples();
  RobotInterface::ExternalAudioChunk msg;
  msg.audio_chunk_size = (u16)event.GetData().external_audio_stream_chunk().audio_chunk_size_bytes();
  LOG_INFO("SDKComponent.HandleAudioStreamChunkRequest", "SDKPassing audio_chunk_size %d", msg.audio_chunk_size);

  if (!ANKI_VERIFY(msg.audio_chunk_size <= 1024,
          "SDKComponent.HandleAudioStreamChunkRequest","Invalid audio playback chunk size %u sent", msg.audio_chunk_size)) {
      auto* gi = _robot->GetGatewayInterface();
      auto* msg = new external_interface::ExternalAudioStreamResponse(new external_interface::ExternalAudioStreamPlaybackFailure());
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
      return;
  }

  if (!ANKI_VERIFY(samplesStr.length() >= msg.audio_chunk_size,
          "SDKComponent.HandleAudioStreamChunkRequest","Invalid audio chunk data size %lu, message specified %u",
          (long)samplesStr.length(), msg.audio_chunk_size)) {
      auto* gi = _robot->GetGatewayInterface();
      auto* msg = new external_interface::ExternalAudioStreamResponse(new external_interface::ExternalAudioStreamPlaybackFailure());
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
      return;
  }

  std::memcpy( msg.audio_chunk_data.data(), samplesStr.c_str(), msg.audio_chunk_size ); //TODO:  do we have to copy?

  // Send request to animation process
  const Result result = _robot->SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  if (RESULT_OK != result) {
    LOG_ERROR("SDKComponent.HandleAudioStreamChunkRequest", "SDK Send Audio Stream Chunk Message to Robot failed");
    return;
  }
}

void SDKComponent::HandleAudioStreamCompleteRequest(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  RobotInterface::ExternalAudioComplete msg;

  // Send request to animation process
  const Result result = _robot->SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  if (RESULT_OK != result) {
    LOG_ERROR("SDKComponent.HandleAudioStreamCompleteRequest", "SDK Send Audio Stream Complete Message to Robot failed");
    return;
  }
}

void SDKComponent::HandleAudioStreamCancelRequest(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  RobotInterface::ExternalAudioCancel msg;

  // Send request to animation process
  const Result result = _robot->SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  if (RESULT_OK != result) {
    LOG_ERROR("SDKComponent.HandleAudioStreamCancelRequest", "SDK Send Audio Stream Cancel Message to Robot failed");
    return;
  }
}

void SDKComponent::SetMasterVolume(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  auto* gi = _robot->GetGatewayInterface();
  external_interface::MasterVolumeRequest request = event.GetData().master_volume_request();

  //set the volume level
  unsigned int desiredVolume = (unsigned int) request.volume_level();
  if (desiredVolume <= 4) {
    desiredVolume += 1; //we don't allow MUTE, so our SDK enum is 1 lower
    auto* settings = _robot->GetComponentPtr<SettingsManager>();
    bool ignoredDueToNoChange;
    const bool success = settings->SetRobotSetting(external_interface::RobotSetting::master_volume,
                                                   desiredVolume,
                                                   true,
                                                   ignoredDueToNoChange);
    if (success || ignoredDueToNoChange) {
      auto* msg = new external_interface::MasterVolumeResponse(new external_interface::ResponseStatus(external_interface::ResponseStatus::OK));
      gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
      return;
    }
  }

  //Bad volume or failed to set:  Send result
  LOG_ERROR("SDKComponent::SetMasterVolume","Failed to change volume.");
  auto* msg = new external_interface::MasterVolumeResponse(new external_interface::ResponseStatus(external_interface::ResponseStatus::FORBIDDEN));
  gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
}

void SDKComponent::DispatchSDKActivationResult(bool enabled, uint64_t connectionId) {
  auto* gi = _robot->GetGatewayInterface();
  if (enabled) {
    LOG_INFO("SDKComponent::DispatchSDKActivationResult","Dispatching SDK enabled activation %llu", connectionId);
    //TODO: better naming, more readable, and logging
    auto* msg = new external_interface::BehaviorControlResponse(new external_interface::ControlGrantedResponse());
    external_interface::GatewayWrapper wrapper = ExternalMessageRouter::WrapResponse(msg, connectionId);
    gi->Broadcast(std::move(wrapper));
  }
  else {
    LOG_INFO("SDKComponent::DispatchSDKActivationResult","Dispatching SDK disabled/lost activation %llu", connectionId);
    auto* msg = new external_interface::BehaviorControlResponse(new external_interface::ControlLostResponse());
    external_interface::GatewayWrapper wrapper = ExternalMessageRouter::WrapResponse(msg, connectionId);
    gi->Broadcast(std::move(wrapper));
  }
}

void SDKComponent::DispatchBehaviorLockLostResult() {
  LOG_INFO("SDKComponent::DispatchBehaviorLockLostResult","Dispatching SDK control lost %llu", _sdkLockConnId);
  auto* gi = _robot->GetGatewayInterface();
  auto* msg = new external_interface::BehaviorControlResponse(new external_interface::ReservedControlLostResponse());
  external_interface::GatewayWrapper wrapper = ExternalMessageRouter::WrapResponse(msg, _sdkLockConnId);
  gi->Broadcast(std::move(wrapper));
}

template <typename MessageType>
external_interface::GatewayWrapper ConstructActionResponseMessage(const ActionResult& actionResult)
{
  MessageType* response = new MessageType();

  external_interface::ActionResult* actionStatus = new external_interface::ActionResult();
  actionStatus->set_code((external_interface::ActionResult_ActionResultCode)actionResult);

  response->set_allocated_result(actionStatus);
  return ExternalMessageRouter::WrapResponse(response);
}

void SDKComponent::OnActionCompleted(ExternalInterface::RobotCompletedAction msg)
{
  auto* gi = _robot->GetGatewayInterface();
  if (gi == nullptr) return;

  switch((RobotActionType)msg.actionType)
  {
    case RobotActionType::PLAY_ANIMATION:
    {
      auto* response = new external_interface::PlayAnimationResponse;
      response->set_result(external_interface::BehaviorResults::BEHAVIOR_COMPLETE_STATE);

      ActionCompletedUnion completionInfo = (ActionCompletedUnion)msg.completionInfo;
      AnimationCompleted animationCompleted = completionInfo.Get_animationCompleted();
      external_interface::Animation* animation = new external_interface::Animation(animationCompleted.animationName);
      response->set_allocated_animation(animation);

      gi->Broadcast( ExternalMessageRouter::WrapResponse(response) );
    }
    break;

    default:
    {
      using ProtoActionResponseFactory = std::function<external_interface::GatewayWrapper(const ActionResult&)>;
      std::map<RobotActionType, ProtoActionResponseFactory> actionResponseFactories = {
        { RobotActionType::TURN_IN_PLACE,       ConstructActionResponseMessage<external_interface::TurnInPlaceResponse> },
        { RobotActionType::DRIVE_STRAIGHT,      ConstructActionResponseMessage<external_interface::DriveStraightResponse> },
        { RobotActionType::MOVE_HEAD_TO_ANGLE,  ConstructActionResponseMessage<external_interface::SetHeadAngleResponse> },
        { RobotActionType::MOVE_LIFT_TO_HEIGHT, ConstructActionResponseMessage<external_interface::SetLiftHeightResponse> },
        { RobotActionType::DRIVE_TO_POSE,       ConstructActionResponseMessage<external_interface::GoToPoseResponse> },
        { RobotActionType::ALIGN_WITH_OBJECT,   ConstructActionResponseMessage<external_interface::DockWithCubeResponse> },
        { RobotActionType::TURN_TOWARDS_FACE,  ConstructActionResponseMessage<external_interface::TurnTowardsFaceResponse> },
        { RobotActionType::DRIVE_TO_OBJECT,  ConstructActionResponseMessage<external_interface::GoToObjectResponse> },
        { RobotActionType::ROLL_OBJECT_LOW,  ConstructActionResponseMessage<external_interface::RollObjectResponse> },
        { RobotActionType::POP_A_WHEELIE,  ConstructActionResponseMessage<external_interface::PopAWheelieResponse> },
        { RobotActionType::PICKUP_OBJECT_LOW,  ConstructActionResponseMessage<external_interface::PickupObjectResponse> },
        { RobotActionType::PLACE_OBJECT_LOW,  ConstructActionResponseMessage<external_interface::PlaceObjectOnGroundHereResponse> }
      };

      if(actionResponseFactories.count((RobotActionType)msg.actionType) == 0)
      {
          PRINT_NAMED_WARNING("SDKComponent.OnActionCompleted.NoMatch", "No match for action tag so no response sent: [Tag=%d]", msg.idTag);
      }
      else
      {
        auto& responseFactory = actionResponseFactories.at((RobotActionType)msg.actionType);
        gi->Broadcast( responseFactory(msg.result) );
      }
      return;
    }
  }
}

bool SDKComponent::SubscribeToVisionMode(bool subscribe, VisionMode mode, bool updateWaitingToChangeSet)
{
  bool res = false;
  if(subscribe)
  {
    _robot->GetVisionScheduleMediator().AddAndUpdateVisionModeSubscriptions(this,
                                                                            {{.mode = mode,
                                                                              .frequency = EVisionUpdateFrequency::High}});
    res = true;
  }
  else
  {
    res = _robot->GetVisionScheduleMediator().RemoveVisionModeSubscriptions(this, {mode});
  }

  if(updateWaitingToChangeSet)
  {
    _visionModesWaitingToChange.insert({mode, subscribe});
  }

  return res;
}

void SDKComponent::DisableMirrorMode()
{
  const bool subscriptionUpdated = SubscribeToVisionMode(false, VisionMode::MirrorMode);
  if(subscriptionUpdated)
  {
    auto* gi = _robot->GetGatewayInterface();
    auto* msg = new external_interface::Event(new external_interface::MirrorModeDisabled());
    gi->Broadcast(ExternalMessageRouter::WrapResponse(msg));
  }
}

void SDKComponent::SayText(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  external_interface::SayTextRequest request = event.GetData().say_text_request();
  AudioMetaData::SwitchState::Robot_Vic_External_Processing processingStyle;

  if (request.use_vector_voice()) {
    processingStyle = AudioMetaData::SwitchState::Robot_Vic_External_Processing::Default_Processed;
  }
  else {
    processingStyle = AudioMetaData::SwitchState::Robot_Vic_External_Processing::Unprocessed;
  }

  auto ttsCallback = [this](const UtteranceState& state) {
    external_interface::SayTextResponse* response =
      new external_interface::SayTextResponse{nullptr, (external_interface::SayTextResponse::UtteranceState) state};
    external_interface::GatewayWrapper wrapper;
    wrapper.set_allocated_say_text_response(response);
    this->_robot->GetGatewayInterface()->Broadcast(wrapper);
  };
  auto & ttsCoordinator = _robot->GetTextToSpeechCoordinator();
  ttsCoordinator.CreateUtterance(request.text(),
                                 UtteranceTriggerType::Immediate,
                                 processingStyle,
                                 request.duration_scalar(),
                                 request.pitch_scalar(),
                                 ttsCallback);
}

void SDKComponent::EnableCameraAutoExposure(bool enable)
{
  //get current camera settings
  auto params = _robot->GetVisionComponent().GetCurrentCameraParams();

  //enable auto exposure, send to vision component
  auto camera_msg = ExternalInterface::SetCameraSettings();
  camera_msg.enableAutoExposure = enable;
  camera_msg.gain = params.gain;
  camera_msg.exposure_ms = params.exposureTime_ms;
  _robot->GetExternalInterface()->Broadcast(ExternalInterface::MessageGameToEngine(std::move(camera_msg)));
}

void SDKComponent::SetCameraSettings(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  using namespace external_interface;

  auto* gi = _robot->GetGatewayInterface();
  if (gi == nullptr) return;

  const auto request = event.GetData().set_camera_settings_request();

  if (request.enable_auto_exposure()) {
    EnableCameraAutoExposure();
  } else {
    //validate exposure params
    auto* vis = &_robot->GetVisionComponent();

    auto minExp_ms = vis->GetMinCameraExposureTime_ms();
    auto maxExp_ms = vis->GetMaxCameraExposureTime_ms();
    auto minGain = vis->GetMinCameraGain();
    auto maxGain = vis->GetMaxCameraGain();
    if (!Util::InRange((s32)request.exposure_ms(), minExp_ms, maxExp_ms)
        || !Util::InRange(request.gain(), minGain, maxGain))
    {
      LOG_ERROR("SDKComponent::SetCameraSettings", "User settings out of bounds: exposure %u, gain %f",
                request.exposure_ms(), request.gain());
      SetCameraSettingsResponse* response = new SetCameraSettingsResponse();
      response->set_allocated_status(new ResponseStatus(ResponseStatus::FORBIDDEN));
      std::stringstream reason;
      reason << "Camera exposure time must be in the range " << minExp_ms << " - " << maxExp_ms
            << " ms, gain must be in the range " << minGain << " - " << maxGain;
      response->set_status_message( reason.str() );
      gi->Broadcast( ExternalMessageRouter::WrapResponse(response));
      return;
    }

    //send to vision component
    auto camera_msg = ExternalInterface::SetCameraSettings();
    camera_msg.enableAutoExposure = request.enable_auto_exposure();
    camera_msg.gain = request.gain();
    camera_msg.exposure_ms = request.exposure_ms();
    _robot->GetExternalInterface()->Broadcast(ExternalInterface::MessageGameToEngine(std::move(camera_msg)));
  }

  //send success response to user
  auto* response = new SetCameraSettingsResponse();
  response->set_allocated_status(new ResponseStatus(ResponseStatus::OK));
  gi->Broadcast( ExternalMessageRouter::WrapResponse(response));
}

void SDKComponent::SetEyeColor(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  external_interface::SetEyeColorRequest request = event.GetData().set_eye_color_request();

  const float hue = request.hue();
  const float saturation = request.saturation();

  _robot->SendRobotMessage<RobotInterface::SetFaceHue>(hue);
  _robot->SendRobotMessage<RobotInterface::SetFaceSaturation>(saturation);
}

void SDKComponent::ListAnimationTriggers(const AnkiEvent<external_interface::GatewayWrapper>& event)
{
  auto* gi = _robot->GetGatewayInterface();
  if (gi == nullptr) return;

  external_interface::ListAnimationTriggersRequest request = event.GetData().list_animation_triggers_request();

  // Note that the last item in AnimationTriggerNumEntries is a Count value, so -1 to not include that (pre-existing hackiness).
  for( size_t i=0; i < AnimationTriggerNumEntries - 1; ++i ) {
    const char* result = EnumToString( static_cast<AnimationTrigger>(i) );
    external_interface::ListAnimationTriggersResponse* list_animation_triggers_response = new external_interface::ListAnimationTriggersResponse;
    std::string animTriggerName = result;
    list_animation_triggers_response->add_animation_trigger_names()->set_name(animTriggerName);
    gi->Broadcast( ExternalMessageRouter::WrapResponse(list_animation_triggers_response));
  }

  // Send "EndOfListAnimationTriggersResponses". vic-gateway recipient depends upon it.
  external_interface::ListAnimationTriggersResponse* end_of_list_animation_triggers_response = new external_interface::ListAnimationTriggersResponse;
  end_of_list_animation_triggers_response->add_animation_trigger_names()->set_name("EndOfListAnimationsResponses");
  gi->Broadcast( ExternalMessageRouter::WrapResponse(end_of_list_animation_triggers_response));
}

} // namespace Vector
} // namespace Anki
