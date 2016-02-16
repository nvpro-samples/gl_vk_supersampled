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
layout(location=0) in    vec4 inCol;

layout(location=0,index=0) out vec4 outCol;

void main()
{
   outCol = inCol;
}
