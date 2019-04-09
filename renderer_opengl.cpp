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

    Note: this section of the code is showing a basic implementation of
    Command-lists using a binary format called bk3d.
    This format has no value for command-list. However you will see that
    it allows to use pre-baked art asset without too parsing: all is
    available from structures in the file (after pointer resolution)

*/ //--------------------------------------------------------------------
#define EXTERNSVCUI
#define WINDOWINERTIACAMERA_EXTERN
#include "renderer_base.h"
#include "NVFBOBox.h"
#include <nvgl/profiler_gl.hpp>

namespace glstandard
{

  //-----------------------------------------------------------------------------
  // Shaders
  //-----------------------------------------------------------------------------
  static const char *g_glslv_fur =
    "#version 430\n"
    "#extension GL_ARB_separate_shader_objects : enable\n"
    "#extension GL_NV_command_list : enable\n"
    "layout(std140,commandBindableNV,binding=" TOSTR(UBO_MATRIX) ") uniform matrixBuffer {\n"
    "   uniform mat4 mV;\n"
    "   uniform mat4 mP;\n"
    "} matrix;\n"
    "layout(location=0) in  vec3 P;\n"
    "layout(location=1) in  vec3 N;\n"
    "layout(location=2) in  vec4 col;\n"
    "layout(location=0) out vec4 outCol;\n"

    "out gl_PerVertex {\n"
    "    vec4  gl_Position;\n"
    "};\n"
    "void main() {\n"
    "   gl_Position = matrix.mP * (matrix.mV * ( vec4(P, 1.0)));\n"
    "   vec3 NV = (matrix.mV * ( vec4(N, 0.0))).xyz;\n"
    "   float diff = abs(NV.x);\n"
    "   outCol = vec4(diff * col.rgb, col.a);\n"
    "}\n"
    ;
  static const char *g_glslf_fur =
    "#version 430\n"
    "#extension GL_ARB_separate_shader_objects : enable\n"
    "#extension GL_NV_command_list : enable\n"
    "layout(location=0) in  vec4 inCol;\n"
    "layout(location=0) out vec4 outCol;\n"
    "void main() {\n"
    "   outCol = inCol;\n"
    "}\n"
    ;
  GLSLShader	s_shaderfur;

  struct BO {
    GLuint      Id;
    GLuint      Sz;
  };

  BO g_uboMatrix = { 0,0 };


  static GLuint      s_vbofur;
  static GLuint      s_vbofurSz;
  static GLuint      s_nElmts;

  static GLuint      s_vao = 0;

  //------------------------------------------------------------------------------
  // Renderer: can be OpenGL or other
  //------------------------------------------------------------------------------
  class RendererStandard : public Renderer
  {
  private:
    bool                             m_bValid;

    bool initResourcesfur();
    bool deleteResourcesfur();

    NVFBOBox::DownSamplingTechnique downsamplingMode;
    NVFBOBox    m_fboBox;
    int         m_winSize[2];

    nvgl::ProfilerGL m_profilerGL;
  public:
    RendererStandard() {
      m_bValid = false;
      g_renderers[g_numRenderers++] = this;
    }
    virtual ~RendererStandard() {}
    virtual const char *getName() { return "Naive Standard VBO"; }
    virtual bool valid() { return m_bValid; };
    virtual bool initGraphics(int w, int h, float SSScale, int MSAA);
    virtual bool terminateGraphics();

    virtual void display(const InertiaCamera& camera, const mat4f& projection);

    virtual void updateMSAA(int MSAA);

    virtual void updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor);

    virtual void setDownSamplingMode(int i) { downsamplingMode = (NVFBOBox::DownSamplingTechnique)i; }
  };

  RendererStandard s_renderer;

  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------

  bool RendererStandard::initResourcesfur()
  {
    glCreateBuffers(1, &s_vbofur);
    std::vector<Vertex> data;

    buildFur(data);

    s_nElmts = data.size();
    s_vbofurSz = data.size() * sizeof(Vertex);
    glNamedBufferData(s_vbofur, s_vbofurSz, &(data[0]), GL_STATIC_DRAW);
    return true;
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  bool RendererStandard::deleteResourcesfur()
  {
    glDeleteBuffers(1, &s_vbofur);
    return true;
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  void RendererStandard::display(const InertiaCamera& camera, const mat4f& projection)
  {
    const nvgl::ProfilerGL::Section profile(m_profilerGL, "frame");

    // bind the FBO
    m_fboBox.Activate();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    //glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    GLuint fbo = m_fboBox.GetFBO();

    //
    // Update what is inside buffers
    //
    g_globalMatrices.mP = projection;
    g_globalMatrices.mV = camera.m4_view;
    glNamedBufferSubData(g_uboMatrix.Id, 0, sizeof(g_globalMatrices), &g_globalMatrices);
    // ------------------------------------------------------------------------------------------
    // Case of regular rendering
    //
    s_shaderfur.bindShader();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    // --------------------------------------------------------------------------------------
  // Using regular VBO
  //
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_MATRIX, g_uboMatrix.Id);
    glBindVertexBuffer(0, s_vbofur, 0, sizeof(Vertex));
    glBindVertexBuffer(1, s_vbofur, 0, sizeof(Vertex));
    glBindVertexBuffer(2, s_vbofur, 0, sizeof(Vertex));
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE, sizeof(vec3f));
    glVertexAttribFormat(2, 4, GL_FLOAT, GL_FALSE, 2 * sizeof(vec3f));
    //
    // Draw!
    //
    glDrawArrays(GL_TRIANGLES, 0, s_nElmts);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    //
    // Blit to backbuffer
    //
    m_fboBox.Deactivate();
    m_fboBox.Draw(downsamplingMode, 0, 0, m_winSize[0], m_winSize[1], NULL);
  }

  void RendererStandard::updateMSAA(int MSAA)
  {
    m_fboBox.resize(0, 0, -1, MSAA, -1);
    m_fboBox.MakeResourcesResident();
  }
  //------------------------------------------------------------------------------
  // this is an example of creating a piece of token buffer that would be put
  // as a header before every single glDrawCommandsStatesAddressNV so that
  // proper view setup (viewport) get properly done without relying on any
  // typical OpenGL command.
  // this approach is good avoid messing with OpenGL state machine and later could
  // prevent extra driver validation
  //------------------------------------------------------------------------------
  void RendererStandard::updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor)
  {
    m_winSize[0] = width;
    m_winSize[1] = height;
    m_fboBox.resize(width, height, SSFactor);
    m_fboBox.MakeResourcesResident();
    width = m_fboBox.getBufferWidth();
    height = m_fboBox.getBufferHeight();
    glLineWidth(m_fboBox.getSSFactor());
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  bool RendererStandard::initGraphics(int w, int h, float SSScale, int MSAA)
  {
    //
    // some offscreen buffer
    //
    m_fboBox.Initialize(w, h, SSScale, MSAA, 0);
    m_fboBox.MakeResourcesResident();
    //
    // Shader compilation
    //
    if (!s_shaderfur.addVertexShaderFromString(g_glslv_fur))
      return false;
    if (!s_shaderfur.addFragmentShaderFromString(g_glslf_fur))
      return false;
    if (!s_shaderfur.link())
      return false;

    //
    // Create some UBO for later share their 64 bits
    //
    glCreateBuffers(1, &g_uboMatrix.Id);
    g_uboMatrix.Sz = sizeof(MatrixBufferGlobal);
    glNamedBufferData(g_uboMatrix.Id, g_uboMatrix.Sz, &g_globalMatrices, GL_STREAM_DRAW);
    //
    // Misc OGL setup
    //
    glClearColor(0.0f, 0.1f, 0.15f, 1.0f);
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    //
    // fur
    //
    if (!initResourcesfur())
      return false;
    
    m_profilerGL = nvgl::ProfilerGL(&g_profiler);
    m_profilerGL.init();

    LOGOK("Initialized renderer %s\n", getName());
    m_bValid = true;
    return true;
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  bool RendererStandard::terminateGraphics()
  {
    m_fboBox.Finish();
    deleteResourcesfur();
    glDeleteVertexArrays(1, &s_vao);
    s_vao = 0;
    glDeleteBuffers(1, &g_uboMatrix.Id);
    g_uboMatrix.Id = 0;
    s_shaderfur.cleanup();
    m_profilerGL.deinit();
    m_bValid = false;
    return true;
  }
} //glstandard
