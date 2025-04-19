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

//float calc_closest_depth(vec3 wpos) {
//    //vec4 posc = vsm_constants.dir_light_proj_view[2] * vec4(world_pos, 1.0);
//    //posc /= posc.w;
//    //posc.xy = posc.xy * 0.5 + 0.5;
//
//    VsmAddresses addr = vsm_calc_addresses(wpos);
//    ivec2 ppage_coords = vsm_calc_physical_address(addr.vsm_uv, addr.page_address);
//    if(ppage_coords == VSM_INVALID_PAGE_TEXEL) { return 1.0; }
//    return uintBitsToFloat(imageLoad(vsm_pdepth_uint, ppage_coords).r);
//}

const vec3 colors[] = vec3[](
    vec3(1.0, 0.0, 0.0),   // Red
    vec3(0.0, 1.0, 0.0),   // Green
    vec3(0.0, 0.0, 1.0),   // Blue
    vec3(1.0, 1.0, 0.0),   // Yellow
    vec3(1.0, 0.0, 1.0),   // Magenta
    vec3(0.0, 1.0, 1.0),   // Cyan
    vec3(1.0, 0.5, 0.0),   // Orange
    vec3(0.6, 0.2, 0.8)    // Purple
);

void main() {
    vec4 col_diffuse = texture(combinedImages_2d[meshes_arr[vsout.instance_index].color_texture_idx], vsout.uv);

    int clip_index = vsm_calc_clip_index(vsout.position);
    vec3 sclip = vsm_calc_rclip(vsout.position, clip_index);
    vec2 vsm_uv = vsm_calc_virtual_uv(vsout.position, clip_index);
    uint vpage = vsm_read_virtual_page(vsm_uv, clip_index);

    float vlight_proj_dist = sclip.z;
    float closest_depth    = vsm_get_depth(vsm_uv, clip_index);

    const float baseBias       = 0.00001;
    const float slopeBiasConst = 0.0001;
    float cascadeBias = baseBias * exp2(float(clip_index));

    float NdotL = max(0.0, dot(normalize(vsout.normal), normalize(vsm_constants.dir_light_dir)));
    float slopeBias = slopeBiasConst * (1.0 - NdotL);

    float depthBias = cascadeBias + slopeBias;

    float current_depth = vlight_proj_dist - depthBias;
    float shadowing     = current_depth > closest_depth ? 0.3 : 1.0;
    ivec2 ppos_coords   = vsm_calc_physical_address(vsm_uv, clip_index);

    OUT_COLOR = vec4(shadowing * col_diffuse.rgb, 1.0);

    //OUT_COLOR = vec4(vec3(closest_depth), 1.0);
    //OUT_COLOR = vec4(vec2(ppos_coords) / vec2(VSM_PHYSICAL_PAGE_RESOLUTION), 0.0, 1.0);
    //OUT_COLOR = vec4(vsm_uv, 0.0, 1.0);
    //OUT_COLOR = vec4(sclip.xy * 0.5 + 0.5, 0.0, 1.0);
    //OUT_COLOR = vec4(vec3(vsm_is_alloc_backed(vpage)), 1.0);
    //OUT_COLOR = vec4(fract(addr.ndc.xy * 0.5 + 0.5), 0.0, 1.0);
    //OUT_COLOR = vec4(vec2(addr.page_address.xy) / 64.0, 0.0, 1.0);
    // int vcascade = vsm_calc_virtual_page_texel(vsout.position).addr.z;
    // ivec2 vcascadeidx = vsm_calc_virtual_page_texel(vsout.position).addr.xy;
    // OUT_COLOR = vec4(fract(vndc.xy * 0.5 + 0.5), 0.0, 1.0);
    // vec2 ax = vec2(vaddr.xy) / float(VSM_VIRTUAL_PAGE_RESOLUTION);
    // OUT_COLOR = vec4(sin(ax.x), cos(ax.y), 0.0, 1.0);
    // OUT_COLOR = vec4(vec3(vaddr.z) / 8.0, 1.0);
    // OUT_COLOR = vec4(vec3(vcascade) / 8.0, 1.0);
    // OUT_COLOR = vec4(vec3(vsm_calc_virtual_page_texel(vsout.position).ndc), 1.0);
    // OUT_COLOR = vec4(vec2(vcascadeidx) / 64.0, 0.0, 1.0);
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