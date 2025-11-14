#version 120

// Inputs from vertex shader
varying vec3 worldPos;
varying vec3 viewPos;
varying vec3 normal;
varying vec2 texCoord;
varying float deformationDepth;

// Uniforms
uniform vec3 playerPos;
uniform float deformationStrength;

// Note: We use simple custom lighting instead of OpenMW's full lighting system
// to keep the snow deformation shader simple and performant

// Snow material properties
const vec3 snowAlbedo = vec3(0.95, 0.95, 0.98); // Slightly blue-white
const float snowRoughness = 0.6;
const float snowMetallic = 0.0;
const vec3 snowSpecular = vec3(0.2, 0.2, 0.2);

void main()
{
    // Normalize interpolated normal
    vec3 N = normalize(normal);

    // View direction
    vec3 V = normalize(-viewPos);

    // Calculate lighting
    vec3 diffuseColor = snowAlbedo;
    vec3 ambientColor = diffuseColor * 0.3; // Ambient contribution

    // Simple directional lighting for now
    // TODO: Integrate with OpenMW's full lighting system
    vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0)); // Sun direction (placeholder)
    float NdotL = max(dot(N, lightDir), 0.0);
    vec3 diffuse = diffuseColor * NdotL;

    // Specular (Blinn-Phong)
    vec3 H = normalize(lightDir + V);
    float NdotH = max(dot(N, H), 0.0);
    float specularPower = mix(8.0, 32.0, 1.0 - snowRoughness);
    vec3 specular = snowSpecular * pow(NdotH, specularPower);

    // Combine lighting
    vec3 finalColor = ambientColor + diffuse + specular;

    // Add subtle darkening based on deformation depth (compressed snow is slightly darker)
    float darkeningFactor = 1.0 - (deformationDepth * 0.1);
    finalColor *= darkeningFactor;

    // Fade out at edges based on distance from player
    float distToPlayer = length(worldPos.xy - playerPos.xy);
    float fadeDist = 45.0; // Start fading at 45 units (near mesh edge at 50 unit radius)
    float fadeWidth = 5.0; // Fade over 5 units for smooth transition
    float alpha = 1.0 - smoothstep(fadeDist, fadeDist + fadeWidth, distToPlayer);

    // Output
    gl_FragColor = vec4(finalColor, alpha);
}
