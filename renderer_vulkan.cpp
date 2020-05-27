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
#include <queue>
#include <nvvk/profiler_vk.hpp>

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
    VkDeviceMemory  bufferMem;
    size_t          Sz;
    void release() {
      if (buffer)       nvk.destroyBuffer(buffer);
      if (bufferMem)
        nvk.freeMemory(bufferMem);
      memset(this, 0, sizeof(BufO));
    }
  };

  //------------------------------------------------------------------------------
  // Renderer: can be OpenGL or other
  //------------------------------------------------------------------------------
  class RendererVk : public Renderer
  {
  private:
    bool                        m_bValid;
    //
    // Vulkan stuff
    //

    VkDescriptorPool            m_descPool;

    VkDescriptorSetLayout       m_descriptorSetLayouts[DSET_TOTALAMOUNT]; // general layout and objects layout
    VkDescriptorSet             m_descriptorSetGlobal; // descriptor set for general part

    VkPipelineLayout            m_pipelineLayout;

    VkPipeline                  m_pipelinefur;

    NVFBOBoxVK                  m_nvFBOBox; // the super-sampled render-target
    NVFBOBoxVK::DownSamplingTechnique downsamplingMode;

    NVK::CommandPool            m_cmdPool;
    std::vector<VkCommandBuffer> m_cmdBufferQueue[2];
    VkFence                     m_sceneFence[2];
    int                         m_cmdSceneIdx;

    // Used for merging Vulkan image to OpenGL backbuffer 
    VkSemaphore                 m_semOpenGLReadDone;
    VkSemaphore                 m_semVKRenderingDone;


    GLuint                      m_nElmts;
    BufO                        m_furBuffer;
    BufO                        m_matrix;

    nvvk::ProfilerVK            m_profilerVK;

    std::string                 m_spv_GLSL_fur_frag;
    std::string                 m_spv_GLSL_fur_vert;
    int                         m_MSAA;

    NVK::PipelineDynamicStateCreateInfo       m_dynamicStateCreateInfo;
    NVK::PipelineRasterizationStateCreateInfo m_vkPipelineRasterStateCreateInfo;
    NVK::PipelineColorBlendStateCreateInfo    m_vkPipelineColorBlendStateCreateInfo;
    NVK::PipelineDepthStencilStateCreateInfo  m_vkPipelineDepthStencilStateCreateInfo;
    NVK::PipelineMultisampleStateCreateInfo   m_vkPipelineMultisampleStateCreateInfo;

    void initRenderPassRelated();

  public:

    RendererVk() {
      m_bValid = false;
      g_renderers[g_numRenderers++] = this;
      m_cmdSceneIdx = 0;

    }
    virtual ~RendererVk() {}

    virtual const char *getName() { return "Vulkan"; }
    virtual bool valid() { return m_bValid; };
    virtual bool initGraphics(int w, int h, float SSScale, int MSAA);
    virtual bool terminateGraphics();
    virtual void waitForGPUIdle();

    virtual void display(const InertiaCamera& camera, const mat4f& projection);

    virtual void updateMSAA(int MSAA);

    virtual void updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor);

    virtual bool bFlipViewport() { return true; }

    virtual void setDownSamplingMode(int i) {
      downsamplingMode = (NVFBOBoxVK::DownSamplingTechnique)i;
    }

  };

  RendererVk s_renderer;


  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  bool load_binary(const std::string &name, std::string &data)
  {
    FILE *fd = NULL;
    std::vector<std::string> paths;
    paths.push_back(name);
    paths.push_back(std::string("GLSL/") + name);
    paths.push_back(std::string(NVPSystem::exePath() + "/" + PROJECT_RELDIRECTORY + "GLSL/") + name);
    paths.push_back(std::string("../GLSL/") + name); // for when working directory in Debug is $(ProjectDir)
    paths.push_back(std::string("../../" PROJECT_NAME "/GLSL/") + name); // for when using $(TargetDir)
    paths.push_back(std::string("../../shipped/" PROJECT_NAME "/GLSL/") + name); // for when using $(TargetDir)
    paths.push_back(std::string("SPV_" PROJECT_NAME "/") + name);

    for (int i = 0; i < paths.size(); i++)
    {
      if ((fd = fopen(paths[i].c_str(), "rb")))
      {
        break;
      }
    }
    if (fd == NULL)
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
    delete[] p;
    return true;
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  bool RendererVk::initGraphics(int w, int h, float SSScale, int MSAA)
  {
    bool bRes;
    if (m_bValid)
      return true;
    m_bValid = true;
    m_MSAA = MSAA;
    //--------------------------------------------------------------------------
    // Create the Vulkan device
    //
    bRes = nvk.utInitialize();
    assert(bRes);
    //--------------------------------------------------------------------------
    // Get the OpenGL extension for merging VULKAN with OpenGL
    //
#ifdef WIN32
#else //ellif (__linux__)
    // TODO
#endif
    VkSemaphoreCreateInfo semCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    m_semOpenGLReadDone = nvk.createSemaphore();
    // Signal Semaphore by default to avoid being stuck
    glSignalVkSemaphoreNV((GLuint64)m_semOpenGLReadDone);
    m_semVKRenderingDone = nvk.createSemaphore();
    //--------------------------------------------------------------------------
    // Command pool for the main thread
    //
    VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolInfo.queueFamilyIndex = 0;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    nvk.createCommandPool(&cmdPoolInfo, NULL, &m_cmdPool);

    //--------------------------------------------------------------------------
    m_profilerVK = nvvk::ProfilerVK(&g_profiler);
    m_profilerVK.init(nvk.m_device, nvk.m_gpu.device);

    //
    // what is needed to tell which states are dynamic
    //
    m_dynamicStateCreateInfo = NVK::PipelineDynamicStateCreateInfo(
      NVK::DynamicState
      (VK_DYNAMIC_STATE_VIEWPORT)
      (VK_DYNAMIC_STATE_SCISSOR)
    );

    m_vkPipelineRasterStateCreateInfo = NVK::PipelineRasterizationStateCreateInfo(
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
    m_vkPipelineColorBlendStateCreateInfo = NVK::PipelineColorBlendStateCreateInfo(
      VK_FALSE/*logicOpEnable*/,
      VK_LOGIC_OP_NO_OP,
      NVK::PipelineColorBlendAttachmentState(
        VK_FALSE/*blendEnable*/,
        VK_BLEND_FACTOR_ZERO   /*srcBlendColor*/,
        VK_BLEND_FACTOR_ZERO   /*destBlendColor*/,
        VK_BLEND_OP_ADD /*blendOpColor*/,
        VK_BLEND_FACTOR_ZERO   /*srcBlendAlpha*/,
        VK_BLEND_FACTOR_ZERO   /*destBlendAlpha*/,
        VK_BLEND_OP_ADD /*blendOpAlpha*/,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT/*colorWriteMask*/),
      NVK::Float4()           //blendConst[4]
    );
    m_vkPipelineDepthStencilStateCreateInfo = NVK::PipelineDepthStencilStateCreateInfo(
      VK_TRUE,                    //depthTestEnable
      VK_TRUE,                    //depthWriteEnable
      VK_COMPARE_OP_LESS_OR_EQUAL,   //depthCompareOp
      VK_FALSE,                   //depthBoundsTestEnable
      VK_FALSE,                   //stencilTestEnable
      NVK::StencilOpState(), NVK::StencilOpState(), //front, back
      0.0f, 1.0f                  //minDepthBounds, maxDepthBounds
    );
    //--------------------------------------------------------------------------
    // Load SpirV shaders
    //
    bRes = true;
    if (!load_binary(std::string("GLSL_fur_frag.spv"), m_spv_GLSL_fur_frag))
      bRes = false;
    if (!load_binary(std::string("GLSL_fur_vert.spv"), m_spv_GLSL_fur_vert))
      bRes = false;
    if (bRes == false)
    {
      LOGE("Failed loading some SPV files\n");
      nvk.utDestroy();
      m_bValid = false;
      return false;
    }

    //--------------------------------------------------------------------------
    // Buffers for general UBOs
    //
    m_matrix.Sz = sizeof(vec4f) * 4 * 2;
    m_matrix.buffer = nvk.utCreateAndFillBuffer(&m_cmdPool, m_matrix.Sz, NULL, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_matrix.bufferMem);
    //--------------------------------------------------------------------------
    // descriptor set
    //
    // descriptor layout for general things (projection matrix; view matrix...)
    m_descriptorSetLayouts[DSET_GLOBAL] = nvk.createDescriptorSetLayout(
      NVK::DescriptorSetLayoutCreateInfo(NVK::DescriptorSetLayoutBinding
      (BINDING_MATRIX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT) // BINDING_MATRIX
      //(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT) // BINDING_LIGHT
      ));
    // descriptor layout for object level: buffers related to the object (objec-matrix; material colors...)
    // This part will use the offsets to adjust buffer data
    m_descriptorSetLayouts[DSET_OBJECT] = nvk.createDescriptorSetLayout(
      NVK::DescriptorSetLayoutCreateInfo(NVK::DescriptorSetLayoutBinding
      (BINDING_MATRIXOBJ, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT) // BINDING_MATRIXOBJ
        (BINDING_MATERIAL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT) // BINDING_MATERIAL
      ));
    //
    // PipelineLayout
    //
    m_pipelineLayout = nvk.createPipelineLayout(m_descriptorSetLayouts, DSET_TOTALAMOUNT);

    //
    // Create the buffer with these data
    //
    std::vector<Vertex> data;
    buildFur(data);
    m_nElmts = data.size();
    GLuint vbofurSz = data.size() * sizeof(Vertex);
    m_furBuffer.buffer = nvk.utCreateAndFillBuffer(&m_cmdPool, vbofurSz, &(data[0]), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_furBuffer.bufferMem);

    //
    // Descriptor Pool: size is 4 to have enough for global; object and ...
    // TODO: try other VkDescriptorType
    //
    m_descPool = nvk.createDescriptorPool(NVK::DescriptorPoolCreateInfo(
      3, NVK::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3)
      (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3))
    );
    //
    // DescriptorSet allocation
    // Here we allocate only the global descriptor set
    // Objects will do their own allocation later
    //
    nvk.allocateDescriptorSets(NVK::DescriptorSetAllocateInfo
    (m_descPool, 1, m_descriptorSetLayouts + DSET_GLOBAL),
      &m_descriptorSetGlobal);
    //
    // update the descriptorset used for Global
    // later we will update the ones local to objects
    //
    NVK::DescriptorBufferInfo descBuffer = NVK::DescriptorBufferInfo(m_matrix.buffer, 0, m_matrix.Sz);

    nvk.updateDescriptorSets(NVK::WriteDescriptorSet
    (m_descriptorSetGlobal, BINDING_MATRIX, 0, descBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
    );
    //
    // Create a Fence for the primary command-buffer
    //
    m_sceneFence[0] = nvk.createFence();
    m_sceneFence[1] = nvk.createFence(VK_FENCE_CREATE_SIGNALED_BIT);
    nvk.resetFences(2, m_sceneFence);

    //
    // initialize the super-sampled render-target. But at this stage we don't know the viewport size...
    // TODO: put it somewhere else
    //
    downsamplingMode = NVFBOBoxVK::DS2;
    m_nvFBOBox.Initialize(nvk, w, h, SSScale, MSAA);
    updateViewport(0, 0, w, h, SSScale);
    return true;
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  void RendererVk::display(const InertiaCamera& camera, const mat4f& projection)
  {
    float w, h;
    std::vector<VkCommandBuffer> &cmdBufferQueue = m_cmdBufferQueue[m_cmdSceneIdx];
    {
      if (m_bValid == false) return;
      //NXPROFILEFUNC(__FUNCTION__);
      //
      // Update general params for all sub-sequent operations IN CMD BUFFER #1
      //
      g_globalMatrices.mV = camera.m4_view;
      g_globalMatrices.mP = projection;
      w = (float)m_nvFBOBox.getBufferWidth();
      h = (float)m_nvFBOBox.getBufferHeight();
      VkRenderPass    renderPass = m_nvFBOBox.getScenePass();
      VkFramebuffer   framebuffer = m_nvFBOBox.getFramebuffer();
      NVK::Rect2D   viewRect = m_nvFBOBox.getViewRect();
      //
      // Create the primary command buffer
      //
      NVK::CommandBuffer cmdScene = m_cmdPool.utRequestCmdBuffer(true);
      cmdBufferQueue.push_back(cmdScene.m_cmdbuffer);

      cmdScene.beginCommandBuffer(false, NVK::CommandBufferInheritanceInfo(renderPass, 0, framebuffer, VK_FALSE, 0, 0));

      {
        const nvvk::ProfilerVK::Section profile(m_profilerVK, "frame", cmdScene.m_cmdbuffer);
        vkCmdUpdateBuffer(cmdScene, m_matrix.buffer, 0, sizeof(g_globalMatrices), (uint32_t*)&g_globalMatrices);
        vkCmdBeginRenderPass(cmdScene,
          NVK::RenderPassBeginInfo(
            renderPass, framebuffer, viewRect,
            NVK::ClearValue(NVK::ClearColorValue(0.0f, 0.1f, 0.15f, 1.0f))
            (NVK::ClearDepthStencilValue(1.0, 0))
            (NVK::ClearColorValue(0.0f, 0.1f, 0.15f, 1.0f))
          ),
          VK_SUBPASS_CONTENTS_INLINE);
        //
        // render the mesh
        //
        vkCmdBindPipeline(cmdScene, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelinefur);
        vkCmdSetViewport(cmdScene, 0, 1, NVK::Viewport(0.0, 0.0, w, h, 0.0f, 1.0f));
        vkCmdSetScissor(cmdScene, 0, 1, NVK::Rect2D(0.0, 0.0, w, h));
        VkDeviceSize vboffsets[1] = { 0 };
        vkCmdBindVertexBuffers(cmdScene, 0, 1, &m_furBuffer.buffer, vboffsets);
        //
        // bind the descriptor set for global stuff
        //
        vkCmdBindDescriptorSets(cmdScene, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, DSET_GLOBAL, 1, &m_descriptorSetGlobal, 0, NULL);

        vkCmdDraw(cmdScene, m_nElmts, 1, 0, 0);
        //
        //
        //
        vkCmdEndRenderPass(cmdScene);
      }
      vkEndCommandBuffer(cmdScene);
    }
    //
    // this is going to issue another command-buffer
    //
    VkCommandBuffer cmdDownSample = m_nvFBOBox.Draw(downsamplingMode);
    if (cmdDownSample)
      cmdBufferQueue.push_back(cmdDownSample);

    VkCommandBuffer *arrayCmdBuffer = &cmdBufferQueue[0];
    const VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    nvk.queueSubmit(NVK::SubmitInfo(
      1, &m_semOpenGLReadDone, &waitStages,
      cmdBufferQueue.size(), arrayCmdBuffer,
      0, NULL/*&m_semVKRenderingDone*/),
      m_sceneFence[m_cmdSceneIdx]
    );
    //
    // pingpong between 2 cmd-buffers to avoid waiting for them to be done
    //
    m_cmdSceneIdx ^= 1;
    std::vector<VkCommandBuffer> &cmdBufferQueue2 = m_cmdBufferQueue[m_cmdSceneIdx];
    if (!cmdBufferQueue2.empty())
    {
      while (nvk.waitForFences(1, &m_sceneFence[m_cmdSceneIdx], VK_TRUE, 100000000) == false)
      {
        LOGW(">>>>>> TIMEOUT ON WAIT FENCE\n");
        break;
      }
      nvk.resetFences(1, &m_sceneFence[m_cmdSceneIdx]);
      m_cmdPool.utFreeCommandBuffers(&cmdBufferQueue2[0], cmdBufferQueue2.size() - (cmdDownSample ? 1 : 0));  // -1 bcause the last one comes from m_nvFBOBox and must be kept intact
      cmdBufferQueue2.clear();
    }

    w = m_nvFBOBox.getWidth();
    h = m_nvFBOBox.getHeight();
    // NO Depth test
    glDisable(GL_DEPTH_TEST);
    //
    // Wait for the queue of Our VK rendering to signal m_semVKRenderingDone so we know the image is ready
    //
    //glWaitVkSemaphoreNV((GLuint64)m_semVKRenderingDone);
    //
    // Blit the image
    //
    glDrawVkImageNV((GLuint64)m_nvFBOBox.getColorImage(), 0, 0, 0, w, h, 0, 0, 1, 1, 0);
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
  void RendererVk::initRenderPassRelated()
  {
    //
    // Init 'pipelines'
    //
    if (m_pipelinefur)
      vkDestroyPipeline(nvk.m_device, m_pipelinefur, NULL);
    m_pipelinefur = NULL;
    // we don't care about the viewport... will be dynamcically setup
    NVK::PipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo(
      NVK::Viewport(0.0f, 0.0f, (float)100, (float)100, 0.0f, 1.0f),
      NVK::Rect2DArray(0.0f, 0.0f, (float)100, (float)100)
    );
    //
    // Get the renderpass on which the pipeline will be used
    //
    VkRenderPass    renderPass = m_nvFBOBox.getScenePass();

    ::VkSampleMask sampleMask = 0xFFFF;
    m_vkPipelineMultisampleStateCreateInfo = NVK::PipelineMultisampleStateCreateInfo(
      (VkSampleCountFlagBits)m_MSAA /*rasterSamples*/, VK_FALSE /*sampleShadingEnable*/, 1.0 /*minSampleShading*/, &sampleMask /*sampleMask*/, VK_FALSE, VK_FALSE);
    //
    // Fur gfx pipeline
    //
    m_pipelinefur = nvk.createGraphicsPipeline(NVK::GraphicsPipelineCreateInfo
    (m_pipelineLayout, renderPass,/*subpass*/0,/*basePipelineHandle*/0,/*basePipelineIndex*/0,/*flags*/0)
      (NVK::PipelineVertexInputStateCreateInfo(
        NVK::VertexInputBindingDescription(0/*binding*/, sizeof(Vertex)/*stride*/, VK_VERTEX_INPUT_RATE_VERTEX),
        NVK::VertexInputAttributeDescription(0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, 0) // pos
        (1/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vec3f)) // normal
        (2/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32A32_SFLOAT, 2 * sizeof(vec3f)) // color
      ))
      (NVK::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE))
      (NVK::PipelineShaderStageCreateInfo(
        VK_SHADER_STAGE_VERTEX_BIT, nvk.createShaderModule(m_spv_GLSL_fur_vert.c_str(), m_spv_GLSL_fur_vert.size()), "main"))
        (vkPipelineViewportStateCreateInfo)
      (m_vkPipelineRasterStateCreateInfo)
      (m_vkPipelineMultisampleStateCreateInfo)
      (NVK::PipelineShaderStageCreateInfo(
        VK_SHADER_STAGE_FRAGMENT_BIT, nvk.createShaderModule(m_spv_GLSL_fur_frag.c_str(), m_spv_GLSL_fur_frag.size()), "main"))
        (m_vkPipelineColorBlendStateCreateInfo)
      (m_vkPipelineDepthStencilStateCreateInfo)
      (m_dynamicStateCreateInfo)
    );
  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  void RendererVk::updateMSAA(int MSAA)
  {
    // first, make sure we are done with any Queue
    nvk.deviceWaitIdle();
    for (int i = 0; i < 2; i++) {
      VkResult res = nvk.getFenceStatus(m_sceneFence[i]);
      if (res != VK_NOT_READY) {
        while (nvk.waitForFences(1, &m_sceneFence[i], VK_TRUE, 100000000) == false)
        {
          LOGW(">>>>>> TIMEOUT ON WAIT FENCE\n");
          break;
        }
        nvk.resetFences(1, &m_sceneFence[i]);
      }
    }
    m_MSAA = MSAA;
    m_nvFBOBox.setMSAA(MSAA);
    initRenderPassRelated();

  }
  //------------------------------------------------------------------------------
  //
  //------------------------------------------------------------------------------
  void RendererVk::updateViewport(GLint x, GLint y, GLsizei width, GLsizei height, float SSFactor)
  {
    if (m_bValid == false) return;
    int prevLineW = m_nvFBOBox.getSSFactor();
    // first, make sure we are done with any Queue
    nvk.deviceWaitIdle();
    for (int i = 0; i < 2; i++) {
      VkResult res = nvk.getFenceStatus(m_sceneFence[i]);
      if (res != VK_NOT_READY) {
        while (nvk.waitForFences(1, &m_sceneFence[i], VK_TRUE, 100000000) == false)
        {
          LOGW(">>>>>> TIMEOUT ON WAIT FENCE\n");
          break;
        }
        nvk.resetFences(1, &m_sceneFence[i]);
      }
    }
    // resize the intermediate super-sampled render-target
    m_nvFBOBox.resize(width, height, SSFactor);

    initRenderPassRelated();
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
    if (!m_bValid)
      return true;
    nvk.deviceWaitIdle();
    for (int i = 0; i < 2; i++) {
      VkResult res = nvk.getFenceStatus(m_sceneFence[i]);
      if (res != VK_NOT_READY) {
        while (nvk.waitForFences(1, &m_sceneFence[i], VK_TRUE, 100000000) == false)
        {
          LOGW(">>>>>> TIMEOUT ON WAIT FENCE\n");
          break;
        }
        nvk.resetFences(1, &m_sceneFence[i]);
      }
    }
    // destroy the super-sampling pass system
    m_nvFBOBox.Finish();
    // destroys commandBuffers: but not really needed since m_cmdPool later gets destroyed
    for (int i = 0; i < 2; i++)
    {
      nvk.destroyFence(m_sceneFence[i]);
      m_sceneFence[i] = NULL;
      if (m_cmdBufferQueue[i].size() > 0)
        m_cmdPool.utFreeCommandBuffers(&m_cmdBufferQueue[i][0], m_cmdBufferQueue[i].size() - 1); // -1 bcause the last one comes from m_nvFBOBox and must be kept intact
      m_cmdBufferQueue[i].clear();
    }
    m_cmdPool.destroyCommandPool(); // destroys commands that are inside, obviously

    for (int i = 0; i < DSET_TOTALAMOUNT; i++)
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

    if (m_pipelinefur)
      vkDestroyPipeline(nvk.m_device, m_pipelinefur, NULL);
    m_pipelinefur = NULL;

    m_furBuffer.release();
    m_matrix.release();

    m_profilerVK.deinit();

    nvk.destroySemaphore(m_semOpenGLReadDone);
    nvk.destroySemaphore(m_semVKRenderingDone);
    m_semOpenGLReadDone = NULL;
    m_semVKRenderingDone = NULL;

    nvk.utDestroy();

    m_bValid = false;
    return false;
  }

} //namespace vk
