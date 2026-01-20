#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 position;

layout(push_constant) uniform Push{
	float time;
};

void main() {
	//https://registry.khronos.org/OpenGL-Refpages/gl4/html/length.xhtml
	/*if((mod ((length(position - vec2(0.5, 0.5)) +time ) ,(0.5) ))< 0.2){
		outColor  = vec4(fract(position.x), 0.0, position.y, 1.0);
	}
	else if((mod ((length(position - vec2(0.5, 0.5)) +time ) ,(0.5) ))< 0.3){
		outColor  = vec4(0.5, cos(position.y + time *2), sin(position.x + time *2), 1.0);
	}

	else{
		outColor  = vec4(mod(length(position)+time/2, 0.1), 0.0, 0.0, 1.0);
	}*/
	
	
	//= vec4(fract(position.x + time), position.y, 0.0, 1.0);
	outColor = vec4(0.0, 0.0,  0.0, 1.0);
	//vec4(fract(gl_FragCoord.x/100), gl_FragCoord.y/400, 0.2, 1.0);
}