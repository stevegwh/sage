#version 330

in vec3 vertexPosition;
in vec4 vertexBoneIds;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 boneMatrices[128];
uniform int skinned;

out vec3 fragWorldPos;

void main()
{
    vec4 pos = vec4(vertexPosition, 1.0);
    if (skinned == 1)
    {
        pos = vertexBoneWeights.x * (boneMatrices[int(vertexBoneIds.x)] * pos)
            + vertexBoneWeights.y * (boneMatrices[int(vertexBoneIds.y)] * pos)
            + vertexBoneWeights.z * (boneMatrices[int(vertexBoneIds.z)] * pos)
            + vertexBoneWeights.w * (boneMatrices[int(vertexBoneIds.w)] * pos);
    }

    fragWorldPos = vec3(matModel * pos);
    gl_Position = mvp * pos;
}
