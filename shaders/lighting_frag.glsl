#version 430 core

in vec2 TexCoords;
out vec4 fragColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightColor;

// world up for AO proxy
const vec3 ambientUp = vec3(0.0, 1.0, 0.0);

void main() {
    vec3 fragPos    = texture(gPosition,  TexCoords).rgb;
    vec3 N          = normalize(texture(gNormal, TexCoords).rgb);
    vec4 albedoSpec = texture(gAlbedoSpec, TexCoords);
    vec3 albedo     = albedoSpec.rgb;
    float specInt   = albedoSpec.a;

    if (specInt < 0.1) {
        discard;
    }

    // Hemispheric ambient: mix sky and ground
    float nh = dot(N, ambientUp) * 0.5 + 0.5; // [0,1]
    vec3 skyColor    = vec3(0.3, 0.3, 0.4);
    vec3 groundColor = vec3(0.2, 0.2, 0.15);
    vec3 hemiAmbient = mix(groundColor, skyColor, nh) * albedo;

    // Proxy AO: reduce ambient in steep contact angles
    float ao = clamp(nh, 0.0, 1.0);
    hemiAmbient *= ao;

    // Diffuse
    vec3 L = normalize(lightPos - fragPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * lightColor * albedo;

    // Specular (optional)
    vec3 V = normalize(viewPos - fragPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 32.0);
    vec3 specular = specInt * spec * lightColor;

    fragColor = vec4(hemiAmbient + diffuse + specular, 1.0);
}
