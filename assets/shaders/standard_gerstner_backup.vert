#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal; // This is now JUST the physics splash normal

out vec3 FragPos;
out vec3 Normal; 

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// NEW: Time passed from main.cpp
uniform float time; 

// --- GERSTNER WAVE ALGORITHM ---
vec3 GerstnerWave(vec4 wave, vec3 p, inout vec3 tangent, inout vec3 binormal) {
    float steepness = wave.z;
    float wavelength = wave.w;
    
    // k is the frequency of the wave
    float k = 2.0 * 3.14159 / wavelength;
    
    // c is the phase speed, calculated based on physical gravity (9.8)
    float c = sqrt(9.8 / k);
    
    vec2 d = normalize(wave.xy);
    
    // The core wave equation
    float f = k * (dot(d, p.xz) - c * time);
    float a = steepness / k; // Amplitude

    // Accumulate Tangent and Binormal to calculate the exact lighting normal
    tangent += vec3(
        -d.x * d.x * (steepness * sin(f)),
        d.x * (steepness * cos(f)),
        -d.x * d.y * (steepness * sin(f))
    );
    binormal += vec3(
        -d.x * d.y * (steepness * sin(f)),
        d.y * (steepness * cos(f)),
        -d.y * d.y * (steepness * sin(f))
    );
    
    // Return the actual X, Y, Z displacement of this vertex
    return vec3(
        d.x * (a * cos(f)),
        a * sin(f),
        d.y * (a * cos(f))
    );
}

void main() {
    // aPos.y currently contains the height of the physics splashes from the CPU
    vec3 p = aPos; 
    
    // Base vectors for a flat grid
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);
    
    // We use the flat XZ position for wave phases so waves don't tear themselves apart
    vec3 wavePos = vec3(aPos.x, 0.0, aPos.z);

    // --- DEFINE THE OCEAN WAVES ---
    // Format: vec4(DirectionX, DirectionZ, Steepness, Wavelength)
    vec4 wave1 = vec4(1.0, 0.5, 0.15, 12.0); // Big, wide rolling swell
    vec4 wave2 = vec4(0.8, -0.6, 0.2, 5.0);  // Medium choppy wave
    vec4 wave3 = vec4(-0.2, 0.3, 0.15, 2.5); // Small surface disturbance

    // Displace the vertex position by adding all 3 waves together
    p += GerstnerWave(wave1, wavePos, tangent, binormal);
    p += GerstnerWave(wave2, wavePos, tangent, binormal);
    p += GerstnerWave(wave3, wavePos, tangent, binormal);

    // Calculate the pure ocean normal from the derivatives
    vec3 oceanNormal = normalize(cross(binormal, tangent));
    
    // BLEND WITH THE SPLASHES: 
    // We perfectly blend the CPU splash normal (aNormal) into the new rolling Ocean Normal
    vec3 finalLocalNormal = normalize(oceanNormal + vec3(aNormal.x, 0.0, aNormal.z));

    FragPos = vec3(model * vec4(p, 1.0));
    
    // Send normal to fragment shader (handles model rotation)
    Normal = mat3(transpose(inverse(model))) * finalLocalNormal;  
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
