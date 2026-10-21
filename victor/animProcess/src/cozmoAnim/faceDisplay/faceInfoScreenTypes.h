/**
* File: faceInfoScreenTypes.h
*
* Author: Kevin Yoon
* Created: 03/02/2018
*
* Description: Types of Customer Support Info / Debug face screens
*
* Copyright: Anki, Inc. 2018
*
*/

#ifndef __AnimProcess_CozmoAnim_FaceDisplay_FaceInfoScreenTypes_H_
#define __AnimProcess_CozmoAnim_FaceDisplay_FaceInfoScreenTypes_H_

#include <string>
#include <utility>
#include <vector>
namespace Anki {
namespace Vector {
  
// The names of all the screens that are supported
enum class ScreenName : uint8_t {
  None = 0,
  FAC  = 1, // Needs to be after None

  Recovery,

  Pairing,
    
  Main,
  ClearUserData,
  ClearUserDataFail,
  Rebooting,
  SelfTest,
  SelfTestRunning,
  Network,
  SensorInfo,
  IMUInfo,
  MotorInfo,
  BuildInfo,
  Camera,
  CameraMotorTest,
  MicInfo,
  MicDirectionClock,
  CustomText,
  MirrorMode, // Like Camera but without links to other screens
  AlexaNotification,   // quick face screen that alexa has a notification (todo: remove)
  AlexaPairing,        // pairing in progress (no timeout)
  AlexaPairingSuccess, // completed pairing (has timeout)
  AlexaPairingExpired, // code expires (has timeout)
  AlexaPairingFailed,  // server error (has timeout)
  ToggleMute, // Quick animation to show change in microphone mute state
  ToF,
  Kercre123,
  
  JamesOS, // JamesOS settings screen
  
  JamesOS2, // JamesOS MODS page 2
  
  JamesStars, // JamesOS COOL FEATURES
  
  JamesHearts, // JamesOS COOL FEATURES
  
  JamesMood, // JamesOS COOL FEATURES
  
  JamesLights, // JamesOS COOL FEATURES
  
  JamesEffects, // JamesOS COOL FEATURES
  
  JamesTheme, // JamesOS COOL FEATURES
  
  JamesSleep, // JamesOS COOL FEATURES
  
  JamesStatus, // JamesOS COOL FEATURES
  
  JamesBadges, // JamesOS COOL FEATURES
  
  JamesConf, // JamesOS COOL FEATURES
  
  JamesAbout, // JamesOS COOL FEATURES
  
  JamesRestart, // JamesOS COOL FEATURES
  
  JamesSecret, // JamesOS COOL FEATURES
  
  JamesProof, // JamesOS COOL FEATURES
  
  JamesNews, // JamesOS COOL FEATURES
  
  JamesFamily, // JamesOS COOL FEATURES
  
  Count
};

// JamesOS: info lines shown under the header of the COOL FEATURES screens
// (color: 0 = white, 1 = green, 2 = red). Defined in faceInfoScreen.cpp.
void JamesSetListInfo(ScreenName screen, const std::vector<std::pair<std::string, int>>& lines);

// JamesOS: values shown on the right of each COOL FEATURES line (same order as the menu).
void JamesSetListValues(ScreenName screen, const std::vector<std::string>& values);

constexpr f32 kDefaultScreenTimeoutDuration_s = 180.f;

} // namespace Vector
} // namespace Anki

#endif // __AnimProcess_CozmoAnim_FaceDisplay_FaceInfoScreenTypes_H_
