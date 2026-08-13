// language: metal1.0
#include <metal_stdlib>
#include <simd/simd.h>

using metal::uint;

struct U {
    metal::float2 uResolution;
    float uTime;
    int uSteps;
};
struct FragmentOutput {
    metal::float4 finalColor;
};

metal::float2x2 rot(
    float a
) {
    float a_1 = {};
    float c = {};
    float s = {};
    a_1 = a;
    float _e9 = a_1;
    c = metal::cos(_e9);
    float _e12 = a_1;
    s = metal::sin(_e12);
    float _e15 = c;
    float _e16 = s;
    float _e18 = s;
    float _e19 = c;
    return metal::float2x2(metal::float2(_e15, -(_e16)), metal::float2(_e18, _e19));
}

float mandelbulb(
    metal::float3 pos
) {
    metal::float3 pos_1 = {};
    metal::float3 z = {};
    float dr = 1.0;
    float r = 0.0;
    float power = 8.0;
    int i = 0;
    float theta = {};
    float phi = {};
    float zr = {};
    pos_1 = pos;
    metal::float3 _e9 = pos_1;
    z = _e9;
    uint2 loop_bound = uint2(4294967295u);
    bool loop_init = true;
    while(true) {
        if (metal::all(loop_bound == uint2(0u))) { break; }
        loop_bound -= uint2(loop_bound.y == 0u, 1u);
        if (!loop_init) {
            int _e23 = i;
            i = as_type<int>(as_type<uint>(_e23) + as_type<uint>(1));
        }
        loop_init = false;
        int _e19 = i;
        if (!((_e19 < 10))) {
            break;
        }
        {
            metal::float3 _e26 = z;
            r = metal::length(_e26);
            float _e28 = r;
            if (_e28 > 2.0) {
                break;
            }
            metal::float3 _e31 = z;
            float _e33 = r;
            theta = metal::acos(_e31.z / _e33);
            metal::float3 _e37 = z;
            metal::float3 _e39 = z;
            phi = metal::atan2(_e37.y, _e39.x);
            float _e43 = r;
            float _e44 = power;
            float _e48 = power;
            float _e50 = dr;
            dr = ((metal::pow(_e43, _e44 - 1.0) * _e48) * _e50) + 1.0;
            float _e54 = r;
            float _e55 = power;
            zr = metal::pow(_e54, _e55);
            float _e58 = theta;
            float _e59 = power;
            theta = _e58 * _e59;
            float _e61 = phi;
            float _e62 = power;
            phi = _e61 * _e62;
            float _e64 = zr;
            float _e65 = theta;
            float _e67 = phi;
            float _e70 = phi;
            float _e72 = theta;
            float _e75 = theta;
            metal::float3 _e79 = pos_1;
            z = (_e64 * metal::float3(metal::sin(_e65) * metal::cos(_e67), metal::sin(_e70) * metal::sin(_e72), metal::cos(_e75))) + _e79;
        }
    }
    float _e82 = r;
    float _e85 = r;
    float _e87 = dr;
    return ((0.5 * metal::log(_e82)) * _e85) / _e87;
}

float map(
    metal::float3 p,
    constant U& global
) {
    metal::float3 p_1 = {};
    p_1 = p;
    metal::float3 _e9 = p_1;
    float _e11 = global.uTime;
    metal::float2x2 _e14 = rot(_e11 * 0.15);
    metal::float3 _e15 = p_1;
    metal::float2 _e17 = _e14 * _e15.xz;
    p_1.x = _e17.x;
    p_1.z = _e17.y;
    metal::float3 _e22 = p_1;
    float _e24 = global.uTime;
    metal::float2x2 _e27 = rot(_e24 * 0.1);
    metal::float3 _e28 = p_1;
    metal::float2 _e30 = _e27 * _e28.xy;
    p_1.x = _e30.x;
    p_1.y = _e30.y;
    metal::float3 _e35 = p_1;
    float _e36 = mandelbulb(_e35);
    return _e36;
}

metal::float3 calcNormal(
    metal::float3 p_2,
    constant U& global
) {
    metal::float3 p_3 = {};
    metal::float2 e = metal::float2(0.0015, 0.0);
    p_3 = p_2;
    metal::float3 _e13 = p_3;
    metal::float2 _e14 = e;
    float _e17 = map(_e13 + _e14.xyy, global);
    metal::float3 _e18 = p_3;
    metal::float2 _e19 = e;
    float _e22 = map(_e18 - _e19.xyy, global);
    metal::float3 _e24 = p_3;
    metal::float2 _e25 = e;
    float _e28 = map(_e24 + _e25.yxy, global);
    metal::float3 _e29 = p_3;
    metal::float2 _e30 = e;
    float _e33 = map(_e29 - _e30.yxy, global);
    metal::float3 _e35 = p_3;
    metal::float2 _e36 = e;
    float _e39 = map(_e35 + _e36.yyx, global);
    metal::float3 _e40 = p_3;
    metal::float2 _e41 = e;
    float _e44 = map(_e40 - _e41.yyx, global);
    return metal::normalize(metal::float3(_e17 - _e22, _e28 - _e33, _e39 - _e44));
}

metal::float3 trace(
    metal::float2 fragXY,
    constant U& global
) {
    metal::float2 fragXY_1 = {};
    int MS = 160;
    metal::float2 uv = {};
    float ct = {};
    metal::float3 ro = {};
    metal::float3 fw = {};
    metal::float3 rt = {};
    metal::float3 up = {};
    metal::float3 rd = {};
    float t = 0.0;
    float d = 1.0;
    int i_1 = {};
    metal::float3 p_4 = {};
    metal::float3 col = metal::float3(0.015, 0.02, 0.035);
    metal::float3 p_5 = {};
    metal::float3 n = {};
    metal::float3 l = {};
    float diff = {};
    float spec = {};
    float glow = {};
    metal::float3 base = {};
    fragXY_1 = fragXY;
    metal::float2 _e11 = fragXY_1;
    metal::float2 _e13 = global.uResolution;
    metal::float2 _e16 = global.uResolution;
    uv = (_e11 - (0.5 * _e13)) / metal::float2(_e16.y);
    float _e21 = global.uTime;
    ct = _e21 * 0.2;
    float _e25 = ct;
    float _e30 = global.uTime;
    float _e35 = ct;
    ro = metal::float3(metal::cos(_e25) * 2.6, 0.9 * metal::sin(_e30 * 0.17), metal::sin(_e35) * 2.6);
    metal::float3 _e41 = ro;
    fw = metal::normalize(-(_e41));
    metal::float3 _e45 = fw;
    rt = metal::normalize(metal::cross(_e45, metal::float3(0.0, 1.0, 0.0)));
    metal::float3 _e53 = rt;
    metal::float3 _e54 = fw;
    up = metal::cross(_e53, _e54);
    metal::float2 _e57 = uv;
    metal::float3 _e59 = rt;
    metal::float2 _e61 = uv;
    metal::float3 _e63 = up;
    metal::float3 _e67 = fw;
    rd = metal::normalize(((_e57.x * _e59) + (_e61.y * _e63)) + (1.8 * _e67));
    i_1 = 0;
    uint2 loop_bound_1 = uint2(4294967295u);
    bool loop_init_1 = true;
    while(true) {
        if (metal::all(loop_bound_1 == uint2(0u))) { break; }
        loop_bound_1 -= uint2(loop_bound_1.y == 0u, 1u);
        if (!loop_init_1) {
            int _e82 = i_1;
            i_1 = as_type<int>(as_type<uint>(_e82) + as_type<uint>(1));
        }
        loop_init_1 = false;
        int _e78 = i_1;
        int _e79 = MS;
        if (!((_e78 < _e79))) {
            break;
        }
        {
            metal::float3 _e85 = ro;
            metal::float3 _e86 = rd;
            float _e87 = t;
            p_4 = _e85 + (_e86 * _e87);
            metal::float3 _e91 = p_4;
            float _e92 = map(_e91, global);
            d = _e92;
            float _e93 = d;
            float _e96 = t;
            if ((_e93 < 0.0004) || (_e96 > 6.0)) {
                break;
            }
            float _e100 = t;
            float _e101 = d;
            t = _e100 + (_e101 * 0.7);
        }
    }
    float _e110 = d;
    float _e113 = t;
    if ((_e110 < 0.002) && (_e113 < 6.0)) {
        {
            metal::float3 _e117 = ro;
            metal::float3 _e118 = rd;
            float _e119 = t;
            p_5 = _e117 + (_e118 * _e119);
            metal::float3 _e123 = p_5;
            metal::float3 _e124 = calcNormal(_e123, global);
            n = _e124;
            float _e126 = global.uTime;
            float _e133 = global.uTime;
            metal::float3 _e140 = p_5;
            l = metal::normalize(metal::float3(metal::cos(_e126 * 0.5) * 2.0, 2.0, metal::sin(_e133 * 0.5) * 2.0) - _e140);
            metal::float3 _e144 = n;
            metal::float3 _e145 = l;
            diff = metal::max(metal::dot(_e144, _e145), 0.0);
            metal::float3 _e150 = l;
            metal::float3 _e152 = n;
            metal::float3 _e154 = rd;
            spec = metal::pow(metal::max(metal::dot(metal::reflect(-(_e150), _e152), -(_e154)), 0.0), 24.0);
            int _e163 = i_1;
            int _e165 = MS;
            glow = 1.0 - (static_cast<float>(_e163) / static_cast<float>(_e165));
            metal::float3 _e173 = p_5;
            float _e180 = global.uTime;
            base = metal::float3(0.5) + (0.5 * metal::cos(((3.0 * _e173) + metal::float3(0.0, 2.0, 4.0)) + metal::float3(_e180)));
            metal::float3 _e188 = base;
            float _e190 = diff;
            float _e195 = spec;
            metal::float3 _e200 = base;
            float _e201 = glow;
            col = ((_e188 * (0.1 + (_e190 * 0.9))) + metal::float3(_e195 * 0.6)) + ((_e200 * _e201) * 0.3);
            metal::float3 _e206 = col;
            float _e214 = t;
            float _e216 = t;
            col = metal::mix(_e206, metal::float3(0.015, 0.02, 0.035), metal::float3(1.0 - metal::exp((-0.4 * _e214) * _e216)));
        }
    }
    metal::float3 _e222 = col;
    return _e222;
}

void main01(
    thread metal::float4& finalColor,
    constant U& global,
    thread metal::float4& gl_FragCoord_1
) {
    int n_1 = {};
    metal::float3 col_1 = metal::float3(0.0);
    int s_1 = 0;
    float fs = {};
    metal::float2 jit = {};
    int _e7 = global.uSteps;
    n_1 = _e7;
    int _e9 = n_1;
    if (_e9 < 1) {
        n_1 = 1;
    }
    uint2 loop_bound_2 = uint2(4294967295u);
    bool loop_init_2 = true;
    while(true) {
        if (metal::all(loop_bound_2 == uint2(0u))) { break; }
        loop_bound_2 -= uint2(loop_bound_2.y == 0u, 1u);
        if (!loop_init_2) {
            int _e22 = s_1;
            s_1 = as_type<int>(as_type<uint>(_e22) + as_type<uint>(1));
        }
        loop_init_2 = false;
        int _e18 = s_1;
        int _e19 = n_1;
        if (!((_e18 < _e19))) {
            break;
        }
        {
            int _e25 = s_1;
            fs = static_cast<float>(_e25);
            float _e28 = fs;
            float _e35 = fs;
            jit = metal::float2(metal::fract(metal::sin(_e28 * 12.9898) * 43758.547), metal::fract(metal::sin(_e35 * 78.233) * 43758.547)) - metal::float2(0.5);
            metal::float3 _e48 = col_1;
            metal::float4 _e49 = gl_FragCoord_1;
            metal::float2 _e51 = jit;
            metal::float3 _e53 = trace(_e49.xy + _e51, global);
            col_1 = _e48 + _e53;
        }
    }
    metal::float3 _e55 = col_1;
    int _e56 = n_1;
    col_1 = _e55 / metal::float3(static_cast<float>(_e56));
    metal::float3 _e60 = col_1;
    col_1 = metal::pow(_e60, metal::float3(0.4545));
    metal::float3 _e64 = col_1;
    finalColor = metal::float4(_e64.x, _e64.y, _e64.z, 1.0);
    return;
}

struct main0Input {
};
struct main0Output {
    metal::float4 finalColor [[color(0)]];
};
fragment main0Output main0(
  metal::float4 gl_FragCoord [[position]]
, constant U& global [[buffer(0)]]
) {
    metal::float4 finalColor = {};
    metal::float4 gl_FragCoord_1 = {};
    gl_FragCoord_1 = gl_FragCoord;
    main01(finalColor, global, gl_FragCoord_1);
    metal::float4 _e11 = finalColor;
    const auto _tmp = FragmentOutput {_e11};
    return main0Output { _tmp.finalColor };
}
