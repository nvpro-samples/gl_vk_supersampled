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


//-----------------------------------------------------------------------------
// Derive the Window for this sample
//-----------------------------------------------------------------------------
class MyWindow: public AppWindowCameraInertia
{
private:
    bool                dummybool;
    int                 dummy[5];
public:
    ImGuiH::Registry    guiRegistry;

    MyWindow();

    virtual bool init();
    virtual void shutdown();
    virtual void reshape(int w=0, int h=0);
    void processUI(int width, int height, double time);
    //virtual void motion(int x, int y);
    //virtual void mousewheel(short delta);
    //virtual void mouse(NVPWindow::MouseButton button, ButtonAction action, int mods, int x, int y);
    //virtual void menu(int m);
    virtual void keyboard(MyWindow::KeyCode key, ButtonAction action, int mods, int x, int y);
    virtual void keyboardchar(unsigned char key, int mods, int x, int y);
    //virtual void idle();
    virtual void display();
};

MyWindow::MyWindow() :
    AppWindowCameraInertia(
    vec3f(0.0f,1.0f,-3.0f), vec3f(0,0,0)
    //vec3f(-0.10, 0.4, -1.0), vec3f(-0.20, -0.43, 0.41)

    )
{
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
void sample_print(int level, const char * txt)
{
    //switch(level)
    //{
    //case 0:
    //case 1:
    //    break;
    //case 2:
    //    break;
    //default:
    //    break;
    //}
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
nv_helpers::Profiler  g_profiler;
int                   g_MSAA             = 8;
float                 g_Supersampling    = 1.5;
int                   g_downSamplingMode = 1;
MatrixBufferGlobal    g_globalMatrices;
bool                  g_helpText         = false;
bool                  g_bUseUI           = true;
#define               HELPDURATION         5.0

//
// Camera animation: captured using '1' in the sample. Then copy and paste...
//
struct CameraAnim {    vec3f eye, focus; float sleep; };

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
        guiRegistry.enumCombobox(COMBO_RENDERER, "Renderer", &g_curRenderer);
        ImGui::Separator();
        guiRegistry.enumCombobox(COMBO_MSAA, "MSAA", &g_MSAA);
        guiRegistry.enumCombobox(COMBO_SS, "SuperSampling", &g_Supersampling);
        guiRegistry.enumCombobox(COMBO_DS, "DownSampling Mode", &g_downSamplingMode);
        ImGui::Separator();

        ImGui::Text("('h' to toggle help)");
        if(g_helpText)
        {
            ImGui::BeginChild("Help", ImVec2(400, 110), true);
            // camera help
            //ImGui::SetNextWindowCollapsed(0);
            const char *txt = getHelpText();
            ImGui::Text(txt);
            ImGui::EndChild();
        }
        const int avg = 10;
        int avgf = g_profiler.getAveragedFrames();
        if (avgf % avg == avg - 1) {
            g_profiler.getAveragedValues("frame", g_statsCpuTime, g_statsGpuTime);
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
bool MyWindow::init()
{
	if(!AppWindowCameraInertia::init())
		return false;
  ImGui::InitGL();

	//
    // UI
    //
    auto &imgui_io = ImGui::GetIO();
    imgui_io.IniFilename = nullptr;
    guiRegistry.enumAdd(COMBO_MSAA, 1, "MSAA 1x");
    guiRegistry.enumAdd(COMBO_MSAA, 4, "MSAA 4x");
    guiRegistry.enumAdd(COMBO_MSAA, 8, "MSAA 8x");
    guiRegistry.enumAdd(COMBO_SS, 1.0f, "SS 1.0");
    guiRegistry.enumAdd(COMBO_SS, 1.5f, "SS 1.5");
    guiRegistry.enumAdd(COMBO_SS, 2.0f, "SS 2.0");
    guiRegistry.enumAdd(COMBO_DS, 0, "1 Tap");
    guiRegistry.enumAdd(COMBO_DS, 1, "5 Taps");
    guiRegistry.enumAdd(COMBO_DS, 2, "9 Taps on Alpha");
    for(int i=0; i<g_numRenderers; i++)
    {
        guiRegistry.enumAdd(COMBO_RENDERER, i, g_renderers[i]->getName());
    }

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::shutdown()
{
    g_pCurRenderer->terminateGraphics();
    ImGui::ShutdownGL();
    AppWindowCameraInertia::shutdown();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::reshape(int w, int h)
{
    if(w == 0) w = m_winSz[0];
    if(h == 0) h = m_winSz[1];
    AppWindowCameraInertia::reshape(w, h);
	if (g_pCurRenderer)
	{
		if (g_pCurRenderer->bFlipViewport())
		{
			m_projection *= nv_math::scale_mat4(nv_math::vec3(1,-1,1));
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
void MyWindow::keyboard(NVPWindow::KeyCode key, MyWindow::ButtonAction action, int mods, int x, int y)
{
    AppWindowCameraInertia::keyboard(key, action, mods, x, y);

	if(action == MyWindow::BUTTON_RELEASE)
        return;
    switch(key)
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
void MyWindow::keyboardchar( unsigned char key, int mods, int x, int y )
{
    AppWindowCameraInertia::keyboardchar(key, mods, x, y);
    switch(key)
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
void MyWindow::display()
{
    AppWindowCameraInertia::display();
   
    if(!g_pCurRenderer->valid())
    {
        glClearColor(0.5,0.0,0.0,0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        swapBuffers();
        return;
    }
    float dt = (float)m_realtime.getTiming();

    int width   = getWidth();
    int height  = getHeight();

    if (g_bUseUI) {
        processUI(width, height, dt);
    }

  //
  // render the scene
  //
  g_profiler.beginFrame();
  {
    g_pCurRenderer->display(m_camera, m_projection);
    ImDrawData *imguiDrawData;
    if (g_bUseUI) {
        ImGui::Render();
        imguiDrawData = ImGui::GetDrawData();
        ImGui::RenderDrawDataGL(imguiDrawData);
        ImGui::EndFrame();
    }
  }
  swapBuffers();
  g_profiler.endFrame();
}
//------------------------------------------------------------------------------
// Main initialization point
//------------------------------------------------------------------------------
int sample_main(int argc, const char** argv)
{
  SETLOGFILENAME();
  // you can create more than only one
    static MyWindow myWindow;
    // -------------------------------
    // Basic OpenGL settings
    //
    NVPWindow::ContextFlagsGL context(
    4,      //major;
    3,      //minor;
    false,   //core;
    1,      //MSAA;
    24,     //depth bits
    8,      //stencil bits
    false,   //debug;
    false,  //robust;
    false,  //forward;
    NULL   //share;
    );

    // -------------------------------
    // Create the window
    //
    if(!myWindow.activate(NVPWindow::WINDOW_API_OPENGL, 1280, 720, "gl_vk_supersampled", &context))
    {
        LOGE("Failed to initialize the sample\n");
        return false;
    }

    // -------------------------------
    // Parse arguments/options
    //
    for(int i=1; i<argc; i++)
    {
        if(strlen(argv[i]) <= 1)
            continue;
        switch(argv[i][1])
        {
        case 'u':
            g_bUseUI = atoi(argv[++i]);
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
	  g_pCurRenderer = g_renderers[g_curRenderer];
	  g_pCurRenderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
    g_profiler.init();
    g_profiler.setDefaultGPUInterface(g_pCurRenderer->getTimerInterface());
    g_pCurRenderer->setDownSamplingMode(g_downSamplingMode);

    // -------------------------------
    // Message pump loop
    //
    myWindow.makeContextCurrentGL();
    myWindow.swapInterval(0);
    myWindow.reshape();

    while(MyWindow::sysPollEvents(false) )
    {
        myWindow.idle();
        if (myWindow.guiRegistry.checkValueChange(COMBO_MSAA))
        {
            g_profiler.reset(1);
            g_pCurRenderer->terminateGraphics();
            g_pCurRenderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
        }
        if (myWindow.guiRegistry.checkValueChange(COMBO_SS))
        {
            g_profiler.reset(1);
			      g_pCurRenderer->updateViewport(0, 0, myWindow.getWidth(), myWindow.getHeight(), g_Supersampling);
        }
        if (myWindow.guiRegistry.checkValueChange(COMBO_DS))
        {
            g_profiler.reset(1);
            g_pCurRenderer->setDownSamplingMode( g_downSamplingMode );
        }
        if (myWindow.guiRegistry.checkValueChange(COMBO_RENDERER))
        {
            g_pCurRenderer->terminateGraphics();
            g_pCurRenderer = g_renderers[g_curRenderer];
            g_pCurRenderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
            g_profiler.reset(1);
            g_profiler.setDefaultGPUInterface(g_pCurRenderer->getTimerInterface());
            g_pCurRenderer->setDownSamplingMode(g_downSamplingMode);
            myWindow.reshape(myWindow.getWidth(), myWindow.getHeight());
        }
    }
    return true;
}
