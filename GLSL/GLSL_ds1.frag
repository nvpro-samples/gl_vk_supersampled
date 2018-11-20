#version 440 core
layout(set=0, binding=0) uniform sampler2D texImage;
//layout(set=0, binding = 0, rgba8) uniform imageBuffer im;
layout(std140, set=0, binding=1) uniform texInfo {
   vec2 texelSize;
};
layout(location=1) in  vec2 tc0;
layout(location=0,index=0) out vec4 outColor;
void main()
{
	outColor = texture(texImage, tc0.xy);
}
