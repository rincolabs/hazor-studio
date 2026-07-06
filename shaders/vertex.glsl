#version 330 core
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

uniform mat4 uTransform;

out vec2 vUV;

void main() {
    vUV = uv;
    gl_Position = uTransform * vec4(pos, 0.0, 1.0);
}
