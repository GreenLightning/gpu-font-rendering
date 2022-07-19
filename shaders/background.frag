#version 330 core

in vec2 position;

out vec3 color;

void main() {
	float t = (position.y + 1.0) / 2.0;
	vec3 bottom = vec3(75.0, 151.0, 201.0) / 255.0;
	vec3 top = vec3(115.0, 193.0, 245.0) / 255.0;
	color = mix(bottom, top, t);
}
