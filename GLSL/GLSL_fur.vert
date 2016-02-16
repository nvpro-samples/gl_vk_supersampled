#version 440 core
#extension GL_ARB_separate_shader_objects : enable

#define DSET_GLOBAL  0
#   define BINDING_MATRIX 0
#   define BINDING_LIGHT  1

#define DSET_OBJECT  1
#   define BINDING_MATRIXOBJ   0
#   define BINDING_MATERIAL    1
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
layout(std140, set= DSET_GLOBAL , binding= BINDING_MATRIX ) uniform matrixBuffer {
   mat4 mV;
   mat4 mP;
} matrix;

layout(location=0) in  vec3 P;
layout(location=1) in  vec3 N;
layout(location=2) in  vec4 col;
layout(location=0) out vec4 outCol;

out gl_PerVertex {
    vec4  gl_Position;
};
void main()
{
   gl_Position = matrix.mP * (matrix.mV * ( vec4(P, 1.0)));
   vec3 NV = (matrix.mV * ( vec4(N, 0.0))).xyz;
   float diff = abs(NV.x);
   outCol = vec4(diff * col.rgb, col.a);
}

