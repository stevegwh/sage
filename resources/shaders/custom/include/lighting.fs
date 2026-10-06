const int MAX_LIGHTS = 50;
const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_POINT = 1;
// Daylight skybox horizon color; keep the fade partial so distant silhouettes remain readable.
const vec3 FOG_COLOR = vec3(0.53, 0.59, 0.52);
const float FOG_START = 65.0;
const float FOG_END = 190.0;
const float FOG_STRENGTH = 0.55;
struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float brightness;
    float constant; // See: https://developer.valvesoftware.com/wiki/Constant-Linear-Quadratic_Falloff
    float linear;
    float quadratic;
};

// Input lighting values
uniform int lightsCount;
uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;
uniform float gamma;
// Separate samplers keep this compatible with GLSL 330 (no dynamically indexed sampler array).
uniform samplerCube pointShadowMap0;
uniform samplerCube pointShadowMap1;
uniform samplerCube pointShadowMap2;
uniform int pointShadowLightIndices[3];
uniform float shadowFarPlane;
uniform sampler2D sunShadowMap;
uniform int sunShadowLightIndex;
uniform mat4 sunLightMatrix;

vec4 Lighting_CalculateLighting(vec4 texelColor)
{
    vec3 diffuse = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);
    for (int i = 0; i < lightsCount; i++)
    {
        if (lights[i].enabled == 1)
        {
            vec3 light = vec3(0.0);
            float attenuation = 1.0;
            float strength = lights[i].brightness;

            if (lights[i].type == LIGHT_DIRECTIONAL)
            {
                light = -normalize(lights[i].target - lights[i].position);
                attenuation = 1.0; // constant
            }

            if (lights[i].type == LIGHT_POINT)
            {
                vec3 lightVector = lights[i].position - fragPosition;
                light = normalize(lightVector);

                float distance = length(lightVector);

                float falloff = lights[i].constant + lights[i].linear * distance
                    + lights[i].quadratic * distance * distance;
                attenuation = 1.0 / max(falloff, 1.0);
            }

            float NdotL = max(dot(normal, light), 0.0);
            float visibility = 1.0;
            bool shadowedPoint = i == pointShadowLightIndices[0] || i == pointShadowLightIndices[1]
                || i == pointShadowLightIndices[2];
            if (shadowedPoint && length(fragPosition - lights[i].position) < shadowFarPlane)
            {
                vec3 fromLight = fragPosition - lights[i].position;
                float nearest = shadowFarPlane;
                if (i == pointShadowLightIndices[0])
                    nearest = texture(pointShadowMap0, fromLight).r * shadowFarPlane;
                else if (i == pointShadowLightIndices[1])
                    nearest = texture(pointShadowMap1, fromLight).r * shadowFarPlane;
                else if (i == pointShadowLightIndices[2])
                    nearest = texture(pointShadowMap2, fromLight).r * shadowFarPlane;
                float bias = max(0.05, 0.05 * (1.0 - NdotL));
                if (length(fromLight) > nearest + bias) visibility = 0.0;
            }
            if (i == sunShadowLightIndex)
            {
                vec4 clip = sunLightMatrix * vec4(fragPosition, 1.0);
                vec3 projected = clip.xyz / clip.w;
                vec2 uv = projected.xy * 0.5 + 0.5;
                float depth = projected.z * 0.5 + 0.5;
                if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && depth >= 0.0 && depth <= 1.0)
                {
                    float nearest = texture(sunShadowMap, uv).r;
                    float bias = max(0.001, 0.002 * (1.0 - NdotL));
                    if (depth > nearest + bias) visibility = 0.0;
                }
            }
            vec3 lightColor = lights[i].color.rgb * attenuation * strength * visibility;
            diffuse += lightColor * NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);// 16 refers to shine
            specular += lightColor * specCo * 0.5;
        }
    }

    vec3 baseColor = texelColor.rgb * colDiffuse.rgb;
    vec3 litColor = (baseColor * (ambient.rgb / 10.0 + diffuse) + specular) * fragColor.rgb;
    vec3 correctedColor = pow(max(litColor, vec3(0.0)), vec3(1.0 / gamma));
    float fogAmount = smoothstep(FOG_START, FOG_END, length(viewPos - fragPosition)) * FOG_STRENGTH;
    return vec4(mix(correctedColor, FOG_COLOR, fogAmount), texelColor.a * colDiffuse.a * fragColor.a);
}
