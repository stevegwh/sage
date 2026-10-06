#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D sceneDepth;
uniform mat4 inverseSceneProjection;
uniform float occlusionRadius;
out vec4 finalColor;
float ViewZ(vec2 uv)
{
    vec4 position = inverseSceneProjection * vec4(uv * 2.0 - 1.0,
        texture(sceneDepth, uv).r * 2.0 - 1.0, 1.0);
    return position.z / position.w;
}
void main()
{
    vec2 texel = 1.0 / vec2(textureSize(texture0, 0));
    float centerZ = ViewZ(fragTexCoord);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        vec2 uv = clamp(fragTexCoord + vec2(x, y) * texel, texel * 0.5, 1.0 - texel * 0.5);
        float spatial = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
        float weight = spatial * exp(-abs(ViewZ(uv) - centerZ) / max(occlusionRadius * 0.1, 0.01));
        sum += texture(texture0, uv).r * weight;
        total += weight;
    }
    finalColor = vec4(vec3(sum / max(total, 0.0001)), 1.0);
}
