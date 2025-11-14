#version 120

// Simple vertex shader for rendering footprints to deformation texture
// Each footprint is rendered as a small quad with radial falloff

varying vec2 texCoord;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;
}
