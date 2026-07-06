#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform float uOpacity;

void main() {
    vec4 c = texture(uTexture, vUV);
    fragColor = vec4(c.rgb, c.a * uOpacity);
}
