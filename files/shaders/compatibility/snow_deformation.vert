#version 120

// Include core vertex functions and deformation utilities
#include "lib/core/vertex.h.glsl"
#include "lib/terrain/deformation.glsl"

// Uniforms - using OpenMW's injected uniforms
uniform vec3 playerPos;

// Deformation uniforms
uniform sampler2D deformationMap;
uniform float deformationStrength;
uniform vec2 textureCenter;
uniform float worldTextureSize;

// Outputs to fragment shader
varying vec3 worldPos;
varying vec3 viewPos;
varying vec3 normal;
varying vec2 texCoord;
varying float deformationDepth;

// Sample deformation height from texture
float sampleDeformation(vec2 worldPos)
{
    // Convert world position to texture coordinates
    vec2 offset = worldPos - textureCenter;
    vec2 uv = (offset / worldTextureSize) + 0.5;

    // Clamp to valid texture range
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 0.0;

    // Sample deformation texture (red channel contains height)
    return texture2D(deformationMap, uv).r;
}

// Calculate normal using finite differences
vec3 calculateNormal(vec2 worldPos, float centerHeight)
{
    // Texel size in world units
    float delta = worldTextureSize / 1024.0;

    // Sample neighboring heights
    float heightRight = sampleDeformation(worldPos + vec2(delta, 0.0));
    float heightLeft = sampleDeformation(worldPos - vec2(delta, 0.0));
    float heightUp = sampleDeformation(worldPos + vec2(0.0, delta));
    float heightDown = sampleDeformation(worldPos - vec2(0.0, delta));

    // Calculate gradients
    float dx = (heightRight - heightLeft) / (2.0 * delta);
    float dy = (heightUp - heightDown) / (2.0 * delta);

    // Construct tangent space vectors
    vec3 tangent = normalize(vec3(1.0, 0.0, dx * deformationStrength));
    vec3 bitangent = normalize(vec3(0.0, 1.0, dy * deformationStrength));

    // Calculate normal via cross product
    return normalize(cross(tangent, bitangent));
}

void main()
{
    // Get world position of vertex (before displacement)
    // The mesh is transformed to be centered at playerPos, so:
    // worldPos = playerPos + vertexOffset
    vec2 worldPosXY = playerPos.xy + gl_Vertex.xy;

    // Sample deformation at this position
    float deformation = sampleDeformation(worldPosXY);

    // Apply vertical displacement (negative = sink into surface)
    // Use TERRAIN_SNOW material type (1) for snow-specific depth
    float depthMultiplier = getDepthMultiplier(1); // From deformation.glsl
    float displacement = -deformation * deformationStrength * depthMultiplier;

    // Displace vertex downward
    vec3 displacedVertex = gl_Vertex.xyz + vec3(0.0, 0.0, displacement);

    // Calculate new normal based on deformation gradient
    vec3 newNormal;
    if (deformation > 0.01) // Only recalculate if there's significant deformation
    {
        newNormal = calculateNormal(worldPosXY, deformation);
    }
    else
    {
        newNormal = gl_Normal;
    }

    // Transform to view space
    viewPos = (gl_ModelViewMatrix * vec4(displacedVertex, 1.0)).xyz;
    normal = normalize(gl_NormalMatrix * newNormal);

    // Pass through to fragment shader
    worldPos = vec3(worldPosXY, playerPos.z + gl_Vertex.z + displacement);
    texCoord = gl_MultiTexCoord0.xy;
    deformationDepth = deformation;

    // Final position
    gl_Position = gl_ModelViewProjectionMatrix * vec4(displacedVertex, 1.0);
}
