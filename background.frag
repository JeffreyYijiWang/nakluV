#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 position;
void main() {
	outColor = vec4(position, 0.0, 1.0);
	//vec4(fract(gl_FragCoord.x/100), gl_FragCoord.y/400, 0.2, 1.0);
}