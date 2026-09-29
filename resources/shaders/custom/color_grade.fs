#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D bloomTexture;
uniform vec4 colDiffuse;

out vec4 finalColor;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 SampleScene(vec2 uv, vec2 texel)
{
    return texture(texture0, clamp(uv, texel * 0.5, vec2(1.0) - texel * 0.5)).rgb;
}

vec3 ApplyFxaa(vec2 uv)
{
    vec2 texel = 1.0 / vec2(textureSize(texture0, 0));
    vec3 center = SampleScene(uv, texel);
    vec3 northwest = SampleScene(uv + texel * vec2(-1.0, -1.0), texel);
    vec3 northeast = SampleScene(uv + texel * vec2(1.0, -1.0), texel);
    vec3 southwest = SampleScene(uv + texel * vec2(-1.0, 1.0), texel);
    vec3 southeast = SampleScene(uv + texel * vec2(1.0, 1.0), texel);

    float centerLuma = Luminance(center);
    float northwestLuma = Luminance(northwest);
    float northeastLuma = Luminance(northeast);
    float southwestLuma = Luminance(southwest);
    float southeastLuma = Luminance(southeast);
    float minLuma = min(centerLuma, min(min(northwestLuma, northeastLuma), min(southwestLuma, southeastLuma)));
    float maxLuma = max(centerLuma, max(max(northwestLuma, northeastLuma), max(southwestLuma, southeastLuma)));
    if (maxLuma - minLuma < max(0.0312, maxLuma * 0.125)) return center;

    vec2 direction = vec2(-((northwestLuma + northeastLuma) - (southwestLuma + southeastLuma)),
                          (northwestLuma + southwestLuma) - (northeastLuma + southeastLuma));
    float directionReduce = max((northwestLuma + northeastLuma + southwestLuma + southeastLuma) * (0.25 * 0.0312),
                                1.0 / 128.0);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + directionReduce),
                      vec2(-8.0), vec2(8.0)) * texel;

    vec3 nearColor = 0.5 * (SampleScene(uv + direction * (1.0 / 3.0 - 0.5), texel) +
                            SampleScene(uv + direction * (2.0 / 3.0 - 0.5), texel));
    vec3 farColor = nearColor * 0.5 + 0.25 * (SampleScene(uv - direction * 0.5, texel) +
                                             SampleScene(uv + direction * 0.5, texel));
    float farLuma = Luminance(farColor);
    return farLuma < minLuma || farLuma > maxLuma ? nearColor : farColor;
}

void main()
{
    vec4 scene = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 color = ApplyFxaa(fragTexCoord) * colDiffuse.rgb * fragColor.rgb;
    color = clamp(color + texture(bloomTexture, fragTexCoord).rgb * 0.65, 0.0, 1.0);
    float luminance = Luminance(color);
    color = mix(vec3(luminance), color, 1.04);
    color = (color - 0.5) * 1.04 + 0.5;
    color *= vec3(1.012, 1.0, 0.988);
    finalColor = vec4(clamp(color, 0.0, 1.0), scene.a);
}
