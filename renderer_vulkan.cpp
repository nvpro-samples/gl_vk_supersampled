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

    Note: this section of the code is showing a basic implementation of
    Command-lists using a binary format called bk3d.
    This format has no value for command-list. However you will see that
    it allows to use pre-baked art asset without too parsing: all is
    available from structures in the file (after pointer resolution)

*/ //--------------------------------------------------------------------
#define USE_vkCmdBindVertexBuffers_Offset
#ifdef USE_vkCmdBindVertexBuffers_Offset
#   define VBOIDX userPtr
#   define EBOIDX userPtr
#endif

#define EXTERNSVCUI
#define WINDOWINERTIACAMERA_EXTERN
#include "renderer_base.h"

#include "NVK.h"
#include "NVFBOBoxVK.h"

//
// NVIDIA Extension to merge Vulkan with OpenGL
//
//typedef GLVULKANPROCNV (GLAPIENTRY* PFNGLGETVKINSTANCEPROCADDRNVPROC) (const GLchar *name);
typedef void (GLAPIENTRY* PFNGLWAITVKSEMAPHORENVPROC) (GLuint64 vkSemaphore);
typedef void (GLAPIENTRY* PFNGLSIGNALVKSEMAPHORENVPROC) (GLuint64 vkSemaphore);
typedef void (GLAPIENTRY* PFNGLSIGNALVKFENCENVPROC) (GLuint64 vkFence);
typedef void (GLAPIENTRY* PFNGLDRAWVKIMAGENVPROC) (GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1);
//PFNGLGETVKINSTANCEPROCADDRNVPROC    glGetVkInstanceProcAddrNV;
PFNGLWAITVKSEMAPHORENVPROC          glWaitVkSemaphoreNV;
PFNGLSIGNALVKSEMAPHORENVPROC        glSignalVkSemaphoreNV;
PFNGLSIGNALVKFENCENVPROC            glSignalVkFenceNV;
PFNGLDRAWVKIMAGENVPROC              glDrawVkImageNV;

///////////////////////////////////////////////////////////////////////////////
// renderer
//
namespace vk
{
static NVK  nvk;

//------------------------------------------------------------------------------
// Buffer Object
//------------------------------------------------------------------------------
struct BufO {
    VkBuffer        buffer;
    VkBufferView    bufferView;
    VkDeviceMemory  bufferMem;
    size_t          Sz;
    void release() { 
	    if(buffer)       nvk.vkDestroyBuffer(buffer);
	    if(bufferView)   nvk.vkDestroyBufferView(bufferView);
	    if(bufferMem)    
            nvk.vkFreeMemory(bufferMem);
        memset(this, 0, sizeof(BufO));
    }
};

//------------------------------------------------------------------------------
// Renderer: can be OpenGL or other
//------------------------------------------------------------------------------
class RendererVk : public Renderer, public nv_helpers::Profiler::GPUInterface
{
private:
    bool                        m_bValid;
    //
    // Vulkan stuff
    //
    VkCommandPool               m_cmdPool;

    VkDescriptorPool            m_descPool;

    VkDescriptorSetLayout       m_descriptorSetLayouts[DSET_TOTALAMOUNT]; // general layout and objects layout
    VkDescriptorSet             m_descriptorSetGlobal; // descriptor set for general part

    VkPipelineLayout            m_pipelineLayout;

    VkPipeline                  m_pipelinefur;

    NVFBOBoxVK                  m_nvFBOBox; // the super-sampled render-target
    NVFBOBoxVK::DownSamplingTechnique downsamplingMode;

    VkCommandBuffer             m_cmdScene[2];
    VkFence                     m_sceneFence[2];
    int                         m_cmdSceneIdx;

    // Used for merging Vulkan image to OpenGL backbuffer 
    VkSemaphore                 m_semOpenGLReadDone;
    VkSemaphore                 m_semVKRenderingDone;


    GLuint                      m_nElmts;
    BufO                        m_furBuffer;
    BufO                        m_matrix;

    VkQueryPool                 m_timePool;
    uint64_t                    m_timeStampFrequency;
    VkBool32                    m_timeStampsSupported;

public:
	RendererVk() {
        m_bValid = false;
        m_timeStampsSupported = false;
		g_renderers[g_numRenderers++] = this;
        m_cmdSceneIdx = 0;
	}
	virtual ~RendererVk() {}

	virtual const char *getName() { return "Vulkan"; }
    virtual bool valid()          { return m_bValid; };
	virtual bool initGraphics(int w, int h, float SSScale, int MSAA);
	virtual bool terminateGraphics();
    virtual void waitForGPUIdle();

    virtual void display(const InertiaCamera& camera, const mat4f& projection);

    virtual void updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor);

    virtual bool bFlipViewport() { return true; }

    virtual void setDownSamplingMode(int i) { 
        downsamplingMode = (NVFBOBoxVK::DownSamplingTechnique)i;
    }
    //
    // Timer methods
    //
    virtual nv_helpers::Profiler::GPUInterface* getTimerInterface();
    virtual void        initTimers(unsigned int n);
    virtual void        deinitTimers();
    //
    // from nv_helpers::Profiler::GPUInterface
    //
    virtual const char* TimerTypeName();
    virtual bool        TimerAvailable(nv_helpers::Profiler::TimerIdx idx);
    virtual void        TimerSetup(nv_helpers::Profiler::TimerIdx idx);
    virtual unsigned long long TimerResult(nv_helpers::Profiler::TimerIdx idxBegin, nv_helpers::Profiler::TimerIdx idxEnd);
    virtual void        TimerEnsureSize(unsigned int slots);
    virtual void        TimerFlush();

};

RendererVk s_renderer;

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// TIMER method for GPUInterface
//------------------------------------------------------------------------------
nv_helpers::Profiler::GPUInterface* RendererVk::getTimerInterface()
{
    if (m_timeStampsSupported) 
        return this;
    return 0;
}
//------------------------------------------------------------------------------
// method for GPUInterface
//------------------------------------------------------------------------------
const char* RendererVk::TimerTypeName()
{
    return "VK ";
}

//------------------------------------------------------------------------------
// method for GPUInterface
//------------------------------------------------------------------------------
bool RendererVk::TimerAvailable(nv_helpers::Profiler::TimerIdx idx)
{
    return true;
}

//------------------------------------------------------------------------------
// method for GPUInterface
//------------------------------------------------------------------------------
void RendererVk::TimerSetup(nv_helpers::Profiler::TimerIdx idx)
{
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if(m_bValid == false) return;

    ::VkCommandBuffer timerCmd;
    timerCmd = nvk.vkAllocateCommandBuffer(m_cmdPool, true);

    nvk.vkBeginCommandBuffer(timerCmd, true);

    vkCmdResetQueryPool(timerCmd, m_timePool, idx, 1); // not ideal to do this per query
    vkCmdWriteTimestamp(timerCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timePool, idx);

    nvk.vkEndCommandBuffer(timerCmd);
    
    nvk.vkQueueSubmit(NVK::VkSubmitInfo(0, NULL, 1, &timerCmd, 0, NULL), NULL);
}

//------------------------------------------------------------------------------
// method for GPUInterface
//------------------------------------------------------------------------------
unsigned long long RendererVk::TimerResult(nv_helpers::Profiler::TimerIdx idxBegin, nv_helpers::Profiler::TimerIdx idxEnd)
{
    if(m_bValid == false) return 0;
    uint64_t end = 0;
    uint64_t begin = 0;
    vkGetQueryPoolResults(nvk.m_device, m_timePool, idxEnd,   1, sizeof(uint64_t), &end,   0, VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT);
    vkGetQueryPoolResults(nvk.m_device, m_timePool, idxBegin, 1, sizeof(uint64_t), &begin, 0, VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT);

    return uint64_t(double(end - begin) * m_timeStampFrequency);
}

//------------------------------------------------------------------------------
// method for GPUInterface
//------------------------------------------------------------------------------
void RendererVk::TimerEnsureSize(unsigned int slots)
{
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RendererVk::initTimers(unsigned int n)
{
    if(m_bValid == false) return;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;

    m_timeStampsSupported = nvk.m_gpu.queueProperties[0].timestampValidBits;

    if (m_timeStampsSupported)
    {
      m_timeStampFrequency = nvk.m_gpu.properties.limits.timestampPeriod;
    }
    else
    {
      return;
    }

    if (m_timePool){
      deinitTimers();
    }
    
    VkQueryPoolCreateInfo queryInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    queryInfo.queryCount = n;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    result = vkCreateQueryPool(nvk.m_device, &queryInfo, NULL, &m_timePool);
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RendererVk::deinitTimers()
{
    if (!m_timeStampsSupported) return;
    vkDestroyQueryPool(nvk.m_device, m_timePool, NULL);
    m_timePool = NULL;
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RendererVk::TimerFlush()
{
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool load_binary(std::string &name, std::string &data)
{
    FILE *fd = NULL;
    std::vector<std::string> paths;
    paths.push_back(name);
    paths.push_back(std::string(PROJECT_RELDIRECTORY) + name);
    paths.push_back(std::string(PROJECT_ABSDIRECTORY) + name);
    for(int i=0; i<paths.size(); i++)
    {
        if(fd = fopen(paths[i].c_str(), "rb") )
        {
            break;
        }
    }
    if(fd == NULL)
    {
        //LOGE("error in loading %s\n", name.c_str());
        return false;
    }
    fseek(fd, 0, SEEK_END);
	long realsize = ftell(fd);
    char *p = new char[realsize];
    fseek(fd, 0, SEEK_SET);
    fread(p, 1, realsize, fd);
    data = std::string(p, realsize);
    delete [] p;
    return true;
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool RendererVk::initGraphics(int w, int h, float SSScale, int MSAA)
{
    bool bRes;
    if(m_bValid)
        return true;
    m_bValid = true;
    //--------------------------------------------------------------------------
    // Create the Vulkan device
    //
    bRes = nvk.CreateDevice();
    assert(bRes);
    //--------------------------------------------------------------------------
    // Get the OpenGL extension for merging VULKAN with OpenGL
    //
#ifdef WIN32
    //glGetVkInstanceProcAddrNV = (PFNGLGETVKINSTANCEPROCADDRNVPROC)GetProcAddress(hlib, "glGetVkInstanceProcAddrNV");
    glWaitVkSemaphoreNV = (PFNGLWAITVKSEMAPHORENVPROC)NVPWindow::sysGetProcAddress("glWaitVkSemaphoreNV");
    glSignalVkSemaphoreNV = (PFNGLSIGNALVKSEMAPHORENVPROC)NVPWindow::sysGetProcAddress("glSignalVkSemaphoreNV");
    glSignalVkFenceNV = (PFNGLSIGNALVKFENCENVPROC)NVPWindow::sysGetProcAddress("glSignalVkFenceNV");
    glDrawVkImageNV = (PFNGLDRAWVKIMAGENVPROC)NVPWindow::sysGetProcAddress("glDrawVkImageNV");
    if(glDrawVkImageNV == NULL)
    {
        LOGE("couldn't find entry points to blit Vulkan to OpenGL back-buffer (glDrawVkImageNV...)");
        nvk.DestroyDevice();
        m_bValid = false;
        return false;
    }
#else //ellif (__linux__)
    // TODO
#endif
    VkSemaphoreCreateInfo semCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    m_semOpenGLReadDone = nvk.vkCreateSemaphore();
    m_semVKRenderingDone = nvk.vkCreateSemaphore();
    //--------------------------------------------------------------------------
    // Command pool for the main thread
    //
    VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolInfo.queueFamilyIndex = 0;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CHECK(vkCreateCommandPool(nvk.m_device, &cmdPoolInfo, NULL, &m_cmdPool));

    //--------------------------------------------------------------------------
    // TODO
    initTimers(nv_helpers::Profiler::START_TIMERS);

    //--------------------------------------------------------------------------
    // Load SpirV shaders
    //
    std::string spv_GLSL_fur_frag;
    std::string spv_GLSL_fur_vert;
    bRes = true;
    if(!load_binary(std::string("GLSL/GLSL_fur_frag.spv"), spv_GLSL_fur_frag)) bRes = false;
    if(!load_binary(std::string("GLSL/GLSL_fur_vert.spv"), spv_GLSL_fur_vert)) bRes = false;
    if (bRes == false)
    {
        LOGE("Failed loading some SPV files\n");
        nvk.DestroyDevice();
        m_bValid = false;
        return false;
    }

    //--------------------------------------------------------------------------
    // Buffers for general UBOs
    //
    m_matrix.Sz = sizeof(vec4f)*4*2;
    m_matrix.buffer        = nvk.createAndFillBuffer(m_cmdPool, m_matrix.Sz, NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_matrix.bufferMem);
    m_matrix.bufferView    = nvk.vkCreateBufferView(m_matrix.buffer, VK_FORMAT_UNDEFINED, m_matrix.Sz);
    //--------------------------------------------------------------------------
    // descriptor set
    //
    // descriptor layout for general things (projection matrix; view matrix...)
    m_descriptorSetLayouts[DSET_GLOBAL] = nvk.vkCreateDescriptorSetLayout(
        NVK::VkDescriptorSetLayoutCreateInfo(NVK::VkDescriptorSetLayoutBinding
         (BINDING_MATRIX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT) // BINDING_MATRIX
         //(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT) // BINDING_LIGHT
    ) );
    // descriptor layout for object level: buffers related to the object (objec-matrix; material colors...)
    // This part will use the offsets to adjust buffer data
    m_descriptorSetLayouts[DSET_OBJECT] = nvk.vkCreateDescriptorSetLayout(
        NVK::VkDescriptorSetLayoutCreateInfo(NVK::VkDescriptorSetLayoutBinding
         (BINDING_MATRIXOBJ, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT) // BINDING_MATRIXOBJ
         (BINDING_MATERIAL , VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT) // BINDING_MATERIAL
    ) );
    //
    // PipelineLayout
    //
    m_pipelineLayout = nvk.vkCreatePipelineLayout(m_descriptorSetLayouts, DSET_TOTALAMOUNT);
    //--------------------------------------------------------------------------
    // Init 'pipelines'
    //
    //
    // what is needed to tell which states are dynamic
    //
    NVK::VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo(
            NVK::VkDynamicState
                (VK_DYNAMIC_STATE_VIEWPORT)
                (VK_DYNAMIC_STATE_SCISSOR)
        );

    NVK::VkPipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo(
        NVK::VkViewport(0.0f,0.0f,(float)w, (float)h,0.0f,1.0f),
        NVK::VkRect2DArray(0.0f,0.0f,(float)w, (float)h)
        );
    NVK::VkPipelineRasterizationStateCreateInfo vkPipelineRasterStateCreateInfo(
        VK_TRUE,            //depthClipEnable
        VK_FALSE,           //rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL, //fillMode
        VK_CULL_MODE_NONE,  //cullMode
        VK_FRONT_FACE_COUNTER_CLOCKWISE,  //frontFace
        VK_TRUE,            //depthBiasEnable
        0.0,                //depthBias
        0.0,                //depthBiasClamp
        0.0,                //slopeScaledDepthBias
        1.0                 //lineWidth
        );
    NVK::VkPipelineColorBlendStateCreateInfo vkPipelineColorBlendStateCreateInfo(
        VK_FALSE/*logicOpEnable*/,
        VK_LOGIC_OP_NO_OP, 
        NVK::VkPipelineColorBlendAttachmentState(
                VK_FALSE/*blendEnable*/,
                VK_BLEND_FACTOR_ZERO   /*srcBlendColor*/,
                VK_BLEND_FACTOR_ZERO   /*destBlendColor*/,
                VK_BLEND_OP_ADD /*blendOpColor*/,
                VK_BLEND_FACTOR_ZERO   /*srcBlendAlpha*/,
                VK_BLEND_FACTOR_ZERO   /*destBlendAlpha*/,
                VK_BLEND_OP_ADD /*blendOpAlpha*/,
                VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT/*colorWriteMask*/),
        NVK::Float4()           //blendConst[4]
                );
    NVK::VkPipelineDepthStencilStateCreateInfo vkPipelineDepthStencilStateCreateInfo(
        VK_TRUE,                    //depthTestEnable
        VK_TRUE,                    //depthWriteEnable
        VK_COMPARE_OP_LESS_OR_EQUAL,   //depthCompareOp
        VK_FALSE,                   //depthBoundsTestEnable
        VK_FALSE,                   //stencilTestEnable
        NVK::VkStencilOpState(), NVK::VkStencilOpState(), //front, back
        0.0f, 1.0f                  //minDepthBounds, maxDepthBounds
        );
    ::VkSampleMask sampleMask = 0xFFFF;
    NVK::VkPipelineMultisampleStateCreateInfo vkPipelineMultisampleStateCreateInfo(
        (VkSampleCountFlagBits)MSAA /*rasterSamples*/, VK_FALSE /*sampleShadingEnable*/, 1.0 /*minSampleShading*/, &sampleMask /*sampleMask*/, VK_FALSE, VK_FALSE);

    //
    // Fur gfx pipeline
    //
    m_pipelinefur = nvk.vkCreateGraphicsPipeline(NVK::VkGraphicsPipelineCreateInfo
        (m_pipelineLayout,0)
        (NVK::VkPipelineVertexInputStateCreateInfo(
            NVK::VkVertexInputBindingDescription    (0/*binding*/, sizeof(Vertex)/*stride*/, VK_VERTEX_INPUT_RATE_VERTEX),
            NVK::VkVertexInputAttributeDescription  (0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, 0) // pos
                                                    (1/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vec3f)) // normal
                                                    (2/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32A32_SFLOAT, 2*sizeof(vec3f)) // color
        ) )
        (NVK::VkPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE) )
        (NVK::VkPipelineShaderStageCreateInfo(
            VK_SHADER_STAGE_VERTEX_BIT, nvk.vkCreateShaderModule(spv_GLSL_fur_vert.c_str(), spv_GLSL_fur_vert.size()), "main") )
        (vkPipelineViewportStateCreateInfo)
        (vkPipelineRasterStateCreateInfo)
        (vkPipelineMultisampleStateCreateInfo)
        (NVK::VkPipelineShaderStageCreateInfo(
            VK_SHADER_STAGE_FRAGMENT_BIT, nvk.vkCreateShaderModule(spv_GLSL_fur_frag.c_str(), spv_GLSL_fur_frag.size()), "main") )
        (vkPipelineColorBlendStateCreateInfo)
        (vkPipelineDepthStencilStateCreateInfo)
        (dynamicStateCreateInfo)
        );
    //
    // Create the buffer with these data
    //
	std::vector<Vertex> data;
    buildFur(data);
    m_nElmts = data.size();
	GLuint vbofurSz = data.size() * sizeof(Vertex);
    m_furBuffer.buffer = nvk.createAndFillBuffer(m_cmdPool, vbofurSz, &(data[0]), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_furBuffer.bufferMem);

    //
    // Descriptor Pool: size is 4 to have enough for global; object and ...
    // TODO: try other VkDescriptorType
    //
    m_descPool = nvk.vkCreateDescriptorPool(NVK::VkDescriptorPoolCreateInfo(
        3, NVK::VkDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3)
                                    (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3) )
    );
    //
    // DescriptorSet allocation
    // Here we allocate only the global descriptor set
    // Objects will do their own allocation later
    //
    nvk.vkAllocateDescriptorSets(NVK::VkDescriptorSetAllocateInfo
        (m_descPool, 1, m_descriptorSetLayouts + DSET_GLOBAL), 
        &m_descriptorSetGlobal);
    //
    // update the descriptorset used for Global
    // later we will update the ones local to objects
    //
    NVK::VkDescriptorBufferInfo descBuffer = NVK::VkDescriptorBufferInfo(m_matrix.buffer, 0, m_matrix.Sz);

    nvk.vkUpdateDescriptorSets(NVK::VkWriteDescriptorSet
        (m_descriptorSetGlobal, BINDING_MATRIX, 0, descBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        );
    //
    // Create a Fence for the primary command-buffer
    //
    m_sceneFence[0] = nvk.vkCreateFence();
    m_sceneFence[1] = nvk.vkCreateFence(VK_FENCE_CREATE_SIGNALED_BIT);
    nvk.vkResetFences(2, m_sceneFence);

    //
    // initialize the super-sampled render-target. But at this stage we don't know the viewport size...
    // TODO: put it somewhere else
    //
    downsamplingMode = NVFBOBoxVK::DS2;
    m_nvFBOBox.Initialize(nvk, w, h, SSScale, MSAA);
    return true;
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RendererVk::display(const InertiaCamera& camera, const mat4f& projection)
{
    PROFILE_SECTION("RendererVk::display");
    if(m_bValid == false) return;
    //NXPROFILEFUNC(__FUNCTION__);
    //
    // Update general params for all sub-sequent operations IN CMD BUFFER #1
    //
    g_globalMatrices.mV = camera.m4_view;
    g_globalMatrices.mP = projection;
    float w = (float)m_nvFBOBox.getBufferWidth();
    float h = (float)m_nvFBOBox.getBufferHeight();
    VkRenderPass    renderPass  = m_nvFBOBox.getScenePass();
    VkFramebuffer   framebuffer = m_nvFBOBox.getFramebuffer();
    NVK::VkRect2D   viewRect    = m_nvFBOBox.getViewRect();
    //
    // Create the primary command buffer
    //
    //
    // pingpong between 2 cmd-buffers to avoid waiting for them to be done
    //
    m_cmdSceneIdx ^= 1;
    if(m_cmdScene[m_cmdSceneIdx])
    {
        while(nvk.vkWaitForFences(1, &m_sceneFence[m_cmdSceneIdx], VK_TRUE, 100000000) == false) { }
        nvk.vkResetFences(1, &m_sceneFence[m_cmdSceneIdx]);
        nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdScene[m_cmdSceneIdx]);
        m_cmdScene[m_cmdSceneIdx] = NULL;
    }
    m_cmdScene[m_cmdSceneIdx] = nvk.vkAllocateCommandBuffer(m_cmdPool, true);
    nvk.vkBeginCommandBuffer(m_cmdScene[m_cmdSceneIdx], false, NVK::VkCommandBufferInheritanceInfo(renderPass, 0, framebuffer, VK_FALSE, 0, 0) );
    vkCmdBeginRenderPass(m_cmdScene[m_cmdSceneIdx],
        NVK::VkRenderPassBeginInfo(
        renderPass, framebuffer, viewRect,
            NVK::VkClearValue(NVK::VkClearColorValue(0.0f, 0.1f, 0.15f, 1.0f))
                             (NVK::VkClearDepthStencilValue(1.0, 0))), 
        VK_SUBPASS_CONTENTS_INLINE );
    vkCmdUpdateBuffer       (m_cmdScene[m_cmdSceneIdx], m_matrix.buffer, 0, sizeof(g_globalMatrices), (uint32_t*)&g_globalMatrices);
    //
    // render the mesh
    //
    vkCmdBindPipeline(m_cmdScene[m_cmdSceneIdx], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelinefur); 
    vkCmdSetViewport( m_cmdScene[m_cmdSceneIdx], 0, 1, NVK::VkViewport(0.0, 0.0, w, h, 0.0f, 1.0f) );
    vkCmdSetScissor(  m_cmdScene[m_cmdSceneIdx], 0, 1, NVK::VkRect2D(0.0, 0.0, w, h) );
    VkDeviceSize vboffsets[1] = {0};
    vkCmdBindVertexBuffers(m_cmdScene[m_cmdSceneIdx], 0, 1, &m_furBuffer.buffer, vboffsets);
    //
    // bind the descriptor set for global stuff
    //
    vkCmdBindDescriptorSets(m_cmdScene[m_cmdSceneIdx], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, DSET_GLOBAL, 1, &m_descriptorSetGlobal, 0, NULL);

    vkCmdDraw(m_cmdScene[m_cmdSceneIdx], m_nElmts, 1, 0, 0);
    //
    //
    //
    vkCmdEndRenderPass(m_cmdScene[m_cmdSceneIdx]);
    vkEndCommandBuffer(m_cmdScene[m_cmdSceneIdx]);

    nvk.vkQueueSubmit( NVK::VkSubmitInfo(
        1, &m_semOpenGLReadDone, 
        1, &m_cmdScene[m_cmdSceneIdx], 
        1, &m_semVKRenderingDone),  
      m_sceneFence[m_cmdSceneIdx]
    );
    //
    // this is going to issue another command-buffer
    //
    m_nvFBOBox.Draw(downsamplingMode);

    w = m_nvFBOBox.getWidth();
    h = m_nvFBOBox.getHeight();
    // NO Depth test
    glDisable(GL_DEPTH_TEST);
    //
    // Wait for the queue of Our VK rendering to signal m_semVKRenderingDone so we know the image is ready
    //
    glWaitVkSemaphoreNV((GLuint64)m_semVKRenderingDone);
    //
    // Blit the image
    //
    glDrawVkImageNV((GLuint64)m_nvFBOBox.getColorImage(), 0, 0,0,w,h, 0, 0,1,1,0);
    //
    // Signal m_semOpenGLReadDone to tell the VK rendering queue that it can render the next one
    //
    glSignalVkSemaphoreNV((GLuint64)m_semOpenGLReadDone);
    //
    // Depth test back to ON (assuming we needed to put it back)
    //
    glEnable(GL_DEPTH_TEST);
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void RendererVk::updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor)
{
    if(m_bValid == false) return;
    int prevLineW = m_nvFBOBox.getSSFactor();
    // resize the intermediate super-sampled render-target
    m_nvFBOBox.resize(width, height, SSFactor);
}

//------------------------------------------------------------------------------
// release the command buffers
//------------------------------------------------------------------------------
void RendererVk::waitForGPUIdle()
{
    vkQueueWaitIdle(nvk.m_queue); // need to wait: some command-buffers could be used by the GPU
}
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool RendererVk::terminateGraphics()
{
    if(!m_bValid)
        return true;
    // destroy the super-sampling pass system
    m_nvFBOBox.Finish();
    // destroys commandBuffers: but not really needed since m_cmdPool later gets destroyed
    nvk.vkDestroyFence(m_sceneFence[0]);
    m_sceneFence[0] = NULL;
    nvk.vkDestroyFence(m_sceneFence[1]);
    m_sceneFence[1] = NULL;
    nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdScene[0]);
    m_cmdScene[0] = NULL;
    nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdScene[1]);
    m_cmdScene[1] = NULL;

	nvk.vkDestroyCommandPool(m_cmdPool); // destroys commands that are inside, obviously

	for(int i=0; i<DSET_TOTALAMOUNT; i++)
	{
		vkDestroyDescriptorSetLayout(nvk.m_device, m_descriptorSetLayouts[i], NULL); // general layout and objects layout
		m_descriptorSetLayouts[i] = 0;
	}
	//vkFreeDescriptorSets(nvk.m_device, m_descPool, 1, &m_descriptorSetGlobal); // no really necessary: we will destroy the pool after that
    m_descriptorSetGlobal = NULL;

    vkDestroyDescriptorPool(nvk.m_device, m_descPool, NULL);
    m_descPool = NULL;

	vkDestroyPipelineLayout(nvk.m_device, m_pipelineLayout, NULL);
    m_pipelineLayout = NULL;

    vkDestroyPipeline(nvk.m_device, m_pipelinefur, NULL);
    m_pipelinefur = NULL;

    m_furBuffer.release();
    m_matrix.release();

    deinitTimers();

    nvk.vkDestroySemaphore(m_semOpenGLReadDone);
    nvk.vkDestroySemaphore(m_semVKRenderingDone);
    m_semOpenGLReadDone = NULL;
    m_semVKRenderingDone = NULL;
    //glGetVkInstanceProcAddrNV = NULL;
    glWaitVkSemaphoreNV = NULL;
    glSignalVkSemaphoreNV = NULL;
    glSignalVkFenceNV = NULL;
    glDrawVkImageNV = NULL;

    nvk.DestroyDevice();

    m_bValid = false;
    return false;
}

} //namespace vk
