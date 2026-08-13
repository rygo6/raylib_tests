

#include <metal_stdlib>

#define ANGLE_ALWAYS_INLINE __attribute__((always_inline))

ANGLE_ALWAYS_INLINE int ANGLE_int_clamp(int value, int minValue, int maxValue)
{
    return ((value < minValue) ?  minValue : ((value > maxValue) ? maxValue : value));
};

#define ANGLE_SAMPLE_COMPARE_GRADIENT_INDEX   0
#define ANGLE_RASTERIZATION_DISCARD_INDEX     1
#define ANGLE_MULTISAMPLED_RENDERING_INDEX    2
#define ANGLE_DEPTH_WRITE_ENABLED_INDEX       3
#define ANGLE_EMULATE_ALPHA_TO_COVERAGE_INDEX 4
#define ANGLE_WRITE_HELPER_SAMPLE_MASK_INDEX  5

constant bool ANGLEUseSampleCompareGradient [[function_constant(ANGLE_SAMPLE_COMPARE_GRADIENT_INDEX)]];
constant bool ANGLERasterizerDisabled       [[function_constant(ANGLE_RASTERIZATION_DISCARD_INDEX)]];
constant bool ANGLEMultisampledRendering    [[function_constant(ANGLE_MULTISAMPLED_RENDERING_INDEX)]];
constant bool ANGLEDepthWriteEnabled        [[function_constant(ANGLE_DEPTH_WRITE_ENABLED_INDEX)]];
constant bool ANGLEEmulateAlphaToCoverage   [[function_constant(ANGLE_EMULATE_ALPHA_TO_COVERAGE_INDEX)]];
constant bool ANGLEWriteHelperSampleMask    [[function_constant(ANGLE_WRITE_HELPER_SAMPLE_MASK_INDEX)]];

#define ANGLE_ALPHA0

#pragma clang diagnostic ignored "-Wunused-value"
ANGLE_ALWAYS_INLINE void ANGLE_loopForwardProgress()
{
    volatile bool p = true;
}

struct ANGLE_InvocationFragmentGlobals
{
  metal::float4 gl_FragCoord [[position]];
};

struct ANGLEDepthRangeParams
{
  float ANGLE_near;
  float ANGLE_far;
  float ANGLE_diff;
};

struct ANGLEUniformBlock
{
  metal::float2 ANGLE_depthRange;
  uint32_t ANGLE_renderArea;
  uint32_t ANGLE_flipXY;
  uint32_t ANGLE_misc;
  int ANGLE_baseInstance;
  metal::uint2 ANGLE_acbBufferOffsets;
  metal::int4 ANGLE_xfbBufferOffsets;
  int ANGLE_xfbVerticesPerInstance;
  uint32_t ANGLE_coverageMask;
  metal::uint2 ANGLE_unused;
};

struct ANGLE_NonConstGlobals
{
  metal::float4 ANGLE_flippedFragCoord;
};

struct ANGLE_UserUniforms
{
  metal::float2 _uuResolution;
  float _uuTime;
  int _uuSteps;
};

struct ANGLE_FragmentOut
{
  metal::float4 _ufinalColor [[color(0)]];
};

metal::float2 ANGLE_sc13(float ANGLE_sc14, float ANGLE_sc15)
{
  metal::float2 ANGLE_sc16 = metal::float2(ANGLE_sc14, ANGLE_sc15);
  return ANGLE_sc16;;
}

metal::float2 ANGLE_sc10(float ANGLE_sc11, float ANGLE_sc12)
{
  metal::float2 ANGLE_sc17 = metal::float2(ANGLE_sc11, ANGLE_sc12);
  return ANGLE_sc17;;
}

metal::float4 ANGLE_sc0d(metal::float3 ANGLE_sc0e, float ANGLE_sc0f)
{
  metal::float4 ANGLE_sc18 = metal::float4(ANGLE_sc0e.x, ANGLE_sc0e.y, ANGLE_sc0e.z, ANGLE_sc0f);
  return ANGLE_sc18;;
}

metal::float2 ANGLE_sc0a(float ANGLE_sc0b, float ANGLE_sc0c)
{
  metal::float2 ANGLE_sc19 = metal::float2(ANGLE_sc0b, ANGLE_sc0c);
  return ANGLE_sc19;;
}

metal::float3 ANGLE_sc06(float ANGLE_sc07, float ANGLE_sc08, float ANGLE_sc09)
{
  metal::float3 ANGLE_sc1a = metal::float3(ANGLE_sc07, ANGLE_sc08, ANGLE_sc09);
  return ANGLE_sc1a;;
}

metal::float3 ANGLE_sc02(float ANGLE_sc03, float ANGLE_sc04, float ANGLE_sc05)
{
  metal::float3 ANGLE_sc1b = metal::float3(ANGLE_sc03, ANGLE_sc04, ANGLE_sc05);
  return ANGLE_sc1b;;
}

metal::float3 ANGLE_sbfe(float ANGLE_sbff, float ANGLE_sc00, float ANGLE_sc01)
{
  metal::float3 ANGLE_sc1c = metal::float3(ANGLE_sbff, ANGLE_sc00, ANGLE_sc01);
  return ANGLE_sc1c;;
}

metal::float3 ANGLE_sbfa(float ANGLE_sbfb, float ANGLE_sbfc, float ANGLE_sbfd)
{
  metal::float3 ANGLE_sc1d = metal::float3(ANGLE_sbfb, ANGLE_sbfc, ANGLE_sbfd);
  return ANGLE_sc1d;;
}

metal::float2x2 ANGLE_sbf5(float ANGLE_sbf6, float ANGLE_sbf7, float ANGLE_sbf8, float ANGLE_sbf9)
{
  metal::float2x2 ANGLE_sc1e = metal::float2x2(ANGLE_sbf6, ANGLE_sbf7, ANGLE_sbf8, ANGLE_sbf9);
  return ANGLE_sc1e;;
}

metal::float2x2 _urot(float _ua)
{
  float _uc = metal::cos(_ua);
  float _us = metal::sin(_ua);
  float ANGLE_sc21 = (-_us);
  metal::float2x2 ANGLE_sc22 = ANGLE_sbf5(_uc, ANGLE_sc21, _us, _uc);
  return ANGLE_sc22;;
}

float _umandelbulb(metal::float3 _upos)
{
  metal::float3 _uz = _upos;
  float _udr = 1.0f;
  float _ur = 0.0f;
  for (int _ui = 0; _ui < 10; _ui++)
  {
    _ur = metal::length(_uz);
    bool ANGLE_sc24 = (_ur > 2.0f);
    if (ANGLE_sc24)
    {
      break;
    } else {}
    float ANGLE_sc25 = (_uz.z / _ur);
    float _utheta = metal::acos(ANGLE_sc25);
    float _uphi = metal::atan2(_uz.y, _uz.x);
    float ANGLE_sc28 = metal::powr(_ur, 7.0f);
    float ANGLE_sc29 = (ANGLE_sc28 * 8.0f);
    float ANGLE_sc2a = (ANGLE_sc29 * _udr);
    _udr = (ANGLE_sc2a + 1.0f);
    float _uzr = metal::powr(_ur, 8.0f);
    _utheta *= 8.0f;
    _uphi *= 8.0f;
    float ANGLE_sc2d = metal::sin(_utheta);
    float ANGLE_sc2e = metal::cos(_uphi);
    float ANGLE_sc2f = (ANGLE_sc2d * ANGLE_sc2e);
    float ANGLE_sc30 = metal::sin(_uphi);
    float ANGLE_sc31 = metal::sin(_utheta);
    float ANGLE_sc32 = (ANGLE_sc30 * ANGLE_sc31);
    float ANGLE_sc33 = metal::cos(_utheta);
    metal::float3 ANGLE_sc34 = ANGLE_sbfa(ANGLE_sc2f, ANGLE_sc32, ANGLE_sc33);
    metal::float3 ANGLE_sc35 = (_uzr * ANGLE_sc34);
    _uz = (ANGLE_sc35 + _upos);
  }
  float ANGLE_sc37 = metal::log(_ur);
  float ANGLE_sc38 = (0.5f * ANGLE_sc37);
  float ANGLE_sc39 = (ANGLE_sc38 * _ur);
  float ANGLE_sc3a = (ANGLE_sc39 / _udr);
  return ANGLE_sc3a;;
}

float _umap(constant ANGLE_UserUniforms & ANGLE_userUniforms, metal::float3 _up)
{
  float ANGLE_sc3b = (ANGLE_userUniforms._uuTime * 0.150000006f);
  metal::float2x2 ANGLE_sc3c = _urot(ANGLE_sc3b);
  _up.xz = (ANGLE_sc3c * _up.xz);
  float ANGLE_sc3e = (ANGLE_userUniforms._uuTime * 0.100000001f);
  metal::float2x2 ANGLE_sc3f = _urot(ANGLE_sc3e);
  _up.xy = (ANGLE_sc3f * _up.xy);
  float ANGLE_sc41 = _umandelbulb(_up);
  return ANGLE_sc41;;
}

metal::float3 _ucalcNormal(constant ANGLE_UserUniforms & ANGLE_userUniforms, metal::float3 _up)
{
  metal::float2 _ue = metal::float2(0.00150000001f, 0.0f);
  metal::float3 ANGLE_sc42 = (_up + _ue.xyy);
  float ANGLE_sc43 = _umap(ANGLE_userUniforms, ANGLE_sc42);
  metal::float3 ANGLE_sc44 = (_up - _ue.xyy);
  float ANGLE_sc45 = _umap(ANGLE_userUniforms, ANGLE_sc44);
  float ANGLE_sc46 = (ANGLE_sc43 - ANGLE_sc45);
  metal::float3 ANGLE_sc47 = (_up + _ue.yxy);
  float ANGLE_sc48 = _umap(ANGLE_userUniforms, ANGLE_sc47);
  metal::float3 ANGLE_sc49 = (_up - _ue.yxy);
  float ANGLE_sc4a = _umap(ANGLE_userUniforms, ANGLE_sc49);
  float ANGLE_sc4b = (ANGLE_sc48 - ANGLE_sc4a);
  metal::float3 ANGLE_sc4c = (_up + _ue.yyx);
  float ANGLE_sc4d = _umap(ANGLE_userUniforms, ANGLE_sc4c);
  metal::float3 ANGLE_sc4e = (_up - _ue.yyx);
  float ANGLE_sc4f = _umap(ANGLE_userUniforms, ANGLE_sc4e);
  float ANGLE_sc50 = (ANGLE_sc4d - ANGLE_sc4f);
  metal::float3 ANGLE_sc51 = ANGLE_sbfe(ANGLE_sc46, ANGLE_sc4b, ANGLE_sc50);
  metal::float3 ANGLE_sc52 = metal::fast::normalize(ANGLE_sc51);
  return ANGLE_sc52;;
}

metal::float3 _utrace(constant ANGLE_UserUniforms & ANGLE_userUniforms, metal::float2 _ufragXY)
{
  metal::float2 ANGLE_sc53 = (0.5f * ANGLE_userUniforms._uuResolution);
  metal::float2 ANGLE_sc54 = (_ufragXY - ANGLE_sc53);
  metal::float2 _uuv = (ANGLE_sc54 / ANGLE_userUniforms._uuResolution.y);
  float _uct = (ANGLE_userUniforms._uuTime * 0.200000003f);
  float ANGLE_sc57 = metal::cos(_uct);
  float ANGLE_sc58 = (ANGLE_sc57 * 2.5999999f);
  float ANGLE_sc59 = (ANGLE_userUniforms._uuTime * 0.170000002f);
  float ANGLE_sc5a = metal::sin(ANGLE_sc59);
  float ANGLE_sc5b = (0.899999976f * ANGLE_sc5a);
  float ANGLE_sc5c = metal::sin(_uct);
  float ANGLE_sc5d = (ANGLE_sc5c * 2.5999999f);
  metal::float3 _uro = ANGLE_sc02(ANGLE_sc58, ANGLE_sc5b, ANGLE_sc5d);
  metal::float3 ANGLE_sc5f = (-_uro);
  metal::float3 _ufw = metal::fast::normalize(ANGLE_sc5f);
  metal::float3 ANGLE_sc61 = metal::cross(_ufw, metal::float3(0.0f, 1.0f, 0.0f));
  metal::float3 _urt = metal::fast::normalize(ANGLE_sc61);
  metal::float3 _uup = metal::cross(_urt, _ufw);
  metal::float3 ANGLE_sc64 = (_uuv.x * _urt);
  metal::float3 ANGLE_sc65 = (_uuv.y * _uup);
  metal::float3 ANGLE_sc66 = (ANGLE_sc64 + ANGLE_sc65);
  metal::float3 ANGLE_sc67 = (1.79999995f * _ufw);
  metal::float3 ANGLE_sc68 = (ANGLE_sc66 + ANGLE_sc67);
  metal::float3 _urd = metal::fast::normalize(ANGLE_sc68);
  float _ut = 0.0f;
  float _ud = 1.0f;
  int _ui = 0;
  for (_ui = 0; _ui < 160; _ui++)
  {
    metal::float3 ANGLE_sc6a = (_urd * _ut);
    metal::float3 _up = (_uro + ANGLE_sc6a);
    _ud = _umap(ANGLE_userUniforms, _up);
    bool ANGLE__1 = (_ud < 0.00039999999f);
    if (!ANGLE__1)
    {
      ANGLE__1 = (_ut > 6.0f);
    } else {}
    if (ANGLE__1)
    {
      break;
    } else {}
    float ANGLE_sc70 = (_ud * 0.699999988f);
    _ut += ANGLE_sc70;
  }
  metal::float3 _ucol = metal::float3(0.0149999997f, 0.0199999996f, 0.0350000001f);
  bool ANGLE__2 = (_ud < 0.00200000009f);
  if (ANGLE__2)
  {
    ANGLE__2 = (_ut < 6.0f);
  } else {}
  if (ANGLE__2)
  {
    metal::float3 ANGLE_sc74 = (_urd * _ut);
    metal::float3 _up = (_uro + ANGLE_sc74);
    metal::float3 _un = _ucalcNormal(ANGLE_userUniforms, _up);
    float ANGLE_sc77 = (ANGLE_userUniforms._uuTime * 0.5f);
    float ANGLE_sc78 = metal::cos(ANGLE_sc77);
    float ANGLE_sc79 = (ANGLE_sc78 * 2.0f);
    float ANGLE_sc7a = (ANGLE_userUniforms._uuTime * 0.5f);
    float ANGLE_sc7b = metal::sin(ANGLE_sc7a);
    float ANGLE_sc7c = (ANGLE_sc7b * 2.0f);
    metal::float3 ANGLE_sc7d = ANGLE_sc06(ANGLE_sc79, 2.0f, ANGLE_sc7c);
    metal::float3 ANGLE_sc7e = (ANGLE_sc7d - _up);
    metal::float3 _ul = metal::fast::normalize(ANGLE_sc7e);
    float ANGLE_sc80 = metal::dot(_un, _ul);
    float _udiff = metal::max(ANGLE_sc80, 0.0f);
    metal::float3 ANGLE_sc82 = (-_ul);
    metal::float3 ANGLE_sc83 = metal::reflect(ANGLE_sc82, _un);
    metal::float3 ANGLE_sc84 = (-_urd);
    float ANGLE_sc85 = metal::dot(ANGLE_sc83, ANGLE_sc84);
    float ANGLE_sc86 = metal::max(ANGLE_sc85, 0.0f);
    float _uspec = metal::powr(ANGLE_sc86, 24.0f);
    float ANGLE_sc88 = float(_ui);
    float ANGLE_sc89 = (ANGLE_sc88 / 160.0f);
    float _uglow = (1.0f - ANGLE_sc89);
    metal::float3 ANGLE_sc8b = (3.0f * _up);
    metal::float3 ANGLE_sc8c = (ANGLE_sc8b + metal::float3(0.0f, 2.0f, 4.0f));
    metal::float3 ANGLE_sc8d = (ANGLE_sc8c + ANGLE_userUniforms._uuTime);
    metal::float3 ANGLE_sc8e = metal::cos(ANGLE_sc8d);
    metal::float3 ANGLE_sc8f = (0.5f * ANGLE_sc8e);
    metal::float3 _ubase = (0.5f + ANGLE_sc8f);
    float ANGLE_sc91 = (_udiff * 0.899999976f);
    float ANGLE_sc92 = (0.100000001f + ANGLE_sc91);
    metal::float3 ANGLE_sc93 = (_ubase * ANGLE_sc92);
    float ANGLE_sc94 = (_uspec * 0.600000024f);
    metal::float3 ANGLE_sc95 = (ANGLE_sc93 + ANGLE_sc94);
    metal::float3 ANGLE_sc96 = (_ubase * _uglow);
    metal::float3 ANGLE_sc97 = (ANGLE_sc96 * 0.300000012f);
    _ucol = (ANGLE_sc95 + ANGLE_sc97);
    float ANGLE_sc99 = (-0.400000006f * _ut);
    float ANGLE_sc9a = (ANGLE_sc99 * _ut);
    float ANGLE_sc9b = metal::exp(ANGLE_sc9a);
    float ANGLE_sc9c = (1.0f - ANGLE_sc9b);
    _ucol = metal::mix(_ucol, metal::float3(0.0149999997f, 0.0199999996f, 0.0350000001f), ANGLE_sc9c);
  } else {}
  return _ucol;;
}

void ANGLE__0_main(thread ANGLE_FragmentOut & ANGLE_fragmentOut, constant ANGLE_UserUniforms & ANGLE_userUniforms, thread ANGLE_NonConstGlobals & ANGLE_nonConstGlobals)
{
  int _un = ANGLE_userUniforms._uuSteps;
  bool ANGLE_sc9e = (_un < 1);
  if (ANGLE_sc9e)
  {
    _un = 1;
  } else {}
  metal::float3 _ucol = metal::float3(0.0f, 0.0f, 0.0f);
  for (int _us = 0; _us < _un; _us++)
  {
    ANGLE_loopForwardProgress();
    {
      float _ufs = float(_us);
      float ANGLE_sca0 = (_ufs * 12.9898005f);
      float ANGLE_sca1 = metal::sin(ANGLE_sca0);
      float ANGLE_sca2 = (ANGLE_sca1 * 43758.5469f);
      float ANGLE_sca3 = metal::fract(ANGLE_sca2);
      float ANGLE_sca4 = (_ufs * 78.2330017f);
      float ANGLE_sca5 = metal::sin(ANGLE_sca4);
      float ANGLE_sca6 = (ANGLE_sca5 * 43758.5469f);
      float ANGLE_sca7 = metal::fract(ANGLE_sca6);
      metal::float2 ANGLE_sca8 = ANGLE_sc0a(ANGLE_sca3, ANGLE_sca7);
      metal::float2 _ujit = (ANGLE_sca8 - 0.5f);
      metal::float2 ANGLE_scaa = (ANGLE_nonConstGlobals.ANGLE_flippedFragCoord.xy + _ujit);
      metal::float3 ANGLE_scab = _utrace(ANGLE_userUniforms, ANGLE_scaa);
      _ucol += ANGLE_scab;
    }
  }
  float ANGLE_scac = float(_un);
  _ucol /= ANGLE_scac;
  _ucol = metal::powr(_ucol, metal::float3(0.45449999f, 0.45449999f, 0.45449999f));
  ANGLE_fragmentOut._ufinalColor = ANGLE_sc0d(_ucol, 1.0f);
}

fragment ANGLE_FragmentOut main0(constant ANGLE_UserUniforms & ANGLE_userUniforms [[buffer(0)]], metal::float4 gl_FragCoord [[position]])
{
  ANGLE_InvocationFragmentGlobals ANGLE_invocationFragmentGlobals;
  ANGLE_invocationFragmentGlobals.gl_FragCoord = gl_FragCoord;
  {
    ANGLE_FragmentOut ANGLE_fragmentOut;
    {
      ANGLE_NonConstGlobals ANGLE_nonConstGlobals;
      {
        ANGLE_nonConstGlobals.ANGLE_flippedFragCoord = ANGLE_invocationFragmentGlobals.gl_FragCoord;
        uint32_t ANGLE_scaf = ((1280u | (720u << 16)) & 65535u);
        float ANGLE_scb0 = float(ANGLE_scaf);
        uint32_t ANGLE_scb1 = ((1280u | (720u << 16)) >> 16u);
        float ANGLE_scb2 = float(ANGLE_scb1);
        metal::float2 ANGLE_scb3 = ANGLE_sc10(ANGLE_scb0, ANGLE_scb2);
        metal::float2 ANGLE_scb4 = (ANGLE_scb3 * 0.5f);
        metal::float2 ANGLE_scb5 = (ANGLE_invocationFragmentGlobals.gl_FragCoord.xy - ANGLE_scb4);
        metal::float4 ANGLE_scb6 = metal::unpack_snorm4x8_to_float(0x7F7F7F7Fu);
        metal::float2 ANGLE_scb7 = (ANGLE_scb5 * ANGLE_scb6.xy);
        uint32_t ANGLE_scb8 = ((1280u | (720u << 16)) & 65535u);
        float ANGLE_scb9 = float(ANGLE_scb8);
        uint32_t ANGLE_scba = ((1280u | (720u << 16)) >> 16u);
        float ANGLE_scbb = float(ANGLE_scba);
        metal::float2 ANGLE_scbc = ANGLE_sc13(ANGLE_scb9, ANGLE_scbb);
        metal::float2 ANGLE_scbd = (ANGLE_scbc * 0.5f);
        ANGLE_nonConstGlobals.ANGLE_flippedFragCoord.xy = (ANGLE_scb7 + ANGLE_scbd);
        ANGLE__0_main(ANGLE_fragmentOut, ANGLE_userUniforms, ANGLE_nonConstGlobals);
        if (ANGLEMultisampledRendering)
        {
        } else {}
      }
    }
    return ANGLE_fragmentOut;;
  }
}


