#version 330

// Raylib defaults start
// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec4 terrainWeights;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
// Raylib defaults end

// Output fragment color
out vec4 finalColor;
uniform int bloomMask;
#include "lighting.fs"

void main()
{
    // Texel color fetching from texture sampler
    vec4 weights = terrainWeights / max(1.0, dot(terrainWeights, vec4(1.0)));
    vec3 base = vec3(92.0, 142.0, 74.0) / 255.0;
    vec4 texelColor = vec4(base * (1.0 - dot(weights, vec4(1.0)))
        + texture(texture0, fragTexCoord).rgb * weights.r
        + texture(texture1, fragTexCoord).rgb * weights.g
        + texture(texture2, fragTexCoord).rgb * weights.b
        + texture(texture3, fragTexCoord).rgb * weights.a, 1.0);

    vec4 litColor = Lighting_CalculateLighting(texelColor);
    if (bloomMask == 1)
    {
        finalColor = vec4(max(litColor.rgb - vec3(0.7), vec3(0.0)) * 2.0, litColor.a);
        return;
    }

    finalColor = litColor;
}
