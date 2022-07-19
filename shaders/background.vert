#version 330 core

const vec2 vertices[4] = vec2[4](
	vec2(-1.0, -1.0),
	vec2( 1.0, -1.0),
	vec2(-1.0,  1.0),
	vec2( 1.0,  1.0)
);

out vec2 position;

void main() {
	position = vertices[gl_VertexID];
	gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
}
