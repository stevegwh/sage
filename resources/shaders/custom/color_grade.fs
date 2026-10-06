#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D bloomTexture;
uniform sampler2D sceneDepth;
uniform sampler2D occlusionTexture;
uniform mat4 sceneProjection;
uniform mat4 inverseSceneProjection;
uniform vec4 colDiffuse;
uniform int enableBloom;
uniform float bloomStrength;
uniform int enableAmbientOcclusion;
uniform float occlusionRadius;
uniform float occlusionStrength;
uniform int enableFxaa;
uniform int enableColorGrading;
uniform float saturation;
uniform float contrast;
uniform int enableDepthOfField;
uniform float focusDistance;
uniform int focusCameraTarget;
uniform float cameraTargetDistance;
uniform float focusRange;
uniform float maxBlurRadius;

out vec4 finalColor;

vec3 ViewPosition(vec2 uv)
{
    vec4 position = inverseSceneProjection * vec4(uv * 2.0 - 1.0,
                                                   texture(sceneDepth, uv).r * 2.0 - 1.0, 1.0);
    return position.xyz / position.w;
}

float AmbientOcclusion(vec2 uv)
{
    if (texture(sceneDepth, uv).r >= 0.99999) return 1.0;
    vec2 size = vec2(textureSize(occlusionTexture, 0));
    vec2 pixel = uv * size - 0.5;
    vec2 base = floor(pixel);
    vec2 fraction = fract(pixel);
    float centerZ = ViewPosition(uv).z;
    float sum = 0.0;
    float total = 0.0;
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        vec2 sampleUv = clamp((base + vec2(x, y) + 0.5) / size, 0.5 / size, 1.0 - 0.5 / size);
        float spatial = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                        (y == 0 ? 1.0 - fraction.y : fraction.y);
        float delta = abs(ViewPosition(sampleUv).z - centerZ);
        float weight = spatial * exp(-delta / max(occlusionRadius * 0.1, 0.01));
        sum += texture(occlusionTexture, sampleUv).r * weight;
        total += weight;
    }
    return total > 0.0001 ? sum / total : 1.0;
}

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 SampleScene(vec2 uv, vec2 texel)
{
    return texture(texture0, clamp(uv, texel * 0.5, vec2(1.0) - texel * 0.5)).rgb;
}

vec3 ApplyDepthOfField(vec2 uv, vec3 sharpColor)
{
    float centerDepth = texture(sceneDepth, uv).r;
    // Skybox pixels retain the far-plane depth and should blur with the background.
    float viewDistance = -ViewPosition(uv).z;
    float focalDistance = focusCameraTarget != 0 ? cameraTargetDistance : focusDistance;
    float blur = smoothstep(max(focusRange, 0.001), max(focusRange, 0.001) * 2.0,
                            abs(viewDistance - focalDistance));
    float radius = blur * max(maxBlurRadius, 0.0) * float(textureSize(texture0, 0).y) / 720.0;
    if (radius < 0.5) return sharpColor;

    vec2 texel = 1.0 / vec2(textureSize(texture0, 0));
    vec3 sum = sharpColor;
    float weight = 1.0;
    // A dense sunflower disk avoids the visible repeated edges of the old 12 taps.
    const int SAMPLE_COUNT = 48;
    const float GOLDEN_ANGLE = 2.39996323;
    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float angle = float(i) * GOLDEN_ANGLE;
        vec2 disk = vec2(cos(angle), sin(angle)) * sqrt((float(i) + 0.5) / float(SAMPLE_COUNT));
        vec2 sampleUv = clamp(uv + disk * texel * radius, texel * 0.5, vec2(1.0) - texel * 0.5);
        float sampleDepth = texture(sceneDepth, sampleUv).r;
        // A sharp foreground object must not smear into blurred scenery behind it.
        if (sampleDepth < centerDepth && sampleDepth < 0.99999 &&
            -ViewPosition(sampleUv).z < viewDistance - max(focusRange * 0.25, 0.1)) continue;
        sum += SampleScene(sampleUv, texel);
        weight += 1.0;
    }
    return mix(sharpColor, sum / weight, blur);
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
    vec4 sceneSample = texture(texture0, fragTexCoord);
    vec4 scene = sceneSample * colDiffuse * fragColor;
    vec3 color = enableFxaa != 0 ? ApplyFxaa(fragTexCoord) : sceneSample.rgb;
    if (enableDepthOfField != 0)
        color = ApplyDepthOfField(fragTexCoord, color);
    color *= colDiffuse.rgb * fragColor.rgb;
    if (enableAmbientOcclusion != 0) color *= AmbientOcclusion(fragTexCoord);
    if (enableBloom != 0) color += texture(bloomTexture, fragTexCoord).rgb * bloomStrength;
    color = clamp(color, 0.0, 1.0);
    if (enableColorGrading != 0)
    {
        float luminance = Luminance(color);
        color = mix(vec3(luminance), color, saturation);
        color = (color - 0.5) * contrast + 0.5;
        color *= vec3(1.012, 1.0, 0.988);
    }
    finalColor = vec4(clamp(color, 0.0, 1.0), scene.a);
}
