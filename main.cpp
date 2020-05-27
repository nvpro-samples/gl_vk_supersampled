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
#define DEFAULT_RENDERER 1
#include "renderer_base.h"

#include <imgui/imgui_impl_gl.h>
#include <nvgl/contextwindow_gl.hpp>


//-----------------------------------------------------------------------------
// Derive the Window for this sample
//-----------------------------------------------------------------------------
class MyWindow : public AppWindowCameraInertia
{
private:
  bool                dummybool;
  int                 dummy[5];
public:
  ImGuiH::Registry    m_guiRegistry;
  nvgl::ContextWindow     m_contextWindowGL;

  MyWindow();

  bool open(int posX, int posY, int width, int height, const char* title, const nvgl::ContextWindowCreateInfo &context);
  void processUI(int width, int height, double time);

  virtual void onWindowClose() override;
  virtual void onWindowResize(int w = 0, int h = 0) override;
  //virtual void motion(int x, int y) override;
  //virtual void mousewheel(short delta) override;
  //virtual void mouse(NVPWindow::MouseButton button, ButtonAction action, int mods, int x, int y) override;
  //virtual void menu(int m) override;
  virtual void onKeyboard(MyWindow::KeyCode key, ButtonAction action, int mods, int x, int y) override;
  virtual void onKeyboardChar(unsigned char key, int mods, int x, int y) override;
  //virtual void idle() override;
  virtual void onWindowRefresh() override;
};

MyWindow::MyWindow() :
  AppWindowCameraInertia(
    vec3f(0.0f, 1.0f, -3.0f), vec3f(0, 0, 0)
    //vec3f(-0.10, 0.4, -1.0), vec3f(-0.20, -0.43, 0.41)

  )
{
}


//-----------------------------------------------------------------------------
// Help
//-----------------------------------------------------------------------------
const char* g_sampleHelp =
"'`' or 'u' : toggle UI\n"
"space: toggles continuous rendering\n"
"'s': toggle stats\n"
;
const char* g_sampleHelpCmdLine =
"---------- Cmd-line arguments ----------\n"
"-s 0 or 1 : stats\n"
"-q <msaa> : MSAA\n"
"-r <ss_val> : supersampling (1.0,1.5,2.0)\n"
"----------------------------------------\n"
;

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
Renderer*	g_renderers[10];
int			              g_numRenderers = 0;
Renderer*		          g_pCurRenderer = NULL;
int				            g_curRenderer = DEFAULT_RENDERER;
int                   g_numCmdBuffers = 16;
double                g_statsCpuTime = 0;
double                g_statsGpuTime = 0;
nvh::Profiler         g_profiler;
int                   g_MSAA = 8;
float                 g_Supersampling = 1.5;
int                   g_downSamplingMode = 1;
MatrixBufferGlobal    g_globalMatrices;
bool                  g_helpText = false;
bool                  g_bUseUI = true;
#define               HELPDURATION         5.0

//
// Camera animation: captured using '1' in the sample. Then copy and paste...
//
struct CameraAnim { vec3f eye, focus; float sleep; };

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
#define COMBO_MSAA      0
#define COMBO_SS        1
#define COMBO_DS        2
#define COMBO_RENDERER  3
void MyWindow::processUI(int width, int height, double dt)
{
  // Update imgui configuration
  auto &imgui_io = ImGui::GetIO();
  imgui_io.DeltaTime = static_cast<float>(dt);
  imgui_io.DisplaySize = ImVec2(width, height);

  ImGui::NewFrame();
  ImGui::SetNextWindowBgAlpha(0.1);
  ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("NVIDIA " PROJECT_NAME, nullptr))
  {
    //ImGui::PushItemWidth(200);
#ifdef USEOPENGL
    ImGui::Text("gl and vk version");
#else
    ImGui::Text("vk only version");
#endif
    m_guiRegistry.enumCombobox(COMBO_RENDERER, "Renderer", &g_curRenderer);
    ImGui::Separator();
    m_guiRegistry.enumCombobox(COMBO_MSAA, "MSAA", &g_MSAA);
    m_guiRegistry.enumCombobox(COMBO_SS, "SuperSampling", &g_Supersampling);
    m_guiRegistry.enumCombobox(COMBO_DS, "DownSampling Mode", &g_downSamplingMode);
    ImGui::Separator();

    ImGui::Text("('h' to toggle help)");
    if (g_helpText)
    {
      ImGui::BeginChild("Help", ImVec2(400, 110), true);
      // camera help
      //ImGui::SetNextWindowCollapsed(0);
      const char *txt = getHelpText();
      ImGui::Text("%s",txt);
      ImGui::EndChild();
    }
    const int avg = 10;
    int avgf = g_profiler.getTotalFrames();
    if (avgf % avg == avg - 1) {
      nvh::Profiler::TimerInfo info;
      g_profiler.getTimerInfo("frame", info);
      g_statsCpuTime = info.cpu.average;
      g_statsGpuTime = info.gpu.average;
    }

    float gpuTimeF = float(g_statsGpuTime);
    float cpuTimeF = float(g_statsCpuTime);
    float maxTimeF = std::max(std::max(cpuTimeF, gpuTimeF), 0.0001f);

    ImGui::Text("Frame     [ms]: %2.1f", dt*1000.0f);
    ImGui::Text("Scene GPU [ms]: %2.3f", gpuTimeF / 1000.0f);
    ImGui::ProgressBar(gpuTimeF / maxTimeF, ImVec2(0.0f, 0.0f));
    ImGui::Text("Scene CPU [ms]: %2.3f", cpuTimeF / 1000.0f);
    ImGui::ProgressBar(cpuTimeF / maxTimeF, ImVec2(0.0f, 0.0f));
  }
  ImGui::End();
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MyWindow::open(int posX, int posY, int width, int height, const char* title, const nvgl::ContextWindowCreateInfo &context)
{
  if (!AppWindowCameraInertia::open(posX, posY, width, height, title, true))
    return false;
  m_contextWindowGL.init(&context, m_internal, title);
  ImGui::InitGL();

  //
  // UI
  //
  auto &imgui_io = ImGui::GetIO();
  imgui_io.IniFilename = nullptr;
  m_guiRegistry.enumAdd(COMBO_MSAA, 1, "MSAA 1x");
  m_guiRegistry.enumAdd(COMBO_MSAA, 4, "MSAA 4x");
  m_guiRegistry.enumAdd(COMBO_MSAA, 8, "MSAA 8x");
  m_guiRegistry.enumAdd(COMBO_SS, 1.0f, "SS 1.0");
  m_guiRegistry.enumAdd(COMBO_SS, 1.5f, "SS 1.5");
  m_guiRegistry.enumAdd(COMBO_SS, 2.0f, "SS 2.0");
  m_guiRegistry.enumAdd(COMBO_DS, 0, "1 Tap");
  m_guiRegistry.enumAdd(COMBO_DS, 1, "5 Taps");
  m_guiRegistry.enumAdd(COMBO_DS, 2, "9 Taps on Alpha");
  for (int i = 0; i < g_numRenderers; i++)
  {
    m_guiRegistry.enumAdd(COMBO_RENDERER, i, g_renderers[i]->getName());
  }

  return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::onWindowClose()
{
  g_pCurRenderer->terminateGraphics();
  ImGui::ShutdownGL();
  AppWindowCameraInertia::onWindowClose();
  m_contextWindowGL.deinit();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::onWindowResize(int w, int h)
{
  if (w == 0) w = getWidth();
  if (h == 0) h = getHeight();
  AppWindowCameraInertia::onWindowResize(w, h);
  if (g_pCurRenderer)
  {
    if (g_pCurRenderer->bFlipViewport())
    {
      m_projection *= nvmath::scale_mat4(nvmath::vec3(1, -1, 1));
    }
    //
    // update the token buffer in which the viewport setup happens for token rendering
    //
    g_pCurRenderer->updateViewport(0, 0, w, h, g_Supersampling);
  }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
#define KEYTAU 0.10f
void MyWindow::onKeyboard(NVPWindow::KeyCode key, MyWindow::ButtonAction action, int mods, int x, int y)
{
  AppWindowCameraInertia::onKeyboard(key, action, mods, x, y);

  if (action == MyWindow::BUTTON_RELEASE)
    return;
  switch (key)
  {
  case NVPWindow::KEY_F1:
    break;
    //...
  case NVPWindow::KEY_F12:
    break;
  }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::onKeyboardChar(unsigned char key, int mods, int x, int y)
{
  AppWindowCameraInertia::onKeyboardChar(key, mods, x, y);
  switch (key)
  {
  case '1':
    m_camera.print_look_at(true);
    break;
  case '2': // dumps the position and scale of current object
    break;
  case '0':
    m_bAdjustTimeScale = true;
  case 'h':
    LOGI(g_sampleHelpCmdLine);
    g_helpText ^= 1;
    break;
  case '`':
  case 'u':
    g_bUseUI ^= 1;
    break;
    break;
  }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int refreshCmdBuffers()
{
  int totalTasks = 0;
  return totalTasks;
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::onWindowRefresh()
{
  if (!g_pCurRenderer) {
    return;
  }

  AppWindowCameraInertia::onWindowRefresh();

  if (!g_pCurRenderer->valid())
  {
    glClearColor(0.5, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    m_contextWindowGL.swapBuffers();
    return;
  }
  float dt = (float)m_realtime.getFrameDT();

  int width = getWidth();
  int height = getHeight();


  //
  // render the scene
  //
  g_profiler.beginFrame();
  {
    g_pCurRenderer->display(m_camera, m_projection);
    ImDrawData *imguiDrawData;
    if (g_bUseUI) {
      processUI(width, height, dt);
      ImGui::Render();
      imguiDrawData = ImGui::GetDrawData();
      ImGui::RenderDrawDataGL(imguiDrawData);
      ImGui::EndFrame();
    }
  }
  m_contextWindowGL.swapBuffers();
  g_profiler.endFrame();
}
//------------------------------------------------------------------------------
// Main initialization point
//------------------------------------------------------------------------------
int main(int argc, char **argv)
{
  NVPSystem system(argv[0], PROJECT_NAME);

  // you can create more than only one
  static MyWindow myWindow;
  // -------------------------------
  // Basic OpenGL settings
  //
  nvgl::ContextWindowCreateInfo context(
    4,      //major;
    3,      //minor;
    false,   //core;
    1,      //MSAA;
    24,     //depth bits
    8,      //stencil bits
    false,   //debug;
    false,  //robust;
    false,  //forward;
    false,  //stereo;
    NULL   //share;
  );

  // -------------------------------
  // Create the window
  //
  if (!myWindow.open(0, 0, 1280, 720, "gl_vk_supersampled", context))
  {
    LOGE("Failed to initialize the sample\n");
    return EXIT_FAILURE;
  }

  // -------------------------------
  // Parse arguments/options
  //
  for (int i = 1; i < argc; i++)
  {
    if (strlen(argv[i]) <= 1)
      continue;
    switch (argv[i][1])
    {
    case 'u':
      g_bUseUI = atoi(argv[++i]) ? true : false;
      break;
    case 'q':
      g_MSAA = atoi(argv[++i]);
      LOGI("g_MSAA set to %d\n", g_MSAA);
      break;
    case 'r':
      g_Supersampling = atof(argv[++i]);
      LOGI("g_Supersampling set to %.2f\n", g_Supersampling);
      break;
    case 'd':
      break;
    default:
      LOGE("Wrong command-line\n");
    case 'h':
      LOGI(g_sampleHelpCmdLine);
      break;
    }
  }

  Renderer* renderer = g_renderers[g_curRenderer];
  renderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
  renderer->setDownSamplingMode(g_downSamplingMode);

  // -------------------------------
  // Message pump loop
  //
  // set last, otherwise display function is triggered whilst not all state has been initialized
  g_pCurRenderer = renderer;
  myWindow.m_contextWindowGL.makeContextCurrent();
  myWindow.m_contextWindowGL.swapInterval(0);
  myWindow.onWindowResize();

  while (myWindow.pollEvents())
  {
    myWindow.idle();
    if (myWindow.m_renderCnt > 0)
    {
      myWindow.m_renderCnt--;
      myWindow.onWindowRefresh();
    }
    if (myWindow.m_guiRegistry.checkValueChange(COMBO_MSAA))
    {
      g_profiler.reset(1);
      g_pCurRenderer->updateMSAA(g_MSAA);
    }
    if (myWindow.m_guiRegistry.checkValueChange(COMBO_SS))
    {
      g_profiler.reset(1);
      g_pCurRenderer->updateViewport(0, 0, myWindow.getWidth(), myWindow.getHeight(), g_Supersampling);
    }
    if (myWindow.m_guiRegistry.checkValueChange(COMBO_DS))
    {
      g_profiler.reset(1);
      g_pCurRenderer->setDownSamplingMode(g_downSamplingMode);
    }
    if (myWindow.m_guiRegistry.checkValueChange(COMBO_RENDERER))
    {
      g_pCurRenderer->terminateGraphics();
      g_pCurRenderer = g_renderers[g_curRenderer];
      g_pCurRenderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
      g_profiler.reset(1);
      g_pCurRenderer->setDownSamplingMode(g_downSamplingMode);
      myWindow.onWindowResize(myWindow.getWidth(), myWindow.getHeight());
    }
  }
  return EXIT_SUCCESS;
}

