#version 120

// Fragment shader for rendering footprints to deformation texture
// Outputs radial gradient for smooth deformation

#include "lib/terrain/deformation.glsl"

varying vec2 texCoord;

uniform float footprintIntensity;  // 0.0 to 1.0
uniform float footprintRadius;     // Footprint size

void main()
{
    // Calculate distance from center (texCoord goes from 0 to 1)
    vec2 centered = texCoord * 2.0 - 1.0;
    float dist = length(centered);

    // Use falloff function from deformation library with actual footprint radius
    float falloff = getFootprintFalloff(dist, footprintRadius);

    // Apply intensity
    float deformation = falloff * footprintIntensity;

    // Output to red channel (height map)
    gl_FragColor = vec4(deformation, 0.0, 0.0, 1.0);
}
