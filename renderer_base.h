/*-----------------------------------------------------------------------
    Copyright (c) 2016, NVIDIA. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Neither the name of its contributors may be used to endorse 
       or promote products derived from this software without specific
       prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
    PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
    CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
    EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
    PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
    OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    feedback to tlorach@nvidia.com (Tristan Lorach)
*/ //--------------------------------------------------------------------
#define USE_NVFBOBOX
#define MAXCMDBUFFERS 100

#include <assert.h>
#include "nvpwindow.hpp"

#include "nvmath/nvmath.h"
#include "nvmath/nvmath_glsltypes.h"
using namespace nvmath;

#include "GLSLShader.h"
#include "nvh/profiler.hpp"

#include "nvh/appwindowcamerainertia.hpp"

#ifdef USESVCUI
#include "svcmfcui.h"
#   define  LOGFLUSH()  { g_pWinHandler->HandleMessageLoop_OnePass(); }
#else
#   define  LOGFLUSH()
#endif

#ifndef NOGZLIB
#   include "zlib.h"
#endif


//
// For the case where we work with Descriptor Sets (Vulkan)
//
#define DSET_GLOBAL  0
#   define BINDING_MATRIX 0
#   define BINDING_LIGHT  1

#define DSET_OBJECT  1
#   define BINDING_MATRIXOBJ   0
#   define BINDING_MATERIAL    1

#define DSET_TOTALAMOUNT 2
//
// For the case where we just assign UBO bindings (cmd-list)
//
#define UBO_MATRIX      0
#define UBO_MATRIXOBJ   1
#define UBO_MATERIAL    2
#define UBO_LIGHT       3
#define NUM_UBOS        4

#define TOSTR_(x) #x
#define TOSTR(x) TOSTR_(x)

//
// Let's assume we would put any matrix that don't get impacted by the local object transformation
//
NV_ALIGN(256, struct MatrixBufferGlobal
{ 
  mat4f mV; 
  mat4f mP;
} );

//
// Externs
//
extern nvh::Profiler  g_profiler;

extern bool         g_bUseCallCommandListNV;
extern bool         g_bUseBindless;
extern int          g_TokenBufferGrouping;
extern GLuint       g_MaxBOSz;

extern MatrixBufferGlobal g_globalMatrices;

struct Vertex {
    vec3f pos;
    vec3f n;
    vec4f col;
};


//------------------------------------------------------------------------------
// Renderer: can be OpenGL or other
//------------------------------------------------------------------------------
class Renderer
{
public:
    Renderer() {}
    virtual ~Renderer() {}
    virtual const char *getName() = 0;
    virtual bool valid() = 0;
    virtual bool initGraphics(int w, int h, float SSScale, int MSAA) = 0;
    virtual bool terminateGraphics() = 0;
    virtual void waitForGPUIdle() {}

    virtual void display(const InertiaCamera& camera, const mat4f& projection) = 0;

    virtual void updateMSAA(int MSAA) = 0;
    virtual void updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor) = 0;

    virtual bool bFlipViewport() { return false; }

    virtual void setDownSamplingMode(int i) = 0;
};
extern Renderer*    g_renderers[10];
extern int            g_numRenderers;

inline void buildStrand(std::vector<Vertex> &data, vec3f pos, vec3f dvec, vec3f nvec, vec2f &sz, int nsteps ,float curve, vec3 &color)
{
    for(int i=0; i<=nsteps; i++)
    {
        float alpha = 1.0 - ((float)i/(float)nsteps);
        vec3f tvec = cross(dvec, nvec);
        tvec *= sz.x;
        sz.x *= 0.8;
        vec3f pos2 = pos + (dvec * sz.y);
        quatf q(nvec, curve*nv_pi/180.0);
        dvec.rotateBy(q);
        vec3f tvec2 = cross(dvec, nvec);
        tvec2 *= sz.x;
        Vertex vtx[4];
        vtx[0].pos = pos + tvec;
        vtx[0].n   = nvec;
        vtx[0].col = vec4(color,alpha);
        vtx[1].pos = pos - tvec;
        vtx[1].n   = nvec;
        vtx[1].col = vec4(color,alpha);
        if(i == nsteps)
        {
            vtx[2].pos = pos + dvec * sz.y;
            vtx[2].n   = nvec;
            vtx[2].col = vec4(color,alpha);
            vtx[3].pos = pos + dvec * sz.y;
            vtx[3].n   = nvec;
            vtx[3].col = vec4(color,alpha);
        } else {
            vtx[2].pos = pos2 + tvec2;
            vtx[2].n   = nvec;
            vtx[2].col = vec4(color,alpha);
            vtx[3].pos = pos2 - tvec2;
            vtx[3].n   = nvec;
            vtx[3].col = vec4(color,alpha);
        }
        data.push_back(vtx[0]);
        data.push_back(vtx[1]);
        data.push_back(vtx[2]);
        data.push_back(vtx[1]);
        data.push_back(vtx[3]);
        data.push_back(vtx[2]);

        pos = pos2;
    }
}
inline void buildFur(std::vector<Vertex> &data)
{
    vec3f pos;
    vec3f dvec;
    vec3f nvec;
    float nsteps = 20;
    srand(10);
    for(int i=0; i<10000; i++)
    {
        float alpha = nv_two_pi * float(rand() & 0x1FFF)/(float)0x1FFF;
        float beta = nv_two_pi * float(rand() & 0x1FFF)/(float)0x1FFF;
        float curve = 10.0*(float(rand() & 0x1F)/(float)0x1F) - 5.0;
        float length = 2.0*(float(rand() & 0x1F)/(float)0x1F);
        float thick = 0.03*(float(rand() & 0x1F)/(float)0x1F);
        vec3  color(0.5 + 0.5*float(rand() & 0x1F)/(float)0x1F,
                    0.5 + 0.5*float(rand() & 0x1F)/(float)0x1F,
                    0.5 + 0.5*float(rand() & 0x1F)/(float)0x1F);
        float r = cos(beta);
        pos.x = r * cos(alpha);
        pos.z = r * sin(alpha);
        pos.y = sin(beta);
        dvec = pos;
        pos *= 0.1;
        nvec = normalize(cross(dvec, vec3f(0,1,0)));
        vec2 sz(thick, length / nsteps);
        buildStrand(data, pos, dvec, nvec, sz, nsteps ,curve, color);
    }
}
