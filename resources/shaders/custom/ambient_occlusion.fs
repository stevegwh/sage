#version 330
in vec2 fragTexCoord;
uniform sampler2D sceneDepth;
uniform mat4 sceneProjection;
uniform mat4 inverseSceneProjection;
uniform float occlusionRadius;
uniform float occlusionStrength;
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

void main()
{
    finalColor = vec4(vec3(AmbientOcclusion(fragTexCoord)), 1.0);
}
