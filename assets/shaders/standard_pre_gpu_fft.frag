#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 viewPos;
uniform samplerCube skybox;
uniform vec3 activeRipples[128];
uniform int numRipples;

void main() {
    vec3 deepWater = vec3(0.01, 0.05, 0.15);
    vec3 shallowWater = vec3(0.05, 0.25, 0.35);
    float heightFactor = clamp((FragPos.y + 1.0) / 2.0, 0.0, 1.0);
    vec3 albedo = mix(deepWater, shallowWater, heightFactor);

    vec3 finalNormal = normalize(Normal);

    for(int i = 0; i < numRipples; i++) {
        vec2 center = activeRipples[i].xy;
        float age = activeRipples[i].z;
        float dist = length(FragPos.xz - center);
        float currentRadius = age * 1.5;

        if(dist < currentRadius && dist > currentRadius - 0.2) {
            vec2 dir = normalize(FragPos.xz - center);
            float wave = sin((dist - currentRadius) * (3.1415 / 0.2));
            float fade = max(0.0, 1.0 - (age / 1.0));
            float strength = 1.0;

            finalNormal.x += dir.x * wave * fade * strength;
            finalNormal.z += dir.y * wave * fade * strength;
        }
    }
    finalNormal = normalize(finalNormal);

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 viewDir = normalize(viewPos - FragPos);
    float cosTheta = max(dot(viewDir, finalNormal), 0.0);
    float R0 = 0.02;
    float fresnel = R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0);

    vec3 I = normalize(FragPos - viewPos);
    vec3 R = reflect(I, finalNormal);
    vec3 reflectionColor = texture(skybox, R).rgb;

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(finalNormal, halfwayDir), 0.0), 256.0);
    vec3 specular = vec3(1.0) * spec * 1.5;

    vec3 finalResult = mix(albedo, reflectionColor, fresnel) + specular;
    FragColor = vec4(finalResult, 0.9);
}
