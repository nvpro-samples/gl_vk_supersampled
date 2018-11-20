#version 440 core
layout(location=0) in  vec3 pos;
layout(location=1) in  vec2 tc0;
layout(location=1) out vec2 out_tc0;
out gl_PerVertex {
    vec4  gl_Position;
};
void main(void)
{
	gl_Position = vec4(pos,1);
	out_tc0 = tc0;
}
