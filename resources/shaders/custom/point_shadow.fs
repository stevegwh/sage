#version 330

in vec3 fragWorldPos;
uniform vec3 lightPosition;
uniform float shadowFarPlane;

out vec4 finalColor;

void main()
{
    float distance = length(fragWorldPos - lightPosition) / shadowFarPlane;
    finalColor = vec4(distance, 0.0, 0.0, 1.0);
}
