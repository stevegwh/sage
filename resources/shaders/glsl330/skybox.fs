#version 330

in vec3 fragPosition;

uniform samplerCube environmentMap;
uniform bool vflipped;
uniform bool doGamma;

out vec4 finalColor;

void main()
{
    vec3 direction = vflipped ? vec3(fragPosition.x, -fragPosition.y, fragPosition.z) : fragPosition;
    vec3 color = texture(environmentMap, direction).rgb;

    if (doGamma)
    {
        color = color/(color + vec3(1.0));
        color = pow(color, vec3(1.0/2.2));
    }

    finalColor = vec4(color, 1.0);
}
