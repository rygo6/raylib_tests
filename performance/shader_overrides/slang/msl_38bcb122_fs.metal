#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 5 "/private/tmp/claude-501/-Users-ryan-Developer-raylib/7c299f12-18d4-406c-9d2e-ecfe0835c017/scratchpad/spike/fs.slang"
matrix<float,int(2),int(2)>  rot2_0(float a_0)
{

#line 5
    float c_0 = cos(a_0);

#line 5
    float s_0 = sin(a_0);

#line 5
    return matrix<float,int(2),int(2)> (c_0, s_0, - s_0, c_0);
}


#line 7
float mandelbulb_0(float3 pos_0)
{

#line 7
    float3 z_0 = pos_0;

#line 7
    float dr_0 = 1.0f;

#line 7
    float r_0 = 0.0f;

#line 7
    int i_0 = int(0);

    for(;;)
    {

#line 9
        if(i_0 < int(10))
        {
        }
        else
        {

#line 9
            break;
        }

#line 10
        float r_1 = length(z_0);

#line 10
        if(r_1 > 2.0f)
        {

#line 10
            r_0 = r_1;

#line 10
            break;
        }
        float _S1 = pow(r_1, 7.0f) * 8.0f * dr_0 + 1.0f;
        float theta_0 = acos(z_0.z / r_1) * 8.0f;

#line 13
        float phi_0 = atan2(z_0.y, z_0.x) * 8.0f;
        float _S2 = sin(theta_0);

#line 14
        float3 _S3 = float3(pow(r_1, 8.0f))  * float3(_S2 * cos(phi_0), sin(phi_0) * _S2, cos(theta_0)) + pos_0;

#line 9
        int _S4 = i_0 + int(1);

#line 9
        z_0 = _S3;

#line 9
        dr_0 = _S1;

#line 9
        r_0 = r_1;

#line 9
        i_0 = _S4;

#line 9
    }

#line 16
    return 0.5f * log(r_0) * r_0 / dr_0;
}


#line 2
struct U_0
{
    float2 uResolution_0;
    float uTime_0;
    int uSteps_0;
};


#line 1084 "core"
struct KernelContext_0
{
    U_0 constant* u_0;
};


#line 18 "/private/tmp/claude-501/-Users-ryan-Developer-raylib/7c299f12-18d4-406c-9d2e-ecfe0835c017/scratchpad/spike/fs.slang"
float mapf_0(float3 p_0, KernelContext_0 thread* kernelContext_0)
{

#line 18
    thread float3 _S5 = p_0;
    _S5.xz = (((p_0.xz) * (rot2_0(kernelContext_0->u_0->uTime_0 * 0.15000000596046448f))));
    _S5.xy = (((_S5.xy) * (rot2_0(kernelContext_0->u_0->uTime_0 * 0.10000000149011612f))));
    return mandelbulb_0(_S5);
}


#line 18
float mapf_1(float3 p_1, KernelContext_0 thread* kernelContext_1)
{

#line 18
    thread float3 _S6 = p_1;
    _S6.xz = (((p_1.xz) * (rot2_0(kernelContext_1->u_0->uTime_0 * 0.15000000596046448f))));
    _S6.xy = (((_S6.xy) * (rot2_0(kernelContext_1->u_0->uTime_0 * 0.10000000149011612f))));
    return mandelbulb_0(_S6);
}


#line 23
float3 calcNormal_0(float3 p_2, KernelContext_0 thread* kernelContext_2)
{
    float3 _S7 = float3(0.00150000001303852f, 0.0f, 0.0f);

#line 25
    float _S8 = mapf_1(p_2 + _S7, kernelContext_2);

#line 25
    float _S9 = mapf_1(p_2 - _S7, kernelContext_2);

#line 25
    float _S10 = _S8 - _S9;

#line 25
    float3 _S11 = float3(0.0f, 0.00150000001303852f, 0.0f);

#line 25
    float _S12 = mapf_1(p_2 + _S11, kernelContext_2);

#line 25
    float _S13 = mapf_1(p_2 - _S11, kernelContext_2);

#line 25
    float _S14 = _S12 - _S13;

#line 25
    float3 _S15 = float3(0.0f, 0.0f, 0.00150000001303852f);

#line 25
    float _S16 = mapf_1(p_2 + _S15, kernelContext_2);

#line 25
    float _S17 = mapf_1(p_2 - _S15, kernelContext_2);

#line 25
    return normalize(float3(_S10, _S14, _S16 - _S17));
}


#line 27
float3 tracef_0(float2 fragXY_0, KernelContext_0 thread* kernelContext_3)
{

#line 27
    bool _S18;

    float2 uv_0 = (fragXY_0 - float2(0.5f)  * kernelContext_3->u_0->uResolution_0) / float2(kernelContext_3->u_0->uResolution_0.y) ;
    float ct_0 = kernelContext_3->u_0->uTime_0 * 0.20000000298023224f;
    float3 ro_0 = float3(cos(ct_0) * 2.59999990463256836f, 0.89999997615814209f * sin(kernelContext_3->u_0->uTime_0 * 0.17000000178813934f), sin(ct_0) * 2.59999990463256836f);
    float3 fw_0 = normalize(- ro_0);
    float3 rt_0 = normalize(cross(fw_0, float3(0.0f, 1.0f, 0.0f)));

    float3 rd_0 = normalize(float3(uv_0.x)  * rt_0 + float3(uv_0.y)  * cross(rt_0, fw_0) + float3(1.79999995231628418f)  * fw_0);

#line 35
    float d_0 = 1.0f;

#line 35
    int i_1 = int(0);

#line 35
    float t_0 = 0.0f;

    for(;;)
    {

#line 37
        if(i_1 < int(160))
        {
        }
        else
        {

#line 37
            break;
        }

#line 37
        float _S19 = mapf_0(ro_0 + rd_0 * float3(t_0) , kernelContext_3);

        if(_S19 < 0.00039999998989515f)
        {

#line 39
            _S18 = true;

#line 39
        }
        else
        {

#line 39
            _S18 = t_0 > 6.0f;

#line 39
        }

#line 39
        if(_S18)
        {

#line 39
            d_0 = _S19;

#line 39
            break;
        }

#line 40
        float t_1 = t_0 + _S19 * 0.69999998807907104f;

#line 37
        int _S20 = i_1 + int(1);

#line 37
        d_0 = _S19;

#line 37
        i_1 = _S20;

#line 37
        t_0 = t_1;

#line 37
    }

#line 42
    float3 _S21 = float3(0.01499999966472387f, 0.01999999955296516f, 0.03500000014901161f);
    if(d_0 < 0.0020000000949949f)
    {

#line 43
        _S18 = t_0 < 6.0f;

#line 43
    }
    else
    {

#line 43
        _S18 = false;

#line 43
    }

#line 43
    float3 col_0;

#line 43
    if(_S18)
    {

#line 44
        float3 p_3 = ro_0 + rd_0 * float3(t_0) ;

#line 44
        float3 _S22 = calcNormal_0(p_3, kernelContext_3);
        float3 l_0 = normalize(float3(cos(kernelContext_3->u_0->uTime_0 * 0.5f) * 2.0f, 2.0f, sin(kernelContext_3->u_0->uTime_0 * 0.5f) * 2.0f) - p_3);

#line 45
        float3 _S23 = float3(0.5f) ;



        float3 base_0 = _S23 + _S23 * cos(float3(3.0f)  * p_3 + float3(0.0f, 2.0f, 4.0f) + float3(kernelContext_3->u_0->uTime_0) );

#line 49
        col_0 = mix(base_0 * float3((0.10000000149011612f + max(dot(_S22, l_0), 0.0f) * 0.89999997615814209f))  + float3((pow(max(dot(reflect(- l_0, _S22), - rd_0), 0.0f), 24.0f) * 0.60000002384185791f))  + base_0 * float3((1.0f - float(i_1) / 160.0f))  * float3(0.30000001192092896f) , _S21, float3((1.0f - exp(-0.40000000596046448f * t_0 * t_0))) );

#line 43
    }
    else
    {

#line 43
        col_0 = _S21;

#line 43
    }

#line 53
    return col_0;
}

struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 56
[[fragment]] pixelOutput_0 main0(float4 pos_1 [[position]], U_0 constant* u_1 [[buffer(0)]])
{

#line 56
    thread KernelContext_0 kernelContext_4;

#line 56
    (&kernelContext_4)->u_0 = u_1;

    int n_0 = u_1->uSteps_0;

#line 58
    int n_1;

#line 58
    if((u_1->uSteps_0) < int(1))
    {

#line 58
        n_1 = int(1);

#line 58
    }
    else
    {

#line 58
        n_1 = n_0;

#line 58
    }
    float3 _S24 = float3(0.0f) ;

#line 59
    int s_1 = int(0);

#line 59
    float3 col_1 = _S24;
    for(;;)
    {

#line 60
        if(s_1 < n_1)
        {
        }
        else
        {

#line 60
            break;
        }

#line 61
        float fs_0 = float(s_1);

#line 61
        float3 _S25 = tracef_0(pos_1.xy + (float2(fract(sin(fs_0 * 12.98980045318603516f) * 43758.546875f), fract(sin(fs_0 * 78.233001708984375f) * 43758.546875f)) - float2(0.5f) ), &kernelContext_4);

        float3 col_2 = col_1 + _S25;

#line 60
        s_1 = s_1 + int(1);

#line 60
        col_1 = col_2;

#line 60
    }

#line 60
    pixelOutput_0 _S26 = { float4(pow(col_1 / float3(float(n_1)) , float3(0.4544999897480011f) ), 1.0f) };

#line 67
    return _S26;
}

