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

Renderer*	g_renderers[10];
int			g_numRenderers = 0;

static Renderer*		s_pCurRenderer = NULL;
static int				s_curRenderer = DEFAULT_RENDERER;

int                 g_numCmdBuffers = 16;

//-----------------------------------------------------------------------------
// Derive the Window for this sample
//-----------------------------------------------------------------------------
class MyWindow: public WindowInertiaCamera
{
private:
public:
    MyWindow();

    virtual bool init();
    virtual void shutdown();
    virtual void reshape(int w=0, int h=0);
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
    WindowInertiaCamera(
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
#ifdef USESVCUI
    switch(level)
    {
    case 0:
    case 1:
        logMFCUI(level, txt);
        break;
    case 2:
        logMFCUI(level, txt);
        break;
    default:
        logMFCUI(level, txt);
        break;
    }
#else
#endif
}


//-----------------------------------------------------------------------------
// Help
//-----------------------------------------------------------------------------
static const char* s_sampleHelp = 
    "space: toggles continuous rendering\n"
    "'s': toggle stats\n"
;
static const char* s_sampleHelpCmdLine = 
    "---------- Cmd-line arguments ----------\n"
    "-s 0 or 1 : stats\n"
    "-q <msaa> : MSAA\n"
    "-r <ss_val> : supersampling (1.0,1.5,2.0)\n"
    "----------------------------------------\n"
;

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
#ifdef USESVCUI
IWindowFolding*   g_pTweakContainer = NULL;
#endif
nv_helpers::Profiler      g_profiler;

int         g_MSAA             = 8;
float       g_Supersampling    = 1.5;
int         g_downSamplingMode = 1;

MatrixBufferGlobal      g_globalMatrices;

//
// Camera animation: captured using '1' in the sample. Then copy and paste...
//
struct CameraAnim {    vec3f eye, focus; float sleep; };

#define        HELPDURATION         5.0
static float   s_helpText           = 0.0;

static bool     s_bStats            = true;

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MyWindow::init()
{
	if(!WindowInertiaCamera::init())
		return false;

	//
    // UI
    //
#ifdef USESVCUI
    initMFCUIBase(0, m_winSz[1]+40, m_winSz[0], 300);
#endif
    //
    // easy Toggles
    //
#ifdef USESVCUI
	class EventUI: public IEventsWnd
	{
	public:
		void Button(IWindow *pWin, int pressed)
            { reinterpret_cast<MyWindow*>(pWin->GetUserData())->m_bAdjustTimeScale = true; };
        void ScalarChanged(IControlScalar *pWin, float &v, float prev)
        {
        }
        void CheckBoxChanged(IControlScalar *pWin, bool &value, bool prev)
        {
        }
        void ComboSelectionChanged(IControlCombo *pWin, unsigned int selectedidx)
            {   
                if(!strcmp(pWin->GetID(), "RENDER"))
                {
                    MyWindow* p = reinterpret_cast<MyWindow*>(pWin->GetUserData());
                    s_pCurRenderer->terminateGraphics();
                    s_pCurRenderer = g_renderers[selectedidx];
                    s_curRenderer = selectedidx;
                    s_pCurRenderer->initGraphics(p->m_winSz[0], p->m_winSz[1], g_Supersampling, g_MSAA);
                    g_profiler.setDefaultGPUInterface(s_pCurRenderer->getTimerInterface());
                    s_pCurRenderer->setDownSamplingMode(g_downSamplingMode);
                    p->reshape(p->m_winSz[0], p->m_winSz[1]);
                }
                else if(!strcmp(pWin->GetID(), "DS"))
                {
                    g_downSamplingMode = pWin->GetItemData(selectedidx);
                    s_pCurRenderer->setDownSamplingMode( g_downSamplingMode );
                }
                else if(!strcmp(pWin->GetID(), "SS"))
                {
                    g_Supersampling = 0.1f * (float)pWin->GetItemData(selectedidx);
                    MyWindow* p = reinterpret_cast<MyWindow*>(pWin->GetUserData());
                    //
                    // update the token buffer in which the viewport setup happens for token rendering
                    //
					s_pCurRenderer->updateViewport(0, 0, p->getWidth(), p->getHeight(), g_Supersampling);
                }
                else if(!strcmp(pWin->GetID(), "MSAA"))
                {
                    g_MSAA = pWin->GetItemData(selectedidx);
                    // involves some changes at the source of initialization... re-create all
                    MyWindow* p = reinterpret_cast<MyWindow*>(pWin->GetUserData());
                    s_pCurRenderer->terminateGraphics();
                    s_pCurRenderer->initGraphics(p->m_winSz[0], p->m_winSz[1], g_Supersampling, g_MSAA);
                }
            }
	};
	static EventUI eventUI;
	//g_pWinHandler->CreateCtrlButton("TIMESCALE", "re-scale timing", g_pToggleContainer)
	//	->SetUserData(this)
	//	->Register(&eventUI);

    g_pToggleContainer->UnFold(false);

    IControlCombo* pCombo = g_pWinHandler->CreateCtrlCombo("RENDER", "Renderer", g_pToggleContainer);
	pCombo->SetUserData(this)->Register(&eventUI);
    for(int i=0; i<g_numRenderers; i++)
    {
        pCombo->AddItem(g_renderers[i]->getName(), i);
    }
    pCombo->SetSelectedByIndex(DEFAULT_RENDERER);

    pCombo = g_pWinHandler->CreateCtrlCombo("MSAA", "MSAA", g_pToggleContainer);
    pCombo->AddItem("MSAA 1x", 1);
    pCombo->AddItem("MSAA 4x", 4);
    pCombo->AddItem("MSAA 8x", 8);
	pCombo->SetUserData(this)->Register(&eventUI);
    pCombo->SetSelectedByData(g_MSAA);

    pCombo = g_pWinHandler->CreateCtrlCombo("SS", "Supersampling", g_pToggleContainer);
    pCombo->AddItem("SS 1.0", 10);
    pCombo->AddItem("SS 1.5", 15);
    pCombo->AddItem("SS 2.0", 20);
	pCombo->SetUserData(this)->Register(&eventUI);
    pCombo->SetSelectedByData(g_Supersampling*10);
    pCombo->PeekMyself();

    pCombo = g_pWinHandler->CreateCtrlCombo("DS", "DownSampling mode", g_pToggleContainer);
	pCombo->SetUserData(this)->Register(&eventUI);
    pCombo->AddItem("1 taps", 0);//NVFBOBox::DS1);
    pCombo->AddItem("5 taps", 1);//NVFBOBox::DS2);
    pCombo->AddItem("9 taps on alpha", 2);//NVFBOBox::DS3);
    pCombo->SetSelectedByData(g_downSamplingMode);
    g_pToggleContainer->UnFold();

#endif
    addToggleKeyToMFCUI(' ', &m_realtime.bNonStopRendering, "space: toggles continuous rendering\n");
    addToggleKeyToMFCUI('s', &s_bStats, "'s': toggle stats\n");

    return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::shutdown()
{
#ifdef USESVCUI
    shutdownMFCUI();
#endif
    s_pCurRenderer->terminateGraphics();
	WindowInertiaCamera::shutdown();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MyWindow::reshape(int w, int h)
{
    if(w == 0) w = m_winSz[0];
    if(h == 0) h = m_winSz[1];
    WindowInertiaCamera::reshape(w, h);
	if (s_pCurRenderer)
	{
		if (s_pCurRenderer->bFlipViewport())
		{
			m_projection *= nv_math::scale_mat4(nv_math::vec3(1,-1,1));
		}
		//
		// update the token buffer in which the viewport setup happens for token rendering
		//
		s_pCurRenderer->updateViewport(0, 0, w, h, g_Supersampling);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
#define KEYTAU 0.10f
void MyWindow::keyboard(NVPWindow::KeyCode key, MyWindow::ButtonAction action, int mods, int x, int y)
{
	WindowInertiaCamera::keyboard(key, action, mods, x, y);

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
    WindowInertiaCamera::keyboardchar(key, mods, x, y);
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
        LOGI(s_sampleHelpCmdLine);
        s_helpText = HELPDURATION;
    break;
    }
#ifdef USESVCUI
    flushMFCUIToggle(key);
#endif
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
  WindowInertiaCamera::display();
  if(!s_pCurRenderer->valid())
  {
      glClearColor(0.5,0.0,0.0,0.0);
      glClear(GL_COLOR_BUFFER_BIT);
      swapBuffers();
      return;
  }
  float dt = (float)m_realtime.getTiming();
  //
  // render the scene
  //
  std::string stats;
  static std::string hudStats = "...";
  {
    nv_helpers::Profiler::FrameHelper helper(g_profiler,sysGetTime(), 2.0, stats);
    PROFILE_SECTION("display");

    s_pCurRenderer->display(m_camera, m_projection);
    //
    // additional HUD stuff
    //
    WindowInertiaCamera::beginDisplayHUD();
    s_helpText -= dt;
    m_oglTextBig.drawString(5, 5, "('h' for help)", 1, vec4f(0.8,0.8,1.0,0.5f).vec_array);
    float h = 30;
    if(s_bStats)
        h += m_oglTextBig.drawString(5, m_winSz[1]-h, hudStats.c_str(), 0, vec4f(0.8,0.8,1.0,0.5).vec_array);
    if(s_helpText > 0)
    {
        // camera help
        const char *txt = getHelpText();
        h += m_oglTextBig.drawString(5, m_winSz[1]-h, txt, 0, vec4f(0.8,0.8,1.0,s_helpText/HELPDURATION).vec_array);
        h += m_oglTextBig.drawString(5, m_winSz[1]-h, s_sampleHelp, 0, vec4f(0.8,0.8,1.0,s_helpText/HELPDURATION).vec_array);
    }
    WindowInertiaCamera::endDisplayHUD();
    {
      //PROFILE_SECTION("SwapBuffers");
      swapBuffers();
    }
  } //PROFILE_SECTION("display");
  //
  // Stats
  //
  if (s_bStats && (!stats.empty()))
  {
    hudStats = stats; // make a copy for the hud display
  }

}
//------------------------------------------------------------------------------
// Main initialization point
//------------------------------------------------------------------------------
int sample_main(int argc, const char** argv)
{
    // you can create more than only one
    static MyWindow myWindow;
    // -------------------------------
    // Basic OpenGL settings
    //
    NVPWindow::ContextFlags context(
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
    if(!myWindow.create("gl_vk_supersampled", &context, 1280,720))
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
        case 's':
            s_bStats = atoi(argv[++i]) ? true : false;
            LOGI("s_bStats set to %s\n", s_bStats ? "true":"false");
            break;
        case 'q':
            g_MSAA = atoi(argv[++i]);
            #ifdef USESVCUI
            if(g_pWinHandler) g_pWinHandler->GetCombo("MSAA")->SetSelectedByData(g_MSAA);
            #endif
            LOGI("g_MSAA set to %d\n", g_MSAA);
            break;
        case 'r':
            g_Supersampling = atof(argv[++i]);
            #ifdef USESVCUI
            if(g_pWinHandler) g_pWinHandler->GetCombo("SS")->SetSelectedByData(g_Supersampling*10);
            #endif
            LOGI("g_Supersampling set to %.2f\n", g_Supersampling);
            break;
        case 'd':
            #ifdef USESVCUI
            if(g_pTweakContainer) g_pTweakContainer->SetVisible(atoi(argv[++i]) ? 1 : 0);
            #endif
            break;
        default:
            LOGE("Wrong command-line\n");
        case 'h':
            LOGI(s_sampleHelpCmdLine);
            break;
        }
    }
	s_pCurRenderer = g_renderers[s_curRenderer];
	s_pCurRenderer->initGraphics(myWindow.getWidth(), myWindow.getHeight(), g_Supersampling, g_MSAA);
    g_profiler.init();
    g_profiler.setDefaultGPUInterface(s_pCurRenderer->getTimerInterface());
    s_pCurRenderer->setDownSamplingMode(g_downSamplingMode);

    // -------------------------------
    // Message pump loop
    //
    myWindow.makeContextCurrent();
    myWindow.swapInterval(0);
    myWindow.reshape();

    while(MyWindow::sysPollEvents(false) )
    {
        myWindow.idle();
    }
    return true;
}
