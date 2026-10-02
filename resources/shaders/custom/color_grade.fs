#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D bloomTexture;
uniform sampler2D sceneDepth;
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
    vec2 depthSize = vec2(textureSize(sceneDepth, 0));
    // Depth uses nearest filtering. Reconstruct at the sampled pixel's center,
    // especially at grazing angles where a subpixel offset spans a large distance.
    uv = (floor(uv * depthSize) + 0.5) / depthSize;
    if (texture(sceneDepth, uv).r >= 0.99999) return 1.0;

    vec2 texel = 1.0 / depthSize;
    vec3 center = ViewPosition(uv);
    vec3 right = ViewPosition(clamp(uv + vec2(texel.x, 0.0), texel * 0.5, 1.0 - texel * 0.5)) - center;
    vec3 left = center - ViewPosition(clamp(uv - vec2(texel.x, 0.0), texel * 0.5, 1.0 - texel * 0.5));
    vec3 up = ViewPosition(clamp(uv + vec2(0.0, texel.y), texel * 0.5, 1.0 - texel * 0.5)) - center;
    vec3 down = center - ViewPosition(clamp(uv - vec2(0.0, texel.y), texel * 0.5, 1.0 - texel * 0.5));
    // Use the neighbor on the same surface instead of crossing a silhouette.
    vec3 dx = abs(right.z) < abs(left.z) ? right : left;
    vec3 dy = abs(up.z) < abs(down.z) ? up : down;
    vec3 normal = cross(dx, dy);
    if (dot(normal, normal) < 1e-12) return 1.0;
    normal = normalize(normal);
    if (dot(normal, -center) < 0.0) normal = -normal;

    // A fixed basis avoids screen-pixel noise crawling across moving surfaces.
    vec3 reference = abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(reference, normal));
    vec3 bitangent = cross(normal, tangent);
    const vec3 samples[12] = vec3[12](
        vec3( 0.25,  0.31, 0.92), vec3(-0.39,  0.18, 0.90),
        vec3( 0.15, -0.48, 0.86), vec3( 0.58,  0.12, 0.80),
        vec3(-0.55, -0.32, 0.77), vec3( 0.37,  0.59, 0.72),
        vec3(-0.12,  0.73, 0.67), vec3( 0.77, -0.35, 0.53),
        vec3(-0.81,  0.20, 0.55), vec3( 0.45, -0.77, 0.45),
        vec3(-0.48, -0.72, 0.50), vec3( 0.05,  0.92, 0.39));
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float radius = occlusionRadius * mix(0.25, 1.0, float(i) / 11.0);
        vec3 samplePosition = center +
            (tangent * samples[i].x + bitangent * samples[i].y + normal * samples[i].z) * radius;
        vec4 projected = sceneProjection * vec4(samplePosition, 1.0);
        vec2 sampleUv = projected.xy / projected.w * 0.5 + 0.5;
        if (projected.w <= 0.0 || any(lessThan(sampleUv, vec2(0.0))) ||
            any(greaterThan(sampleUv, vec2(1.0)))) continue;
        sampleUv = clamp((floor(sampleUv * depthSize) + 0.5) / depthSize,
                         texel * 0.5, 1.0 - texel * 0.5);
        float sampleDepth = texture(sceneDepth, sampleUv).r;
        if (sampleDepth >= 0.99999) continue;
        vec3 actualPosition = ViewPosition(sampleUv);
        // The receiver's own plane is not an occluder. A depth-only comparison
        // otherwise falsely darkens sloping terrain at shallow viewing angles.
        if (dot(actualPosition - center, normal) <= 0.035) continue;
        float actualZ = actualPosition.z;
        float withinRadius = smoothstep(0.0, 1.0, radius / max(abs(center.z - actualZ), 0.001));
        occlusion += step(samplePosition.z + 0.035, actualZ) * withinRadius;
    }
    return 1.0 - occlusion * (occlusionStrength / 12.0);
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
