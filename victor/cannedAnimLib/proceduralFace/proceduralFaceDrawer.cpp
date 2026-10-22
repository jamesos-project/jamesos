#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h"
#include "cannedAnimLib/proceduralFace/scanlineDistorter.h"

#include "coretech/common/shared/array2d.h"
#include "coretech/common/shared/math/matrix.h"
#include "coretech/common/engine/math/quad.h"
#include "coretech/common/shared/math/rotation.h"

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/math/math.h"
#include "util/math/numericCast.h"
#include "util/random/randomGenerator.h"
#include "util/jamesos/jamesosSeasonal.h" // JamesOS: one holiday calendar
#include <mutex>
#include <functional>
#include <cstring>
#include <map>
#include <deque>
#include <ctime>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <sys/stat.h>

// switch std::round() to macro for easier change
// switched from (x) to std::round(x) as it was noticed edges were jittering when
// the roundness of corners was animated (VIC-3930). Keeping as a macro in-case
// there's a faster way to round()/cast().
#define ROUND(x) std::round(x)

// switch Util::numeric_cast_clamped() to macro for easier change
#define NUMERIC_CAST_CLAMPED(x) (u8)(x) // Util::numeric_cast_clamped<u8>(x)

namespace Anki {
namespace Vector {

  static std::mutex gCustomEyeMtx;

  #define CONSOLE_GROUP "Face.ParameterizedFace"

  enum class Filter {
    None           = 0,
    BoxFilter      = 1,
    GaussianFilter = 2,
  };

  CONSOLE_VAR_ENUM(u8,      kProcFace_LineType,                   CONSOLE_GROUP, IsXray() ? 2 : 1, "Line_4,Line_8,Line_AA"); // Only affects OpenCV drawing, not post-smoothing
  CONSOLE_VAR_ENUM(u8,      kProcFace_InterpolationType,          CONSOLE_GROUP, 1, "Nearest,Linear,Cubic,Area,Lanczos,LinearExact,Max,WarpFillOutliers");
  CONSOLE_VAR_RANGED(s32,   kProcFace_EllipseDelta,               CONSOLE_GROUP, IsXray() ? 15 : 10, 1, 90);
  CONSOLE_VAR_RANGED(f32,   kProcFace_EyeLightnessMultiplier,     CONSOLE_GROUP, 1.f, 0.f, 2.f);

  CONSOLE_VAR(bool,         kProcFace_HotspotRender,              CONSOLE_GROUP, true); // Render glow
  CONSOLE_VAR_RANGED(f32,   kProcFace_HotspotFalloff,             CONSOLE_GROUP, 0.48f, 0.05f, 1.f);
 
  CONSOLE_VAR(bool,         kProcFace_EnableAntiAliasing,         CONSOLE_GROUP, true);
  CONSOLE_VAR_RANGED(s32,   kProcFace_AntiAliasingSize,           CONSOLE_GROUP, IsXray() ? 2 : 3, 0, 15); // full image antialiasing (3 will use NEON)
  CONSOLE_VAR_ENUM(uint8_t, kProcFace_AntiAliasingFilter,         CONSOLE_GROUP, (uint8_t)Filter::BoxFilter, "None,Box,Gaussian");
  CONSOLE_VAR_RANGED(f32,   kProcFace_AntiAliasingSigmaFraction,  CONSOLE_GROUP, IsXray() ? 0.3f : 0.5f, 0.0f, 1.0f);
  CONSOLE_VAR(bool, kProcFace_CustomEyes, CONSOLE_GROUP, false);
  CONSOLE_VAR_RANGED(f32, kProcFace_CustomEyeOpacity, CONSOLE_GROUP, 0.8f, 0.f, 1.f);
  CONSOLE_VAR_ENUM(u8, kProcFace_FlavorOfGay, CONSOLE_GROUP, 0, "Lesbian,Gay,Bi,Trans,Pan,Frog,All,Galaxy,Custom");
  static void LOOK_LoadFaceOverlay(ConsoleFunctionContextRef context)
  {
    ProceduralFaceDrawer::LoadCustomEyePNG();
  }
  CONSOLE_FUNC(LOOK_LoadFaceOverlay, CONSOLE_GROUP);


#if PROCEDURALFACE_GLOW_FEATURE
  CONSOLE_VAR_RANGED(f32, kProcFace_GlowSizeMultiplier,           CONSOLE_GROUP, 1.f, 0.f, 1.f);
  CONSOLE_VAR_RANGED(f32, kProcFace_GlowLightnessMultiplier,      CONSOLE_GROUP, 1.f, 0.f, 10.f);
  CONSOLE_VAR_ENUM(uint8_t, kProcFace_GlowFilter,                 CONSOLE_GROUP, (uint8_t)Filter::BoxFilter, "None,Box,Gaussian,Box (NEON code; size 3)");
#endif

#if PROCEDURALFACE_SCANLINE_FEATURE
  CONSOLE_VAR(bool,                         kProcFace_Scanlines,              CONSOLE_GROUP, false);
  CONSOLE_VAR_RANGED(ProceduralFace::Value, kProcFace_DefaultScanlineOpacity, CONSOLE_GROUP, 1.f, 0.f, 1.f);
#endif

#if PROCEDURALFACE_NOISE_FEATURE
  static const s32 kNumNoiseImages = 7;

  CONSOLE_VAR_RANGED(s32, kProcFace_NoiseNumFrames,              CONSOLE_GROUP, 5, 0, kNumNoiseImages);
  CONSOLE_VAR_RANGED(f32, kProcFace_NoiseMinLightness,           CONSOLE_GROUP, IsXray() ? 0.98f : 0.92f, 0.f, 2.f); // replaces kProcFace_NoiseFraction
  CONSOLE_VAR_RANGED(f32, kProcFace_NoiseMaxLightness,           CONSOLE_GROUP, IsXray() ? 1.05f : 1.14f, 0.f, 2.f);

  CONSOLE_VAR_EXTERN(s32, kProcFace_NoiseNumFrames);
#else
  static const s32 kProcFace_NoiseNumFrames = 0;
#endif

  #undef CONSOLE_GROUP

  // Initialize static vars

#if PROCEDURALFACE_GLOW_FEATURE
  Vision::Image ProceduralFaceDrawer::_glowImg;
#endif
  Vision::Image ProceduralFaceDrawer::_eyeShape; // temporary working surface, I8

  struct ProceduralFaceDrawer::FaceCache ProceduralFaceDrawer::_faceCache;

  Rectangle<f32> ProceduralFaceDrawer::_leftBBox;
  Rectangle<f32> ProceduralFaceDrawer::_rightBBox;
  s32 ProceduralFaceDrawer::_faceColMin;
  s32 ProceduralFaceDrawer::_faceColMax;
  s32 ProceduralFaceDrawer::_faceRowMin;
  s32 ProceduralFaceDrawer::_faceRowMax;

  Vision::ImageRGB565 ProceduralFaceDrawer::_customEyeOverlay;
  bool ProceduralFaceDrawer::_hasCustomEyes = false;
  Vision::Image ProceduralFaceDrawer::_customEyeAlpha;

  Matrix_3x3f ProceduralFaceDrawer::GetTransformationMatrix(f32 angleDeg, f32 scaleX, f32 scaleY,
                                                            f32 tX, f32 tY, f32 x0, f32 y0)
  {
    //
    // Create a 2x3 warp matrix which incorporates scale, rotation, and translation
    //    W = R * [scale_x    0   ] * [x - x0] + [x0] + [tx]
    //            [   0    scale_y]   [y - y0] + [y0] + [ty]
    //
    // So a given point gets scaled (first!) and then rotated around the given center
    // (x0,y0)and then translated by (tx,ty).
    //
    // Note: can't use cv::getRotationMatrix2D, b/c it only incorporates one
    // scale factor, not separate scaling in x and y. Otherwise, this is
    // exactly the same thing
    //
    f32 cosAngle = 1, sinAngle = 0;
    if(angleDeg != 0) {
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      cosAngle = std::cos(angleRad);
      sinAngle = std::sin(angleRad);
    }

    const f32 alpha_x = scaleX * cosAngle;
    const f32 beta_x  = scaleX * sinAngle;
    const f32 alpha_y = scaleY * cosAngle;
    const f32 beta_y  = scaleY * sinAngle;

    Matrix_3x3f W{
      alpha_x, beta_y,  (1.f - alpha_x)*x0 - beta_y*y0 + tX,
      -beta_x, alpha_y, beta_x*x0 + (1.f - alpha_y)*y0 + tY,
      0, 0, 1
    };

    return W;
  } // GetTransformationMatrix()


  // Based on Taylor series expansion with N terms
  inline static f32 fastExp(f32 x)
  {
#   define NUM_FAST_EXP_TERMS 2

    if(x < -(f32)(2*NUM_FAST_EXP_TERMS))
    {
      // Things get numerically unstable for very negative inputs x. Value is basically zero anyway.
      return 0.f;
    }
#   if NUM_FAST_EXP_TERMS==2
    x = 1.f + (x * 0.25f);  // Constant here is 1/(2^N)
    x *= x; x *= x;         // Number of multiplies here is also N
#   elif NUM_FAST_EXP_TERMS==3
    x = 1.f + (x * 0.125f); // Constant here is 1/(2^N)
    x *= x; x *= x; x *= x; // Number of multiplies here is also N
#   else
#   error Unsupported number of terms for fastExp()
#   endif

    return x;

#   undef NUM_FAST_EXP_TERMS
  }

#if PROCEDURALFACE_NOISE_FEATURE
  inline Array2d<u8> CreateNoiseImage(const Util::RandomGenerator& rng)
  {
    Array2d<u8> noiseImg(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
    u8* noiseImg_i = noiseImg.GetRow(0);
    for(s32 j=0; j<noiseImg.GetNumElements(); ++j)
    {
      // Storing noise in the range [0 255] instead of [0 2.0] both to reduce memory usage as well
      // as improving NEON performance by not having to convert between float and u8 
      noiseImg_i[j] = Util::numeric_cast_clamped<u8>(rng.RandDblInRange(kProcFace_NoiseMinLightness,
                                                                        kProcFace_NoiseMaxLightness) * 128);
    }
    return noiseImg;
  }

  const Array2d<u8>& ProceduralFaceDrawer::GetNoiseImage(const Util::RandomGenerator& rng)
  {
    // NOTE: Since this is called separately for each eye, this looks better if we use an odd number of images
    static_assert(kNumNoiseImages % 2 == 1, "Use odd number of noise images");
    static std::array<Array2d<u8>, kNumNoiseImages> kNoiseImages{{
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
    }};

  #if REMOTE_CONSOLE_ENABLED
    kProcFace_NoiseMinLightness = Anki::Util::Clamp(kProcFace_NoiseMinLightness, 0.f, kProcFace_NoiseMaxLightness);
    kProcFace_NoiseMaxLightness = Anki::Util::Clamp(kProcFace_NoiseMaxLightness, kProcFace_NoiseMinLightness, 2.f);
  #endif // REMOTE_CONSOLE_ENABLED

    static f32 kProcFace_NoiseMinLightness_old = kProcFace_NoiseMinLightness;
    static f32 kProcFace_NoiseMaxLightness_old = kProcFace_NoiseMaxLightness;
    if(kProcFace_NoiseMinLightness_old != kProcFace_NoiseMinLightness || kProcFace_NoiseMaxLightness_old != kProcFace_NoiseMaxLightness) {
      for(auto& currentNoiseImage : kNoiseImages) {
        Array2d<u8> noiseImg = CreateNoiseImage(rng);
        std::swap(currentNoiseImage, noiseImg);
      }

      kProcFace_NoiseMinLightness_old = kProcFace_NoiseMinLightness;
      kProcFace_NoiseMaxLightness_old = kProcFace_NoiseMaxLightness;
    }

    if(kProcFace_NoiseNumFrames == 0) {
      return kNoiseImages[0];
    } else {
      // Cycle circularly through the set of noise images
      static s32 index = 0;
      index = (index + 1) % kProcFace_NoiseNumFrames;
      return kNoiseImages[index];
    }
  }
#endif // PROCEDURALFACE_NOISE_FEATURE

  void ProceduralFaceDrawer::ApplyAntiAliasing(Vision::Image& shape, float minX, float minY, float maxX, float maxY)
  {
    if(kProcFace_AntiAliasingSize > 0) {
      ANKI_CPU_PROFILE("AntiAliasing");

      Rectangle<s32> boundingBoxS32(minX, minY, maxX - minX + 1, maxY - minY + 1);
      Vision::Image shapeROI = shape.GetROI(boundingBoxS32);

      switch(kProcFace_AntiAliasingFilter) {
        case (uint8_t)Filter::BoxFilter:
        {
          static Vision::Image temp(shape.GetNumRows(), shape.GetNumCols());
          temp.FillWith(0);
          Vision::Image tempImage = temp.GetROI(boundingBoxS32);
          shapeROI.BoxFilter(tempImage, kProcFace_AntiAliasingSize);
          std::swap(shape, temp);
          break;
        }
        case (uint8_t)Filter::GaussianFilter:
        {
          cv::GaussianBlur(shape.get_CvMat_(),
                           shape.get_CvMat_(),
                           cv::Size(kProcFace_AntiAliasingSize,kProcFace_AntiAliasingSize),
                           (f32)kProcFace_AntiAliasingSize * kProcFace_AntiAliasingSigmaFraction);
          break;
        }
      }
    }
  }

void ProceduralFaceDrawer::LoadCustomEyePNG()
{
  std::lock_guard<std::mutex> lk(gCustomEyeMtx);
  _hasCustomEyes = false;
  static const cv::String kFaceOverlays[9] = { 
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/lesbian.jpg", 
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/gay.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/bi.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/trans.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/pan.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/frog.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/all.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/galaxy.jpg",
    "/data/data/customFaceOverlay.jpg"
  };
  const cv::String kFaceOverlay = kFaceOverlays[kProcFace_FlavorOfGay];
  cv::Mat img = cv::imread(kFaceOverlay, cv::IMREAD_UNCHANGED);
  if(img.empty()) {
    return;
  }

  // if given a 1920x1080 image, we don't want to scale that upon every face draw
  if (img.cols != FACE_DISPLAY_WIDTH || img.rows > FACE_DISPLAY_HEIGHT) {
    cv::resize(img, img, cv::Size(FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT), 0, 0, cv::INTER_AREA);
  }

  bool hasAlpha = (img.channels() == 4);
  if(!hasAlpha && img.channels() != 3) {
    // what silly image is this
    return;
  }

  _customEyeOverlay.Allocate(img.rows, img.cols);
  _customEyeAlpha  .Allocate(img.rows, img.cols);

  for(int r=0; r<img.rows; ++r)
  {
    const cv::Vec4b* src4 = hasAlpha ? img.ptr<cv::Vec4b>(r) : nullptr;
    const cv::Vec3b* src3 = !hasAlpha? img.ptr<cv::Vec3b>(r) : nullptr;
    auto* dstPix = _customEyeOverlay.GetRow(r);
    u8*   dstA   = _customEyeAlpha .GetRow(r);

    for(int c=0; c<img.cols; ++c)
    {
      u8 b,g,r8,a;
      if(hasAlpha) { 
        b=src4[c][0]; 
        g=src4[c][1]; 
        r8=src4[c][2]; 
        a=src4[c][3]; 
      } else { 
        b=src3[c][0]; 
        g=src3[c][1]; 
        r8=src3[c][2]; 
        a = Util::Clamp<u8>(kProcFace_CustomEyeOpacity*255.f,0,255); 
      }

      dstPix[c] = { 
        static_cast<u16>( ((r8 & 0xF8)<<8) | ((g & 0xFC)<<3) | (b>>3) ) 
      };
      dstA[c] = a;
    }
  }
  _hasCustomEyes = true;
}


  void ProceduralFaceDrawer::DrawEye(const ProceduralFace& faceData, WhichEye whichEye, const Matrix_3x3f* W_facePtr,
                                     Vision::Image& faceImg, Rectangle<f32>& eyeBoundingBox)
  {
    const s32 eyeWidth  = ProceduralFace::NominalEyeWidth;
    const s32 eyeHeight = ProceduralFace::NominalEyeHeight;
    const f32 halfEyeWidth  = 0.5f*eyeWidth;
    const f32 halfEyeHeight = 0.5f*eyeHeight;

    // Left/right here will be in terms of the left eye. We will mirror to get
    // the right eye. So
    const f32 upLeftRadX  = faceData.GetParameter(whichEye, Parameter::UpperOuterRadiusX)*halfEyeWidth;
    const f32 upLeftRadY  = faceData.GetParameter(whichEye, Parameter::UpperOuterRadiusY)*halfEyeHeight;
    const f32 lowLeftRadX = faceData.GetParameter(whichEye, Parameter::LowerOuterRadiusX)*halfEyeWidth;
    const f32 lowLeftRadY = faceData.GetParameter(whichEye, Parameter::LowerOuterRadiusY)*halfEyeHeight;

    const f32 upRightRadX  = faceData.GetParameter(whichEye, Parameter::UpperInnerRadiusX)*halfEyeWidth;
    const f32 upRightRadY  = faceData.GetParameter(whichEye, Parameter::UpperInnerRadiusY)*halfEyeHeight;
    const f32 lowRightRadX = faceData.GetParameter(whichEye, Parameter::LowerInnerRadiusX)*halfEyeWidth;
    const f32 lowRightRadY = faceData.GetParameter(whichEye, Parameter::LowerInnerRadiusY)*halfEyeHeight;

    //
    // Compute eye and lid polygons:
    //
    std::vector<cv::Point> eyePoly, segment, lowerLidPoly, upperLidPoly;
    static const s32 kLineTypes[3] = { cv::LINE_4, cv::LINE_8, cv::LINE_AA };
    const s32 kLineType = kLineTypes[kProcFace_LineType];

    // 1. Eye shape poly
    {
      //ANKI_CPU_PROFILE("EyeShapePoly");
      // Upper right corner
      if(upRightRadX > 0 && upRightRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(halfEyeWidth - upRightRadX), ROUND(-halfEyeHeight + upRightRadY)),
                         cv::Size(upRightRadX,upRightRadY), 0, 270, 360, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(halfEyeWidth,-halfEyeHeight);
      }

      // Lower right corner
      if(lowRightRadX > 0 && lowRightRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(halfEyeWidth - lowRightRadX), ROUND(halfEyeHeight - lowRightRadY)),
                         cv::Size(lowRightRadX,lowRightRadY), 0, 0, 90, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(halfEyeWidth, halfEyeHeight);
      }

      // Lower left corner
      if(lowLeftRadX > 0 && lowLeftRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(-halfEyeWidth + lowLeftRadX), ROUND(halfEyeHeight - lowLeftRadY)),
                         cv::Size(lowLeftRadX,lowLeftRadY), 0, 90, 180, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(-halfEyeWidth, halfEyeHeight);
      }

      // Upper left corner
      if(upLeftRadX > 0 && upLeftRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(-halfEyeWidth + upLeftRadX), ROUND(-halfEyeHeight + upLeftRadY)),
                         cv::Size(upLeftRadX,upLeftRadY), 0, 180, 270, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(-halfEyeWidth,-halfEyeHeight);
      }
    }

    // 2. Lower lid poly
    {
      //ANKI_CPU_PROFILE("LowerLidPoly");
      const f32 lowerLidY = faceData.GetParameter(whichEye, Parameter::LowerLidY) * static_cast<f32>(eyeHeight);
      const f32 angleDeg = faceData.GetParameter(whichEye, Parameter::LowerLidAngle);
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      const f32 yAngleAdj = -halfEyeWidth * std::tan(angleRad);
      lowerLidPoly = {
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(halfEyeHeight - lowerLidY - yAngleAdj)}, // Upper right corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(halfEyeHeight + 1.f)}, // Lower right corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(halfEyeHeight + 1.f)}, // Lower left corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(halfEyeHeight - lowerLidY + yAngleAdj)}, // Upper left corner
      };
      // Add bend:
      const f32 yRad = faceData.GetParameter(whichEye, Parameter::LowerLidBend) * static_cast<f32>(eyeHeight);
      if(yRad != 0) {
        const f32 xRad = ROUND(halfEyeWidth / std::cos(angleRad));
        cv::ellipse2Poly(cv::Point(0, ROUND(halfEyeHeight - lowerLidY)),
                         cv::Size(xRad,yRad), angleDeg, 180, 360, kProcFace_EllipseDelta, segment);
        DEV_ASSERT(std::abs(segment.front().x - lowerLidPoly.back().x)<3 &&
                   std::abs(segment.front().y - lowerLidPoly.back().y)<3,
                   "First curved lower lid segment point not close to last lid poly point.");
        DEV_ASSERT(std::abs(segment.back().x - lowerLidPoly.front().x)<3 &&
                   std::abs(segment.back().y - lowerLidPoly.front().y)<3,
                   "Last curved lower lid segment point not close to first lid poly point.");
        lowerLidPoly.insert(lowerLidPoly.end(), segment.begin(), segment.end());
      }
    }

    // 3. Upper lid poly
    {
      //ANKI_CPU_PROFILE("UpperLidPoly");
      const f32 upperLidY = faceData.GetParameter(whichEye, Parameter::UpperLidY) * static_cast<f32>(eyeHeight);
      const f32 angleDeg = faceData.GetParameter(whichEye, Parameter::UpperLidAngle);
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      const f32 yAngleAdj = -halfEyeWidth * std::tan(angleRad);
      upperLidPoly = {
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(-halfEyeHeight + upperLidY + yAngleAdj)}, // Lower left corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(-halfEyeHeight - 1.f)}, // Upper left corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(-halfEyeHeight - 1.f)}, // Upper right corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(-halfEyeHeight + upperLidY - yAngleAdj)}, // Lower right corner
      };
      // Add bend:
      const f32 yRad = faceData.GetParameter(whichEye, Parameter::UpperLidBend) * static_cast<f32>(eyeHeight);
      if(yRad != 0) {
        const f32 xRad = ROUND(halfEyeWidth / std::cos(angleRad));
        cv::ellipse2Poly(cv::Point(0, ROUND(-halfEyeHeight + upperLidY)),
                         cv::Size(xRad,yRad), angleDeg, 0, 180, kProcFace_EllipseDelta, segment);
        DEV_ASSERT(std::abs(segment.front().x - upperLidPoly.back().x)<3 &&
                   std::abs(segment.front().y - upperLidPoly.back().y)<3,
                   "First curved upper lid segment point not close to last lid poly point");
        DEV_ASSERT(std::abs(segment.back().x - upperLidPoly.front().x)<3 &&
                   std::abs(segment.back().y - upperLidPoly.front().y)<3,
                   "Last curved upper lid segment point not close to first lid poly point");
        upperLidPoly.insert(upperLidPoly.end(), segment.begin(), segment.end());
      }
    }

    ANKI_CPU_PROFILE_START(prof_getMat, "GetMatrices");
    Point<2, Value> eyeCenter = (whichEye == WhichEye::Left) ?
                                 Point<2, Value>(ProceduralFace::GetNominalLeftEyeX(), ProceduralFace::GetNominalEyeY()) :
                                 Point<2, Value>(ProceduralFace::GetNominalRightEyeX(), ProceduralFace::GetNominalEyeY());
    eyeCenter.x() += faceData.GetParameter(whichEye, Parameter::EyeCenterX);
    eyeCenter.y() += faceData.GetParameter(whichEye, Parameter::EyeCenterY);

    // Apply rotation, translation, and scaling to the eye and lid polygons.
    // This warp is a combination of the eye-specific parameters and full-face parameters
    auto W = GetTransformationMatrix(faceData.GetParameter(whichEye, Parameter::EyeAngle),
                                     faceData.GetParameter(whichEye, Parameter::EyeScaleX),
                                     faceData.GetParameter(whichEye, Parameter::EyeScaleY),
                                     eyeCenter.x(),
                                     eyeCenter.y());
    
    if(W_facePtr != nullptr)
    {
      // Compose full-face warp with eye-only warp
      W = (*W_facePtr) * W;
      
      // Update eye center now that full-face warp has been composed in
      eyeCenter.x() = W(0,2);
      eyeCenter.y() = W(1,2);
    }
    
#if PROCEDURALFACE_GLOW_FEATURE
    const Value glowFraction = Util::Min(1.f, Util::Max(-1.f, kProcFace_GlowSizeMultiplier * faceData.GetParameter(whichEye, Parameter::GlowSize)));
    const SmallMatrix<2, 3, f32> W_glow = GetTransformationMatrix(faceData.GetParameter(whichEye, Parameter::EyeAngle),
                                                                  (1+glowFraction) * faceData.GetParameter(whichEye, Parameter::EyeScaleX),
                                                                  (1+glowFraction) * faceData.GetParameter(whichEye, Parameter::EyeScaleY),
                                                                  eyeCenter.x(),
                                                                  eyeCenter.y());
#endif
    //ANKI_CPU_PROFILE_STOP(prof_getMat);

    // Initialize bounding box corners at their opposite extremes. We will figure out their
    // true locations as we loop over the eyePoly below.
    Point2f upperLeft(ProceduralFace::WIDTH, ProceduralFace::HEIGHT);
    Point2f bottomRight(0.f,0.f);

    ANKI_CPU_PROFILE_START(prof_applyMat, "ApplyMatrices");
    // Warp the poly and the glow. Use the warped glow (which is a larger shape) to compute
    // the overall eye bounding box
    for(auto & point : eyePoly)
    {
      const Point<3,f32> pointF32{
        static_cast<f32>(whichEye == WhichEye::Left ? point.x : -point.x), static_cast<f32>(point.y), 1.f
      };

      Point<2,f32> temp = W * pointF32;
      point.x = ROUND(temp.x());
      point.y = ROUND(temp.y());

#if PROCEDURALFACE_GLOW_FEATURE
      // Use glow warp to figure out larger bounding box which contains glow as well
      temp = W_glow * pointF32;
#endif
      upperLeft.x()   = std::min(upperLeft.x(),   (f32)std::floor(temp.x()));
      bottomRight.x() = std::max(bottomRight.x(), (f32)std::ceil(temp.x()));
      upperLeft.y()   = std::min(upperLeft.y(),   (f32)std::floor(temp.y()));
      bottomRight.y() = std::max(bottomRight.y(), (f32)std::ceil(temp.y()));
    }

    // Warp the lids
    for(auto poly : {&lowerLidPoly, &upperLidPoly}) {
      for(auto & point : *poly) {
        const Point<3,f32> pointF32{
          static_cast<f32>(whichEye == WhichEye::Left ? point.x : -point.x), static_cast<f32>(point.y), 1.f
        };

        Point<2,f32> temp = W * pointF32;
        point.x = ROUND(temp.x());
        point.y = ROUND(temp.y());
      }
    }
    //ANKI_CPU_PROFILE_STOP(prof_applyMat);

    // Make sure the upper left and bottom right points are in bounds (note that we loop over
    // pixels below *inclusive* of the bottom right point, so we use HEIGHT/WIDTH-1)

    // Note: visual artifacts can be seen with the NEON pathway, only slightly on the top/left border, which
    //       is fixed by extending the ROI by half the filter size (as expected).
    //       However, they are very clearly visible on the bottom/right and only disappear by extending the
    //       ROI by kProcFace_AntiAliasingSize.

    upperLeft.x() = std::max(0.f, upperLeft.x()-kProcFace_AntiAliasingSize*.5f);
    upperLeft.y() = std::max(0.f, upperLeft.y()-kProcFace_AntiAliasingSize*.5f);
    bottomRight.x() = std::min((f32)(ProceduralFace::WIDTH-1), bottomRight.x()+kProcFace_AntiAliasingSize);
    bottomRight.y() = std::min((f32)(ProceduralFace::HEIGHT-1), bottomRight.y()+kProcFace_AntiAliasingSize);

    // Create the bounding that we're returning from the upperLeft and bottomRight points
    eyeBoundingBox = Rectangle<f32>(upperLeft, bottomRight);

    // Draw eye
    _eyeShape.Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
    _eyeShape.FillWith(0);
    
    cv::fillConvexPoly(_eyeShape.get_CvMat_(), eyePoly, 255, kLineType);

    // Black out lids
    if(!upperLidPoly.empty()) {
      if(faceData.GetParameter(whichEye, Parameter::UpperLidBend) < 0.f) {
        const cv::Point* pts[1] = { &upperLidPoly[0] };
        int npts[1] = { (int)upperLidPoly.size() };
        cv::fillPoly(_eyeShape.get_CvMat_(), pts, npts, 1, 0, kLineType);
      } else {
        cv::fillConvexPoly(_eyeShape.get_CvMat_(), upperLidPoly, 0, kLineType);
      }
    }
    if(!lowerLidPoly.empty()) {
      if(faceData.GetParameter(whichEye, Parameter::LowerLidBend) < 0.f) {
        const cv::Point* pts[1] = { &lowerLidPoly[0] };
        int npts[1] = { (int)lowerLidPoly.size() };
        cv::fillPoly(_eyeShape.get_CvMat_(), pts, npts, 1, 0, kLineType);
      } else {
        cv::fillConvexPoly(_eyeShape.get_CvMat_(), lowerLidPoly, 0, kLineType);
      }
    }
  
    // only render if the eyes are large enough, scale is handled in the calling function
    if(eyeWidth > 0 && eyeHeight > 0)
    {
      // The hotspot center params leave the hot spot at the eye center if zero. If non-zero,
      // they shift left/right/up/down where a magnitude of 1.0 moves the center to the extreme
      // edge of the eye shape
      const Point2f hotSpotCenter = W * Point3f(0.5f*eyeWidth*faceData.GetParameter(whichEye, Parameter::HotSpotCenterX),
                                                0.5f*eyeHeight*faceData.GetParameter(whichEye, Parameter::HotSpotCenterY),
                                                1.f);

      // Inner Glow = the brighter glow at the center of the eye that falls off radially towards the edge of the eye
      // Outer Glow = the "halo" effect around the outside of the eye shape
      // Add inner glow to the eye shape, before we compute the outer glow, so that boundaries conditions match.
      if(kProcFace_HotspotRender)
      {
        ANKI_CPU_PROFILE("HotspotRender");

        const f32 sigmaX = kProcFace_HotspotFalloff*eyeWidth;
        const f32 sigmaY = kProcFace_HotspotFalloff*eyeHeight;
        
        // Compute the 2x2 inverse covariance matrix for the hotspot Gaussian falloff, incorporating
        // the scale and rotation from the eye and face warp (so it moves and rotates with the eyes)
        Matrix_2x2f sigmaInv;
        {
          const Matrix_2x2f W22{W(0,0), W(0,1), W(1,0), W(1,1)};
          Matrix_2x2f W22t = W22.GetTranspose();
          const Matrix_2x2f sigma{sigmaX, 0.f, 0.f, sigmaY};
          const Matrix_2x2f sigmaWarped(sigma * W22t * W22 * sigma);
          sigmaInv = sigmaWarped.GetInverse();
        }

        DEV_ASSERT_MSG(upperLeft.y()>=0 && bottomRight.y()<_eyeShape.GetNumRows(), "ProceduralFaceDrawer.DrawEye.BadRow", "%f %f", upperLeft.y(), bottomRight.y());
        DEV_ASSERT_MSG(upperLeft.x()>=0 && bottomRight.x()<_eyeShape.GetNumCols(), "ProceduralFaceDrawer.DrawEye.BadCol", "%f %f", upperLeft.x(), bottomRight.x());
        
        for(s32 i=upperLeft.y(); i<=bottomRight.y(); ++i) {

          u8* eyeShape_i = _eyeShape.GetRow(i);
          for(s32 j=upperLeft.x(); j<=bottomRight.x(); ++j) {

            u8& eyeValue  = eyeShape_i[j];
            const bool insideEye = (eyeValue > 0);
            if(insideEye) {
              // TODO: Use a separate approximation helper or LUT to get falloff
              const f32 dx = (f32)j-hotSpotCenter.x();
              const f32 dy = (f32)i-hotSpotCenter.y();
              
              // Hardcode simple 1x2 x 2x2 x 2x1 matrix multiplication here:
              //   [dx dy] * Sigma^(-1) * [dx]
              //                          [dy]
              const f32 x = ((dx*sigmaInv(0,0) + dy*sigmaInv(1,0))*dx +
                             (dx*sigmaInv(0,1) + dy*sigmaInv(1,1))*dy);
              
              const f32 falloff = fastExp(-0.5f*x);
              DEV_ASSERT_MSG(Util::InRange(falloff, 0.f, 1.f), "ProceduralFaceDrawer.DrawEye.BadInnerGlowFalloffValue", "%f", falloff);

              eyeValue = NUMERIC_CAST_CLAMPED(ROUND(static_cast<f32>(eyeValue) * falloff));
            }
          }
        }
      }
      
#if PROCEDURALFACE_GLOW_FEATURE
      Rectangle<s32> eyeBoundingBoxS32(upperLeft.CastTo<s32>(), bottomRight.CastTo<s32>());
      Vision::Image eyeShapeROI = _eyeShape.GetROI(eyeBoundingBoxS32);

      // Compute glow from the final eye shape (after lids are drawn)
      _glowImg.Allocate(faceImg.GetNumRows(), faceImg.GetNumCols());
      _glowImg.FillWith(0);

      const Value glowLightness = kProcFace_GlowLightnessMultiplier * faceData.GetParameter(whichEye, Parameter::GlowLightness);
      if(Util::IsFltGTZero(glowLightness) && Util::IsFltGTZero(glowFraction)) {
        ANKI_CPU_PROFILE("Glow");
        Vision::Image glowImgROI = _glowImg.GetROI(eyeBoundingBoxS32);

        s32 glowSizeX = std::ceil(glowFraction * 0.5f * scaledEyeWidth);
        s32 glowSizeY = std::ceil(glowFraction * 0.5f * scaledEyeHeight);

        // Make sure sizes are odd:
        if(glowSizeX % 2 == 0) {
          ++glowSizeX;
        }
        if(glowSizeY % 2 == 0) {
          ++glowSizeY;
        }

        switch(kProcFace_GlowFilter) {
          case (uint8_t)Filter::BoxFilter:
            cv::boxFilter(eyeShapeROI.get_CvMat_(), glowImgROI.get_CvMat_(), -1, cv::Size(glowSizeX,glowSizeY));
            break;
          case (uint8_t)Filter::GaussianFilter:
            cv::GaussianBlur(eyeShapeROI.get_CvMat_(), glowImgROI.get_CvMat_(), cv::Size(glowSizeX,glowSizeY),
                             (f32)glowSizeX, (f32)glowSizeY);
            break;
          case (uint8_t)Filter::NEONBoxFilter: {
            eyeShapeROI.BoxFilter(glowImgROI, 3);
            break;
          }
      }
#endif

      if(kProcFace_EnableAntiAliasing) {
        // Antialiasing (AFTER glow because it changes eyeShape, which we use to compute the glow above)

        // Optimization: if we're applying interpolation and transforming the face, skip antialiasing?
        ApplyAntiAliasing(_eyeShape, upperLeft.x(), upperLeft.y(), bottomRight.x(), bottomRight.y());
      }
        
      const f32 eyeLightness = faceData.GetParameter(whichEye, Parameter::Lightness);
      DEV_ASSERT(Util::InRange(eyeLightness, -1.f, 1.f), "ProceduralFaceDrawer.DrawEye.InvalidLightness");

      // Draw the eye into the face image, adding outer glow, noise, and stylized scanlines
      {
        ANKI_CPU_PROFILE("DrawEyePixels");

        DEV_ASSERT_MSG(upperLeft.y()>=0 && bottomRight.y()<faceImg.GetNumRows(), "ProceduralFaceDrawer.DrawEye.BadRow", "%f %f", upperLeft.y(), bottomRight.y());
        DEV_ASSERT_MSG(upperLeft.x()>=0 && bottomRight.x()<faceImg.GetNumCols(), "ProceduralFaceDrawer.DrawEye.BadCol", "%f %f", upperLeft.x(), bottomRight.x());
        for(s32 i=upperLeft.y(); i<=bottomRight.y(); ++i) {

          u8* faceImg_i = faceImg.GetRow(i);
          const u8* eyeShape_i = _eyeShape.GetRow(i);
#if PROCEDURALFACE_GLOW_FEATURE
          const u8* glowImg_i  = _glowImg.GetRow(i);
#endif

          for(s32 j=upperLeft.x(); j<=bottomRight.x(); ++j) {

            const u8 eyeValue = eyeShape_i[j];
#if PROCEDURALFACE_GLOW_FEATURE
            const u8 glowValue = glowImg_i[j];
            const bool somethingToDraw = (eyeValue > 0 || glowValue > 0);
#else
            const bool somethingToDraw = (eyeValue > 0);
#endif

            if(somethingToDraw) {
              f32 newValue = static_cast<f32>(eyeValue);

#if PROCEDURALFACE_GLOW_FEATURE
              // Combine everything together: inner glow falloff, and the glow value.
              // Note that the value in glowImg/eyeShape is already [0,255]
              newValue = std::max(newValue,eyeValue);

              const bool isPartOfEye = (eyeValue >= glowValue); // (and not part of glow)
              if(isPartOfEye) {
                newValue *= kProcFace_EyeLightnessMultiplier;
              } else {
                newValue *= glowLightness;
              }
#else
              newValue *= kProcFace_EyeLightnessMultiplier;
#endif

              newValue *= eyeLightness;

              // Put the final value into the face image
              // Note: If we're drawing the right eye, there may already be something in the image
              //       from when we drew the left eye (e.g. with large glow), so use max
              faceImg_i[j] = std::max(faceImg_i[j], NUMERIC_CAST_CLAMPED(ROUND(newValue)));
            }
          }
        }
      }
    }

    // Add distortion noise
    auto scanlineDistorter = faceData.GetScanlineDistorter();
    if(nullptr != scanlineDistorter) {
      scanlineDistorter->AddOffNoise(W, eyeHeight, eyeWidth, faceImg);
    }
      
  } // DrawEye()

  void ProceduralFaceDrawer::DrawFace(const ProceduralFace& faceData,
                                      const Util::RandomGenerator& rng,
                                      Vision::ImageRGB565& output)
  {
    ANKI_CPU_PROFILE("DrawFace");

    bool dirty = false; // set to true to force all stages to render, previous pipeline
    dirty = DrawEyes(faceData, dirty);
    dirty = ApplyScanlines(_faceCache.img8[_faceCache.finalFace], faceData.GetScanlineOpacity(), dirty);
    dirty = DistortScanlines(faceData, dirty);
    dirty = ApplyNoise(rng, dirty);
    dirty = ConvertColorspace(faceData, output, dirty);
    dirty = ApplyCustomOverlay(faceData, output, dirty);
    dirty = ApplyStarlight(output, dirty); // JamesOS starlight
    dirty = ApplyJamesHearts(output, dirty); // JamesOS hearts
    dirty = ApplyJamesExtras(output, dirty); // JamesOS theme, sleep screen, confetti
  } // DrawFace()

  bool ProceduralFaceDrawer::DrawEyes(const ProceduralFace& faceData, bool dirty)
  {
    ANKI_CPU_PROFILE("DrawEyes");

    if(!dirty) {
      
      if (_faceCache.faceData.GetParameters(WhichEye::Left) != faceData.GetParameters(WhichEye::Left) ||
          _faceCache.faceData.GetParameters(WhichEye::Right) != faceData.GetParameters(WhichEye::Right) ||
          _faceCache.faceData.GetFaceAngle() != faceData.GetFaceAngle() ||
          _faceCache.faceData.GetFacePosition() != faceData.GetFacePosition() ||
          _faceCache.faceData.GetFaceScale() != faceData.GetFaceScale() ||
          !Util::IsFltNear(_faceCache.faceData.GetHue(), faceData.GetHue()) ||
          !Util::IsFltNear(_faceCache.faceData.GetSaturation(), faceData.GetSaturation())) {
        // Something changed, we must draw
        dirty = true;
      }
    }

    if(dirty) {
      // Update parameters used to generate this cached image
      _faceCache.faceData.SetParameters(WhichEye::Left, faceData.GetParameters(WhichEye::Left));
      _faceCache.faceData.SetParameters(WhichEye::Right, faceData.GetParameters(WhichEye::Right));

      _faceCache.faceData.SetFaceAngle(faceData.GetFaceAngle());
      _faceCache.faceData.SetFacePosition(faceData.GetFacePosition());
      _faceCache.faceData.SetFaceScale(faceData.GetFaceScale());
      
      // Target image for this stage
      // Eyes are always first, assign first element in face cache
      _faceCache.finalFace = _faceCache.eyes = 0;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer.DistortScanlines.FaceCacheTooSmall");
      _faceCache.img8[_faceCache.eyes].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH); // Will do nothing if already the right size
      _faceCache.img8[_faceCache.eyes].FillWith(0);

      // Create a full-face warp matrix if needed and provide it to the eye-rendering call
      const Matrix_3x3f* W_facePtr = nullptr;
      Matrix_3x3f W_face;
      const bool hasFaceTransform = (!Util::IsNearZero(_faceCache.faceData.GetFaceAngle()) ||
                                     !(_faceCache.faceData.GetFacePosition() == 0) ||
                                     !(_faceCache.faceData.GetFaceScale() == 1.f));
      if(hasFaceTransform)
      {
        W_face = GetTransformationMatrix(faceData.GetFaceAngle(),
                                         faceData.GetFaceScale().x(),
                                         faceData.GetFaceScale().y(),
                                         faceData.GetFacePosition().x(),
                                         faceData.GetFacePosition().y(),
                                         static_cast<f32>(ProceduralFace::WIDTH)*0.5f,
                                         static_cast<f32>(ProceduralFace::HEIGHT)*0.5f);
        W_facePtr = &W_face;
      }
      
      DrawEye(_faceCache.faceData, WhichEye::Left,  W_facePtr, _faceCache.img8[_faceCache.eyes], _leftBBox);
      DrawEye(_faceCache.faceData, WhichEye::Right, W_facePtr, _faceCache.img8[_faceCache.eyes], _rightBBox);
      
      const std::array<Quad2f,2> leftRightQuads{{ Quad2f(_leftBBox), Quad2f(_rightBBox) }};
      
      _faceRowMin = ProceduralFace::HEIGHT-1;
      _faceRowMax = 0;
      _faceColMin = _leftBBox.GetX();
      _faceColMax = _rightBBox.GetXmax();
      
      for(const auto& quad : leftRightQuads) {
        for(const auto& pt : quad) {
          _faceRowMin = std::min(_faceRowMin, (s32)std::floor(pt.y()));
          _faceRowMax = std::max(_faceRowMax, (s32)std::ceil(pt.y()));
          _faceColMin = std::min(_faceColMin, (s32)std::floor(pt.x()));
          _faceColMax = std::max(_faceColMax, (s32)std::ceil(pt.x()));
        }
      }
     
      // Just to be safe:
      _faceColMin = Util::Clamp(_faceColMin, 0, ProceduralFace::WIDTH-1);
      _faceColMax = Util::Clamp(_faceColMax, 0, ProceduralFace::WIDTH-1);
      _faceRowMin = Util::Clamp(_faceRowMin, 0, ProceduralFace::HEIGHT-1);
      _faceRowMax = Util::Clamp(_faceRowMax, 0, ProceduralFace::HEIGHT-1);

      _faceCache.finalFace = _faceCache.eyes;
    }
    
    return dirty;
  } // DrawEyes()

  bool ProceduralFaceDrawer::DistortScanlines(const ProceduralFace& faceData, bool dirty)
  {
    ANKI_CPU_PROFILE("DistortScanlines");

    // Distort the scanlines
    auto scanlineDistorter = faceData.GetScanlineDistorter();
    if(nullptr != scanlineDistorter) {
      // Any scanline distorter affects the output image, assigned a new element in the face cache
      // and make all later stages needed updates

      // Note: scanline distortion has changed from modifying the input image to generating a new
      //       output image, this allows eye rendering and face transforms to be cached and the
      //       scanline distortion applied to the original output rather than a face that has already
      //       had scanline distortion applied.
      dirty = true;

      _faceCache.finalFace = _faceCache.distortedFace = _faceCache.eyes+1;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer, face cache too small.");
      _faceCache.img8[_faceCache.distortedFace].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
      _faceCache.img8[_faceCache.distortedFace].FillWith(0);

      s32 newColMin = _faceColMin;
      s32 newColMax = _faceColMax;

      const f32 scale = 1.f / (_faceRowMax - _faceRowMin);
      for(s32 row=_faceRowMin; row < _faceRowMax; ++row) {
        const f32 eyeFrac = (row - _faceRowMin) * scale;
        const s32 shift = scanlineDistorter->GetEyeDistortionAmount(eyeFrac) * sizeof(Vision::PixelRGB);

        if(shift < 0) {
          const u8* faceImg_row = _faceCache.img8[_faceCache.eyes].GetRow(row);
          u8* img_row = _faceCache.img8[_faceCache.distortedFace].GetRow(row);
          memcpy(img_row, faceImg_row-shift, ProceduralFace::WIDTH+shift);

          if(_faceColMin+shift < newColMin) {
            // shift left increases face bounding box, update new bounds,
            // but keep existing bounds for the rest of the effect
            newColMin = _faceColMin+shift;
          }
        } else if(shift > 0) {
          const u8* faceImg_row = _faceCache.img8[_faceCache.eyes].GetRow(row);
          u8* img_row = _faceCache.img8[_faceCache.distortedFace].GetRow(row);
          memcpy(img_row+shift, faceImg_row, ProceduralFace::WIDTH-shift);

          if(_faceColMax+shift > newColMax) {
            // shift right increases face bounding box, update new bounds,
            // but keep existing bounds for the rest of the effect
            newColMax = _faceColMax+shift;
          }
        }
      }

      _faceColMin = newColMin;
      _faceColMax = newColMax;
    } else {
      // No scanline distortion, pass forward the cached face transform as output
      _faceCache.finalFace = _faceCache.distortedFace = _faceCache.eyes;
    }

    return dirty;
  } // DistortScanlines()

  bool ProceduralFaceDrawer::ApplyNoise(const Util::RandomGenerator& rng, bool dirty)
  {
    if(PROCEDURALFACE_NOISE_FEATURE && (kProcFace_NoiseNumFrames > 0)) {
      ANKI_CPU_PROFILE("ApplyNoise");

      _faceCache.finalFace = _faceCache.distortedFace+1;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer, face cache too small.");
      _faceCache.img8[_faceCache.finalFace].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
      if(dirty) {
        _faceCache.img8[_faceCache.finalFace].FillWith(0);
      }

      dirty = true;

      // Assign a new face cache for noise, the reason for the changes.
      // If the eyes haven't changed, the face hasn't changed and no-scanline distorter
      // then there is no work to do until the noise stage

      // TODO: https://ankiinc.atlassian.net/browse/VIC-3647 Apply noise to eyes individually rather than entire face
      // ^ might not be desirable with the neon version. May incur cache penalty when pulling images from memory twice
      // instead of just once...
      
      const Array2d<u8>& noiseImg = GetNoiseImage(rng);

      for(s32 i=_faceRowMin; i<=_faceRowMax; ++i) {

        const u8* noiseImg_i = noiseImg.GetRow(i);
        const u8* eyeShape_i = _faceCache.img8[_faceCache.distortedFace].GetRow(i);
        u8* faceImg_i = _faceCache.img8[_faceCache.finalFace].GetRow(i);

        noiseImg_i += _faceColMin;
        eyeShape_i += _faceColMin;
        faceImg_i += _faceColMin;

        s32 j = _faceColMin;
#ifdef __ARM_NEON__
        const s32 kNumElementsProcessed = 16;
        for(; j <= _faceColMax-(kNumElementsProcessed-1); j += kNumElementsProcessed)
        {
          uint8x16_t eye = vld1q_u8(eyeShape_i);
          eyeShape_i += kNumElementsProcessed;

          uint8x16_t noise = vld1q_u8(noiseImg_i);
          noiseImg_i += kNumElementsProcessed;

          // Multiply eye values by noise and expand to u16
          uint16x8_t value1 = vmull_u8(vget_low_u8(eye), vget_low_u8(noise));
          uint16x8_t value2 = vmull_u8(vget_high_u8(eye), vget_high_u8(noise));
          // Saturating narrowing right shift by 7 (divide by 128)
          uint8x8_t output1 = vqshrn_n_u16(value1, 7);
          uint8x8_t output2 = vqshrn_n_u16(value2, 7);
          // Combine back into u8x16
          uint8x16_t output = vcombine_u8(output1, output2);

          vst1q_u8(faceImg_i, output);
          faceImg_i += kNumElementsProcessed;
        }
#endif
     
        for(; j<=_faceColMax; ++j) {
          *faceImg_i = Util::numeric_cast_clamped<u8>((static_cast<u16>(*eyeShape_i) * static_cast<u16>(*noiseImg_i)) >> 7);
          ++noiseImg_i;
          ++eyeShape_i;
          ++faceImg_i;
        }
      }
    }

    return dirty;
  } // ApplyNoise()

bool ProceduralFaceDrawer::ConvertColorspace(const ProceduralFace& faceData,
                                             Vision::ImageRGB565& output,
                                             bool dirty)
{
  ANKI_CPU_PROFILE("ConvertColorspace");

  output.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  output.FillWith(Vision::PixelRGB(0,0,0));

  if(!dirty) {
    if(!Util::IsFltNear(_faceCache.faceData.GetHue(), faceData.GetHue()) ||
       !Util::IsFltNear(_faceCache.faceData.GetSaturation(), faceData.GetSaturation()))
      dirty = true;
  }

  if(dirty)
  {
    _faceCache.faceData.SetHue(faceData.GetHue());
    _faceCache.faceData.SetSaturation(faceData.GetSaturation());

    const u8 drawHue = ROUND(255.f*JamesMoodHue(faceData.GetHue())); // JamesOS mood colors

    f32 satF = 1.f;
#if PROCEDURALFACE_ANIMATED_SATURATION
    satF *= faceData.GetParameter(whichEye, Parameter::Saturation);
#endif
#if PROCEDURALFACE_PROCEDURAL_SATURATION
    satF *= faceData.GetSaturation();
#endif
    const u8 drawSat = ROUND(255.f*JamesThemeSat(satF)); // JamesOS themes

    Rectangle<s32> roiR(_faceColMin,_faceRowMin,
                        _faceColMax-_faceColMin+1,
                        _faceRowMax-_faceRowMin+1);

    Vision::ImageRGB565 outROI = output.GetROI(roiR);
    _faceCache.img8[_faceCache.finalFace]
            .GetROI(roiR)
            .ConvertV2RGB565(drawHue,drawSat, outROI);
  }

  return dirty;
}

// ============================================================================
// JamesOS starlight eyes
// JamesOS eye effects (v17):
//   - starlight sparkles (white / rainbow / shooting stars)
//   - galaxy eyes (or hearts) while petted, pet counter + rainbow celebration every 10 pets
//   - mood-tinted eye color
//   - themes: normal / halloween (orange flickering eyes, bats, spooky sparkles)
//   - sleep screen while sleeping on the charger: drifting stars and/or a clock
//   - sleepy eyes on low battery, badges (achievements) with a pop-up
//
// Switches live in /data/data/jamesos on the robot (the COOL FEATURES menu and
// jamesos.sh write them). Everything is re-checked about every 2 seconds, so
// changes apply without a restart. With no switch files, the face is exactly
// like normal WireOS.
//   starlight.conf   sparkle settings (exists = on)    starlight.off   sparkles off
//   starlight.style  white | rainbow | shooting
//   hearts.on        petting effect on                  mood.on         mood colors
//   petfx            galaxy (default) | hearts
//   theme            normal | halloween | christmas | lunar | auto (picks by date)
//   sleepscreen      stars | clock | both
//   pets.txt         "<yyyymmdd> <pets today>"  (written by Vector)
//   sleepy.off       no sleepy eyes on low battery (default: on)
//   stats.txt        total pets, days-in-a-row streak (written by Vector)
//   badges.txt       unlocked badges, "<KEY> <yyyymmdd>" per line (written by Vector)
//   /dev/shm/jamesos_mood  (vic-engine writes Vector's mood here)
//   shiny            "<yyyymmdd> 0|1|test"  today's 1-in-4096 shiny roll (v23)
//   bluemoon.test    pretend it's a blue moon night (testing, no badge) (v23)
// ============================================================================
// v23: JamesOS private extras (Vector family, prototype). 1 only in James's own builds:
// the installer turns it on when jamesos/private.on exists (that folder never goes to GitHub).
#define JAMESOS_PRIVATE 0
namespace {
  const char* kJamesStarlightFile    = "/data/data/jamesos/starlight.conf";
  const char* kJamesStarlightOffFlag = "/data/data/jamesos/starlight.off";
  const char* kJamesStarlightStyle   = "/data/data/jamesos/starlight.style";
  const char* kJamesHeartsOnFlag     = "/data/data/jamesos/hearts.on";
  const char* kJamesMoodOnFlag       = "/data/data/jamesos/mood.on";
  const char* kJamesThemeFile        = "/data/data/jamesos/theme";
  const char* kJamesHolidaysFile     = "/data/data/jamesos/holidays";   // all (default) | western | off
  const char* kJamesFunEyesOffFlag   = "/data/data/jamesos/funeyes.off"; // v23: no shaped eyes (pumpkin, heart, flag, year)
  const char* kJamesFunEyesAutoFlag  = "/data/data/jamesos/funeyes.auto"; // v24: shaped eyes only on the holiday itself
  const char* kJamesShinyFile        = "/data/data/jamesos/shiny";          // v23: today's shiny roll
  const char* kJamesBlueMoonTestFile = "/data/data/jamesos/bluemoon.test";  // v23: test a blue moon night
  const char* kJamesSeenVersionFile  = "/data/data/jamesos/seen_version";   // v23h: last update he announced
  const char* kJamesSleepFile        = "/data/data/jamesos/sleepscreen";
  const char* kJamesPetsFile         = "/data/data/jamesos/pets.txt";
  const char* kJamesMoodFile         = "/dev/shm/jamesos_mood";
  const u8    kJamesStarMaskThreshold = 64; // only draw where the eye is lit
  const s32   kJamesPetGoal = 10;           // celebrate every 10 pets

  enum class JamesStarStyle { White, Rainbow, Shooting, Gold };
  // (new themes go at the END: the number of a theme is saved in own_eyes)
  enum class JamesTheme { Normal, Halloween, Christmas, Lunar, Glitch, Retro, GalaxyMax,
                          NewYear, Valentine, StPatrick, AprilFools, Easter, MothersDay, Fireworks, Thanksgiving };
  enum class JamesSleepMode { Off, Stars, Clock, Both };

  struct JamesStarlightConfig {
    bool enabled  = false;
    s32  maxStars = 3;      // how many sparkles at once
    f32  chance   = 0.04f;  // chance per frame (~30 frames/sec) to start a new sparkle
    s32  life     = 24;     // how many frames one sparkle lasts
    s32  size     = 2;      // arm length of the sparkle (1-4 pixels)
    u8   r = 255, g = 255, b = 255;
    JamesStarStyle style = JamesStarStyle::White;
  };

  struct JamesStar {
    f32 x, y, vx, vy;
    s32 age, life;
    u8  r, g, b;
  };

  // shared between the robot-state message handler and the face drawer
  std::atomic<bool>  gJamesPetting{false};
  std::atomic<float> gJamesBattV{0.f};
  std::atomic<int>   gJamesTempC{0};
  std::atomic<bool>  gJamesOnCharger{false};
  std::atomic<bool>  gJamesCharging{false};
  std::atomic<int>   gJamesHour{-1};
  std::atomic<int>   gJamesMinute{0};
  std::atomic<int>   gJamesDateKey{0};
  std::atomic<int>   gJamesPetsToday{0};
  std::atomic<int>   gJamesPetsDate{0};
  std::atomic<int>   gJamesCelebrate{0};   // >0 = start a celebration
  std::atomic<long long> gJamesTickleUntilMs{0};  // tickle mode (blush + steam) until this time
  std::atomic<long long> gJamesShyCandidateMs{0}; // one short gentle pet ended at this time
  std::atomic<long long> gJamesPetStartMs{0};
  bool gJamesStarSolid = false;   // only while drawing gold sparkles
  std::atomic<int>   gJamesTheme{0};       // JamesTheme
  std::atomic<int>   gJamesHolidayMask{3}; // which holidays: 1 = western, 2 = Chinese New Year (3 = all, 0 = off)
  std::atomic<bool>  gJamesHolidayDayOnly{false}; // v25: EXTRAS -> HOLIDAY TIME -> DAY
  std::atomic<int>   gJamesFunEyesMode{2}; // v24: EXTRAS -> FUN EYES: 0 = off, 1 = auto (the day itself), 2 = on (whole holiday)
  std::atomic<bool>  gJamesShiny{false};       // v23: a shiny day (1 in 4096)
  std::atomic<bool>  gJamesShinyTest{false};   // v23: shiny forced for a test (no badge)
  std::atomic<bool>  gJamesBlueMoon{false};    // v23: blue moon night right now (6 PM - 6 AM)
  std::atomic<bool>  gJamesBlueMoonTest{false};// v23: blue moon forced for a test (no badge)
  std::atomic<bool>  gJamesOwnEyes{false}; // you picked an eye color (voice/app) while a theme is on: your color wins
  const char* kJamesOwnEyesFile = "/data/data/jamesos/own_eyes";   // remembers that (per theme) across restarts
  std::atomic<int>   gJamesSleepMode{0};   // JamesSleepMode
  std::mutex         gJamesPetsMtx;
  bool               gJamesPetsLoaded = false;

  bool JamesFileExists(const char* path)
  {
    struct stat st;
    return stat(path, &st) == 0;
  }

  long JamesFileMtime(const char* path)
  {
    struct stat st;
    return (stat(path, &st) == 0) ? (long)st.st_mtime : 0;
  }

  std::string JamesTrim(const std::string& s)
  {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) { return ""; }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
  }

  std::string JamesReadWord(const char* path)
  {
    std::ifstream in(path);
    std::string word;
    if (in.is_open()) {
      std::getline(in, word);
    }
    return JamesTrim(word);
  }

  JamesStarStyle JamesReadStyle()
  {
    const std::string word = JamesReadWord(kJamesStarlightStyle);
    if (word == "rainbow")  { return JamesStarStyle::Rainbow; }
    if (word == "shooting") { return JamesStarStyle::Shooting; }
    if (word == "gold")     { return JamesStarStyle::Gold; }      // LOYAL FRIEND reward
    return JamesStarStyle::White;
  }

  // days since 1970-01-01 for a yyyymmdd date (no time zones involved)
  int JamesDayNumber(int key)
  {
    int y = key / 10000, m = (key / 100) % 100, d = key % 100;
    y -= (m <= 2) ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
  }

  // ---------------- JamesOS holiday calendar (v24: ONE calendar) ----------------
  // The dates live in util/jamesos/jamesosSeasonal.h. The boot animation, the first screen,
  // the eyes and the back lights all use it, so every screen agrees on which holiday it is.
  jamesos::Date JamesCalDate(int key) { return jamesos::Date{key / 10000, (key / 100) % 100, key % 100}; }
  int JamesCalKey(const jamesos::Date& d) { return d.y * 10000 + d.m * 100 + d.d; }

  struct JamesHoliday {
    const char* name;        // shown in the countdown banner
    const char* greet;       // first word on the day itself
    JamesTheme theme;
    int list;                // 1 = western, 2 = Chinese New Year (HOLIDAYS -> WESTERN leaves it out)
    jamesos::Theme cal;      // its day in the shared calendar
    u8 r, g, b;              // banner color
  };
  const JamesHoliday kJamesHolidays[] = {
    {"NEW YEAR",          "HAPPY",  JamesTheme::NewYear,      1, jamesos::Theme::NewYear,      255, 210,  80},
    {"VALENTINE'S DAY",   "HAPPY",  JamesTheme::Valentine,    1, jamesos::Theme::Valentine,    255, 110, 170},
    {"ST PATRICK'S DAY",  "HAPPY",  JamesTheme::StPatrick,    1, jamesos::Theme::StPatrick,     90, 230, 110},
    {"APRIL FOOLS",       "HAPPY",  JamesTheme::AprilFools,   1, jamesos::Theme::AprilFools,   120, 220, 255},
    {"EASTER",            "HAPPY",  JamesTheme::Easter,       1, jamesos::Theme::Easter,       220, 180, 255},
    {"MOTHER'S DAY",      "HAPPY",  JamesTheme::MothersDay,   1, jamesos::Theme::Mothers,      255, 150, 190},
    {"JULY 4TH",          "HAPPY",  JamesTheme::Fireworks,    1, jamesos::Theme::July4,         90, 140, 255},
    {"THANKSGIVING",      "HAPPY",  JamesTheme::Thanksgiving, 1, jamesos::Theme::Thanksgiving, 255, 150,  50},
    {"CHINESE NEW YEAR",  "HAPPY",  JamesTheme::Lunar,        2, jamesos::Theme::Lunar,        255,  80,  60},
    {"HALLOWEEN",         "HAPPY",  JamesTheme::Halloween,    1, jamesos::Theme::Halloween,    255, 130,  30},
    {"CHRISTMAS",         "MERRY",  JamesTheme::Christmas,    1, jamesos::Theme::Christmas,    120, 230, 140},
  };

  // calendar theme -> eye theme (Fall and Father's Day have no eye theme yet: normal eyes)
  JamesTheme JamesEyeTheme(jamesos::Theme t)
  {
    switch (t) {
      case jamesos::Theme::NewYear:      return JamesTheme::NewYear;
      case jamesos::Theme::Lunar:        return JamesTheme::Lunar;
      case jamesos::Theme::Valentine:    return JamesTheme::Valentine;
      case jamesos::Theme::StPatrick:    return JamesTheme::StPatrick;
      case jamesos::Theme::AprilFools:   return JamesTheme::AprilFools;
      case jamesos::Theme::Easter:       return JamesTheme::Easter;
      case jamesos::Theme::Mothers:      return JamesTheme::MothersDay;
      case jamesos::Theme::July4:        return JamesTheme::Fireworks;
      case jamesos::Theme::Halloween:    return JamesTheme::Halloween;
      case jamesos::Theme::Thanksgiving: return JamesTheme::Thanksgiving;
      case jamesos::Theme::Christmas:    return JamesTheme::Christmas;
      default:                           return JamesTheme::Normal;
    }
  }

  // "auto" theme: the shared calendar (HOLIDAYS -> WESTERN = no Chinese New Year, OFF = none)
  JamesTheme JamesThemeForDate(int today)
  {
    const int mask = gJamesHolidayMask;
    if (today == 0 || mask == 0) { return JamesTheme::Normal; }
    return JamesEyeTheme(jamesos::PickTheme(JamesCalDate(today), (mask & 2) != 0, gJamesHolidayDayOnly));
  }

  // the next holiday within 7 days: its index, and in how many days (0 = today). false = none
  bool JamesNextHoliday(int today, int& index, int& days)
  {
    const int mask = gJamesHolidayMask;
    if (today == 0 || mask == 0) { return false; }
    const int dn = JamesDayNumber(today), year = today / 10000;
    index = -1; days = 99;
    for (int i = 0; i < (int)(sizeof(kJamesHolidays) / sizeof(kJamesHolidays[0])); ++i) {
      const auto& h = kJamesHolidays[i];
      if ((h.list & mask) == 0) { continue; }
      for (int y = year; y <= year + 1; ++y) {
        jamesos::Date day;
        if (!jamesos::MainDay(h.cal, y, day)) { continue; }
        const int diff = JamesDayNumber(JamesCalKey(day)) - dn;
        if (diff >= 0 && diff <= 7 && diff < days) { days = diff; index = i; }
      }
    }
    return index >= 0;
  }

  // v24 FUN EYES: ON = the whole holiday, AUTO = only on the holiday itself (Oct 31, Feb 14...), OFF = never
  bool JamesFunEyesToday(JamesTheme theme, int today)
  {
    const int mode = gJamesFunEyesMode;
    if (mode != 1) { return mode == 2; }
    for (const auto& h : kJamesHolidays) {
      jamesos::Date day;
      if (h.theme == theme && jamesos::MainDay(h.cal, today / 10000, day) && JamesCalKey(day) == today) { return true; }
    }
    return false;
  }

  void JamesRefreshThemeAndSleep()
  {
    {
      const std::string hl = JamesReadWord(kJamesHolidaysFile);
      gJamesHolidayMask = (hl == "western") ? 1 : (hl == "off") ? 0 : 3;   // v24: "asian" is gone (counts as all)
      gJamesHolidayDayOnly = (JamesReadWord(jamesos::kHolidayTimeFile) == "day");  // v25
    }
    gJamesFunEyesMode = JamesFileExists(kJamesFunEyesOffFlag) ? 0 : (JamesFileExists(kJamesFunEyesAutoFlag) ? 1 : 2);
    const std::string theme = JamesReadWord(kJamesThemeFile);
    JamesTheme t = JamesTheme::Normal;
    if (theme == "halloween")      { t = JamesTheme::Halloween; }
    else if (theme == "christmas") { t = JamesTheme::Christmas; }
    else if (theme == "lunar")     { t = JamesTheme::Lunar; }
    else if (theme == "auto" || theme.empty()) { t = JamesThemeForDate(gJamesDateKey); }   // v22: auto is the default
    else if (theme == "newyear")      { t = JamesTheme::NewYear; }
    else if (theme == "valentine")    { t = JamesTheme::Valentine; }
    else if (theme == "stpatrick")    { t = JamesTheme::StPatrick; }
    else if (theme == "aprilfools")   { t = JamesTheme::AprilFools; }
    else if (theme == "easter")       { t = JamesTheme::Easter; }
    else if (theme == "mothersday")   { t = JamesTheme::MothersDay; }
    else if (theme == "fireworks")    { t = JamesTheme::Fireworks; }
    else if (theme == "thanksgiving") { t = JamesTheme::Thanksgiving; }
    else if (theme == "glitch")    { t = JamesTheme::Glitch; }     // secret themes
    else if (theme == "retro")     { t = JamesTheme::Retro; }
    else if (theme == "galaxymax") { t = JamesTheme::GalaxyMax; }
    gJamesTheme = (int)t;
    const std::string sleep = JamesReadWord(kJamesSleepFile);
    JamesSleepMode mode = JamesSleepMode::Off;
    if (sleep == "stars")      { mode = JamesSleepMode::Stars; }
    else if (sleep == "clock") { mode = JamesSleepMode::Clock; }
    else if (sleep == "both")  { mode = JamesSleepMode::Both; }
    gJamesSleepMode = (int)mode;
  }

  bool JamesIsHalloween() { return gJamesTheme == (int)JamesTheme::Halloween; }
  bool JamesIsChristmas() { return gJamesTheme == (int)JamesTheme::Christmas; }
  bool JamesIsLunar()     { return gJamesTheme == (int)JamesTheme::Lunar; }
  bool JamesIsGlitch()    { return gJamesTheme == (int)JamesTheme::Glitch; }
  bool JamesIsRetro()     { return gJamesTheme == (int)JamesTheme::Retro; }
  bool JamesIsGalaxyMax() { return gJamesTheme == (int)JamesTheme::GalaxyMax; }
  bool JamesIsTheme(JamesTheme t) { return gJamesTheme == (int)t; }

  long long JamesNowMs()
  {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  JamesStarlightConfig JamesLoadStarlightConfig()
  {
    JamesStarlightConfig c;
    std::ifstream in(kJamesStarlightFile);
    if (!in.is_open()) {
      return c; // no file = off
    }
    c.enabled = true; // file exists = on (unless it says enabled=0)
    std::string line;
    while (std::getline(in, line)) {
      line = JamesTrim(line);
      if (line.empty() || line[0] == '#') { continue; }
      const auto eq = line.find('=');
      if (eq == std::string::npos) { continue; }
      const std::string key = JamesTrim(line.substr(0, eq));
      const std::string val = JamesTrim(line.substr(eq + 1));
      if (key == "enabled") {
        c.enabled = (std::atoi(val.c_str()) != 0);
      } else if (key == "stars") {
        c.maxStars = Util::Clamp(std::atoi(val.c_str()), 0, 20);
      } else if (key == "chance") {
        c.chance = Util::Clamp((f32)std::atof(val.c_str()), 0.f, 1.f);
      } else if (key == "life") {
        c.life = Util::Clamp(std::atoi(val.c_str()), 4, 150);
      } else if (key == "size") {
        c.size = Util::Clamp(std::atoi(val.c_str()), 1, 4);
      } else if (key == "color") {
        int r = 255, g = 255, b = 255;
        if (std::sscanf(val.c_str(), "%d , %d , %d", &r, &g, &b) == 3) {
          c.r = (u8)Util::Clamp(r, 0, 255);
          c.g = (u8)Util::Clamp(g, 0, 255);
          c.b = (u8)Util::Clamp(b, 0, 255);
        }
      }
    }
    return c;
  }

  // hue 0..1 (full saturation, full brightness) -> RGB
  void JamesHueToRGB(f32 hue, u8& r, u8& g, u8& b)
  {
    hue = hue - std::floor(hue);
    const f32 h6 = hue * 6.f;
    const s32 sector = (s32)h6;
    const f32 f = h6 - (f32)sector;
    const u8 up = (u8)(255.f * f);
    const u8 down = (u8)(255.f * (1.f - f));
    switch (sector % 6) {
      case 0:  r = 255;  g = up;   b = 0;    break;
      case 1:  r = down; g = 255;  b = 0;    break;
      case 2:  r = 0;    g = 255;  b = up;   break;
      case 3:  r = 0;    g = down; b = 255;  break;
      case 4:  r = up;   g = 0;    b = 255;  break;
      default: r = 255;  g = 0;    b = down; break;
    }
  }

  // full HSV (0..1 each) -> RGB 0..255
  void JamesHsvToRGB(f32 h, f32 sat, f32 val, f32& r, f32& g, f32& b)
  {
    h = (h - std::floor(h)) * 6.f;
    const s32 i = (s32)h;
    const f32 f = h - (f32)i;
    const f32 p = val * (1.f - sat), q = val * (1.f - sat * f), t = val * (1.f - sat * (1.f - f));
    switch (i % 6) {
      case 0:  r = val; g = t;   b = p;   break;
      case 1:  r = q;   g = val; b = p;   break;
      case 2:  r = p;   g = val; b = t;   break;
      case 3:  r = p;   g = q;   b = val; break;
      case 4:  r = t;   g = p;   b = val; break;
      default: r = val; g = p;   b = q;   break;
    }
    r *= 255.f; g *= 255.f; b *= 255.f;
  }

  bool JamesInBounds(const Vision::ImageRGB565& output, const Vision::Image& mask, s32 x, s32 y)
  {
    return x >= 0 && y >= 0 &&
           y < output.GetNumRows() && x < output.GetNumCols() &&
           y < mask.GetNumRows()   && x < mask.GetNumCols();
  }

  // Brighten one pixel toward a color, only inside the lit eye (never darkens)
  void JamesDrawStarPixel(Vision::ImageRGB565& output, Vision::Image& mask,
                          s32 x, s32 y, f32 intensity, u8 cr, u8 cg, u8 cb)
  {
    if (!JamesInBounds(output, mask, x, y)) { return; }
    if (mask.GetRow(y)[x] < kJamesStarMaskThreshold) {
      return; // outside the eye (or the eye is blinking) - don't draw
    }
    intensity = Util::Clamp(intensity, 0.f, 1.f);
    Vision::PixelRGB565& px = output.GetRow(y)[x];
    if (gJamesStarSolid) {
      // gold sparkles: paint the color (brightening would turn gold into white on a bright eye)
      px.SetValue((u8)(px.r() + (cr - (f32)px.r()) * intensity), (u8)(px.g() + (cg - (f32)px.g()) * intensity),
                  (u8)(px.b() + (cb - (f32)px.b()) * intensity));
      return;
    }
    const u8 R = std::max<u8>(px.r(), (u8)(cr * intensity));
    const u8 G = std::max<u8>(px.g(), (u8)(cg * intensity));
    const u8 B = std::max<u8>(px.b(), (u8)(cb * intensity));
    px.SetValue(R, G, B);
  }

  // Brighten one pixel anywhere on the screen (for confetti, night sky, clock)
  void JamesLightenPixel(Vision::ImageRGB565& output, s32 x, s32 y, f32 intensity, u8 cr, u8 cg, u8 cb)
  {
    if (x < 0 || y < 0 || y >= output.GetNumRows() || x >= output.GetNumCols()) { return; }
    intensity = Util::Clamp(intensity, 0.f, 1.f);
    Vision::PixelRGB565& px = output.GetRow(y)[x];
    px.SetValue(std::max<u8>(px.r(), (u8)(cr * intensity)),
                std::max<u8>(px.g(), (u8)(cg * intensity)),
                std::max<u8>(px.b(), (u8)(cb * intensity)));
  }

  // ---- pet counter (saved to /data so it survives restarts) ----
  void JamesLoadPetsLocked()
  {
    if (gJamesPetsLoaded) { return; }
    gJamesPetsLoaded = true;
    std::ifstream in(kJamesPetsFile);
    int date = 0, count = 0;
    if (in.is_open() && (in >> date >> count)) {
      gJamesPetsDate = date;
      gJamesPetsToday = std::max(0, count);
    }
  }

  void JamesSavePetsLocked()
  {
    mkdir("/data/data/jamesos", 0777);
    std::ofstream out(kJamesPetsFile, std::ios::trunc);
    if (out.is_open()) {
      out << gJamesPetsDate.load() << " " << gJamesPetsToday.load() << "\n";
    }
  }

  // ---- tiny 5x7 font for the night clock ----
  const u8* JamesGlyph(char c)
  {
    static const u8 kDigits[10][7] = {
      {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
      {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
      {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
      {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
      {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
    static const u8 kColon[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
    static const u8 kA[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const u8 kP[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    static const u8 kM[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    if (c >= '0' && c <= '9') { return kDigits[c - '0']; }
    if (c == ':') { return kColon; }
    if (c == 'A') { return kA; }
    if (c == 'P') { return kP; }
    if (c == 'M') { return kM; }
    return nullptr;
  }

  s32 JamesTextWidth(const std::string& s, s32 scale)
  {
    return s.empty() ? 0 : (s32)s.size() * 6 * scale - scale;
  }

  void JamesDrawText(Vision::ImageRGB565& output, const std::string& s, s32 x0, s32 y0, s32 scale,
                     f32 intensity, u8 cr, u8 cg, u8 cb)
  {
    s32 x = x0;
    for (char c : s) {
      const u8* g = JamesGlyph(c);
      if (g != nullptr) {
        for (s32 row = 0; row < 7; ++row) {
          for (s32 col = 0; col < 5; ++col) {
            if (g[row] & (0x10 >> col)) {
              for (s32 dy = 0; dy < scale; ++dy) {
                for (s32 dx = 0; dx < scale; ++dx) {
                  JamesLightenPixel(output, x + col * scale + dx, y0 + row * scale + dy, intensity, cr, cg, cb);
                }
              }
            }
          }
        }
      }
      x += 6 * scale;
    }
  }

  // ---- little bat for the Halloween theme (two wing positions) ----
  const s32 kJamesBatW = 13, kJamesBatH = 6;
  const char* kJamesBat[2][6] = {
    { "X...........X",     // wings up
      "XX...X.X...XX",
      "XXX..XXX..XXX",
      "XXXXXXXXXXXXX",
      ".XX.XXXXX.XX.",
      "......X......" },
    { ".....X.X.....",     // wings down
      ".....XXX.....",
      "..XXXXXXXXX..",
      ".XXXXXXXXXXX.",
      "XX..XXXXX..XX",
      "X.....X.....X" } };
} // anonymous namespace

// ---------------------------------------------------------------- JamesOS badges (achievements)
namespace {
  const char* kJamesSleepyOffFlag = "/data/data/jamesos/sleepy.off";
  const char* kJamesStatsFile     = "/data/data/jamesos/stats.txt";
  const char* kJamesBadgesFile    = "/data/data/jamesos/badges.txt";

  // easter egg tuning (change here if they are too easy / too hard to trigger)
  const s32       kJamesTicklePets     = 5;      // this many pets...
  const long long kJamesTickleWindowMs = 8000;   // ...within this time = tickle mode
  const long long kJamesTickleShowMs   = 4500;   // how long he stays flustered
  const long long kJamesShyMaxPetMs    = 1500;   // a pet shorter than this counts as "gentle"
  const long long kJamesShyWaitMs      = 3000;   // then nothing for this long = shy
  const long long kJamesShyShowMs      = 4000;   // how long he peeks
  const long long kJamesShyCooldownMs  = 90000;  // at most once every 1.5 minutes

  // secret = shows as "???" until found (easter eggs)
  struct JamesBadgeDef { const char* key; const char* name; const char* hint; bool secret; };
  const JamesBadgeDef kJamesBadges[] = {
    {"FIRST_PET",  "FIRST PET",        "PET HIM ONCE",        false},
    {"DAY_10",     "10 IN A DAY",      "10 PETS IN 1 DAY",    false},
    {"DAY_50",     "50 IN A DAY",      "50 PETS IN 1 DAY",    false},
    {"TOTAL_100",  "100 PETS",         "100 PETS TOTAL",      false},
    {"TOTAL_500",  "500 PETS",         "500 PETS TOTAL",      false},
    {"TOTAL_1000", "1000 PETS",        "1000 PETS TOTAL",     false},
    {"STREAK_3",   "3 DAYS IN A ROW",  "PET 3 DAYS IN ROW",   false},
    {"STREAK_7",   "7 DAYS IN A ROW",  "PET 7 DAYS IN ROW",   false},
    {"STREAK_30",  "30 DAYS IN A ROW", "PET 30 DAYS IN ROW",  false},
    {"STREAK_100", "LOYAL FRIEND",     "PET 100 DAYS IN ROW", false},
    {"NIGHT_OWL",  "NIGHT OWL",        "PET 12AM TO 5AM",     false},
    {"EARLY_BIRD", "EARLY BIRD",       "PET 5AM TO 7AM",      false},
    {"SPOOKY",     "SPOOKY FRIEND",    "PET IN HALLOWEEN",    false},
    {"HALLOWEEN",  "HALLOWEEN NIGHT",  "PET OCT 31 NIGHT",    false},
    {"KHMER_NY",   "KHMER NEW YEAR",   "PET APRIL 14-16",     false},
    {"CHRISTMAS",  "CHRISTMAS",        "PET ON DEC 25",       false},
    {"NEW_YEAR",   "HAPPY NEW YEAR",   "PET ON JAN 1",        false},
    {"NY_MIDNIGHT","NEW YEAR MIDNIGHT","PET JAN 1 12-1 AM",   false},
    {"CNY",        "CHINESE NEW YEAR", "PET ON CHINESE NY",   false},
    {"VALENTINE",  "VALENTINE'S DAY",  "PET ON FEB 14",       false},
    {"TICKLISH",   "TICKLISH",         "A SECRET. KEEP LOOKING", true},
    {"SHY_GUY",    "SHY GUY",          "A SECRET. KEEP LOOKING", true},
    {"SECRET_ROOM","SECRET ROOM",      "A SECRET. KEEP LOOKING", true},
    {"SHINY",      "SHINY VECTOR",     "A SECRET. KEEP LOOKING", true},    // v23: 1 in 4096 days
    {"BLUE_MOON",  "BLUE MOON",        "PET UNDER A BLUE MOON",  false},   // v23
  };


  // Chinese New Year moves every year (lunar calendar)
  bool JamesIsChineseNewYear(int today)
  {
    static const int kDays[] = {20260217, 20270206, 20280126, 20290213, 20300203, 20310123,
                                20320211, 20330131, 20340219, 20350208};
    for (int d : kDays) {
      if (d == today) { return true; }
    }
    return false;
  }
  const s32 kJamesBadgeCount = (s32)(sizeof(kJamesBadges) / sizeof(kJamesBadges[0]));

  struct JamesStats { int total = 0; int streak = 0; int best = 0; int lastday = 0; };
  JamesStats                  gJamesStats;            // all guarded by gJamesPetsMtx
  bool                        gJamesStatsLoaded = false;
  std::map<std::string, int>  gJamesBadgeDates;       // key -> yyyymmdd (1 = date unknown)
  bool                        gJamesBadgesLoaded = false;
  std::deque<std::string>     gJamesBadgePopups;      // badge names waiting to pop up

  void JamesLoadStatsLocked()
  {
    if (gJamesStatsLoaded) { return; }
    gJamesStatsLoaded = true;
    std::ifstream in(kJamesStatsFile);
    std::string line;
    while (in.is_open() && std::getline(in, line)) {
      int v = 0;
      if (std::sscanf(line.c_str(), "total=%d", &v) == 1)   { gJamesStats.total = v; }
      if (std::sscanf(line.c_str(), "streak=%d", &v) == 1)  { gJamesStats.streak = v; }
      if (std::sscanf(line.c_str(), "best=%d", &v) == 1)    { gJamesStats.best = v; }
      if (std::sscanf(line.c_str(), "lastday=%d", &v) == 1) { gJamesStats.lastday = v; }
    }
  }

  void JamesSaveStatsLocked()
  {
    mkdir("/data/data/jamesos", 0777);
    std::ofstream out(kJamesStatsFile, std::ios::trunc);
    if (out.is_open()) {
      out << "total=" << gJamesStats.total << "\nstreak=" << gJamesStats.streak
          << "\nbest=" << gJamesStats.best << "\nlastday=" << gJamesStats.lastday << "\n";
    }
  }

  void JamesLoadBadgesLocked()
  {
    if (gJamesBadgesLoaded) { return; }
    gJamesBadgesLoaded = true;
    std::ifstream in(kJamesBadgesFile);
    std::string key;
    int date = 0;
    while (in.is_open() && (in >> key >> date)) {
      gJamesBadgeDates[key] = std::max(1, date);
    }
  }

  // the day before a yyyymmdd date (handles month and year changes)
  int JamesPrevDay(int key)
  {
    struct tm t = {};
    t.tm_year = key / 10000 - 1900;
    t.tm_mon = (key / 100) % 100 - 1;
    t.tm_mday = key % 100 - 1;
    t.tm_hour = 12;
    std::mktime(&t);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
  }

  // v24: copy the badges to /persist the moment one unlocks (CLEAR OUT SOUL and a dead battery
  // can't lose it). Merges with the saved copy, so a badge saved there is never dropped.
  // If /persist can't be written right now, jamesos-keep.sh still saves at shutdown as before.
  void JamesBackupBadges()
  {
    const char* kSaved = "/persist/jamesos/badges.txt";
    const char* kTmp   = "/persist/jamesos/badges.txt.tmp";
    std::vector<std::string> lines;
    std::map<std::string, bool> seen;
    for (const char* path : {kSaved, kJamesBadgesFile}) {
      std::ifstream in(path);
      std::string line;
      while (std::getline(in, line)) {
        const std::string k = line.substr(0, line.find(' '));
        if (k.empty() || seen.count(k) != 0) { continue; }
        seen[k] = true;
        lines.push_back(line);
      }
    }
    if (lines.empty()) { return; }
    mkdir("/persist/jamesos", 0755);
    {
      std::ofstream out(kTmp, std::ios::trunc);
      if (!out.is_open()) { return; }
      for (const auto& l : lines) { out << l << "\n"; }
      out.flush();
      if (!out.good()) { out.close(); std::remove(kTmp); return; }
    }
    if (std::rename(kTmp, kSaved) != 0) { std::remove(kTmp); }
  }

  void JamesUnlockLocked(const char* key, int date)
  {
    if (gJamesBadgeDates.count(key) != 0) { return; }
    gJamesBadgeDates[key] = std::max(1, date);
    mkdir("/data/data/jamesos", 0777);
    {
      std::ofstream out(kJamesBadgesFile, std::ios::app);
      if (out.is_open()) {
        out << key << " " << std::max(1, date) << "\n";
      }
    }
    JamesBackupBadges();   // v24: saved right away
    for (s32 i = 0; i < kJamesBadgeCount; ++i) {
      if (std::string(kJamesBadges[i].key) == key) {
        gJamesBadgePopups.push_back(kJamesBadges[i].name);
      }
    }
    gJamesCelebrate = 1; // confetti too
  }

  // called for every new pet (gJamesPetsMtx held)
  void JamesUpdateStatsAndBadgesLocked(int petsToday)
  {
    JamesLoadStatsLocked();
    JamesLoadBadgesLocked();
    const int today = gJamesDateKey;
    gJamesStats.total++;
    if (today != 0 && today != gJamesStats.lastday) {
      const bool continues = (gJamesStats.lastday != 0) && (JamesPrevDay(today) == gJamesStats.lastday);
      gJamesStats.streak = continues ? gJamesStats.streak + 1 : 1;
      gJamesStats.lastday = today;
    }
    gJamesStats.best = std::max(gJamesStats.best, gJamesStats.streak);
    JamesSaveStatsLocked();

    const int total = gJamesStats.total, streak = gJamesStats.streak;
    const int hour = gJamesHour, month = (today / 100) % 100, day = today % 100;
    if (total >= 1)        { JamesUnlockLocked("FIRST_PET", today); }
    if (petsToday >= 10)   { JamesUnlockLocked("DAY_10", today); }
    if (petsToday >= 50)   { JamesUnlockLocked("DAY_50", today); }
    if (total >= 100)      { JamesUnlockLocked("TOTAL_100", today); }
    if (total >= 500)      { JamesUnlockLocked("TOTAL_500", today); }
    if (total >= 1000)     { JamesUnlockLocked("TOTAL_1000", today); }
    if (streak >= 3)       { JamesUnlockLocked("STREAK_3", today); }
    if (streak >= 7)       { JamesUnlockLocked("STREAK_7", today); }
    if (streak >= 30)      { JamesUnlockLocked("STREAK_30", today); }
    if (streak >= 100)     { JamesUnlockLocked("STREAK_100", today); }   // LOYAL FRIEND: gold sparkles
    if (hour >= 0 && hour < 5) { JamesUnlockLocked("NIGHT_OWL", today); }
    if (hour >= 5 && hour < 7) { JamesUnlockLocked("EARLY_BIRD", today); }
    if (JamesIsHalloween())    { JamesUnlockLocked("SPOOKY", today); }
    if (today != 0 && month == 10 && day == 31 && hour >= 18)  { JamesUnlockLocked("HALLOWEEN", today); }
    if (today != 0 && month == 4 && day >= 14 && day <= 16)    { JamesUnlockLocked("KHMER_NY", today); }
    if (today != 0 && month == 12 && day == 25)                { JamesUnlockLocked("CHRISTMAS", today); }
    if (today != 0 && month == 1 && day == 1)                  { JamesUnlockLocked("NEW_YEAR", today); }
    if (today != 0 && month == 1 && day == 1 && hour == 0)     { JamesUnlockLocked("NY_MIDNIGHT", today); }
    if (today != 0 && JamesIsChineseNewYear(today))            { JamesUnlockLocked("CNY", today); }
    if (today != 0 && month == 2 && day == 14)                 { JamesUnlockLocked("VALENTINE", today); }
    if (gJamesBlueMoon && !gJamesBlueMoonTest)                 { JamesUnlockLocked("BLUE_MOON", today); }   // v23
  }

  void JamesSetPx(Vision::ImageRGB565& img, s32 x, s32 y, u8 r, u8 g, u8 b)
  {
    if (x < 0 || y < 0 || y >= img.GetNumRows() || x >= img.GetNumCols()) { return; }
    img.GetRow(y)[x].SetValue(r, g, b);
  }

  void JamesFillRect(Vision::ImageRGB565& img, s32 x0, s32 y0, s32 w, s32 h, u8 r, u8 g, u8 b)
  {
    for (s32 y = y0; y < y0 + h; ++y) {
      for (s32 x = x0; x < x0 + w; ++x) { JamesSetPx(img, x, y, r, g, b); }
    }
  }

  // holiday countdown banner (v22): small line on top, big line below, slides down and back up
  void JamesDrawHolidayBanner(Vision::ImageRGB565& img, const std::string& top, const std::string& big,
                              u8 r, u8 g, u8 b, s32 framesLeft, s32 framesTotal)
  {
    const s32 W = img.GetNumCols(), H = img.GetNumRows();
    const s32 shown = framesTotal - framesLeft;
    const s32 bh = 34;
    const s32 restY = (H - bh) / 2;
    s32 y0 = restY;
    if (shown < 10)      { y0 = -bh + (restY + bh) * shown / 10; }
    if (framesLeft < 10) { y0 = -bh + (restY + bh) * framesLeft / 10; }
    const s32 x0 = 4, bw = W - 8;
    JamesFillRect(img, x0, y0, bw, bh, 0, 0, 0);
    JamesFillRect(img, x0, y0, bw, 1, r, g, b);
    JamesFillRect(img, x0, y0 + bh - 1, bw, 1, r, g, b);
    JamesFillRect(img, x0, y0, 1, bh, r, g, b);
    JamesFillRect(img, x0 + bw - 1, y0, 1, bh, r, g, b);
    const ColorRGBA col(r, g, b);
    const ColorRGBA white(u8(255), u8(255), u8(255));
    // shrink the text until it fits (the 2.0 screen is narrower)
    f32 s1 = 0.32f, s2 = 0.42f;
    while (s1 > 0.2f && img.GetTextSize(top, s1, 1).x() > (f32)(bw - 8)) { s1 -= 0.02f; }
    while (s2 > 0.24f && img.GetTextSize(big, s2, 1).x() > (f32)(bw - 8)) { s2 -= 0.02f; }
    const f32 w1 = img.GetTextSize(top, s1, 1).x(), w2 = img.GetTextSize(big, s2, 1).x();
    img.DrawText({(f32)x0 + ((f32)bw - w1) / 2.f, (f32)(y0 + 13)}, top, col, s1);
    img.DrawText({(f32)x0 + ((f32)bw - w2) / 2.f, (f32)(y0 + 28)}, big, white, s2);
  }

  // "BADGE UNLOCKED" strip along the BOTTOM of the screen (v23: below his eyes, so they stay visible),
  // with a little gold trophy. Slides up, stays ~4 seconds, slides back down.
  void JamesDrawBadgePopup(Vision::ImageRGB565& img, const std::string& name, s32 framesLeft, s32 framesTotal)
  {
    const s32 W = img.GetNumCols(), H = img.GetNumRows();
    const s32 shown = framesTotal - framesLeft;
    const bool small = (H < 96);   // Vector 2.0 screen
    const s32 bh = small ? 19 : 24;
    const s32 restY = H - bh - 1, hiddenY = H;
    s32 y0 = restY;
    if (shown < 10)      { y0 = hiddenY + (restY - hiddenY) * shown / 10; }
    if (framesLeft < 10) { y0 = hiddenY + (restY - hiddenY) * framesLeft / 10; }
    const s32 x0 = 4, bw = W - 8;
    JamesFillRect(img, x0, y0, bw, bh, 0, 0, 0);
    JamesFillRect(img, x0, y0, bw, 1, 255, 200, 40);
    JamesFillRect(img, x0, y0 + bh - 1, bw, 1, 255, 200, 40);
    JamesFillRect(img, x0, y0, 1, bh, 255, 200, 40);
    JamesFillRect(img, x0 + bw - 1, y0, 1, bh, 255, 200, 40);
    // small trophy: cup, handles, stem, base
    const u8 gr = 255, gg = 200, gb = 40;
    const s32 tx = x0 + 6, ty = y0 + (bh - 11) / 2;
    JamesFillRect(img, tx, ty, 9, 5, gr, gg, gb);
    JamesFillRect(img, tx + 1, ty + 5, 7, 2, gr, gg, gb);
    JamesFillRect(img, tx - 2, ty + 1, 2, 1, gr, gg, gb);
    JamesFillRect(img, tx - 2, ty + 1, 1, 3, gr, gg, gb);
    JamesFillRect(img, tx - 2, ty + 3, 2, 1, gr, gg, gb);
    JamesFillRect(img, tx + 9, ty + 1, 2, 1, gr, gg, gb);
    JamesFillRect(img, tx + 10, ty + 1, 1, 3, gr, gg, gb);
    JamesFillRect(img, tx + 9, ty + 3, 2, 1, gr, gg, gb);
    JamesFillRect(img, tx + 3, ty + 7, 3, 2, gr, gg, gb);
    JamesFillRect(img, tx + 1, ty + 9, 7, 2, gr, gg, gb);
    // words: small gold line + the badge name (shrinks to fit the 2.0's narrower screen)
    const ColorRGBA gold(gr, gg, gb);
    const ColorRGBA white(u8(255), u8(255), u8(255));
    const s32 textX = x0 + 20, room = bw - 24;
    f32 s1 = small ? 0.22f : 0.26f, s2 = small ? 0.28f : 0.34f;
    while (s1 > 0.18f && img.GetTextSize("BADGE UNLOCKED", s1, 1).x() > (f32)room) { s1 -= 0.02f; }
    while (s2 > 0.22f && img.GetTextSize(name, s2, 1).x() > (f32)room) { s2 -= 0.02f; }
    img.DrawText({(f32)textX, (f32)(y0 + (small ? 8 : 10))}, "BADGE UNLOCKED", gold, s1);
    img.DrawText({(f32)textX, (f32)(y0 + (small ? 17 : 21))}, name, white, s2);
  }
}


void ProceduralFaceDrawer::JamesSetPetting(bool petting)
{
  const bool wasPetting = gJamesPetting.exchange(petting);
  if (petting == wasPetting) {
    return;
  }
  const long long nowMs = JamesNowMs();
  if (!petting) {
    // a pet just ended: one short, gentle pet may make him shy (if nothing else follows)
    const long long started = gJamesPetStartMs;
    if (started != 0 && nowMs - started < kJamesShyMaxPetMs) {
      gJamesShyCandidateMs = nowMs;
    }
    return;
  }

  // a new pet started
  gJamesPetStartMs = nowMs;
  gJamesShyCandidateMs = 0;   // more petting = not shy
  {
    // TICKLE MODE: lots of quick pets in a row
    static std::deque<long long> starts;
    starts.push_back(nowMs);
    while (!starts.empty() && nowMs - starts.front() > kJamesTickleWindowMs) { starts.pop_front(); }
    if ((s32)starts.size() >= kJamesTicklePets && nowMs > gJamesTickleUntilMs + 2000) {
      gJamesTickleUntilMs = nowMs + kJamesTickleShowMs;
      starts.clear();
      std::lock_guard<std::mutex> lk(gJamesPetsMtx);
      JamesLoadBadgesLocked();
      JamesUnlockLocked("TICKLISH", gJamesDateKey);
    }
  }
  // a new petting started: count it (at most once every 3 seconds)
  static std::chrono::steady_clock::time_point lastPet;
  const auto now = std::chrono::steady_clock::now();
  if (lastPet.time_since_epoch().count() != 0 &&
      std::chrono::duration_cast<std::chrono::seconds>(now - lastPet).count() < 3) {
    return;
  }
  lastPet = now;

  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadPetsLocked();
  const int today = gJamesDateKey;
  if (today != 0 && today != gJamesPetsDate) {
    gJamesPetsDate = today;
    gJamesPetsToday = 0;
  }
  const int count = ++gJamesPetsToday;
  JamesSavePetsLocked();
  JamesUpdateStatsAndBadgesLocked(count); // JamesOS badges
  if (count % kJamesPetGoal == 0) {
    gJamesCelebrate = 1;
  }
}

void ProceduralFaceDrawer::JamesSetStatus(f32 battVolts, s32 battTempC, bool onCharger, bool charging,
                                          s32 hour, s32 minute, s32 dateKey)
{
  gJamesBattV = battVolts;
  gJamesTempC = battTempC;
  gJamesOnCharger = onCharger;
  gJamesCharging = charging;
  gJamesHour = hour;
  gJamesMinute = minute;
  gJamesDateKey = dateKey;

  // JamesOS auto-update: tell the updater if Vector is on the charger (it only updates there).
  // Written about every 5 seconds to RAM (/dev/shm), never to the flash.
  static u32 jamesStatusWrites = 0;
  if ((jamesStatusWrites++ % 5) == 0) {
    FILE* f = fopen("/dev/shm/jamesos_status.tmp", "w");
    if (f != nullptr) {
      fprintf(f, "charger=%d\ncharging=%d\nvolts=%.2f\n", onCharger ? 1 : 0, charging ? 1 : 0, (double)battVolts);
      fclose(f);
      rename("/dev/shm/jamesos_status.tmp", "/dev/shm/jamesos_status");
    }
  }
}

void ProceduralFaceDrawer::JamesGetStatus(f32& battVolts, s32& battTempC, bool& onCharger, bool& charging)
{
  battVolts = gJamesBattV;
  battTempC = gJamesTempC;
  onCharger = gJamesOnCharger;
  charging = gJamesCharging;
}

s32 ProceduralFaceDrawer::JamesPetsToday()
{
  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadPetsLocked();
  const int today = gJamesDateKey;
  if (today != 0 && today != gJamesPetsDate) {
    return 0; // a new day started, nothing counted yet
  }
  return gJamesPetsToday;
}

// ---------------- v23: rare things (shiny days, blue moon nights; private: family, prototype) ----------------
// full moon times (UTC, unix seconds) from Meeus' Astronomical Algorithms, through 2037
// (Vector's clock is 32-bit, so it stops in 2038 anyway). Each robot works out blue moons in its own time zone.
static const long long kJamesFullMoons[] = {
    1736807215LL, 1739368404LL, 1741935277LL, 1744503737LL, 1747068962LL, 1749627830LL,
    1752179804LL, 1754726103LL, 1757268530LL, 1759808853LL, 1762348759LL, 1764890045LL,
    1767434575LL, 1769983757LL, 1772537876LL, 1775095921LL, 1777656195LL, 1780217116LL,
    1782777403LL, 1785335743LL, 1787890710LL, 1790441342LL, 1792987908LL, 1795532011LL,
    1798075693LL, 1800620240LL, 1803165813LL, 1805712225LL, 1808260027LL, 1810810738LL,
    1813365863LL, 1815925497LL, 1818487721LL, 1821049416LL, 1823608023LL, 1826162752LL,
    1828714127LL, 1831262584LL, 1833807821LL, 1836349566LL, 1838888798LL, 1841428131LL,
    1843970928LL, 1846519851LL, 1849075790LL, 1851637659LL, 1854203104LL, 1856769441LL,
    1859334012LL, 1861894109LL, 1864447415LL, 1866993017LL, 1869531986LL, 1872067009LL,
    1874601451LL, 1877138539LL, 1879680946LL, 1882230681LL, 1884788964LL, 1887355659LL,
    1889928185LL, 1892501192LL, 1895068466LL, 1897626000LL, 1900173393LL, 1902712800LL,
    1905247150LL, 1907779258LL, 1910311912LL, 1912848262LL, 1915391874LL, 1917946005LL,
    1920511821LL, 1923086432LL, 1925663154LL, 1928234785LL, 1930796991LL, 1933348886LL,
    1935891598LL, 1938427113LL, 1940958079LL, 1943487935LL, 1946020832LL, 1948561075LL,
    1951111965LL, 1953674310LL, 1956245580LL, 1958820749LL, 1961394196LL, 1963961184LL,
    1966518588LL, 1969065439LL, 1971603155LL, 1974135092LL, 1976665606LL, 1979199018LL,
    1981738697LL, 1984286530LL, 1986842946LL, 1989407231LL, 1991977452LL, 1994549845LL,
    1997119049LL, 1999680173LL, 2002231160LL, 2004773321LL, 2007310065LL, 2009845233LL,
    2012381891LL, 2014921933LL, 2017466532LL, 2020016836LL, 2022573885LL, 2025137418LL,
    2027704735LL, 2030271346LL, 2032833245LL, 2035388675LL, 2037938075LL, 2040482962LL,
    2043025008LL, 2045565749LL, 2048106730LL, 2050649668LL, 2053196197LL, 2055747236LL,
    2058302526LL, 2060860843LL, 2063420747LL, 2065981049LL, 2068540613LL, 2071098019LL,
    2073651814LL, 2076201336LL, 2078747332LL, 2081291591LL, 2083835770LL, 2086380525LL,
    2088925773LL, 2091471759LL, 2094019778LL, 2096571729LL, 2099128770LL, 2101690140LL,
    2104253135LL, 2106814510LL, 2109372253LL, 2111926109LL, 2114476519LL, 2117023446LL,
    2119566488LL, 2122106016LL, 2124644033LL, 2127183851LL, 2129728807LL, 2132280919LL,
    2134840167LL, 2137404708LL, 2139971791LL, 2142538512LL, 2145101918LL,
};

static int JamesKeyOf(const struct tm& t) { return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }

// is `key` (yyyymmdd, local time) the day of the 2nd full moon in its month?
static bool JamesIsBlueMoonDay(int key)
{
  int count = 0;
  for (long long fm : kJamesFullMoons) {
    const std::time_t ft = (std::time_t)fm;
    struct tm t;
    if (localtime_r(&ft, &t) == nullptr) { continue; }
    const int k = JamesKeyOf(t);
    if (k / 100 != key / 100) { continue; }
    ++count;
    if (k == key) { return count == 2; }
  }
  return false;
}

static void JamesUnlockNow(const char* key)
{
  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadBadgesLocked();
  JamesUnlockLocked(key, gJamesDateKey);
}

// about every 2 seconds (from JamesMoodHue)
static void JamesRefreshRare()
{
  const int today = gJamesDateKey;
  // --- shiny: one roll per day; saved, so restarting him doesn't roll again
  if (today != 0) {
    int fileDay = 0;
    char what[16] = {0};
    FILE* f = std::fopen(kJamesShinyFile, "r");
    if (f != nullptr) {
      if (std::fscanf(f, "%d %15s", &fileDay, what) != 2) { fileDay = 0; }
      std::fclose(f);
    }
    bool shiny = false, test = false;
    if (fileDay == today) {
      test = (std::strcmp(what, "test") == 0);
      shiny = test || (std::strcmp(what, "1") == 0);
    } else {
      std::random_device rd;
      std::mt19937 rng(rd() ^ (u32)std::time(nullptr));
      shiny = (rng() % 4096u) == 0u;
      mkdir("/data/data/jamesos", 0777);
      FILE* o = std::fopen(kJamesShinyFile, "w");
      if (o != nullptr) {
        std::fprintf(o, "%d %s\n", today, shiny ? "1" : "0");
        std::fclose(o);
      }
    }
    gJamesShiny = shiny;
    gJamesShinyTest = test;
    static bool shinyBadgeDone = false;
    if (shiny && !test && !shinyBadgeDone) { shinyBadgeDone = true; JamesUnlockNow("SHINY"); }
  }

  // --- blue moon night: 6 PM on the day of the 2nd full moon in a month, until 6 AM
  {
    static std::time_t lastCheck = 0;
    static bool blue = false;
    const std::time_t now = std::time(nullptr);
    if (now - lastCheck >= 60 || now < lastCheck) {
      lastCheck = now;
      blue = false;
      struct tm lt;
      if (localtime_r(&now, &lt) != nullptr) {
        if (lt.tm_hour >= 18) {
          blue = JamesIsBlueMoonDay(JamesKeyOf(lt));
        } else if (lt.tm_hour < 6) {
          struct tm y = lt;
          y.tm_mday -= 1;
          y.tm_hour = 12;
          std::mktime(&y);
          blue = JamesIsBlueMoonDay(JamesKeyOf(y));
        }
      }
    }
    const bool test = JamesFileExists(kJamesBlueMoonTestFile);
    gJamesBlueMoon = blue || test;
    gJamesBlueMoonTest = test && !blue;
  }

}

// v23i private: the family page (COOL -> FAMILY)
bool ProceduralFaceDrawer::JamesHasFamily() { return JAMESOS_PRIVATE != 0; }

// v24: the About page credit. The name is only compiled into private builds, so the public
// OTA (and its published source) never contains it.
std::string ProceduralFaceDrawer::JamesCreator()
{
  return "Based on WireOS";
}

std::string ProceduralFaceDrawer::JamesCreatorWebsite()
{
  return "";
}

std::vector<std::string> ProceduralFaceDrawer::JamesFamilyLines()
{
  std::vector<std::string> out;
  return out;
}

std::string ProceduralFaceDrawer::JamesFamilyOnlineText()
{
  return "";
}

bool ProceduralFaceDrawer::JamesIsPrototype()
{
  return false;
}

// v23: which holiday back lights to use ("" = the normal JamesOS lights).
// The back lights ask this every time they start, so they follow the theme live (no restart).
std::string ProceduralFaceDrawer::JamesLightTheme()
{
  if (gJamesShiny) { return "shiny"; }   // a shiny day beats every holiday
  switch ((JamesTheme)gJamesTheme.load()) {
    case JamesTheme::Halloween:    return "halloween";
    case JamesTheme::Christmas:    return "christmas";
    case JamesTheme::Lunar:        return "lunar";
    case JamesTheme::NewYear: {
      const s32 key = gJamesDateKey;
      return ((key / 100) % 100 == 1 && key % 100 == 1) ? "newyear-midnight" : "newyear";   // Jan 1: rainbow
    }
    case JamesTheme::Valentine:    return "valentine";
    case JamesTheme::MothersDay:   return "mothersday";
    case JamesTheme::StPatrick:    return "stpatrick";
    case JamesTheme::Easter:       return "easter";
    case JamesTheme::Fireworks:    return "fireworks";
    case JamesTheme::Thanksgiving: return "thanksgiving";
    default:                       return "";
  }
}

s32 ProceduralFaceDrawer::JamesBadgeCount()
{
  return kJamesBadgeCount;
}

std::string ProceduralFaceDrawer::JamesBadgeName(s32 i)
{
  return (i >= 0 && i < kJamesBadgeCount) ? kJamesBadges[i].name : "";
}

std::string ProceduralFaceDrawer::JamesBadgeHint(s32 i)
{
  return (i >= 0 && i < kJamesBadgeCount) ? kJamesBadges[i].hint : "";
}

bool ProceduralFaceDrawer::JamesBadgeIsSecret(s32 i)
{
  return (i >= 0 && i < kJamesBadgeCount) ? kJamesBadges[i].secret : false;
}

bool ProceduralFaceDrawer::JamesHasBadge(const std::string& key)
{
  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadBadgesLocked();
  return gJamesBadgeDates.count(key) != 0;
}

void ProceduralFaceDrawer::JamesUnlockBadge(const std::string& key)
{
  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadBadgesLocked();
  JamesUnlockLocked(key.c_str(), gJamesDateKey);
}

s32 ProceduralFaceDrawer::JamesBadgeDate(s32 i)
{
  if (i < 0 || i >= kJamesBadgeCount) { return 0; }
  std::lock_guard<std::mutex> lk(gJamesPetsMtx);
  JamesLoadBadgesLocked();
  const auto it = gJamesBadgeDates.find(kJamesBadges[i].key);
  return (it == gJamesBadgeDates.end()) ? 0 : it->second;
}

f32 ProceduralFaceDrawer::JamesThemeSat(f32 saturation)
{
  if (gJamesOwnEyes) { return saturation; }   // your own eye color wins over the theme
  if (JamesIsHalloween()) { return 1.f; }
  if (JamesIsChristmas()) { return 0.32f; }   // icy, almost white
  if (JamesIsLunar())     { return 0.95f; }
  if (JamesIsGlitch())    { return 0.9f; }
  if (JamesIsRetro())     { return 1.f; }
  if (JamesIsGalaxyMax()) { return 0.8f; }
  if (JamesIsTheme(JamesTheme::NewYear))      { return 0.65f; }  // gold
  if (JamesIsTheme(JamesTheme::Valentine))    { return 0.6f; }   // pink
  if (JamesIsTheme(JamesTheme::StPatrick))    { return 0.9f; }   // green
  if (JamesIsTheme(JamesTheme::Easter))       { return 0.45f; }  // pastel
  if (JamesIsTheme(JamesTheme::MothersDay))   { return 0.4f; }   // soft pink
  if (JamesIsTheme(JamesTheme::Fireworks))    { return 0.35f; }  // icy blue-white
  if (JamesIsTheme(JamesTheme::Thanksgiving)) { return 0.85f; }  // warm orange
  return saturation;
}

// v24: JamesOS auto-update, the last step. Vector's own updater checks the JamesOS server every hour
// and installs a new version in the background (into the other system slot); it is used only after a
// restart. This restarts him at a good time: on the charger for 10+ min, not petted for 30+ min, and
// between 1 and 5 AM (or any quiet charger time once the update has waited a day).
// Only for updates from the JamesOS server: a WireOS update is never finished by this.
// Test switch: /data/data/jamesos/update.test = no night / waiting rules (1 min on the charger).
namespace {
  void JamesUpdateRestartCheck()
  {
    static bool sent = false;
    static time_t chargerSince = 0;
    static time_t lastPet = 0;
    if (sent) { return; }
    const time_t now = time(nullptr);
    if (gJamesPetting) { lastPet = now; }
    if (!gJamesOnCharger) { chargerSince = 0; } else if (chargerSince == 0) { chargerSince = now; }
    struct stat st;
    if (stat("/run/update-engine/done", &st) != 0 || st.st_size == 0) { return; }
    {
      std::ifstream env("/anki/etc/update-engine.env");
      std::string all, line;
      while (std::getline(env, line)) { all += line; all += '\n'; }
      if (all.empty() || all.find("pvic.xyz") != std::string::npos) { return; }
    }
    if (chargerSince == 0) { return; }
    const bool test = JamesFileExists("/data/data/jamesos/update.test");
    if (test) {
      if (now - chargerSince < 60) { return; }
    } else {
      if (now - chargerSince < 600 || now - lastPet < 1800) { return; }
      struct tm lt;
      localtime_r(&now, &lt);
      const bool night = lt.tm_hour >= 1 && lt.tm_hour < 5;
      const bool waitedLong = now - st.st_mtime > 24 * 3600;
      if (!night && !waitedLong) { return; }
    }
    sent = true;
    std::ofstream("/data/data/jamesos/just-updated") << (long)now << "\n";
    const int rc = std::system("sync; (systemctl reboot || /sbin/reboot) >/dev/null 2>&1 &");
    (void)rc;
  }
}

f32 ProceduralFaceDrawer::JamesMoodHue(f32 baseHue)
{
  static u32 frameCount = 0;
  static bool moodOn = false;
  static f32 targetShift = 0.f;  // -1 (cool) .. +1 (warm)
  static f32 currentShift = 0.f;

  static int lastTheme = -1;
  static f32 lastBase = -1.f;
  if ((frameCount++ % 60) == 0) {
    JamesRefreshThemeAndSleep();
    JamesRefreshRare();
    JamesUpdateRestartCheck();   // v24: finish a JamesOS auto-update at a good time
    // v20: changing the eye color by voice (or in the app) while a theme is on keeps YOUR color.
    // Picking a theme again in the menu brings the theme's color back.
    const int theme = gJamesTheme;
    if (theme != lastTheme) {
      if (lastTheme == -1) {   // just started: was a color picked for this theme before the restart?
        // (v23h: "v2:" marks a color YOU picked; older files were often written by mistake at startup)
        gJamesOwnEyes = (theme != (int)JamesTheme::Normal) && (JamesReadWord(kJamesOwnEyesFile) == "v2:" + std::to_string(theme));
      } else {
        gJamesOwnEyes = false;
        std::ofstream(kJamesOwnEyesFile) << "none\n";
      }
      lastTheme = theme;
    }
    moodOn = JamesFileExists(kJamesMoodOnFlag);
    targetShift = 0.f;
    if (moodOn) {
      std::ifstream in(kJamesMoodFile);
      f32 happy = 0.f, confident = 0.f, stimulated = 0.f;
      if (in >> happy >> confident >> stimulated) {
        // happy and excited -> warm, sad or bored -> cool
        targetShift = Util::Clamp(0.6f * happy + 0.2f * confident + 0.2f * stimulated, -1.f, 1.f);
      }
    }
  }

  // the eye color setting changed (voice command or app) while a theme is on: keep that color.
  // v23h: Vector loads his saved eye color a few seconds AFTER he starts, and some animations
  // tint the eyes for a moment - neither is "you picked a color". So: ignore the first 90 s,
  // and the new color has to stay for ~3 s before it counts.
  static s32 changedFrames = 0;
  const bool settled = frameCount > 30u * 90u;
  if (lastBase < 0.f) {
    lastBase = baseHue;
  } else if (std::fabs(baseHue - lastBase) > 0.002f) {
    if (++changedFrames >= 90) {
      if (settled && gJamesTheme != (int)JamesTheme::Normal && !gJamesOwnEyes) {
        gJamesOwnEyes = true;
        std::ofstream(kJamesOwnEyesFile) << "v2:" << (int)gJamesTheme << "\n";
      }
      lastBase = baseHue;
      changedFrames = 0;
    }
  } else {
    changedFrames = 0;
  }

  if (gJamesOwnEyes) {
    // your color: no theme color
  } else if (JamesIsHalloween()) {
    baseHue = 0.075f; // pumpkin orange
  } else if (JamesIsChristmas()) {
    baseHue = 0.55f;  // icy blue
  } else if (JamesIsLunar()) {
    baseHue = 0.985f; // lucky red
  } else if (JamesIsGlitch()) {
    baseHue = 0.5f;   // cyan
  } else if (JamesIsRetro()) {
    baseHue = 0.33f;  // old green screen
  } else if (JamesIsGalaxyMax()) {
    baseHue = 0.76f;  // deep purple
  } else if (JamesIsTheme(JamesTheme::NewYear)) {
    baseHue = 0.13f;  // gold
  } else if (JamesIsTheme(JamesTheme::Valentine)) {
    baseHue = 0.94f;  // pink
  } else if (JamesIsTheme(JamesTheme::StPatrick)) {
    baseHue = 0.33f;  // green
  } else if (JamesIsTheme(JamesTheme::Easter)) {
    static const f32 kPastels[4] = {0.92f, 0.42f, 0.75f, 0.15f};   // pink, mint, lavender, yellow
    const f32 t = (f32)(frameCount % 1200) / 300.f;                 // a new pastel every ~10 s
    const s32 i = (s32)t;
    f32 h0 = kPastels[i % 4], h1 = kPastels[(i + 1) % 4];
    if (h1 - h0 > 0.5f) { h0 += 1.f; } else if (h0 - h1 > 0.5f) { h1 += 1.f; }
    baseHue = h0 + (h1 - h0) * (t - (f32)i);
    baseHue -= std::floor(baseHue);
  } else if (JamesIsTheme(JamesTheme::MothersDay)) {
    baseHue = 0.95f;  // soft pink
  } else if (JamesIsTheme(JamesTheme::Fireworks)) {
    baseHue = 0.6f;   // blue-white
  } else if (JamesIsTheme(JamesTheme::Thanksgiving)) {
    baseHue = 0.08f;  // warm orange
  }

  // move slowly so the color drifts instead of jumping
  currentShift += (targetShift - currentShift) * 0.02f;
  if (std::fabs(currentShift) < 0.001f) {
    return baseHue;
  }

  // nudge the hue at most 35% of the way toward orange (warm) or blue (cool)
  const f32 kWarmHue = 0.07f;
  const f32 kCoolHue = 0.62f;
  const f32 target = (currentShift >= 0.f) ? kWarmHue : kCoolHue;
  const f32 amount = 0.35f * std::fabs(currentShift);
  f32 diff = target - baseHue;
  if (diff > 0.5f)  { diff -= 1.f; }  // go the short way around the color wheel
  if (diff < -0.5f) { diff += 1.f; }
  f32 hue = baseHue + diff * amount;
  hue -= std::floor(hue);
  return hue;
}

bool ProceduralFaceDrawer::ApplyStarlight(Vision::ImageRGB565& output, bool dirty)
{
  static JamesStarlightConfig cfg;
  static std::vector<JamesStar> stars;
  static std::minstd_rand gen(0x1A3E5u);
  static u32 frameCount = 0;
  static long lastMtime = -1; // -1 = never checked, 0 = no file
  static long lastStyleMtime = -1;
  static bool lastOffFlag = false;

  // Re-check the settings, style and off switch about every 2 seconds
  if ((frameCount++ % 60) == 0) {
    const long mtime = JamesFileMtime(kJamesStarlightFile);
    const long styleMtime = JamesFileMtime(kJamesStarlightStyle);
    const bool offFlag = JamesFileExists(kJamesStarlightOffFlag);
    if (mtime != lastMtime || styleMtime != lastStyleMtime || offFlag != lastOffFlag) {
      lastMtime = mtime;
      lastStyleMtime = styleMtime;
      lastOffFlag = offFlag;
      cfg = JamesLoadStarlightConfig();
      cfg.style = JamesReadStyle();
      if (offFlag) {
        cfg.enabled = false;
      }
      stars.clear();
    }
  }

  if (!cfg.enabled || cfg.maxStars <= 0) {
    return dirty;
  }

  Vision::Image& mask = _faceCache.img8[_faceCache.finalFace];
  const s32 w = _faceColMax - _faceColMin + 1;
  const s32 h = _faceRowMax - _faceRowMin + 1;
  if (w < 4 || h < 4 || mask.IsEmpty() || output.IsEmpty()) {
    stars.clear();
    return dirty;
  }

  std::uniform_real_distribution<f32> uni(0.f, 1.f);
  const bool shooting = (cfg.style == JamesStarStyle::Shooting);

  // Maybe start a new sparkle somewhere inside an eye (gold and Galaxy Max sparkle more)
  const bool extra = (cfg.style == JamesStarStyle::Gold) || JamesIsGalaxyMax();
  const s32 maxStars = extra ? cfg.maxStars + 3 : cfg.maxStars;
  const f32 chance = extra ? cfg.chance * 3.f : cfg.chance;
  if ((s32)stars.size() < maxStars && uni(gen) < chance) {
    for (int tries = 0; tries < 10; ++tries) {
      const s32 x = Util::Clamp(_faceColMin + (s32)(uni(gen) * w), 0, mask.GetNumCols() - 1);
      const s32 y = Util::Clamp(_faceRowMin + (s32)(uni(gen) * h), 0, mask.GetNumRows() - 1);
      if (mask.GetRow(y)[x] >= kJamesStarMaskThreshold) {
        JamesStar s;
        s.x = (f32)x;
        s.y = (f32)y;
        s.vx = 0.f;
        s.vy = 0.f;
        s.age = 0;
        s.life = std::max<s32>(4, cfg.life / 2 + (s32)(uni(gen) * cfg.life));
        s.r = cfg.r; s.g = cfg.g; s.b = cfg.b;
        if (cfg.style == JamesStarStyle::Gold) {
          // LOYAL FRIEND: warm gold, some brighter than others
          if (uni(gen) < 0.6f) { s.r = 255; s.g = 185; s.b = 20; }
          else                 { s.r = 255; s.g = 225; s.b = 90; }
        } else if (cfg.style == JamesStarStyle::Rainbow || JamesIsGalaxyMax()) {
          JamesHueToRGB(uni(gen), s.r, s.g, s.b);
        } else if (JamesIsRetro()) {
          s.r = 120; s.g = 255; s.b = 120;
        } else if (JamesIsGlitch()) {
          if (uni(gen) < 0.5f) { s.r = 255; s.g = 60; s.b = 220; }
          else                 { s.r = 60;  s.g = 255; s.b = 255; }
        } else if (JamesIsHalloween()) {
          // spooky sparkles: purple or slime green
          if (uni(gen) < 0.5f) { s.r = 190; s.g = 90;  s.b = 255; }
          else                 { s.r = 140; s.g = 255; s.b = 90;  }
        } else if (JamesIsChristmas()) {
          // frosty sparkles: white or ice blue
          if (uni(gen) < 0.5f) { s.r = 255; s.g = 255; s.b = 255; }
          else                 { s.r = 170; s.g = 220; s.b = 255; }
        } else if (JamesIsLunar()) {
          // lucky gold
          s.r = 255; s.g = 205; s.b = 70;
        }
        if (shooting) {
          const f32 dir = (uni(gen) < 0.5f) ? -1.f : 1.f;
          s.vx = dir * (1.5f + 1.5f * uni(gen));   // fast sideways
          s.vy = 0.3f + 0.5f * uni(gen);           // falling a little
          s.life = std::max<s32>(10, cfg.life);
        }
        stars.push_back(s);
        break;
      }
    }
  }

  gJamesStarSolid = (cfg.style == JamesStarStyle::Gold);
  for (auto it = stars.begin(); it != stars.end(); ) {
    JamesStar& s = *it;
    const f32 t = (f32)s.age / (f32)s.life;
    const s32 cx = (s32)std::round(s.x);
    const s32 cy = (s32)std::round(s.y);

    if (shooting) {
      // bright head with a fading tail behind it
      const f32 bright = (t < 0.8f) ? 1.f : (1.f - t) / 0.2f;
      const s32 kTail = 4 + 2 * cfg.size;
      const f32 speed = std::max(0.001f, std::sqrt(s.vx * s.vx + s.vy * s.vy));
      for (s32 k = 0; k <= kTail; ++k) {
        const f32 tx = s.x - s.vx / speed * (f32)k;
        const f32 ty = s.y - s.vy / speed * (f32)k;
        const f32 fade = bright * (1.f - (f32)k / (f32)(kTail + 1));
        JamesDrawStarPixel(output, mask, (s32)std::round(tx), (s32)std::round(ty), fade, s.r, s.g, s.b);
      }
      JamesDrawStarPixel(output, mask, cx, cy - 1, bright * 0.5f, s.r, s.g, s.b);
      JamesDrawStarPixel(output, mask, cx, cy + 1, bright * 0.5f, s.r, s.g, s.b);
      s.x += s.vx;
      s.y += s.vy;
    } else {
      // a little "+" that fades in and out
      const f32 bright = std::sin(t * 3.14159265f);
      JamesDrawStarPixel(output, mask, cx, cy, bright, s.r, s.g, s.b);
      const s32 arm = cfg.size + (gJamesStarSolid ? 1 : 0);   // gold sparkles are a bit bigger
      for (s32 k = 1; k <= arm; ++k) {
        const f32 armBright = bright * (1.f - (f32)k / (f32)(arm + 1));
        JamesDrawStarPixel(output, mask, cx + k, cy, armBright, s.r, s.g, s.b);
        JamesDrawStarPixel(output, mask, cx - k, cy, armBright, s.r, s.g, s.b);
        JamesDrawStarPixel(output, mask, cx, cy + k, armBright, s.r, s.g, s.b);
        JamesDrawStarPixel(output, mask, cx, cy - k, armBright, s.r, s.g, s.b);
      }
    }

    ++s.age;
    const bool offScreen = (s.x < -10.f || s.x > (f32)output.GetNumCols() + 10.f ||
                            s.y < -10.f || s.y > (f32)output.GetNumRows() + 10.f);
    if (s.age >= s.life || offScreen) {
      it = stars.erase(it);
    } else {
      ++it;
    }
  }
  gJamesStarSolid = false;

  return true;
}

bool ProceduralFaceDrawer::ApplyJamesHearts(Vision::ImageRGB565& output, bool dirty)
{
  // JamesOS petting effect: galaxy eyes (v23: the pink hearts mode was removed)
  struct GalaxyStar { f32 u, v, ph, sp; };
  static u32 frameCount = 0;
  static bool petFxOn = false;
  static bool galaxyAlways = false;   // v25: EXTRAS -> PETTING -> ALWAYS
  static f32 amount = 0.f;   // 0 = normal eyes, 1 = full effect (fades in and out)
  static u32 pulse = 0;
  static std::vector<GalaxyStar> galaxyStars;

  if ((frameCount++ % 60) == 0) {
    petFxOn = JamesFileExists(kJamesHeartsOnFlag);
    galaxyAlways = JamesFileExists("/data/data/jamesos/galaxy.always");
  }

  const bool galaxyMax = JamesIsGalaxyMax();
  const bool want = (petFxOn && gJamesPetting) || galaxyMax || galaxyAlways;   // Galaxy Max theme / ALWAYS: always on
  amount += want ? 0.10f : -0.07f;
  amount = Util::Clamp(amount, 0.f, 1.f);
  if (amount <= 0.f) {
    pulse = 0;
    return dirty;
  }
  ++pulse;

  Vision::Image& mask = _faceCache.img8[_faceCache.finalFace];
  if (mask.IsEmpty() || output.IsEmpty()) {
    return dirty;
  }

  if (galaxyStars.empty()) {
    std::minstd_rand gen(0x6A1Au);
    std::uniform_real_distribution<f32> uni(0.f, 1.f);
    for (int i = 0; i < 26; ++i) {
      galaxyStars.push_back({0.06f + 0.88f * uni(gen), 0.06f + 0.88f * uni(gen), uni(gen) * 6.2832f, 0.12f + 0.2f * uni(gen)});
    }
  }

  const f32 t = (f32)pulse;

  const Rectangle<f32>* boxes[2] = {&_leftBBox, &_rightBBox};
  for (int e = 0; e < 2; ++e) {
    const Rectangle<f32>* box = boxes[e];
    const f32 bw = box->GetWidth(), bh = box->GetHeight();
    if (bw < 4.f || bh < 6.f) { continue; } // eye closed / blinking

    const s32 x0 = std::max<s32>(0, (s32)std::floor(box->GetX()));
    const s32 x1 = std::min<s32>(output.GetNumCols() - 1, (s32)std::ceil(box->GetXmax()));
    const s32 y0 = std::max<s32>(0, (s32)std::floor(box->GetY()));
    const s32 y1 = std::min<s32>(output.GetNumRows() - 1, (s32)std::ceil(box->GetYmax()));
    if (x1 < x0 || y1 < y0) { continue; }

    // galaxy: precompute the parts that only depend on the column or the row (fast)
    const s32 nCols = x1 - x0 + 1;
    std::vector<f32> colHue(nCols), colSin(nCols), colCos(nCols);
    for (s32 i = 0; i < nCols; ++i) {
      const f32 u = ((f32)(x0 + i) - box->GetX()) / bw;
      colHue[i] = 0.12f * std::sin(u * 5.f + t * 0.06f + (f32)e);
      colSin[i] = std::sin(u * 3.f + t * 0.04f);
      colCos[i] = std::cos(u * 3.f + t * 0.04f);
    }

    for (s32 y = y0; y <= y1 && y < mask.GetNumRows(); ++y) {
      Vision::PixelRGB565* outRow = output.GetRow(y);
      const u8* maskRow = mask.GetRow(y);
      const f32 v = ((f32)y - box->GetY()) / bh;
      const f32 rowHue = 0.72f + 0.08f * std::cos(v * 4.f - t * 0.05f);
      const f32 rowSin = std::sin(v * 2.f), rowCos = std::cos(v * 2.f);
      for (s32 x = x0; x <= x1 && x < mask.GetNumCols(); ++x) {
        const u8 m = maskRow[x];
        if (m < kJamesStarMaskThreshold) { continue; }
        Vision::PixelRGB565& px = outRow[x];
        const f32 lum = (f32)m / 255.f;
        f32 R, G, B;
        {
          // swirling purple / pink / blue nebula
          const s32 i = x - x0;
          const f32 s = colSin[i] * rowCos + colCos[i] * rowSin;   // sin(a + b)
          const f32 val = 0.45f + 0.35f * s * s;
          JamesHsvToRGB(rowHue + colHue[i], 0.8f, val * lum, R, G, B);
        }
        // fade between the normal eye and the effect
        R = px.r() + (R - (f32)px.r()) * amount;
        G = px.g() + (G - (f32)px.g()) * amount;
        B = px.b() + (B - (f32)px.b()) * amount;
        px.SetValue((u8)Util::Clamp(R, 0.f, 255.f),
                    (u8)Util::Clamp(G, 0.f, 255.f),
                    (u8)Util::Clamp(B, 0.f, 255.f));
      }
    }

    {
      // twinkling stars inside the galaxy
      for (const auto& gs : galaxyStars) {
        const f32 su = 0.06f + std::fmod(gs.u - 0.06f + 0.37f * (f32)e, 0.88f); // different stars in each eye
        const f32 sv = 0.06f + std::fmod(gs.v - 0.06f + 0.21f * (f32)e, 0.88f);
        const s32 sx = (s32)(box->GetX() + su * bw);
        const s32 sy = (s32)(box->GetY() + sv * bh);
        const f32 tw = (0.5f + 0.5f * std::sin(gs.ph + t * gs.sp)) * amount;
        JamesDrawStarPixel(output, mask, sx, sy, tw, 255, 255, 255);
        if (tw > 0.75f) {
          JamesDrawStarPixel(output, mask, sx + 1, sy, tw * 0.5f, 255, 255, 255);
          JamesDrawStarPixel(output, mask, sx - 1, sy, tw * 0.5f, 255, 255, 255);
          JamesDrawStarPixel(output, mask, sx, sy + 1, tw * 0.5f, 255, 255, 255);
          JamesDrawStarPixel(output, mask, sx, sy - 1, tw * 0.5f, 255, 255, 255);
        }
      }
    }
  }

  return true;
}

bool ProceduralFaceDrawer::ApplyJamesExtras(Vision::ImageRGB565& output, bool dirty)
{
  struct Particle { f32 x, y, vx, vy; u8 r, g, b; };
  struct Bat { f32 x, y, vx, phase; };
  struct SkyStar { f32 x, y, ph, sp, b; };

  static std::minstd_rand gen(0x5EEDu);
  static std::vector<Particle> confetti;
  static s32 celebrateFrames = 0;
  static std::vector<Bat> bats;
  static f32 flicker = 1.f, flickerTarget = 1.f;
  static std::vector<SkyStar> sky;
  static s32 closedFrames = 0;
  static f32 sleepAmt = 0.f;
  static u32 frame = 0;
  ++frame;

  Vision::Image& mask = _faceCache.img8[_faceCache.finalFace];
  if (mask.IsEmpty() || output.IsEmpty()) {
    return dirty;
  }
  std::uniform_real_distribution<f32> uni(0.f, 1.f);
  const s32 W = output.GetNumCols();
  const s32 H = output.GetNumRows();
  bool drew = false;

  // ---------------- FUN EYES: Halloween = glowing pumpkin eyes + grin ----------------
  // (v24: the only fun eyes; the other holidays keep Vector's own eyes, with theme colors.)
  // Drawn inside each eye's box, so he still blinks and looks around. EXTRAS -> FUN EYES.
  // While you pet him with galaxy/hearts on, the petting effect wins.
  static bool jamesPetFxOn = false;
  if (frame % 60 == 1) { jamesPetFxOn = JamesFileExists(kJamesHeartsOnFlag); }
  const s32 jamesMonth = (gJamesDateKey / 100) % 100, jamesDay = gJamesDateKey % 100;
  const bool jamesYearMode = JamesIsTheme(JamesTheme::NewYear) && jamesMonth == 1 && jamesDay == 1;   // after midnight
  {
    const bool shiny = gJamesShiny;   // v23: a shiny day beats the theme's eye shape
    const bool pumpkin = !shiny && JamesIsHalloween();
    if ((shiny || (pumpkin && JamesFunEyesToday((JamesTheme)gJamesTheme.load(), gJamesDateKey)))
        && !(gJamesPetting && jamesPetFxOn)) {
      const Rectangle<f32>* boxes[2] = {&_leftBBox, &_rightBBox};
      const f32 glow = 0.85f + 0.15f * std::sin((f32)frame * 0.31f) * std::sin((f32)frame * 0.17f);   // candle flicker
      for (s32 e = 0; e < 2; ++e) {
        const Rectangle<f32>* box = boxes[e];
        const f32 bw = box->GetWidth(), bh = box->GetHeight();
        if (bw < 4.f || bh < 3.f) { continue; }
        const s32 x0 = std::max<s32>(0, (s32)std::floor(box->GetX()));
        const s32 x1 = std::min<s32>(W - 1, (s32)std::ceil(box->GetXmax()));
        const s32 y0 = std::max<s32>(0, (s32)std::floor(box->GetY()));
        const s32 y1 = std::min<s32>(H - 1, (s32)std::ceil(box->GetYmax()));
        for (s32 y = y0; y <= y1 && y < mask.GetNumRows(); ++y) {
          Vision::PixelRGB565* row = output.GetRow(y);
          const u8* mrow = mask.GetRow(y);
          for (s32 x = x0; x <= x1 && x < mask.GetNumCols(); ++x) {
            const u8 m = mrow[x];
            if (m < kJamesStarMaskThreshold) { continue; }
            const f32 lum = (f32)m / 255.f;
            const f32 u = ((f32)x - box->GetX()) / bw, v = ((f32)y - box->GetY()) / bh;
            f32 R = 0.f, G = 0.f, B = 0.f;
            if (shiny) {
              // shimmering gold: a bright band slowly sweeps across the eye
              const f32 sw = 0.5f + 0.5f * std::sin((u * 1.3f + v * 0.9f) * 6.2832f - (f32)frame * 0.12f);
              R = 255.f; G = 165.f + 75.f * sw; B = 25.f + 150.f * sw * sw * sw * sw;
            } else if (pumpkin) {
              // triangle (point at the top) with a brighter flame inside
              const f32 half = 0.5f * (v - 0.04f) / 0.92f;
              if (v >= 0.04f && std::fabs(u - 0.5f) <= half) {
                const bool flame = v > 0.42f && std::fabs(u - 0.5f) <= 0.24f * (v - 0.42f) / 0.54f;
                if (flame) { R = 255.f; G = 225.f; B = 110.f; } else { R = 255.f; G = 125.f; B = 15.f; }
                R *= glow; G *= glow; B *= glow;
              }
            }
            row[x].SetValue((u8)Util::Clamp(R * lum, 0.f, 255.f), (u8)Util::Clamp(G * lum, 0.f, 255.f),
                            (u8)Util::Clamp(B * lum, 0.f, 255.f));
          }
        }
      }
      // shiny: little white sparkles that pop in and out
      if (shiny) {
        for (s32 e = 0; e < 2; ++e) {
          const Rectangle<f32>* box = boxes[e];
          const s32 bx = (s32)box->GetX(), by = (s32)box->GetY();
          const s32 bw = std::max<s32>(1, (s32)box->GetWidth()), bh = std::max<s32>(1, (s32)box->GetHeight());
          for (s32 k = 0; k < 3; ++k) {
            const u32 hsh = (u32)(frame / 12 + (u32)(k * 7 + e * 13)) * 2654435761u;
            const s32 sx = bx + 2 + (s32)(hsh % (u32)std::max<s32>(1, bw - 4));
            const s32 sy = by + 2 + (s32)((hsh >> 12) % (u32)std::max<s32>(1, bh - 4));
            const f32 tw = std::sin(3.1416f * (f32)(frame % 12) / 12.f);
            JamesDrawStarPixel(output, mask, sx, sy, tw, 255, 255, 255);
            JamesDrawStarPixel(output, mask, sx + 1, sy, tw * 0.6f, 255, 255, 255);
            JamesDrawStarPixel(output, mask, sx - 1, sy, tw * 0.6f, 255, 255, 255);
            JamesDrawStarPixel(output, mask, sx, sy + 1, tw * 0.6f, 255, 255, 255);
            JamesDrawStarPixel(output, mask, sx, sy - 1, tw * 0.6f, 255, 255, 255);
          }
        }
      }
      // pumpkin: a jagged grin under the eyes
      if (pumpkin) {
        const s32 sc = (W >= 184) ? 2 : 1;
        const s32 gx0 = (s32)(_leftBBox.GetX() + 0.3f * _leftBBox.GetWidth());
        const s32 gx1 = (s32)(_rightBBox.GetXmax() - 0.3f * _rightBBox.GetWidth());
        const s32 gy = (s32)(std::max(_leftBBox.GetYmax(), _rightBBox.GetYmax()) + 4.f * sc);
        for (s32 x = gx0; x <= gx1; ++x) {
          const s32 zig = ((x - gx0) / (3 * sc)) % 2 == 0 ? ((x - gx0) % (3 * sc)) : (3 * sc - (x - gx0) % (3 * sc));
          for (s32 t = 0; t < sc; ++t) {
            JamesSetPx(output, x, gy + zig + t, (u8)(255 * glow), (u8)(125 * glow), 15);
          }
        }
      }
      drew = true;
    }
  }

  // ---------------- low battery: sleepy eyes + a blinking battery icon ----------------
  {
    static f32 droop = 0.f;
    static bool isLow = false;
    static bool sleepyOff = false;
    if (frame % 60 == 1) { sleepyOff = JamesFileExists(kJamesSleepyOffFlag); }
    const f32 v = gJamesBattV;
    if (gJamesOnCharger || v < 1.f || sleepyOff) { isLow = false; }
    else if (v < 3.68f) { isLow = true; }       // about 15% left
    else if (v > 3.75f) { isLow = false; }      // a little margin so it doesn't flicker
    droop += ((isLow ? 1.f : 0.f) - droop) * 0.03f;
    if (droop > 0.01f) {
      // a slow, heavy blink every 5 seconds
      const u32 cyc = frame % 150;
      const f32 blink = (cyc < 24) ? std::sin((f32)cyc / 24.f * 3.1416f) : 0.f;
      const f32 lid = std::min(1.f, droop * (0.42f + 0.45f * blink));
      const Rectangle<f32>* eyes[2] = {&_leftBBox, &_rightBBox};
      for (s32 e = 0; e < 2; ++e) {
        const Rectangle<f32>* box = eyes[e];
        const f32 bx = box->GetX(), by = box->GetY(), bw = box->GetWidth(), bh = box->GetHeight();
        if (bw < 2.f || bh < 2.f) { continue; }
        for (s32 x = (s32)bx - 1; x <= (s32)(bx + bw) + 1; ++x) {
          // straight lid, the outer corner hanging a little lower (tired look)
          f32 u = Util::Clamp(((f32)x - bx) / bw, 0.f, 1.f);
          if (e == 1) { u = 1.f - u; }               // outer side = left of left eye, right of right eye
          const f32 slant = 1.f + 0.18f * (1.f - u) - 0.06f;
          const s32 yLid = (s32)(by - 2.f + (bh + 2.f) * lid * slant);
          for (s32 y = std::max<s32>(0, (s32)by - 3); y <= std::min<s32>(H - 1, yLid); ++y) {
            JamesSetPx(output, x, y, 0, 0, 0);
          }
        }
      }
      // tired eyes are a bit dimmer
      const f32 dim = 1.f - 0.25f * droop;
      const s32 r0 = std::max<s32>(0, _faceRowMin), r1 = std::min<s32>(H - 1, _faceRowMax);
      const s32 c0 = std::max<s32>(0, _faceColMin), c1 = std::min<s32>(W - 1, _faceColMax);
      for (s32 y = r0; y <= r1; ++y) {
        Vision::PixelRGB565* row = output.GetRow(y);
        for (s32 x = c0; x <= c1; ++x) {
          row[x].SetValue((u8)(row[x].r() * dim), (u8)(row[x].g() * dim), (u8)(row[x].b() * dim));
        }
      }
      // small battery icon in the top right corner, blinking
      if (droop > 0.5f && (frame / 20) % 2 == 0) {
        const s32 ix = W - 17, iy = 3;
        JamesFillRect(output, ix, iy, 12, 1, 255, 255, 255);
        JamesFillRect(output, ix, iy + 6, 12, 1, 255, 255, 255);
        JamesFillRect(output, ix, iy, 1, 7, 255, 255, 255);
        JamesFillRect(output, ix + 11, iy, 1, 7, 255, 255, 255);
        JamesFillRect(output, ix + 12, iy + 2, 2, 3, 255, 255, 255);
        JamesFillRect(output, ix + 2, iy + 2, 2, 3, 255, 60, 60);
      }
      drew = true;
    }
  }

  // ---------------- Halloween: candle flicker + bats ----------------
  if (JamesIsHalloween()) {
    if (frame % 4 == 0) {
      flickerTarget = 0.80f + 0.20f * uni(gen);
    }
    flicker += (flickerTarget - flicker) * 0.3f;
    const s32 r0 = std::max<s32>(0, _faceRowMin), r1 = std::min<s32>(H - 1, _faceRowMax);
    const s32 c0 = std::max<s32>(0, _faceColMin), c1 = std::min<s32>(W - 1, _faceColMax);
    for (s32 y = r0; y <= r1; ++y) {
      Vision::PixelRGB565* row = output.GetRow(y);
      for (s32 x = c0; x <= c1; ++x) {
        Vision::PixelRGB565& px = row[x];
        px.SetValue((u8)(px.r() * flicker), (u8)(px.g() * flicker), (u8)(px.b() * flicker));
      }
    }

    // bats fly across the eyes as dark silhouettes
    if (bats.size() < 2 && uni(gen) < 0.012f && _faceRowMax > _faceRowMin + 8) {
      Bat b;
      const bool fromLeft = uni(gen) < 0.5f;
      b.vx = (fromLeft ? 1.f : -1.f) * (0.8f + 0.6f * uni(gen));
      b.x = fromLeft ? (f32)_faceColMin - 28.f : (f32)_faceColMax + 2.f;
      b.y = (f32)_faceRowMin + 4.f + uni(gen) * (f32)std::max<s32>(1, _faceRowMax - _faceRowMin - 12);
      b.phase = uni(gen) * 6.28f;
      bats.push_back(b);
    }
    const s32 kBatScale = (output.GetNumCols() >= 184) ? 2 : 1; // smaller on the 2.0 screen
    for (auto it = bats.begin(); it != bats.end(); ) {
      Bat& b = *it;
      const char** sprite = kJamesBat[(frame / 4) % 2];
      const f32 by = b.y + 2.f * std::sin(b.phase + (f32)frame * 0.2f);
      for (s32 row = 0; row < kJamesBatH; ++row) {
        for (s32 col = 0; col < kJamesBatW; ++col) {
          if (sprite[row][col] != 'X') { continue; }
          for (s32 dy = 0; dy < kBatScale; ++dy) {
            for (s32 dx = 0; dx < kBatScale; ++dx) {
              const s32 px = (s32)b.x + col * kBatScale + dx;
              const s32 py = (s32)by + row * kBatScale + dy;
              if (JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] >= kJamesStarMaskThreshold) {
                output.GetRow(py)[px].SetValue(0, 0, 0);
              }
            }
          }
        }
      }
      b.x += b.vx;
      if (b.x < (f32)_faceColMin - 30.f || b.x > (f32)_faceColMax + 30.f) {
        it = bats.erase(it);
      } else {
        ++it;
      }
    }
    drew = true;
  } else {
    bats.clear();
  }

  // ---------------- Christmas: falling snow + a string of twinkling lights ----------------
  {
    struct Flake { f32 x, y, vy, ph; s32 big; };
    static std::vector<Flake> snow;
    if (JamesIsChristmas()) {
      if (snow.empty()) {
        const s32 n = (W * H) / 360;
        for (s32 i = 0; i < n; ++i) {
          snow.push_back({uni(gen) * W, uni(gen) * H, 0.18f + 0.35f * uni(gen), uni(gen) * 6.28f, uni(gen) < 0.3f ? 1 : 0});
        }
      }
      for (auto& f : snow) {
        f.y += f.vy;
        f.x += 0.25f * std::sin(f.ph + (f32)frame * 0.05f);
        if (f.y > H + 2) { f.y = -2.f; f.x = uni(gen) * W; }
        const s32 x = (s32)f.x, y = (s32)f.y;
        for (s32 dy = 0; dy <= f.big; ++dy) {
          for (s32 dx = 0; dx <= f.big; ++dx) {
            const s32 px = x + dx, py = y + dy;
            if (JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] < 64) {   // snow falls behind the eyes
              JamesLightenPixel(output, px, py, 1.f, 240, 248, 255);
            }
          }
        }
      }
      // string of lights along the top
      static const u8 kBulbs[4][3] = {{255, 60, 60}, {70, 220, 100}, {255, 200, 60}, {90, 160, 255}};
      const s32 bs = (W >= 184) ? 3 : 2;
      const s32 spacing = (W >= 184) ? 14 : 12;
      for (s32 x = 0; x < W; ++x) {
        const f32 u = (f32)(x % spacing) / (f32)spacing;
        const s32 y = 2 + (s32)(2.5f * std::sin(u * 3.1416f));
        JamesSetPx(output, x, y, 40, 70, 45);   // the wire
      }
      for (s32 k = 0, x = spacing / 2; x < W; ++k, x += spacing) {
        const f32 tw = 0.45f + 0.55f * (0.5f + 0.5f * std::sin((f32)frame * 0.12f + (f32)k * 1.7f));
        const u8* c = kBulbs[k % 4];
        JamesFillRect(output, x - bs / 2, 4, bs, bs + 1, (u8)(c[0] * tw), (u8)(c[1] * tw), (u8)(c[2] * tw));
      }
      drew = true;
    } else {
      snow.clear();
    }
  }

  // ---------------- Lunar New Year: two red lanterns swinging at the sides ----------------
  if (JamesIsLunar()) {
    const s32 sc = (W >= 184) ? 2 : 1;
    const f32 swing = std::sin((f32)frame * 0.05f);
    const s32 lanternX[2] = {(s32)(W * 0.075f), (s32)(W * 0.925f)};
    for (s32 k = 0; k < 2; ++k) {
      const s32 sway = (s32)((k == 0 ? 1.f : -1.f) * swing * 2.f * sc);
      const s32 cx = lanternX[k] + sway;
      const s32 topY = 6 * sc + (s32)(H * 0.12f);
      // string from the top
      for (s32 y = 0; y < topY; ++y) {
        const s32 x = lanternX[k] + (sway * y) / std::max<s32>(1, topY);
        JamesSetPx(output, x, y, 200, 160, 60);
      }
      // gold cap, red body (rounded), gold cap, tassel
      const s32 bw = 5 * sc, bh = 7 * sc;
      JamesFillRect(output, cx - 2 * sc, topY, 4 * sc + 1, sc + 1, 255, 200, 60);
      for (s32 y = 0; y < bh; ++y) {
        const f32 v = ((f32)y + 0.5f) / (f32)bh * 2.f - 1.f;               // -1 .. 1
        const s32 half = (s32)(bw * std::sqrt(std::max(0.f, 1.f - 0.55f * v * v)));
        for (s32 x = -half; x <= half; ++x) {
          const bool rib = (half > 2) && ((x == -half / 2) || (x == half / 2));
          const f32 glow = 0.75f + 0.25f * std::cos(v * 1.5f);
          JamesSetPx(output, cx + x, topY + sc + 1 + y,
                     (u8)((rib ? 170 : 230) * glow), (u8)((rib ? 20 : 30) * glow), (u8)((rib ? 20 : 25) * glow));
        }
      }
      const s32 by = topY + sc + 1 + bh;
      JamesFillRect(output, cx - 2 * sc, by, 4 * sc + 1, sc + 1, 255, 200, 60);
      for (s32 y = by + sc + 1; y < by + sc + 1 + 4 * sc; ++y) {
        JamesSetPx(output, cx, y, 255, 200, 60);
      }
    }
    drew = true;
  }

  // ---------------- holiday themes (v22) ----------------
  {
    const s32 sc = (W >= 184) ? 2 : 1;
    // little pixel pictures, one string per row ('X' = lit)
    auto sprite = [&](const char* const* rows, s32 nRows, s32 cx, s32 cy, s32 scale, f32 a, u8 r, u8 g, u8 b, bool behindEyes) {
      const s32 nCols = (s32)strlen(rows[0]);
      for (s32 ry = 0; ry < nRows; ++ry) {
        for (s32 rx = 0; rx < nCols; ++rx) {
          if (rows[ry][rx] != 'X') { continue; }
          for (s32 dy = 0; dy < scale; ++dy) {
            for (s32 dx = 0; dx < scale; ++dx) {
              const s32 px = cx + (rx - nCols / 2) * scale + dx, py = cy + (ry - nRows / 2) * scale + dy;
              if (behindEyes && JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] >= 64) { continue; }
              JamesLightenPixel(output, px, py, a, r, g, b);
            }
          }
        }
      }
    };
    static const char* const kHeart[] = {".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..."};
    static const char* const kClover[] = {"..XX.XX..", ".XXXXXXX.", "..XXXXX..", ".XXXXXXX.", "XXXX.XXXX", "XXX.X.XXX", "....X....", "....X...."};
    static const char* const kLeaf[] = {"..X..", ".XXX.", "XXXXX", ".XXX.", "..X.."};
    static const char* const kCoin[] = {".XXX.", "XXXXX", "XXXXX", "XXXXX", ".XXX."};

    // ---- fireworks: New Year (gold + colors) and July (red, white, blue) ----
    {
      struct Spark { f32 x, y, vx, vy; s32 age, life; u8 r, g, b; };
      static std::vector<Spark> sparks;
      // New Year: fireworks only after midnight (before that: party hats + confetti, below)
      const bool newYear = jamesYearMode, july = JamesIsTheme(JamesTheme::Fireworks);
      if (newYear || july) {
        if (frame % 40 == 0) {
          const f32 cx = W * (0.12f + 0.76f * uni(gen)), cy = H * (0.12f + 0.3f * uni(gen));
          u8 r = 255, g = 210, b = 80;
          if (july) {
            static const u8 kRWB[3][3] = {{255, 50, 50}, {255, 255, 255}, {70, 120, 255}};
            const s32 k = (s32)(uni(gen) * 3.f) % 3;
            r = kRWB[k][0]; g = kRWB[k][1]; b = kRWB[k][2];
          } else if (uni(gen) < 0.5f) {
            JamesHueToRGB(uni(gen), r, g, b);
          }
          const s32 n = 24;
          for (s32 i = 0; i < n; ++i) {
            const f32 a = 6.2832f * (f32)i / (f32)n + 0.2f * uni(gen);
            const f32 sp = (0.6f + 0.5f * uni(gen)) * (f32)sc;
            sparks.push_back({cx, cy, std::cos(a) * sp, std::sin(a) * sp, 0, 28 + (s32)(12.f * uni(gen)), r, g, b});
          }
        }
        for (auto& p : sparks) {
          p.x += p.vx; p.y += p.vy; p.vy += 0.025f; p.vx *= 0.97f; p.vy *= 0.97f; ++p.age;
          const s32 px = (s32)p.x, py = (s32)p.y;
          if (JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] < 64) {
            JamesLightenPixel(output, px, py, 1.f - (f32)p.age / (f32)p.life, p.r, p.g, p.b);
          }
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& p) { return p.age >= p.life; }), sparks.end());
        drew = true;
      } else {
        sparks.clear();
      }
    }

    // ---- floating shapes: hearts (Valentine's, Mother's Day), clovers (St Patrick's), leaves (Thanksgiving) ----
    {
      struct Floater { f32 x, y, vy, ph; s32 kind; u8 r, g, b; };
      static std::vector<Floater> floaters;
      static s32 lastTheme = -1;
      const bool val = JamesIsTheme(JamesTheme::Valentine), mom = JamesIsTheme(JamesTheme::MothersDay);
      const bool pat = JamesIsTheme(JamesTheme::StPatrick), thx = JamesIsTheme(JamesTheme::Thanksgiving);
      const bool party = JamesIsTheme(JamesTheme::NewYear) && !jamesYearMode;   // New Year's Eve
      if (val || mom || pat || thx || party) {
        if (lastTheme != gJamesTheme * 2 + (party ? 1 : 0)) { floaters.clear(); lastTheme = gJamesTheme * 2 + (party ? 1 : 0); }
        const s32 want = mom ? 3 : (thx ? 9 : (party ? 26 : (pat ? 8 : 6)));
        while ((s32)floaters.size() < want) {
          Floater f;
          f.x = uni(gen) * W;
          f.ph = uni(gen) * 6.28f;
          if (thx) {   // leaves fall down
            f.y = -uni(gen) * H; f.vy = 0.25f + 0.25f * uni(gen); f.kind = 2;
            static const u8 kLeafCol[3][3] = {{230, 90, 30}, {200, 50, 30}, {230, 160, 40}};
            const s32 k = (s32)(uni(gen) * 3.f) % 3;
            f.r = kLeafCol[k][0]; f.g = kLeafCol[k][1]; f.b = kLeafCol[k][2];
          } else if (pat) {   // gold coins rain down (and a few clovers)
            f.y = -uni(gen) * H; f.vy = 0.3f + 0.3f * uni(gen);
            f.kind = (uni(gen) < 0.25f) ? 1 : 3;
            f.r = 60; f.g = 200; f.b = 80;
          } else if (party) {   // confetti
            f.y = -uni(gen) * H; f.vy = 0.3f + 0.4f * uni(gen); f.kind = 4;
            static const u8 kConf[5][3] = {{255, 80, 160}, {255, 220, 80}, {90, 200, 255}, {140, 255, 120}, {255, 255, 255}};
            const s32 k = (s32)(uni(gen) * 5.f) % 5;
            f.r = kConf[k][0]; f.g = kConf[k][1]; f.b = kConf[k][2];
          } else {   // hearts float up
            f.y = H + uni(gen) * H; f.vy = -(0.2f + 0.25f * uni(gen)); f.kind = 0;
            f.r = 255; f.g = mom ? 150 : 90; f.b = mom ? 190 : 150;
          }
          floaters.push_back(f);
        }
        for (auto& f : floaters) {
          f.y += f.vy;
          const f32 x = f.x + 3.f * std::sin(f.ph + (f32)frame * 0.04f);
          if (f.kind == 0) { sprite(kHeart, 6, (s32)x, (s32)f.y, sc, 0.8f, f.r, f.g, f.b, true); }
          else if (f.kind == 1) { sprite(kClover, 8, (s32)x, (s32)f.y, sc, 0.85f, f.r, f.g, f.b, true); }
          else if (f.kind == 3) {   // gold coin with a shine
            sprite(kCoin, 5, (s32)x, (s32)f.y, sc, 0.95f, 235, 175, 30, true);
            JamesLightenPixel(output, (s32)x - sc, (s32)f.y - sc, 0.9f, 255, 245, 170);
          }
          else if (f.kind == 4) {   // confetti piece
            for (s32 dy = 0; dy < 2 * sc; ++dy) { for (s32 dx = 0; dx < sc; ++dx) {
              const s32 px = (s32)x + dx, py = (s32)f.y + dy;
              if (!(JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] >= 64)) { JamesSetPx(output, px, py, f.r, f.g, f.b); }
            } }
          }
          else { sprite(kLeaf, 5, (s32)x, (s32)f.y, sc, 0.9f, f.r, f.g, f.b, true); }
          if (f.vy < 0.f && f.y < -10.f) { f.y = H + 8.f; f.x = uni(gen) * W; }
          if (f.vy > 0.f && f.y > H + 10.f) { f.y = -8.f; f.x = uni(gen) * W; }
        }
        // Mother's Day: soft blush under the eyes
        if (mom) {
          const Rectangle<f32>* eyes[2] = {&_leftBBox, &_rightBBox};
          for (s32 e = 0; e < 2; ++e) {
            const f32 cx = eyes[e]->GetX() + eyes[e]->GetWidth() * (e == 0 ? 0.3f : 0.7f);
            const f32 cy = eyes[e]->GetY() + eyes[e]->GetHeight() + 3.f * sc;
            const f32 rx = eyes[e]->GetWidth() * 0.22f, ry = 2.5f * sc;
            for (s32 y = (s32)(cy - ry); y <= (s32)(cy + ry); ++y) {
              for (s32 x = (s32)(cx - rx); x <= (s32)(cx + rx); ++x) {
                const f32 u = (x - cx) / rx, v = (y - cy) / ry;
                if (u * u + v * v <= 1.f && JamesInBounds(output, mask, x, y) && mask.GetRow(y)[x] < 64) {
                  JamesLightenPixel(output, x, y, 0.55f, 255, 120, 160);
                }
              }
            }
          }
        }
        drew = true;
      } else {
        floaters.clear();
        lastTheme = -1;
      }
    }

    // ---- Easter: pastel eggs bobbing in the bottom corners ----
    if (JamesIsTheme(JamesTheme::Easter)) {
      static const u8 kEgg[3][2][3] = {{{255, 170, 210}, {255, 240, 150}},
                                       {{170, 230, 200}, {200, 170, 255}},
                                       {{200, 180, 255}, {255, 200, 170}}};
      const s32 ew = 5 * sc, eh = 7 * sc;
      const s32 xs[3] = {(s32)(W * 0.08f), (s32)(W * 0.92f), (s32)(W * 0.5f)};
      for (s32 k = 0; k < 3; ++k) {
        if (k == 2 && W < 184) { continue; }
        const s32 cx = xs[k];
        const s32 cy = H - eh - 2 + (s32)(1.5f * sc * std::sin((f32)frame * 0.08f + k * 2.f));
        for (s32 y = -eh; y <= eh; ++y) {
          const f32 v = (f32)y / (f32)eh;
          const f32 widen = (v < 0.f) ? (1.f + 0.35f * v) : 1.f;   // narrower at the top, like an egg
          const s32 half = (s32)((f32)ew * widen * std::sqrt(std::max(0.f, 1.f - v * v)));
          const bool stripe = (y > -eh / 4 && y < eh / 6);
          const u8* c = kEgg[k][stripe ? 1 : 0];
          for (s32 x = -half; x <= half; ++x) {
            if (JamesInBounds(output, mask, cx + x, cy + y) && mask.GetRow(cy + y)[cx + x] >= 64) { continue; }
            JamesSetPx(output, cx + x, cy + y, c[0], c[1], c[2]);
          }
        }
      }
      drew = true;
    }

    // ---- April Fools: every ~12 s a fake glitch (and on the day a banner says APRIL FOOLS) ----
    if (JamesIsTheme(JamesTheme::AprilFools)) {
      static s32 burst = 0, nextBurst = 200;
      if (burst == 0 && --nextBurst <= 0) { burst = 14; nextBurst = 300 + (s32)(uni(gen) * 150.f); }
      if (burst > 0) {
        --burst;
        std::vector<Vision::PixelRGB565> tmp(W);
        for (s32 k = 0; k < 3; ++k) {
          const s32 by = (s32)(uni(gen) * (f32)H), bh = 2 + (s32)(uni(gen) * 8.f);
          const s32 shift = (s32)((uni(gen) - 0.5f) * 30.f);
          for (s32 y = by; y < std::min<s32>(H, by + bh); ++y) {
            Vision::PixelRGB565* row = output.GetRow(y);
            for (s32 x = 0; x < W; ++x) { tmp[x] = row[x]; }
            for (s32 x = 0; x < W; ++x) { row[x] = tmp[(x - shift + W * 2) % W]; }
          }
        }
        drew = true;
      }
    }
  }

  // ---------------- v23 accessories: hats, ears, snow caps, rainbow, leaf pile ----------------
  {
    const Rectangle<f32>* boxes[2] = {&_leftBBox, &_rightBBox};
    auto fillEllipse = [&](f32 cx, f32 cy, f32 rx, f32 ry, u8 r, u8 g, u8 b) {
      for (s32 y = (s32)(cy - ry); y <= (s32)(cy + ry); ++y) {
        for (s32 x = (s32)(cx - rx); x <= (s32)(cx + rx); ++x) {
          const f32 dx = ((f32)x + 0.5f - cx) / rx, dy = ((f32)y + 0.5f - cy) / ry;
          if (dx * dx + dy * dy <= 1.f) { JamesSetPx(output, x, y, r, g, b); }
        }
      }
    };
    // triangle; color can change by height (stripes) through the callback
    auto fillTri = [&](f32 ax, f32 ay, f32 bx, f32 by, f32 qx, f32 qy, const std::function<void(s32, s32, f32)>& put) {
      const s32 minx = (s32)std::floor(std::min({ax, bx, qx})), maxx = (s32)std::ceil(std::max({ax, bx, qx}));
      const s32 miny = (s32)std::floor(std::min({ay, by, qy})), maxy = (s32)std::ceil(std::max({ay, by, qy}));
      const f32 den = (by - qy) * (ax - qx) + (qx - bx) * (ay - qy);
      if (std::fabs(den) < 1e-3f) { return; }
      for (s32 y = miny; y <= maxy; ++y) {
        for (s32 x = minx; x <= maxx; ++x) {
          const f32 px = (f32)x + 0.5f, py = (f32)y + 0.5f;
          const f32 l1 = ((by - qy) * (px - qx) + (qx - bx) * (py - qy)) / den;
          const f32 l2 = ((qy - ay) * (px - qx) + (ax - qx) * (py - qy)) / den;
          if (l1 >= 0.f && l2 >= 0.f && l1 + l2 <= 1.f) { put(x, y, l1); }   // l1 = 1 at the first corner
        }
      }
    };
    // where the top of each eye is when it's OPEN (so a blink doesn't move the hat), and its open height
    static f32 openH[2] = {0.f, 0.f};
    f32 eyeCx[2], eyeTopY[2], eyeW[2], eyeOpenH[2];
    for (s32 e = 0; e < 2; ++e) {
      const Rectangle<f32>* box = boxes[e];
      const f32 bh = box->GetHeight();
      f32& oh = openH[e];
      if (bh >= 0.85f * oh) { oh = bh; } else { oh = std::max(bh, oh - 0.3f); }
      eyeW[e] = std::max(box->GetWidth(), 6.f);
      eyeCx[e] = box->GetX() + 0.5f * box->GetWidth();
      eyeOpenH[e] = std::max(oh, 4.f);
      eyeTopY[e] = box->GetY() + 0.5f * bh - 0.5f * eyeOpenH[e];
    }
    // Vector's eyes are big and close to the top of the screen. A hat sits on top of the eye and only
    // covers a little of it (10%); it gets as tall as the space above allows (up to its full size),
    // and its other parts shrink with it. k = how big it could be (1 = full size)
    auto fitHat = [&](s32 e, f32 designH, f32& base, f32& h, f32& k) {
      base = eyeTopY[e] + 0.1f * eyeOpenH[e];
      h = std::min(designH, std::max(base - 1.f, 0.35f * designH));
      base = std::max(base, h + 1.f);
      k = h / std::max(designH, 1.f);
    };

    // Christmas: snow on both eyes + a Santa hat on the right eye
    if (JamesIsChristmas()) {
      {   // a little snow on the left eye (the right one wears the hat)
        const f32 cx = eyeCx[0], top = eyeTopY[0], bw = eyeW[0];
        const f32 ry = Util::Clamp(0.22f * (top + 0.1f * eyeOpenH[0]), 2.f, 0.12f * bw);
        fillEllipse(cx, top + 0.3f * ry, 0.47f * bw, ry, 250, 252, 255);
      }
      f32 base, h, k; fitHat(1, 0.62f * eyeW[1], base, h, k);   // base = bottom of the white fur
      const f32 cx = eyeCx[1], bw = eyeW[1];
      const f32 furH = std::max(2.f, 0.22f * h), pom = std::max(2.f, 0.17f * h);
      const f32 tipX = std::min(cx + (0.3f + 0.35f * k) * bw, (f32)W - pom - 1.f), tipY = base - h + pom;
      fillTri(tipX, tipY, cx - 0.45f * bw, base - furH + 1.f, cx + 0.45f * bw, base - furH + 1.f,
              [&](s32 x, s32 y, f32) { JamesSetPx(output, x, y, 220, 30, 40); });
      JamesFillRect(output, (s32)(cx - 0.52f * bw), (s32)(base - furH), (s32)(1.04f * bw), (s32)furH, 255, 255, 255);
      fillEllipse(tipX, tipY, pom, pom, 255, 255, 255);
      drew = true;
    }

    // Easter: bunny ears on both eyes
    if (JamesIsTheme(JamesTheme::Easter)) {
      for (s32 e = 0; e < 2; ++e) {
        f32 base, h, k; fitHat(e, 0.68f * eyeW[e], base, h, k);
        const f32 cx = eyeCx[e], bw = eyeW[e];
        const f32 ry = 0.5f * h, rx = std::max(2.5f, 0.15f * bw * std::max(k, 0.55f));
        for (s32 side = -1; side <= 1; side += 2) {
          const f32 ex = cx + 0.22f * bw * side, ey = base - ry;
          fillEllipse(ex, ey, rx, ry, 255, 255, 255);
          fillEllipse(ex, ey + 0.1f * ry, 0.45f * rx, 0.75f * ry, 255, 175, 205);
        }
      }
      drew = true;
    }

    // St Patrick's: leprechaun hat on the right eye; while petted, a rainbow to a pot of gold
    if (JamesIsTheme(JamesTheme::StPatrick)) {
      static f32 rainbow = 0.f;
      rainbow = Util::Clamp(rainbow + (gJamesPetting ? 0.06f : -0.04f), 0.f, 1.f);
      if (rainbow > 0.f) {
        static const u8 kBow[6][3] = {{255, 60, 60}, {255, 150, 40}, {255, 230, 60}, {80, 210, 90}, {70, 140, 255}, {160, 90, 230}};
        // a big arc over his face, from the bottom-left to a pot of gold in the bottom-right corner
        const f32 band = std::max(1.5f, (f32)H * 0.04f), outer = (f32)H * 1.0f;
        const f32 rcx = (f32)W * 0.5f, rcy = (f32)H * 1.12f;
        for (s32 y = 0; y < H; ++y) {
          for (s32 x = 0; x < W; ++x) {
            const f32 d = std::sqrt(((f32)x - rcx) * ((f32)x - rcx) + ((f32)y - rcy) * ((f32)y - rcy));
            const s32 k = (s32)((outer - d) / band);
            if (k < 0 || k > 5) { continue; }
            if (JamesInBounds(output, mask, x, y) && mask.GetRow(y)[x] >= 64) { continue; }   // behind the eyes
            JamesLightenPixel(output, x, y, rainbow, kBow[k][0], kBow[k][1], kBow[k][2]);
          }
        }
        // pot of gold where the rainbow lands
        const f32 pr = (f32)H * 0.1f;
        const f32 potX = std::min(rcx + outer - 3.f * band, (f32)W - 1.1f * pr), potY = (f32)H - 0.9f * pr;
        fillEllipse(potX, potY, pr, pr * 0.85f, (u8)(30 * rainbow), (u8)(30 * rainbow), (u8)(35 * rainbow));
        for (s32 k = 0; k < 5; ++k) {
          fillEllipse(potX - 0.6f * pr + 0.3f * pr * k, potY - 0.75f * pr - (k % 2) * 0.15f * pr, 0.22f * pr, 0.18f * pr,
                      (u8)(255 * rainbow), (u8)(205 * rainbow), (u8)(50 * rainbow));
        }
      }
      f32 top, h, k; fitHat(1, 0.62f * eyeW[1], top, h, k);   // top = bottom of the brim
      (void)k;
      const f32 cx = eyeCx[1], bw = eyeW[1];
      const f32 brimH = std::max(2.f, 0.2f * h), crownH = h - brimH;
      JamesFillRect(output, (s32)(cx - 0.62f * bw), (s32)(top - brimH), (s32)(1.24f * bw), (s32)brimH, 20, 110, 40);
      fillTri(cx - 0.36f * bw, top - brimH, cx + 0.36f * bw, top - brimH, cx - 0.3f * bw, top - brimH - crownH,
              [&](s32 x, s32 y, f32) { JamesSetPx(output, x, y, 30, 140, 55); });
      fillTri(cx + 0.36f * bw, top - brimH, cx + 0.3f * bw, top - brimH - crownH, cx - 0.3f * bw, top - brimH - crownH,
              [&](s32 x, s32 y, f32) { JamesSetPx(output, x, y, 30, 140, 55); });
      const s32 bandH = std::max<s32>(2, (s32)(0.2f * crownH)), bandY = (s32)(top - brimH) - bandH - 1;
      JamesFillRect(output, (s32)(cx - 0.35f * bw), bandY, (s32)(0.7f * bw), bandH, 15, 15, 15);
      const s32 bk = std::max<s32>(3, (s32)(0.35f * crownH));
      JamesFillRect(output, (s32)(cx - bk / 2), bandY - 1, bk, bandH + 2, 255, 210, 60);
      JamesFillRect(output, (s32)(cx - bk / 4), bandY, std::max<s32>(1, bk / 2), bandH, 15, 15, 15);
      drew = true;
    }

    // New Year's Eve: party hats on both eyes
    if (JamesIsTheme(JamesTheme::NewYear) && !jamesYearMode) {
      static const u8 kHat[2][2][3] = {{{255, 70, 160}, {150, 70, 230}}, {{60, 150, 255}, {255, 255, 255}}};
      for (s32 e = 0; e < 2; ++e) {
        f32 base, h, k; fitHat(e, 0.55f * eyeW[e], base, h, k);
        (void)k;
        const f32 cx = eyeCx[e], bw = eyeW[e];
        const f32 pom = std::max(2.f, 0.12f * h), half = std::max(0.14f * bw, 0.35f * h);
        const f32 tipX = cx + (e == 0 ? -0.08f : 0.08f) * bw, tipY = base - h + pom;
        fillTri(tipX, tipY, cx - half, base, cx + half, base,
                [&](s32 x, s32 y, f32 l1) {
                  const u8* c = kHat[e][((s32)(l1 * 4.f)) % 2];   // stripes around the cone
                  JamesSetPx(output, x, y, c[0], c[1], c[2]);
                });
        fillEllipse(tipX, tipY, pom, pom, 255, 255, 255);
      }
      drew = true;
    }

    // Thanksgiving: a pile of leaves along the bottom
    if (JamesIsTheme(JamesTheme::Thanksgiving)) {
      struct PileLeaf { s32 x, y; u8 r, g, b; };
      static std::vector<PileLeaf> pile;
      static s32 pileW = 0;
      if (pileW != W) {
        pile.clear(); pileW = W;
        std::minstd_rand pg(0x7EAFu);
        std::uniform_real_distribution<f32> pu(0.f, 1.f);
        static const u8 kLeafCol[4][3] = {{230, 90, 30}, {200, 50, 30}, {230, 160, 40}, {150, 80, 30}};
        const s32 n = W / 2;
        for (s32 i = 0; i < n; ++i) {
          const f32 x = pu(pg) * W;
          const f32 depth = 0.09f * H * (0.6f + 0.4f * std::sin(x / W * 3.1416f));
          const s32 k = (s32)(pu(pg) * 4.f) % 4;
          pile.push_back({(s32)x, H - 1 - (s32)(pu(pg) * depth), kLeafCol[k][0], kLeafCol[k][1], kLeafCol[k][2]});
        }
      }
      const s32 sc = (W >= 184) ? 2 : 1;
      for (const auto& l : pile) {
        for (s32 dy = -sc; dy <= sc; ++dy) {
          for (s32 dx = -sc + std::abs(dy); dx <= sc - std::abs(dy); ++dx) {
            const s32 px = l.x + dx, py = l.y + dy;
            if (JamesInBounds(output, mask, px, py) && mask.GetRow(py)[px] >= 64) { continue; }
            JamesSetPx(output, px, py, l.r, l.g, l.b);
          }
        }
      }
      drew = true;
    }
  }

  // ---------------- easter eggs: TICKLE MODE (blush + steam) and SHY (peeking) ----------------
  {
    const s32 sc = (W >= 184) ? 2 : 1;
    const long long nowMs = JamesNowMs();
    const Rectangle<f32>* boxes[2] = {&_leftBBox, &_rightBBox};

    // pink blush under both eyes (only on the black part, never over the eyes)
    auto blush = [&](f32 amt) {
      for (int e = 0; e < 2; ++e) {
        const Rectangle<f32>* box = boxes[e];
        if (box->GetWidth() < 4.f) { continue; }
        const f32 cx = box->GetX() + 0.5f * box->GetWidth();
        const f32 cy = box->GetYmax() + 3.5f * (f32)sc;
        const f32 rx = 0.30f * box->GetWidth(), ry = 2.5f * (f32)sc;
        for (s32 y = (s32)(cy - ry); y <= (s32)(cy + ry); ++y) {
          for (s32 x = (s32)(cx - rx); x <= (s32)(cx + rx); ++x) {
            const f32 nx = ((f32)x - cx) / rx, ny = ((f32)y - cy) / ry;
            const f32 d = nx * nx + ny * ny;
            if (d > 1.f || !JamesInBounds(output, mask, x, y) || mask.GetRow(y)[x] >= 64) { continue; }
            const f32 a = amt * (1.f - 0.5f * d);
            Vision::PixelRGB565& px = output.GetRow(y)[x];
            px.SetValue((u8)(px.r() + (255.f - px.r()) * a), (u8)(px.g() + (110.f - px.g()) * a),
                        (u8)(px.b() + (160.f - px.b()) * a));
          }
        }
      }
    };

    // TICKLE MODE: lots of quick pets -> flustered: blush + little steam puffs
    static f32 tickleAmt = 0.f;
    tickleAmt = Util::Clamp(tickleAmt + ((nowMs < gJamesTickleUntilMs) ? 0.15f : -0.05f), 0.f, 1.f);
    if (tickleAmt > 0.f) {
      blush(0.85f * tickleAmt);
      for (int e = 0; e < 2; ++e) {
        const Rectangle<f32>* box = boxes[e];
        if (box->GetWidth() < 4.f) { continue; }
        for (s32 k = 0; k < 3; ++k) {
          const f32 ph = (f32)((frame + (u32)(k * 12 + e * 6)) % 36) / 36.f;   // 0 = new puff, 1 = gone
          const f32 baseX = (e == 0) ? box->GetX() - 2.f * sc : box->GetXmax() + 2.f * sc;
          const f32 px = baseX + 2.f * std::sin(ph * 6.f + (f32)k) * sc;
          const f32 py = box->GetY() + 2.f * sc - ph * 14.f * sc;
          const f32 r = (1.f + 2.f * ph) * sc;
          const f32 bright = (1.f - 0.7f * ph) * tickleAmt;
          for (s32 y = (s32)(py - r); y <= (s32)(py + r); ++y) {
            for (s32 x = (s32)(px - r); x <= (s32)(px + r); ++x) {
              const f32 dx = (f32)x - px, dy = (f32)y - py;
              if (dx * dx + dy * dy > r * r || !JamesInBounds(output, mask, x, y) || mask.GetRow(y)[x] >= 64) { continue; }
              JamesLightenPixel(output, x, y, bright, 245, 245, 255);
            }
          }
        }
      }
      drew = true;
    }

    // SHY: one short gentle pet, then nothing -> he peeks with half-closed eyes and a light blush
    static long long shyUntil = 0, lastShy = 0;
    static f32 shyAmt = 0.f;
    const long long cand = gJamesShyCandidateMs;
    if (cand != 0 && !gJamesPetting && nowMs - cand >= kJamesShyWaitMs) {
      gJamesShyCandidateMs = 0;
      if ((lastShy == 0 || nowMs - lastShy > kJamesShyCooldownMs) && nowMs > gJamesTickleUntilMs) {
        lastShy = nowMs;
        shyUntil = nowMs + kJamesShyShowMs;
        std::lock_guard<std::mutex> lk(gJamesPetsMtx);
        JamesLoadBadgesLocked();
        JamesUnlockLocked("SHY_GUY", gJamesDateKey);
      }
    }
    shyAmt = Util::Clamp(shyAmt + ((nowMs < shyUntil) ? 0.08f : -0.06f), 0.f, 1.f);
    if (shyAmt > 0.f) {
      const f32 cover = shyAmt * (0.5f + 0.12f * std::sin((f32)frame * 0.12f));  // lids go up and down a little
      for (int e = 0; e < 2; ++e) {
        const Rectangle<f32>* box = boxes[e];
        if (box->GetHeight() < 6.f) { continue; }
        const s32 x0 = std::max<s32>(0, (s32)box->GetX()), x1 = std::min<s32>(W - 1, (s32)box->GetXmax());
        const s32 y0 = std::max<s32>(0, (s32)box->GetY() - 1);
        const s32 lid = std::min<s32>(H - 1, (s32)(box->GetY() + box->GetHeight() * cover));
        for (s32 y = y0; y <= lid; ++y) {
          for (s32 x = x0; x <= x1; ++x) {
            if (JamesInBounds(output, mask, x, y) && mask.GetRow(y)[x] >= kJamesStarMaskThreshold) {
              output.GetRow(y)[x].SetValue(0, 0, 0);
            }
          }
        }
      }
      blush(0.45f * shyAmt);
      drew = true;
    }
  }

  // ---------------- secret theme: GLITCH (cyan eyes that glitch like a broken TV) ----------------
  if (JamesIsGlitch()) {
    static s32 burst = 0, nextBurst = 45;
    if (burst == 0 && --nextBurst <= 0) {
      burst = 4 + (s32)(uni(gen) * 6.f);
      nextBurst = 40 + (s32)(uni(gen) * 100.f);
    }
    if (burst > 0) {
      --burst;
      std::vector<Vision::PixelRGB565> tmp(W);
      const s32 bands = 2 + (s32)(uni(gen) * 3.f);
      for (s32 k = 0; k < bands; ++k) {
        const s32 by = (s32)(uni(gen) * (f32)H);
        const s32 bh = 1 + (s32)(uni(gen) * 6.f);
        const s32 shift = (s32)((uni(gen) - 0.5f) * 24.f);
        for (s32 y = by; y < std::min<s32>(H, by + bh); ++y) {
          Vision::PixelRGB565* row = output.GetRow(y);
          for (s32 x = 0; x < W; ++x) { tmp[x] = row[x]; }
          for (s32 x = 0; x < W; ++x) { row[x] = tmp[(x - shift + W * 2) % W]; }
        }
      }
      // red shifted to the side (color split)
      const s32 r0 = std::max<s32>(0, _faceRowMin - 2), r1 = std::min<s32>(H - 1, _faceRowMax + 2);
      for (s32 y = r0; y <= r1; ++y) {
        Vision::PixelRGB565* row = output.GetRow(y);
        for (s32 x = W - 1; x >= 3; --x) {
          row[x].SetValue(row[x - 3].r(), row[x].g(), row[x].b());
        }
      }
      // static
      for (s32 k = 0; k < 30; ++k) {
        const u8 v = (u8)(120 + uni(gen) * 135.f);
        JamesSetPx(output, (s32)(uni(gen) * W), (s32)(uni(gen) * H), v, v, v);
      }
    }
    drew = true;
  }

  // ---------------- secret theme: RETRO GREEN (old computer screen) ----------------
  if (JamesIsRetro()) {
    for (s32 y = 1; y < H; y += 2) {            // scanlines
      Vision::PixelRGB565* row = output.GetRow(y);
      for (s32 x = 0; x < W; ++x) {
        row[x].SetValue((u8)(row[x].r() * 0.55f), (u8)(row[x].g() * 0.55f), (u8)(row[x].b() * 0.55f));
      }
    }
    if ((frame / 15) % 2 == 0) {                // blinking cursor in the corner
      const s32 sc = (W >= 184) ? 2 : 1;
      JamesFillRect(output, W - 12 * sc, H - 6 * sc, 6 * sc, 2 * sc, 90, 255, 90);
    }
    drew = true;
  }

  // ---------------- sleep screen: night sky and/or clock ----------------
  {
    // count how much of the eyes is lit (eyes closed = very little)
    s32 lit = 0;
    const s32 r0 = std::max<s32>(0, _faceRowMin), r1 = std::min<s32>(mask.GetNumRows() - 1, _faceRowMax);
    const s32 c0 = std::max<s32>(0, _faceColMin), c1 = std::min<s32>(mask.GetNumCols() - 1, _faceColMax);
    for (s32 y = r0; y <= r1; y += 2) {
      const u8* row = mask.GetRow(y);
      for (s32 x = c0; x <= c1; x += 2) {
        if (row[x] >= kJamesStarMaskThreshold) { lit += 4; }
      }
    }
    const bool closed = lit < (s32)(0.06f * (f32)(W * H));
    closedFrames = closed ? std::min(closedFrames + 1, 100000) : 0;
    const JamesSleepMode mode = (JamesSleepMode)gJamesSleepMode.load();
    const bool sleeping = (mode != JamesSleepMode::Off) && gJamesOnCharger && closedFrames > 180; // ~6 s
    sleepAmt = Util::Clamp(sleepAmt + (sleeping ? 0.02f : -0.08f), 0.f, 1.f);

    if (sleepAmt > 0.f) {
      if (sky.empty()) {
        const s32 n = (W * H) / 380; // ~46 stars on the 1.0 screen
        for (s32 i = 0; i < n; ++i) {
          sky.push_back({uni(gen) * W, uni(gen) * H, uni(gen) * 6.28f, 0.05f + 0.1f * uni(gen), 0.4f + 0.6f * uni(gen)});
        }
      }
      if (mode == JamesSleepMode::Stars || mode == JamesSleepMode::Both) {
        for (auto& s : sky) {
          s.x -= 0.04f; // slow drift
          if (s.x < 0.f) { s.x += (f32)W; }
          const s32 x = (s32)s.x, y = (s32)s.y;
          if (JamesInBounds(output, mask, x, y) && mask.GetRow(y)[x] < 32) {
            const f32 tw = s.b * (0.55f + 0.45f * std::sin(s.ph + (f32)frame * s.sp)) * sleepAmt;
            JamesLightenPixel(output, x, y, tw, 200, 200, 255);
          }
        }
      }
      const s32 hour = gJamesHour;
      if ((mode == JamesSleepMode::Clock || mode == JamesSleepMode::Both) && hour >= 0) {
        const s32 h12 = (hour % 12 == 0) ? 12 : hour % 12;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d:%02d", h12, (int)gJamesMinute.load());
        const std::string timeStr(buf);
        const std::string ampm = (hour < 12) ? "AM" : "PM";
        const s32 scale = 3;
        const s32 tw = JamesTextWidth(timeStr, scale);
        const s32 aw = JamesTextWidth(ampm, 1);
        const s32 x0 = (W - (tw + 3 + aw)) / 2;
        const s32 y0 = std::max<s32>(2, (s32)(0.10f * H));
        u8 cr = 170, cg = 140, cb = 255;               // soft lavender
        if (JamesIsHalloween()) { cr = 255; cg = 120; cb = 20; }
        if (JamesIsChristmas()) { cr = 190; cg = 230; cb = 255; }
        if (JamesIsLunar())     { cr = 255; cg = 200; cb = 70; }
        JamesDrawText(output, timeStr, x0, y0, scale, 0.75f * sleepAmt, cr, cg, cb);
        JamesDrawText(output, ampm, x0 + tw + 3, y0 + 7 * scale - 7, 1, 0.6f * sleepAmt, cr, cg, cb);
      } else if (mode == JamesSleepMode::Clock || mode == JamesSleepMode::Both) {
        // v25: he doesn't know the local time yet (time zone not set at setup) - say so instead of nothing
        const std::string msg = "SET TIME ZONE";
        const s32 mw = JamesTextWidth(msg, 1);
        JamesDrawText(output, msg, (W - mw) / 2, std::max<s32>(2, (s32)(0.10f * H)), 1, 0.6f * sleepAmt, 170, 140, 255);
      }
      drew = true;
    }
  }

  // ---------------- v23: blue moon (corner while awake, big on the sleep screen) ----------------
  if (gJamesBlueMoon) {
    const f32 big = sleepAmt;   // 0 awake .. 1 asleep
    const f32 r = (0.07f + 0.08f * big) * (f32)H;
    const f32 mx = r + 3.f + (0.83f * (f32)W - r - 3.f) * big;   // awake: top-left corner
    const f32 my = r + 3.f + (0.76f * (f32)H - r - 3.f) * big;   // asleep: lower right (clear of the clock)
    static const f32 kCraters[3][3] = {{-0.35f, -0.25f, 0.22f}, {0.30f, 0.15f, 0.28f}, {-0.05f, 0.45f, 0.16f}};
    for (s32 y = (s32)(my - r - 3.f); y <= (s32)(my + r + 3.f); ++y) {
      for (s32 x = (s32)(mx - r - 3.f); x <= (s32)(mx + r + 3.f); ++x) {
        if (!JamesInBounds(output, mask, x, y) || mask.GetRow(y)[x] >= 64) { continue; }   // never on the eyes
        const f32 dx = ((f32)x + 0.5f - mx) / r, dy = ((f32)y + 0.5f - my) / r;
        const f32 d2 = dx * dx + dy * dy;
        if (d2 <= 1.f) {
          bool crater = false;
          for (const auto& c : kCraters) {
            if ((dx - c[0]) * (dx - c[0]) + (dy - c[1]) * (dy - c[1]) < c[2] * c[2]) { crater = true; }
          }
          if (crater) { JamesSetPx(output, x, y, 95, 135, 215); } else { JamesSetPx(output, x, y, 150, 190, 255); }
        } else if (d2 <= 1.6f) {   // soft glow
          JamesLightenPixel(output, x, y, 0.35f * (1.6f - d2) / 0.6f, 120, 160, 255);
        }
      }
    }
    drew = true;
  }



  // ---------------- pet goal celebration: rainbow confetti ----------------
  if (gJamesCelebrate.exchange(0) != 0) {
    celebrateFrames = 100;
    confetti.clear();
    for (s32 i = 0; i < 46; ++i) {
      Particle p;
      const bool leftEye = (i % 2) == 0;
      const Rectangle<f32>& box = leftEye ? _leftBBox : _rightBBox;
      p.x = box.GetX() + 0.5f * box.GetWidth();
      p.y = box.GetY() + 0.5f * box.GetHeight();
      const f32 a = uni(gen) * 6.2832f;
      const f32 sp = 1.2f + 2.4f * uni(gen);
      p.vx = std::cos(a) * sp;
      p.vy = std::sin(a) * sp - 1.0f;
      JamesHueToRGB(uni(gen), p.r, p.g, p.b);
      confetti.push_back(p);
    }
  }
  if (celebrateFrames > 0) {
    const f32 fade = std::min(1.f, (f32)celebrateFrames / 25.f);
    for (auto& p : confetti) {
      p.x += p.vx;
      p.y += p.vy;
      p.vy += 0.06f; // gravity
      for (s32 dy = 0; dy < 2; ++dy) {
        for (s32 dx = 0; dx < 2; ++dx) {
          JamesLightenPixel(output, (s32)p.x + dx, (s32)p.y + dy, fade, p.r, p.g, p.b);
        }
      }
    }
    --celebrateFrames;
    drew = true;
  }

  // ---------------- badge unlocked: banner pops up for ~4 seconds ----------------
  {
    static std::string popupName;
    static s32 popupFrames = 0;
    const s32 kPopupFrames = 120;
    if (popupFrames <= 0) {
      std::lock_guard<std::mutex> lk(gJamesPetsMtx);
      if (!gJamesBadgePopups.empty()) {
        popupName = gJamesBadgePopups.front();
        gJamesBadgePopups.pop_front();
        popupFrames = kPopupFrames;
      }
    }
    if (popupFrames > 0) {
      JamesDrawBadgePopup(output, popupName, popupFrames, kPopupFrames);
      --popupFrames;
      drew = true;
    }
  }

  // ---------------- holiday countdown banner (v22) ----------------
  // up to 7 days before a holiday: "3 DAYS TO GO / HALLOWEEN", on the day "HAPPY / HALLOWEEN".
  // Shows ~8 s after he starts, when he wakes up, and when you start petting him (at most every 10 minutes).
  {
    static s32 bannerFrames = 0;
    static std::string bTop, bBig;
    static u8 br = 255, bg = 255, bb = 255;
    static long long lastShownMs = 0;
    static bool wasPetting = false, wasAsleep = false;
    const s32 kBannerFrames = 120;
    const bool petting = gJamesPetting;
    const bool asleep = closedFrames > 300;
    bool trigger = (frame == 240);                                // just started
    if (petting && !wasPetting && !JamesIsTheme(JamesTheme::StPatrick)) { trigger = true; }   // started petting (St Patrick's: the rainbow shows instead)
    if (wasAsleep && closedFrames == 0) { trigger = true; }       // woke up
    wasPetting = petting;
    if (asleep) { wasAsleep = true; } else if (closedFrames == 0) { wasAsleep = false; }
    const long long nowMs = JamesNowMs();
    if (trigger && bannerFrames <= 0 && sleepAmt < 0.1f && (lastShownMs == 0 || nowMs - lastShownMs > 10LL * 60 * 1000)) {
      int idx = 0, days = 0;
      // v23h: just updated (a published version he hasn't shown yet) -> tell about WHAT'S NEW once
      static int updState = 0;   // 0 = not checked, 1 = show it, 2 = done
      static std::string updId, updLabel;
      if (updState == 0) {
        std::ifstream vf("/anki/etc/jamesos-version");
        std::getline(vf, updId);
        std::getline(vf, updLabel);
        updId = JamesTrim(updId);
        updLabel = JamesTrim(updLabel);
        for (auto& ch : updLabel) { if (ch >= 'a' && ch <= 'z') { ch = (char)(ch - 'a' + 'A'); } }
        const bool real = !updId.empty() && updId.compare(0, 5, "test-") != 0;   // test builds don't count
        updState = (real && JamesReadWord(kJamesSeenVersionFile) != updId) ? 1 : 2;
      }
      if (updState == 1) {
        bTop = "UPDATED! SEE WHAT'S NEW"; bBig = "JAMESOS " + (updLabel.empty() ? std::string("") : updLabel);
        br = 180; bg = 130; bb = 255;
        bannerFrames = kBannerFrames;
        lastShownMs = nowMs;
        updState = 2;
        mkdir("/data/data/jamesos", 0777);
        std::ofstream(kJamesSeenVersionFile) << updId << "\n";
      } else if (gJamesBlueMoon) {                // v23
        bTop = "ONCE IN A"; bBig = "BLUE MOON"; br = 120; bg = 170; bb = 255;
        bannerFrames = kBannerFrames;
        lastShownMs = nowMs;
      } else if (gJamesShiny) {                   // v23
        bTop = "TODAY I'M"; bBig = "SHINY!"; br = 255; bg = 200; bb = 40;
        bannerFrames = kBannerFrames;
        lastShownMs = nowMs;
      } else if (JamesNextHoliday(gJamesDateKey, idx, days)) {
        const auto& h = kJamesHolidays[idx];
        bBig = h.name;
        if (days == 0)      { bTop = h.greet; }
        else if (days == 1) { bTop = "TOMORROW IS"; }
        else                { bTop = std::to_string(days) + " DAYS TO"; }
        if (days == 0 && h.theme == JamesTheme::AprilFools) { bTop = "GOTCHA!"; }
        br = h.r; bg = h.g; bb = h.b;
        bannerFrames = kBannerFrames;
        lastShownMs = nowMs;
      }
    }
    if (bannerFrames > 0) {
      JamesDrawHolidayBanner(output, bTop, bBig, br, bg, bb, bannerFrames, kBannerFrames);
      --bannerFrames;
      drew = true;
    }
  }

  return drew ? true : dirty;
}
// ============================== end JamesOS starlight ==============================

bool ProceduralFaceDrawer::ApplyCustomOverlay(const ProceduralFace& faceData,
                                             Vision::ImageRGB565& output,
                                             bool dirty)
{
  static bool _didLoadCustom = (LoadCustomEyePNG(), true);

  if(kProcFace_CustomEyes && _hasCustomEyes)
  {
    std::lock_guard<std::mutex> lk(gCustomEyeMtx);
    (void)_didLoadCustom;

    const int padX = 5;
    int xMin0 = _faceColMin - padX;
    int xMax0 = _faceColMax + padX;
    int yMin0 = _faceRowMin;
    int yMax0 = _faceRowMax;
    int fullW = xMax0 - xMin0 + 1;
    int fullH = yMax0 - yMin0 + 1;

    int xMin = std::max(0, xMin0);
    int xMax = std::min(ProceduralFace::WIDTH - 1, xMax0);
    int yMin = std::max(0, yMin0);
    int yMax = std::min(ProceduralFace::HEIGHT - 1, yMax0);
    Rectangle<s32> roiR(xMin, yMin,
                        xMax - xMin + 1,
                        yMax - yMin + 1);

    auto dstROI  = output.GetROI(roiR);
    auto maskROI = _faceCache.img8[_faceCache.finalFace].GetROI(roiR);

    int overlayW = _customEyeOverlay.GetNumCols();
    int overlayH = _customEyeOverlay.GetNumRows();
    float scaleX = overlayW / float(fullW);
    float scaleY = overlayH / float(fullH);

    for(int y = 0; y < roiR.GetHeight(); ++y) {
      auto* dstRow  = dstROI .GetRow(y);
      auto* maskRow = maskROI.GetRow(y);
      int globalY    = yMin + y;
      int sampleY    = std::min(overlayH - 1, std::max(0, int((globalY - yMin0) * scaleY)));
      for(int x = 0; x < roiR.GetWidth(); ++x) {
        if(maskRow[x] == 0) continue;
        int globalX = xMin + x;
        int sampleX = std::min(overlayW - 1, std::max(0, int((globalX - xMin0) * scaleX)));
        u8 alpha = _customEyeAlpha.GetRow(sampleY)[sampleX];
        if(alpha == 0) continue;

        // unpack d
        u16 destVal = dstRow[x].GetValue();
        u8 dR = (destVal >> 11) & 0x1F;
        u8 dG = (destVal >> 5)  & 0x3F;
        u8 dB =  destVal        & 0x1F;
        u8 D_R = (dR << 3) | (dR >> 2);
        u8 D_G = (dG << 2) | (dG >> 4);
        u8 D_B = (dB << 3) | (dB >> 2);

        // overlay
        u16 oVal = _customEyeOverlay.GetRow(sampleY)[sampleX].GetValue();
        u8 oR = (oVal >> 11) & 0x1F;
        u8 oG = (oVal >> 5)  & 0x3F;
        u8 oB =  oVal        & 0x1F;
        u8 O_R = (oR << 3) | (oR >> 2);
        u8 O_G = (oG << 2) | (oG >> 4);
        u8 O_B = (oB << 3) | (oB >> 2);

        // modulate by d intensity
        u8 dV = std::max({D_R, D_G, D_B});
        O_R = O_R * dV / 255;
        O_G = O_G * dV / 255;
        O_B = O_B * dV / 255;

        // alpha blend
        u8 invA = 255 - alpha;
        u8 R = (O_R * alpha + D_R * invA) / 255;
        u8 G = (O_G * alpha + D_G * invA) / 255;
        u8 B = (O_B * alpha + D_B * invA) / 255;

        // pack
        u16 packed = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
        dstRow[x].SetValue(packed);
      }
    }

    dirty = true;
  }
  return dirty;
}



  bool ProceduralFaceDrawer::GetNextBlinkFrame(ProceduralFace& faceData,
                                               BlinkState& out_blinkState,
                                               TimeStamp_t& out_offset)
  {
    static ProceduralFace originalFace;

    struct BlinkParams {
      ProceduralFace::Value height, width;
      TimeStamp_t t;
      BlinkState blinkState;
    };

    static const std::vector<BlinkParams> blinkParams{
      {.height = .85f, .width = 1.05f, .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .6f,  .width = 1.2f,  .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .1f,  .width = 2.5f,  .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .05f, .width = 5.f,   .t = 33,  .blinkState = BlinkState::Closed},
      {.height = .15f, .width = 2.f,   .t = 33,  .blinkState = BlinkState::JustOpened},
      {.height = .7f,  .width = 1.2f,  .t = 33,  .blinkState = BlinkState::Opening},
      {.height = .9f,  .width = 1.f,   .t = 100, .blinkState = BlinkState::Opening}
    };

    static const std::vector<Parameter> lidParams{
      Parameter::LowerLidY, Parameter::LowerLidBend, Parameter::LowerLidAngle,
      Parameter::UpperLidY, Parameter::UpperLidBend, Parameter::UpperLidAngle,
    };

    static auto paramIter = blinkParams.begin();

    if(paramIter == blinkParams.end()) {
      // Set everything back to original params
      faceData = originalFace;
      out_offset = 33;

      // Reset for next time
      paramIter = blinkParams.begin();
      // Let caller know this is the last blink frame
      return false;

    } else {
      if(paramIter == blinkParams.begin()) {
        // Store the current pre-blink parameters before we muck with them
        originalFace = faceData;
      }

      for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
        faceData.SetParameter(whichEye, Parameter::EyeScaleX,
                              originalFace.GetParameter(whichEye, Parameter::EyeScaleX) * paramIter->width);
        faceData.SetParameter(whichEye, Parameter::EyeScaleY,
                              originalFace.GetParameter(whichEye, Parameter::EyeScaleY) * paramIter->height);
      }
      out_offset = paramIter->t;

      switch(paramIter->blinkState) {
        case BlinkState::Closed:
        {
          // In case eyes are at different height, get the average height so the
          // blink line when completely closed is nice and horizontal
          const ProceduralFace::Value blinkHeight = (originalFace.GetParameter(WhichEye::Left,  Parameter::EyeCenterY) +
                                                     originalFace.GetParameter(WhichEye::Right, Parameter::EyeCenterY))/2;

          // Zero out the lids so they don't interfere with the "closed" line
          for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
            faceData.SetParameter(whichEye, Parameter::EyeCenterY, blinkHeight);
            for(auto lidParam : lidParams) {
              faceData.SetParameter(whichEye, lidParam, 0);
            }
          }
          break;
        }
        case BlinkState::JustOpened:
        {
          // Restore eye heights and lids
          for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
            faceData.SetParameter(whichEye, Parameter::EyeCenterY,
                                  originalFace.GetParameter(whichEye, Parameter::EyeCenterY));
            for(auto lidParam : lidParams) {
              faceData.SetParameter(whichEye, lidParam, originalFace.GetParameter(whichEye, lidParam));
            }
          }
          break;
        }
        default:
          break;
      }
      
      out_blinkState = paramIter->blinkState;
      ++paramIter;

      // Let caller know there are more blink frames left, so keep calling
      return true;
    }

  } // GetNextBlinkFrame()
  
  bool ProceduralFaceDrawer::ApplyScanlines(Vision::ImageRGB& imageHsv, const float opacity, bool dirty)
  {
#if PROCEDURALFACE_SCANLINE_FEATURE
    if(kProcFace_Scanlines) {
      ANKI_CPU_PROFILE("ApplyScanlines");

      const bool applyScanlines = !Util::IsNear(opacity, 1.f);
      if (applyScanlines) {
        DEV_ASSERT(Util::InRange(opacity, 0.f, 1.f), "ProceduralFaceDrawer.ApplyScanlines.InvalidOpacity");

        dirty = true;

        const auto nRows = imageHsv.GetNumRows();
        const auto nCols = imageHsv.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            auto* thisRow = imageHsv.GetRow(i);
            for (int j=0 ; j < nCols ; j++) {
              // the 'blue' channel in an HSV image is the value
              thisRow[j].b() *= opacity;
            }
          }
        }
      }
    }
#endif

    return dirty;
  } // ApplyScanlines()

  bool ProceduralFaceDrawer::ApplyScanlines(Vision::Image& image8, const float opacity, bool dirty)
  {
#if PROCEDURALFACE_SCANLINE_FEATURE
    if(kProcFace_Scanlines) {
      ANKI_CPU_PROFILE("ApplyScanlines");

      DEV_ASSERT(Util::InRange(opacity, 0.f, 1.f), "ProceduralFaceDrawer.ApplyScanlines.InvalidOpacity");

      const bool is0 = Util::IsNear(opacity, 0.f);
      const bool is1 = Util::IsNear(opacity, 1.f);
      if (is0) {
        dirty = true;

        const auto nRows = image8.GetNumRows();
        const auto nCols = image8.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            u8* thisRow = image8.GetRow(i);
            memset(thisRow, 0, nCols);
          }
        }
      } else if (!is1) {
        dirty = true;

        const auto nRows = image8.GetNumRows();
        const auto nCols = image8.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            u8* thisRow = image8.GetRow(i);
            for (int j=0 ; j < nCols ; j++) {
              *thisRow *= opacity;
              ++thisRow;
            }
          }
        }
      }
    }
#endif

    return dirty;
  } // ApplyScanlines()
} // namespace Vector
} // namespace Anki
