#version 400 compatibility

#if @terrainDeformTess

layout(triangles, fractional_odd_spacing, ccw) in;

// Per-pixel lighting support (must be defined before use)
#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

// Uniforms
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;
uniform mat4 osg_ViewMatrixInverse;
uniform sampler2D terrainDeformationMap;
uniform vec2 deformationOffset;
uniform float deformationScale;
uniform int materialType;
uniform float maxDisplacementDepth;  // Maximum displacement in world units (default: 50.0)

// Shadow uniforms (if shadows are enabled)
#if @shadows_enabled
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        uniform mat4 shadowSpaceMatrix@shadow_texture_unit_index;
        out vec4 shadowSpaceCoords@shadow_texture_unit_index;
#if @perspectiveShadowMaps
        uniform mat4 validRegionMatrix@shadow_texture_unit_index;
        out vec4 shadowRegionCoords@shadow_texture_unit_index;
#endif
    @endforeach
    const bool onlyNormalOffsetUV = false;
#endif

// Input from TCS (matching vertex shader outputs)
in vec3 worldPos_TE_in[];
in vec2 uv_TE_in[];
in vec3 passNormal_TE_in[];
in vec3 passViewPos_TE_in[];

#if !PER_PIXEL_LIGHTING
in vec3 passLighting_TE_in[];
in vec3 passSpecular_TE_in[];
in vec3 shadowDiffuseLighting_TE_in[];
in vec3 shadowSpecularLighting_TE_in[];
#endif

in vec4 passColor_TE_in[];
in float euclideanDepth_TE_in[];
in float linearDepth_TE_in[];

// Output to fragment shader (must match fragment shader inputs)
out vec2 uv;
out vec3 passNormal;
out vec3 passViewPos;
out float euclideanDepth;
out float linearDepth;
out mat3 normalToViewMatrix;

#if !PER_PIXEL_LIGHTING
centroid out vec3 passLighting;
centroid out vec3 passSpecular;
centroid out vec3 shadowDiffuseLighting;
centroid out vec3 shadowSpecularLighting;
#endif

// Vertex colors
out vec4 passColor;

#include "lib/terrain/deformation.glsl"
#include "lib/view/depth.glsl"

// Interpolate barycentric coordinates
vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2)
{
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2)
{
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

void main()
{
    // Interpolate vertex attributes (in model space)
    vec3 modelPos = interpolate3D(worldPos_TE_in[0], worldPos_TE_in[1], worldPos_TE_in[2]);
    uv = interpolate2D(uv_TE_in[0], uv_TE_in[1], uv_TE_in[2]);
    vec3 baseNormal = interpolate3D(passNormal_TE_in[0], passNormal_TE_in[1], passNormal_TE_in[2]);
    passViewPos = interpolate3D(passViewPos_TE_in[0], passViewPos_TE_in[1], passViewPos_TE_in[2]);

#if !PER_PIXEL_LIGHTING
    passLighting = interpolate3D(passLighting_TE_in[0], passLighting_TE_in[1], passLighting_TE_in[2]);
    passSpecular = interpolate3D(passSpecular_TE_in[0], passSpecular_TE_in[1], passSpecular_TE_in[2]);
    shadowDiffuseLighting = interpolate3D(shadowDiffuseLighting_TE_in[0], shadowDiffuseLighting_TE_in[1], shadowDiffuseLighting_TE_in[2]);
    shadowSpecularLighting = interpolate3D(shadowSpecularLighting_TE_in[0], shadowSpecularLighting_TE_in[1], shadowSpecularLighting_TE_in[2]);
#endif

    // Interpolate color (simple linear interpolation for vec4)
    passColor = gl_TessCoord.x * passColor_TE_in[0] + gl_TessCoord.y * passColor_TE_in[1] + gl_TessCoord.z * passColor_TE_in[2];

    // Convert model position to world space for deformation texture sampling
    vec4 viewPos_temp = osg_ModelViewMatrix * vec4(modelPos, 1.0);
    vec3 worldPos = (osg_ViewMatrixInverse * viewPos_temp).xyz;

    // Sample deformation texture using world coordinates
    vec2 deformUV = (worldPos.xy + deformationOffset) / deformationScale;
    float deformValue = texture(terrainDeformationMap, deformUV).r;

    // Apply material-specific depth multiplier
    float depthMultiplier = getDepthMultiplier(materialType);

    // Displace vertex downward in model space (negative Z)
    // Clamp deformValue to prevent NaN/Inf issues
    deformValue = clamp(deformValue, 0.0, 1.0);
    float displacement = deformValue * depthMultiplier * maxDisplacementDepth;
    vec3 displacedPos = modelPos;
    displacedPos.z -= displacement;

    // Calculate new normal by sampling neighbors for gradient
    float texelSize = 1.0 / 1024.0;  // Match deformation map size
    float hL = texture(terrainDeformationMap, deformUV + vec2(-texelSize, 0.0)).r;
    float hR = texture(terrainDeformationMap, deformUV + vec2(texelSize, 0.0)).r;
    float hD = texture(terrainDeformationMap, deformUV + vec2(0.0, -texelSize)).r;
    float hU = texture(terrainDeformationMap, deformUV + vec2(0.0, texelSize)).r;

    // Compute gradient in world space
    vec3 gradient = vec3(
        (hL - hR) * depthMultiplier * maxDisplacementDepth,
        (hD - hU) * depthMultiplier * maxDisplacementDepth,
        2.0 * deformationScale * texelSize
    );

    // Blend with base normal based on deformation amount
    vec3 deformedNormal = normalize(baseNormal + gradient);
    passNormal = mix(baseNormal, deformedNormal, smoothstep(0.0, 0.2, deformValue));

    // Set up normal-to-view matrix for tangent space calculations
    normalToViewMatrix = osg_NormalMatrix;

#if @normalMap
    // Generate tangent space for normal mapping
    // Using a fixed tangent for terrain (aligned with world X axis)
    vec4 tangent = vec4(1.0, 0.0, 0.0, -1.0);
    vec3 normalizedNormal = normalize(passNormal);
    vec3 normalizedTangent = normalize(tangent.xyz);
    vec3 binormal = cross(normalizedTangent, normalizedNormal) * tangent.w;
    // Rederive tangent at 90 degrees to normal
    vec3 finalTangent = -normalize(cross(normalizedNormal, binormal));
    mat3 tbnMatrix = mat3(finalTangent, binormal, normalizedNormal);
    normalToViewMatrix *= tbnMatrix;
#endif

    // Update view position with displacement
    vec4 viewPos = osg_ModelViewMatrix * vec4(displacedPos, 1.0);
    passViewPos = viewPos.xyz;

    // Transform to clip space
    gl_Position = osg_ModelViewProjectionMatrix * vec4(displacedPos, 1.0);

    // NOTE: Water clipping (gl_ClipVertex) is not supported with tessellation shaders.
    // gl_ClipVertex doesn't work in TES, and gl_ClipDistance requires gl_ClipPlane[]
    // which may not be available in all drivers for tessellation shaders.
    // Water reflections/refractions may show more terrain than necessary, but this
    // is a minor visual artifact compared to having invisible terrain.

    // Calculate depth values for fog and other effects
    euclideanDepth = length(passViewPos);
    linearDepth = getLinearDepth(gl_Position.z, passViewPos.z);

    // Recalculate shadow coordinates for displaced geometry
#if @shadows_enabled
    vec3 viewNormal = osg_NormalMatrix * passNormal;
    vec4 shadowOffset;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
#if @perspectiveShadowMaps
        shadowRegionCoords@shadow_texture_unit_index = validRegionMatrix@shadow_texture_unit_index * viewPos;
#endif

#if @disableNormalOffsetShadows
        shadowSpaceCoords@shadow_texture_unit_index = shadowSpaceMatrix@shadow_texture_unit_index * viewPos;
#else
        shadowOffset = vec4(viewNormal * @shadowNormalOffset, 0.0);

        if (onlyNormalOffsetUV)
        {
            vec4 lightSpaceXY = viewPos + shadowOffset;
            lightSpaceXY = shadowSpaceMatrix@shadow_texture_unit_index * lightSpaceXY;

            shadowSpaceCoords@shadow_texture_unit_index.xy = lightSpaceXY.xy;
        }
        else
        {
            vec4 offsetViewPosition = viewPos + shadowOffset;
            shadowSpaceCoords@shadow_texture_unit_index = shadowSpaceMatrix@shadow_texture_unit_index * offsetViewPosition;
        }
#endif
    @endforeach
#endif
}

#endif
