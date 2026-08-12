// Mandelbulb raymarcher, Slang port of performance_stress_test_direct's GLSL (identical math)
struct U { float2 uResolution; float uTime; int uSteps; };
ConstantBuffer<U> u : register(b17);

float2x2 rot2(float a){ float c = cos(a), s = sin(a); return float2x2(c, s, -s, c); }   // column-major match

float mandelbulb(float3 pos){
    float3 z = pos; float dr = 1.0; float r = 0.0;
    for (int i = 0; i < 10; i++){
        r = length(z); if (r > 2.0) break;
        float theta = acos(z.z/r); float phi = atan2(z.y, z.x);
        dr = pow(r, 7.0)*8.0*dr + 1.0;
        float zr = pow(r, 8.0); theta *= 8.0; phi *= 8.0;
        z = zr*float3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta)) + pos;
    }
    return 0.5*log(r)*r/dr;
}
float mapf(float3 p){
    float2 xz = mul(rot2(u.uTime*0.15), p.xz); p.xz = xz;
    float2 xy = mul(rot2(u.uTime*0.1), p.xy); p.xy = xy;
    return mandelbulb(p);
}
float3 calcNormal(float3 p){
    float2 e = float2(0.0015, 0.0);
    return normalize(float3(mapf(p+e.xyy)-mapf(p-e.xyy), mapf(p+e.yxy)-mapf(p-e.yxy), mapf(p+e.yyx)-mapf(p-e.yyx)));
}
float3 tracef(float2 fragXY){
    const int MS = 160;
    float2 uv = (fragXY - 0.5*u.uResolution)/u.uResolution.y;
    float ct = u.uTime*0.2;
    float3 ro = float3(cos(ct)*2.6, 0.9*sin(u.uTime*0.17), sin(ct)*2.6);
    float3 fw = normalize(-ro);
    float3 rt = normalize(cross(fw, float3(0.0,1.0,0.0)));
    float3 up = cross(rt, fw);
    float3 rd = normalize(uv.x*rt + uv.y*up + 1.8*fw);
    float t = 0.0; float d = 1.0; int i;
    for (i = 0; i < MS; i++){
        float3 p = ro + rd*t; d = mapf(p);
        if (d < 0.0004 || t > 6.0) break;
        t += d*0.7;
    }
    float3 col = float3(0.015, 0.02, 0.035);
    if (d < 0.002 && t < 6.0){
        float3 p = ro + rd*t; float3 n = calcNormal(p);
        float3 l = normalize(float3(cos(u.uTime*0.5)*2.0, 2.0, sin(u.uTime*0.5)*2.0) - p);
        float diff = max(dot(n, l), 0.0);
        float spec = pow(max(dot(reflect(-l, n), -rd), 0.0), 24.0);
        float glow = 1.0 - float(i)/float(MS);
        float3 base = 0.5 + 0.5*cos(3.0*p + float3(0.0, 2.0, 4.0) + u.uTime);
        col = base*(0.1 + diff*0.9) + spec*0.6 + base*glow*0.3;
        col = lerp(col, float3(0.015,0.02,0.035), 1.0 - exp(-0.4*t*t));
    }
    return col;
}
[shader("fragment")]
float4 main0(float4 pos : SV_Position) : SV_Target
{
    int n = u.uSteps; if (n < 1) n = 1;
    float3 col = float3(0.0);
    for (int s = 0; s < n; s++){
        float fs = float(s);
        float2 jit = float2(frac(sin(fs*12.9898)*43758.5453), frac(sin(fs*78.233)*43758.5453)) - 0.5;
        col += tracef(pos.xy + jit);
    }
    col /= float(n);
    col = pow(col, float3(0.4545));
    return float4(col, 1.0);
}
