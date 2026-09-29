#version 330

in vec3 vertexPosition;

uniform mat4 matProjection;
uniform mat4 matView;

out vec3 fragPosition;

void main()
{
    fragPosition = vertexPosition;

    mat4 rotationOnlyView = mat4(mat3(matView));
    gl_Position = matProjection*rotationOnlyView*vec4(vertexPosition, 1.0);
}
