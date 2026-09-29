#version 330

// Raylib defaults start
// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
// Raylib defaults end

// Output fragment color
out vec4 finalColor;

// Custom
// macOS GLSL doesn't seem to like converting bools to ints
uniform int lit;
uniform int hasEmissionTex;
uniform int hasEmissionCol;
uniform int grayscale;
uniform int bloomMask;
uniform sampler2D emissionMap;
uniform vec4 colEmission;

#include "lighting.fs"

void main()
{
    // Texel color fetching from texture sampler
    vec4 texelColor = texture(texture0, fragTexCoord);

    if (bloomMask == 1)
    {
        vec3 emission = vec3(0.0);
        if (hasEmissionTex == 1) emission = texture(emissionMap, fragTexCoord).rgb;
        else if (hasEmissionCol == 1) emission = colEmission.rgb;
        vec4 baseColor = lit == 1 ? Lighting_CalculateLighting(texelColor)
                                  : texelColor * colDiffuse * fragColor;
        if (grayscale == 1)
        {
            float luminance = dot(baseColor.rgb, vec3(0.299, 0.587, 0.114));
            baseColor.rgb = vec3(luminance);
        }
        vec3 highlights = max(baseColor.rgb - vec3(0.7), vec3(0.0)) * 2.0;
        finalColor = vec4(max(highlights, emission), baseColor.a);
        return;
    }

    if (lit == 1)
    {
       finalColor = Lighting_CalculateLighting(texelColor);
    }
    else
    {
       finalColor = texelColor * colDiffuse * fragColor;
    }

    if (hasEmissionTex == 1)
    {
        vec4 emissionTexelCol = texture(emissionMap, fragTexCoord);
        finalColor = finalColor + emissionTexelCol;
    }
    else if (hasEmissionCol == 1)
    {
        finalColor = finalColor + colEmission;
    }

    if (grayscale == 1)
    {
        float luminance = dot(finalColor.rgb, vec3(0.299, 0.587, 0.114));
        finalColor.rgb = vec3(luminance);
    }
}
