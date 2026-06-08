#version 460 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;

uniform vec3        viewPos;
uniform samplerCube skybox;       // environment ambient
uniform vec3        baseColor;    // fallback albedo when no texture

// glTF material maps (bound by Model::draw)
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;
uniform int hasAlbedo;
uniform int hasNormalMap;
uniform int hasAO;

// Same sun as the water shader, for consistent lighting.
const vec3 kSunDir = vec3(0.43193, 0.86386, 0.25932);

// Perturb the geometric normal by a tangent-space normal map WITHOUT needing
// precomputed tangents: derive the tangent basis from screen-space derivatives
// of position and UV (Mikkelsen's cotangent-frame trick).
vec3 applyNormalMap(vec3 N, vec3 worldPos, vec2 uv) {
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    mat3 TBN = mat3(T * invmax, B * invmax, N);

    vec3 n = texture(normalMap, uv).xyz * 2.0 - 1.0;
    return normalize(TBN * n);
}

void main() {
    // ---- albedo ----
    vec3 albedo = (hasAlbedo == 1) ? texture(albedoMap, UV).rgb : baseColor;

    // ---- normal (geometric, optionally perturbed by the normal map) ----
    vec3 N = normalize(Normal);
    if (hasNormalMap == 1) {
        N = applyNormalMap(N, FragPos, UV);
    }

    vec3 V = normalize(viewPos - FragPos);

    // ---- roughness (glTF packs roughness in G channel) ----
    float rough = (textureSize(roughnessMap, 0).x > 1) ? texture(roughnessMap, UV).g : 0.85;
    float ao    = (hasAO == 1) ? texture(aoMap, UV).r : 1.0;

    // ---- diffuse (sun) ----
    float diff = max(dot(N, kSunDir), 0.0);

    // ---- environment ambient from the skybox (rougher -> dimmer/flatter) ----
    vec3 R = reflect(-V, N);
    vec3 skyAmbient = texture(skybox, R).rgb;

    // ---- specular, dampened by roughness ----
    vec3  H = normalize(kSunDir + V);
    float specPow = mix(64.0, 8.0, rough);
    float spec = pow(max(dot(N, H), 0.0), specPow) * (1.0 - rough) * 0.5;

    // Albedo maps are uploaded as sRGB so we have LINEAR colour here; light in
    // linear then encode back to sRGB, or it renders too dark/muddy.
    vec3 ambient = 0.85 * albedo * skyAmbient;       // brighter sky fill
    vec3 color   = (ambient + albedo * diff * 1.2) * ao + vec3(spec);

    color = pow(color, vec3(1.0 / 2.2));             // linear -> sRGB

    FragColor = vec4(color, 1.0);
}
