const int MAX_LIGHTS = 50;
const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_POINT = 1;
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
            vec3 lightColor = lights[i].color.rgb * attenuation * strength;
            diffuse += lightColor * NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);// 16 refers to shine
            specular += lightColor * specCo * 0.5;
        }
    }

    vec3 baseColor = texelColor.rgb * colDiffuse.rgb;
    vec3 litColor = (baseColor * (ambient.rgb / 10.0 + diffuse) + specular) * fragColor.rgb;
    vec3 correctedColor = pow(max(litColor, vec3(0.0)), vec3(1.0 / gamma));
    return vec4(correctedColor, texelColor.a * colDiffuse.a * fragColor.a);
}
