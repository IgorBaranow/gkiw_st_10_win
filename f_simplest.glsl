#version 330 core
in  vec2 vTex;

uniform sampler2D tex;      // bound to unit 0
uniform int  useTexture;    // 1 = sample texture, 0 = solid colour
uniform vec3 baseColor;     // material Kd

out vec4 fragColor;

void main()
{
    vec3 col = (useTexture == 1)
             ? texture(tex, vTex).rgb
             : baseColor;

    fragColor = vec4(col, 1.0);
}
