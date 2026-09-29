#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texelStep;

out vec4 finalColor;

void main()
{
    vec3 glow = texture(texture0, fragTexCoord).rgb * 0.227027;
    glow += texture(texture0, fragTexCoord + texelStep * 1.384615).rgb * 0.316216;
    glow += texture(texture0, fragTexCoord - texelStep * 1.384615).rgb * 0.316216;
    glow += texture(texture0, fragTexCoord + texelStep * 3.230769).rgb * 0.070270;
    glow += texture(texture0, fragTexCoord - texelStep * 3.230769).rgb * 0.070270;
    finalColor = vec4(glow, 1.0);
}
