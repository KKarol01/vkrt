#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
}
vsout;

float lindepth(float d, float n, float f) {
    return n * f / (f - d * (f - n));
}

layout(location = 0) out vec4 OUT_COLOR;

void main() {
    vec4 col_diffuse = texture(combinedImages_2d[meshes_arr[vsout.instance_index].color_texture_idx], vsout.uv);

    vec2 vcoords = vsm_calc_virtual_coords(vsout.position);
    vec4 vlight_pos = vsm_constants.dir_light_proj * vsm_constants.dir_light_view * vec4(vsout.position, 1.0);
    vlight_pos /= vlight_pos.w;
    vlight_pos.xy = vlight_pos.xy * 0.5 + 0.5;
    float closest_depth = texture(combinedImages_2d[vsm_depth_map], vlight_pos.xy).r;
    float current_depth = vlight_pos.z - 0.005;
    float shadowing = current_depth > closest_depth ? 0.3 : 1.0;

    OUT_COLOR = vec4(shadowing * col_diffuse.rgb, 1.0);
    //OUT_COLOR = vec4(vec3(vdepth), 1.0);
#if 0
    cam_pos = vec3(GetResource(GPUConstantsBuffer, constants_index).constants.inv_view * vec4(0.0, 0.0, 0.0, 1.0));
    light_view = GetResource(VsmBuffer, vsm_buffer_index).dir_light_view;
    vsm_rclip_0_mat = GetResource(VsmBuffer, vsm_buffer_index).dir_light_proj * light_view;
    GPUMeshInstance mesh = GetResource(GPUMeshInstancesBuffer, mesh_instances_index).at[vsout.instance_index];
    vec4 sampled_color = texture(GetResource(CombinedImages2D, mesh.color_texture_idx), vsout.uv);

    vec2 vpi = vsm_calc_page_index(vsout.position, vsm_buffer_index);
    OUT_COLOR = vec4(pow(vec2(vpi) / 64.0, vec2(8.0)) * 8.0, 0.0, 1.0);
    OUT_COLOR = vec4(vsm_calc_page_uv(vsout.position, ivec2(vpi), vsm_buffer_index), 0.0, 1.0);
    if(use_vsm == 1) {
        // imageStore(GetResource(StorageImages2Dr32f, vsm_clip0_index), ivec2(vpi * 128.0), vec4(0.2));
        // for(int i=0; i<16; ++i) {
        //     for(int j=0; j<16; ++j) {
        //         //imageStore(GetResource(StorageImages2Dr32f, vsm_clip0_index), ivec2(vpi * 8.0 * 1024.0) + ivec2(i, j), vec4(0.2));
        //     }
        // }
        // OUT_COLOR = vec4(vpi, 0.0, 1.0);
        uint val = atomicAdd(GetResource(VsmBuffer, vsm_buffer_index).num_frags, 1);
        if(val < 64 * 64) { GetResource(VsmBuffer, vsm_buffer_index).pages[val] = uint(vpi.y * 64 + vpi.x); }
    }
#endif
#if 0
    OUT_COLOR = vec4(1.0);
    if(dep < lpos.z - 0.001) {
        OUT_COLOR.rgb *= 0.3;
    }

    OUT_COLOR.rgb *= sampled_color.rgb * max(0.1, dot(-vsmconsts.dir_light_dir, vsout.normal));

    vec3 lspos = vsm_calc_rclip(vsout.position, 0);
    vec2 clip_index2 = vec2(
        ceil(log2(max(abs(lspos.x), 1.0))),
        ceil(log2(max(abs(lspos.y), 1.0)))
    );
    int clip_index = 0; int(max(clip_index2.x, clip_index2.y));
    lspos = vsm_calc_sclip(vsout.position, clip_index);
    lspos.xy = lspos.xy * 0.5 + 0.5;
    lspos.xy = fract(lspos.xy);
    vec2 vtc = lspos.xy;
    
    float dep = imageLoad(GetResource(StorageImages2Dr32f, vsm_clip0_index), ivec2(vtc * 8.0 * 1024.0)).r;

    OUT_COLOR = vec4(sampled_color.rgb, 1.0);
    if(lspos.z > dep) {
        OUT_COLOR.rgb *= 0.2;
    }
#endif
    // OUT_COLOR = vec4(vec3(dep), 1.0);
}