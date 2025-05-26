#version 330 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;          // not used, but left for future
layout(location = 2) in vec2 inTexCoord;

uniform mat4 MVP;

out vec2 vTex;

void main()
{
    gl_Position = MVP * vec4(inPos, 1.0);
    vTex        = inTexCoord;
}
