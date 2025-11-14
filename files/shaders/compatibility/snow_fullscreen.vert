#version 120

// Simple fullscreen quad vertex shader for RTT operations

varying vec2 texCoord;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;
}
