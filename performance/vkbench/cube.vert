#version 450
layout(location=0) in vec3 inPos;
layout(push_constant) uniform PC { mat4 mvp; vec4 col; } pc;
layout(location=0) out vec4 vColor;
void main(){ vColor = pc.col; gl_Position = pc.mvp*vec4(inPos, 1.0); }
