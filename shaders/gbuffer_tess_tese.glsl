#version 430 core
layout(triangles, equal_spacing, ccw) in;

// Inputs from the TCS
layout (location = 0) in vec2 tes_in_TexCoords[];
layout (location = 1) in vec3 tes_in_Normal[];

// Outputs for the Fragment Shader
layout (location = 0) out vec3 fs_FragPos;
layout (location = 1) out vec2 fs_TexCoords;
layout (location = 2) out vec3 fs_Normal;

uniform mat4 view;
uniform mat4 projection;
uniform sampler2D heightMap;
uniform float displacementScale;
uniform float u_texture_scale;

// Helper functions remain the same
vec3 interpolateVec3(vec3 v0, vec3 v1, vec3 v2) {
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}
float triplanar_height_sample(sampler2D tex, vec3 world_pos, vec3 world_normal) {
    vec3 blend_weights = pow(abs(world_normal), vec3(3.0));
    blend_weights /= (blend_weights.x + blend_weights.y + blend_weights.z);
    vec2 uv_x = world_pos.yz * u_texture_scale;
    vec2 uv_y = world_pos.xz * u_texture_scale;
    vec2 uv_z = world_pos.xy * u_texture_scale;
    float height_x = texture(tex, uv_x).r;
    float height_y = texture(tex, uv_y).r;
    float height_z = texture(tex, uv_z).r;
    return height_x * blend_weights.x + height_y * blend_weights.y + height_z * blend_weights.z;
}

void main()
{
    // --- 1. Calculate Interpolated Data & Normals ---
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 interpPos = interpolateVec3(p0, p1, p2);
    
    // This is the smooth, per-vertex normal used for high-quality lighting
    vec3 shadingNormal = normalize(interpolateVec3(tes_in_Normal[0], tes_in_Normal[1], tes_in_Normal[2]));

    // This is the single, stable, geometric normal of the original low-poly patch
    vec3 geoNormal = normalize(cross(p1 - p0, p2 - p0));

    // --- 2. Calculate the Displacement Value ---
    float displacement = triplanar_height_sample(heightMap, interpPos, shadingNormal);
    
    // --- 3. Hybrid Normal Blending for Crack-Free Displacement ---
    // gl_TessCoord (x,y,z) tells us how close we are to the 3 corners of the triangle.
    // If any component is 0, we are on an edge. We create a smooth falloff zone.
    float edge_tolerance = 0.15;
    float interior_weight = smoothstep(0.0, edge_tolerance, gl_TessCoord.x) *
                            smoothstep(0.0, edge_tolerance, gl_TessCoord.y) *
                            smoothstep(0.0, edge_tolerance, gl_TessCoord.z);

    // Blend between the two normals. At the edges (weight=0), use the stable geoNormal.
    // In the center of the triangle (weight=1), use the smooth shadingNormal.
    vec3 displacementNormal = mix(geoNormal, shadingNormal, interior_weight);
    
    // Displace the vertex position along this blended, crack-free normal
    interpPos += displacementNormal * displacement * displacementScale;

    // --- 4. Final Outputs ---
    gl_Position = projection * view * vec4(interpPos, 1.0);
    
    // Pass the displaced position to the fragment shader
    fs_FragPos = interpPos;
    
    // ALWAYS pass the original smooth SHADING normal for correct lighting
    fs_Normal = shadingNormal; 
    
    fs_TexCoords = vec2(0.0);
}