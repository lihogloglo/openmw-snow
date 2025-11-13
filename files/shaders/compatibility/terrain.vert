#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/vertex.h.glsl"

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

varying vec2 uv;
varying float euclideanDepth;
varying float linearDepth;

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 passSpecular;
centroid varying vec3 shadowDiffuseLighting;
centroid varying vec3 shadowSpecularLighting;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

#include "vertexcolors.glsl"
#include "shadows_vertex.glsl"
#include "compatibility/normals.glsl"

#include "lib/light/lighting.glsl"
#include "lib/view/depth.glsl"

#if @terrainDeformTess
// Tessellation outputs
uniform mat4 osg_ViewMatrixInverse;

out vec3 worldPos_TC_in;
out vec2 uv_TC_in;
out vec3 passNormal_TC_in;
out vec3 passViewPos_TC_in;

#if !PER_PIXEL_LIGHTING
out vec3 passLighting_TC_in;
out vec3 passSpecular_TC_in;
out vec3 shadowDiffuseLighting_TC_in;
out vec3 shadowSpecularLighting_TC_in;
#endif

out vec4 passColor_TC_in;
out float euclideanDepth_TC_in;
out float linearDepth_TC_in;
#endif

void main(void)
{
    gl_Position = modelToClip(gl_Vertex);

    vec4 viewPos = modelToView(gl_Vertex);
    gl_ClipVertex = viewPos;
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    passColor = gl_Color;
    passNormal = gl_Normal.xyz;
    passViewPos = viewPos.xyz;
    normalToViewMatrix = gl_NormalMatrix;

#if @normalMap
    mat3 tbnMatrix = generateTangentSpace(vec4(1.0, 0.0, 0.0, -1.0), passNormal);
    tbnMatrix[0] = -normalize(cross(tbnMatrix[2], tbnMatrix[1]));
    normalToViewMatrix *= tbnMatrix;
#endif

#if !PER_PIXEL_LIGHTING || @shadows_enabled
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
#endif

#if !PER_PIXEL_LIGHTING
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(viewPos.xyz, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, shadowDiffuseLighting, shadowSpecularLighting);
    passLighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    passSpecular = getSpecularColor().xyz * specularLight;
    clampLightingResult(passLighting);
    shadowDiffuseLighting *= getDiffuseColor().xyz;
    shadowSpecularLighting *= getSpecularColor().xyz;
#endif

    uv = gl_MultiTexCoord0.xy;

#if (@shadows_enabled)
    setupShadowCoords(viewPos, viewNormal);
#endif

#if @terrainDeformTess
    // Pass data to tessellation control shader
    worldPos_TC_in = gl_Vertex.xyz;  // Pass model space position
    uv_TC_in = uv;
    passNormal_TC_in = passNormal;
    passViewPos_TC_in = passViewPos;

#if !PER_PIXEL_LIGHTING
    passLighting_TC_in = passLighting;
    passSpecular_TC_in = passSpecular;
    shadowDiffuseLighting_TC_in = shadowDiffuseLighting;
    shadowSpecularLighting_TC_in = shadowSpecularLighting;
#endif

    passColor_TC_in = passColor;
    euclideanDepth_TC_in = euclideanDepth;
    linearDepth_TC_in = linearDepth;
#endif
}
