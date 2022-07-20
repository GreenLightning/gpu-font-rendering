#version 330 core

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

in vec3 position;

void main() {
	gl_Position = projection * view * model * vec4(position, 1);
}
