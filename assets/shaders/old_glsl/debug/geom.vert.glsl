#version 460

#include "./debug/common.glsli"

void main() 
{
    vec3 pos = get_buf(GPUDebugVertices).positions_us[gl_VertexIndex];
    gl_Position = get_buf(GPUEngConstant).proj_view * vec4(pos, 1.0);
}