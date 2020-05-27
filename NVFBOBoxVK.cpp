//--------------------------------------------------------------------------------------
// Author: Tristan Lorach
// Email: tlorach@nvidia.com
//
// Copyright (c) NVIDIA Corporation 2009
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA OR ITS SUPPLIERS
// BE  LIABLE  FOR  ANY  SPECIAL,  INCIDENTAL,  INDIRECT,  OR  CONSEQUENTIAL DAMAGES
// WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS OF BUSINESS PROFITS,
// BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS)
// ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS
// BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Implementation of different antialiasing methods through FBOs
// - typical MSAA
// - CSAA
// - Hardware AA mixed with FBO for supersampling pass
//   - simple downsampling
//   - downsampling with 1 or 2 kernel filters
//
// NVFBOBox is the class that will handle everything related to supersampling through 
// an offscreen surface defined thanks to FBO
//
//--------------------------------------------------------------------------------------

#define EXTERNSVCUI
#define WINDOWINERTIACAMERA_EXTERN

#if defined(_MSC_VER)
#  include <windows.h>
#  include <direct.h>
#endif
#include <assert.h>
#include <float.h>
#include <math.h>

#include <string>
#include <vector>

#include "nvh/nvprint.hpp"
#include "nvmath/nvmath.h"
using namespace nvmath;

// declared in renderer_vulkan.cpp
namespace vk {
  extern bool load_binary(const std::string &name, std::string &data);
}

std::string fsArray[3];
std::string vsPassthrough;

#ifdef USE_UNMANAGED
#  pragma managed(push,off)
#endif

///////////////////////////////////////////////////////////////////////////////
// VULKAN: NVK.h > vkfnptrinline.h > vulkannv.h > vulkan.h
//
#include "NVK.h"
#include "NVFBOBoxVK.h"

#include <map>

/////////////////////////////////////////////
// 
// Methods
//
/////////////////////////////////////////////

NVFBOBoxVK::NVFBOBoxVK() : 
  bOneFBOPerTile(false),
  scaleFactor(1.0),
  depthSamples(0), coverageSamples(0),
  bValid(false),
  vpx(0), vpy(0), vpw(0), vph(0),
  bCSAA(false),
  bufw(0), bufh(0),
  width(0), height(0),
  curtilex(0), curtiley(0),
  pngData(NULL),
  pngDataTile(NULL),
  pngDataSz(0)
{
}
NVFBOBoxVK::~NVFBOBoxVK()
{
    pngDataSz = 0;
    if(pngData)
        delete []pngData;
    pngData = NULL;
    if(pngDataTile)
        delete []pngDataTile;
    pngDataTile = NULL;
}
void NVFBOBoxVK::Finish()
{
    pngDataSz = 0;
    if(pngData)
        delete []pngData;
    pngData = NULL;
    if(pngDataTile)
        delete []pngDataTile;
    pngDataTile = NULL;
    //
    // Free Vulkan resources
    //
    deleteRenderPass();
    deleteFramebufferAndRelated();
    if(m_sampler)
        m_pnvk->destroySampler(m_sampler);
    m_sampler = NULL;

    if(m_cmdPool)
        m_cmdPool.destroyCommandPool();

    if(m_descriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_pnvk->m_device, m_descriptorSetLayout, NULL); // general layout and objects layout
    m_descriptorSetLayout = 0;
    // Not calling it because VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT not set at creation time
    //if(m_descriptorSet)
    //    vkFreeDescriptorSets(m_pnvk->m_device, m_descPool, 1, &m_descriptorSet); // no really necessary: we will destroy the pool after that
    m_descriptorSet = NULL;

    if(m_descPool)
        vkDestroyDescriptorPool(m_pnvk->m_device, m_descPool, NULL);
    m_descPool = NULL;

    if(m_pipelineLayout)
        vkDestroyPipelineLayout(m_pnvk->m_device, m_pipelineLayout, NULL);
    m_pipelineLayout = NULL;

    release(m_texInfo);
    release(m_quadBuffer);
}
/*-------------------------------------------------------------------------

-------------------------------------------------------------------------*/
bool NVFBOBoxVK::deleteRenderPass()
{
  if (m_scenePass)
    vkDestroyRenderPass(m_pnvk->m_device, m_scenePass, NULL);
  m_scenePass = NULL;
  if (m_downsamplePass)
    vkDestroyRenderPass(m_pnvk->m_device, m_downsamplePass, NULL);
  m_downsamplePass = NULL;

  for (int i = 0; i<3; i++)
  {
    if(m_pipelines[i])
      m_pnvk->destroyPipeline(m_pipelines[i], NULL);
    m_pipelines[i] = NULL;
  }
  return true;
}
/*-------------------------------------------------------------------------

-------------------------------------------------------------------------*/
bool NVFBOBoxVK::initRenderPass()
{
  deleteRenderPass();

  bool multisample = depthSamples > 1;
  //
  // Create the render passes for the scene-render
  //
  NVK::AttachmentReference color(0/*attachment*/, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL/*layout*/);
  NVK::AttachmentReference dst(1/*attachment*/, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL/*layout*/);
  NVK::AttachmentReference colorResolved(2/*attachment*/, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL/*layout*/);

  NVK::RenderPassCreateInfo rpinfo;
  if (multisample)
  {
    //
    // Multisample case: have a color buffer as the resolve-target
    //
    rpinfo = NVK::RenderPassCreateInfo(
      NVK::AttachmentDescription
      (VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)depthSamples,                             //format, samples
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
      )
      (VK_FORMAT_D24_UNORM_S8_UINT, (VkSampleCountFlagBits)depthSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        )
        (VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)1,                                        //format, samples
          VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
          ),
      NVK::SubpassDescription
      (VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
        NULL,                           //inputAttachments
        &color,                         //colorAttachments
        &colorResolved,                 //resolveAttachments
        &dst,                           //depthStencilAttachment
        NULL,                           //preserveAttachments
        0                               //flags
      ),
      NVK::SubpassDependency(/*NONE*/)
    );
  }
  else {
    //
    // NON-Multisample case: no need for intermediate resolve target
    //
    rpinfo = NVK::RenderPassCreateInfo(
      NVK::AttachmentDescription
      (VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,                                        //format, samples
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
      )
      (VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        ),
      NVK::SubpassDescription
      (VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
        NULL,                           //inputAttachments
        &color,                         //colorAttachments
        NULL,                           //resolveAttachments
        &dst,                           //depthStencilAttachment
        NULL,                           //preserveAttachments
        0                               //flags
      ),
      NVK::SubpassDependency(/*NONE*/)
    );
  }
  m_scenePass = m_pnvk->createRenderPass(rpinfo);
  //
  // Create the render pass for downsampling step: just a color buffer
  //
  rpinfo = NVK::RenderPassCreateInfo(
    NVK::AttachmentDescription
    (VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,                                        //format, samples
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
    ),
    NVK::SubpassDescription
    (VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
      NULL,                           //inputAttachments
      &color,                         //colorAttachments
      NULL,                           //resolveAttachments
      NULL,                           //depthStencilAttachment
      NULL,                           //preserveAttachments
      0                               //flags
    ),
    NVK::SubpassDependency(/*NONE*/)
  );
  m_downsamplePass = m_pnvk->createRenderPass(rpinfo);

  NVK::PipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo(
    NVK::Viewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f),
    NVK::Rect2DArray(0.0f, 0.0f, (float)width, (float)height)
  );
  //
  // GRID gfx pipelines
  //
  for(int i=0; i<3; i++)
  {
      m_pipelines[i] = m_pnvk->createGraphicsPipeline(NVK::GraphicsPipelineCreateInfo
          (m_pipelineLayout, m_downsamplePass,/*subpass*/0,/*basePipelineHandle*/0,/*basePipelineIndex*/0,/*flags*/0)
          (NVK::PipelineVertexInputStateCreateInfo(
              NVK::VertexInputBindingDescription    (0/*binding*/, 2*sizeof(vec3f)/*stride*/, VK_VERTEX_INPUT_RATE_VERTEX),
              NVK::VertexInputAttributeDescription  (0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, 0            /*offset*/ ) // pos
                                                      (1/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vec3f)/*offset*/ )
          ) )
          (NVK::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_FALSE/*primitiveRestartEnable*/) )
          (NVK::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, m_pnvk->createShaderModule(vsPassthrough.c_str(), vsPassthrough.size() ), "main") )
          (vkPipelineViewportStateCreateInfo)
          (m_vkPipelineRasterStateCreateInfo)
          (m_vkPipelineMultisampleStateCreateInfo)
          (NVK::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, m_pnvk->createShaderModule(fsArray[i].c_str(), fsArray[i].size() ), "main") )
          (m_vkPipelineColorBlendStateCreateInfo)
          (m_vkPipelineDepthStencilStateCreateInfo)
          (NVK::PipelineDynamicStateCreateInfo(NVK::DynamicState
              (VK_DYNAMIC_STATE_VIEWPORT)(VK_DYNAMIC_STATE_SCISSOR) ) )
          );
  }
  return true;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::deleteFramebufferAndRelated()
{
    //
    //loop in tiles
    //
    for(unsigned int i=0; i<m_tileData.size(); i++)
    {
        if(m_tileData[i].FBSS)
            vkDestroyFramebuffer(m_pnvk->m_device, m_tileData[i].FBSS, NULL);
        m_tileData[i].FBSS = NULL;
        if(m_tileData[i].FBDS)
            vkDestroyFramebuffer(m_pnvk->m_device, m_tileData[i].FBDS, NULL);
        m_tileData[i].FBDS = NULL;
        if(m_tileData[i].color_texture_DS.img)
            release(m_tileData[i].color_texture_DS);
        if(m_tileData[i].color_texture_SS.img)
            release(m_tileData[i].color_texture_SS);
        if(m_tileData[i].color_texture_SSMS.img)
            release(m_tileData[i].color_texture_SSMS);
    }
    if(m_depth_texture_SSMS.img)
        release(m_depth_texture_SSMS);
    if(m_depth_texture_SS.img)
        release(m_depth_texture_SS);

    for(int i=0; i<3; i++)
    {
        if(m_cmdDownsample[i])
            m_cmdPool.utFreeCommandBuffer(m_cmdDownsample[i]);
        m_cmdDownsample[i] = NULL;
    }

    return true;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::initFramebufferAndRelated()
{
    deleteFramebufferAndRelated();
    bool multisample = depthSamples > 1;
    bool csaa = false;
    bool ret = true;
    if(bOneFBOPerTile)
        m_tileData.resize(tilesw*tilesh);
    else
        m_tileData.resize(1);

    //
    //loop in tiles
    //
    for(unsigned int i=0; i<m_tileData.size(); i++)
    {
        //
        // init the texture that will also be the buffer to render to
        //
        m_tileData[i].color_texture_SS.img        = m_pnvk->utCreateImage2D(bufw, bufh, m_tileData[i].color_texture_SS.imgMem, VK_FORMAT_R8G8B8A8_UNORM);
        m_tileData[i].color_texture_SS.imgView    = m_pnvk->createImageView(NVK::ImageViewCreateInfo(
            m_tileData[i].color_texture_SS.img, // image
            VK_IMAGE_VIEW_TYPE_2D, //viewType
            VK_FORMAT_R8G8B8A8_UNORM, //format
            NVK::ComponentMapping(),//channels
            NVK::ImageSubresourceRange()//subresourceRange
            ) );
        //
        // Handle multisample FBO's first
        //
        if (multisample) 
        {
            // initialize color texture
            m_tileData[i].color_texture_SSMS.img        = m_pnvk->utCreateImage2D(bufw, bufh, m_tileData[i].color_texture_SSMS.imgMem, VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)depthSamples, (VkSampleCountFlagBits)coverageSamples);
            m_tileData[i].color_texture_SSMS.imgView    = m_pnvk->createImageView(NVK::ImageViewCreateInfo(
                m_tileData[i].color_texture_SSMS.img, // image
                VK_IMAGE_VIEW_TYPE_2D, //viewType
                VK_FORMAT_R8G8B8A8_UNORM, //format
                NVK::ComponentMapping(),//channels
                NVK::ImageSubresourceRange()//subresourceRange
                ) );

            // bind the multisampled depth buffer
            if(m_depth_texture_SSMS.img == 0)
            {
                m_depth_texture_SSMS.img      = m_pnvk->utCreateImage2D(bufw, bufh, m_depth_texture_SSMS.imgMem, VK_FORMAT_D24_UNORM_S8_UINT, (VkSampleCountFlagBits)depthSamples, (VkSampleCountFlagBits)(bCSAA ? coverageSamples:0));
                m_depth_texture_SSMS.imgView  = m_pnvk->createImageView(NVK::ImageViewCreateInfo(
                    m_depth_texture_SSMS.img, // image
                    VK_IMAGE_VIEW_TYPE_2D, //viewType
                    VK_FORMAT_D24_UNORM_S8_UINT, //format
                    NVK::ComponentMapping(),//channels
                    NVK::ImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)//subresourceRange
                    ) );
            }
            //
            // create the framebuffer
            //
            m_tileData[i].FBSS = m_pnvk->createFramebuffer(
                NVK::FramebufferCreateInfo
                (   m_scenePass,    //renderPass
                    bufw, bufh, 1,  //w, h, Layers
                (m_tileData[i].color_texture_SSMS.imgView) ) // first VkImageView
                (m_depth_texture_SSMS.imgView) // additional VkImageView via functor
                (m_tileData[i].color_texture_SS.imgView)
            );
        } // if (multisample)
        else // Depth buffer created without the need to resolve MSAA
        {
            // Create it one for many FBOs
            if(m_depth_texture_SS.img == NULL)
            {
                m_depth_texture_SS.img      = m_pnvk->utCreateImage2D(bufw, bufh, m_depth_texture_SS.imgMem, VK_FORMAT_D24_UNORM_S8_UINT);
                m_depth_texture_SS.imgView  = m_pnvk->createImageView(NVK::ImageViewCreateInfo(
                    m_depth_texture_SS.img, // image
                    VK_IMAGE_VIEW_TYPE_2D, //viewType
                    VK_FORMAT_D24_UNORM_S8_UINT, //format
                    NVK::ComponentMapping(),//channels
                    NVK::ImageSubresourceRange()//subresourceRange
                    ) );
            }
            //
            // create the framebuffer
            //
            m_tileData[i].FBSS = m_pnvk->createFramebuffer(
                NVK::FramebufferCreateInfo
                (   m_scenePass,            //renderPass
                    bufw, bufh, 1,          //width, height, layers
                (m_tileData[i].color_texture_SS.imgView) )
                (m_depth_texture_SS.imgView)
            );
        }
        //
        // create the framebuffer for downsampling
        //
        m_tileData[i].color_texture_DS.img        = m_pnvk->utCreateImage2D(width, height, m_tileData[i].color_texture_DS.imgMem, VK_FORMAT_R8G8B8A8_UNORM);
        m_tileData[i].color_texture_DS.imgView    = m_pnvk->createImageView(NVK::ImageViewCreateInfo(
            m_tileData[i].color_texture_DS.img, // image
            VK_IMAGE_VIEW_TYPE_2D, //viewType
            VK_FORMAT_R8G8B8A8_UNORM, //format
            NVK::ComponentMapping(),//channels
            NVK::ImageSubresourceRange()//subresourceRange
            ) );
        m_tileData[i].FBDS = m_pnvk->createFramebuffer(
            NVK::FramebufferCreateInfo
            (   m_downsamplePass,       //renderPass
                width, height, 1,          //width, height, layers
                (m_tileData[i].color_texture_DS.imgView)
            )
        );
            
    } // for i

    //
    // update the descriptorset used for Global
    // later we will update the ones local to objects
    //
    NVK::DescriptorImageInfo bufferImageViews = NVK::DescriptorImageInfo
        (m_sampler, m_tileData[0].color_texture_SS.imgView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // texImage sampler
    NVK::DescriptorBufferInfo descBuffer = NVK::DescriptorBufferInfo
        (m_texInfo.buffer, 0, m_texInfo.Sz);

    m_pnvk->updateDescriptorSets(NVK::WriteDescriptorSet
        //(descSetDest,   binding, arrayIndex, VkDescriptorImageInfo/BufferInfo, kDescriptorType)
        (m_descriptorSet, 0,       0,          bufferImageViews,                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        (m_descriptorSet, 1,       0,          descBuffer,                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        );

    //
    // command buffer
    // Warning: depends on the render-pass and frame-buffers
    //
    for(int i=0; i<3; i++)
    {
        if(m_cmdDownsample[i])
            m_cmdPool.utFreeCommandBuffer(m_cmdDownsample[i]);
        m_cmdDownsample[i] = m_cmdPool.utAllocateCommandBuffer(true);
        {
            m_cmdDownsample[i].beginCommandBuffer(false, NVK::CommandBufferInheritanceInfo(m_downsamplePass, 0, m_tileData[0].FBDS, 0/*occlusionQueryEnable*/, 0/*queryFlags*/, 0/*pipelineStatistics*/) );

            VkRect2D viewRect = NVK::Rect2D(NVK::Offset2D(0,0), NVK::Extent2D(width, height));
            float v2[2] = {1.0f/(float)bufw, 1.0f/(float)bufh };
            vkCmdUpdateBuffer       (m_cmdDownsample[i], m_texInfo.buffer, 0, sizeof(float)*2, (uint32_t*)&v2[0]);
            vkCmdBeginRenderPass    (m_cmdDownsample[i],
                NVK::RenderPassBeginInfo(m_downsamplePass, m_tileData[0].FBDS, viewRect,
                    NVK::ClearValue(NVK::ClearColorValue(0.8f,0.2f,0.2f,0.0f)) ), 
                VK_SUBPASS_CONTENTS_INLINE );
            vkCmdBindPipeline(m_cmdDownsample[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[i]); 
            vkCmdSetViewport(m_cmdDownsample[i], 0, 1, NVK::Viewport(0,0,width, height, 0.0f, 1.0f) );
            vkCmdSetScissor( m_cmdDownsample[i], 0, 1, NVK::Rect2D(0.0,0.0, width, height) );
            VkDeviceSize vboffsets[1] = {0};
            vkCmdBindVertexBuffers(m_cmdDownsample[i], 0, 1, &m_quadBuffer.buffer, vboffsets);
            uint32_t offsets = 0;
            vkCmdBindDescriptorSets(m_cmdDownsample[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, &offsets);
            vkCmdDraw(m_cmdDownsample[i], 4, 1, 0, 0);
            vkCmdEndRenderPass(m_cmdDownsample[i]);
            vkEndCommandBuffer(m_cmdDownsample[i]);
        }
    }

    return ret;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::resize(int w, int h, float ssfact)
{
    if(ssfact >= 1.0)
        scaleFactor = ssfact;
    if(w > 0)
        width = w;
    if(h > 0)
        height = h;
    bufw = (int)(scaleFactor*(float)width);
    bufh = (int)(scaleFactor*(float)height);
    //
    // resizing only require to reallocate resources :
    //
    return initFramebufferAndRelated();
}
/*-------------------------------------------------------------------------

-------------------------------------------------------------------------*/
bool NVFBOBoxVK::setMSAA(int depthSamples_, int coverageSamples_)
{
  bool bRes = true;
  if (depthSamples_ >= 0)
    depthSamples = depthSamples_;
  if (coverageSamples_ >= 0)
    coverageSamples = coverageSamples_;
  //
  // New MSAA requires to re-create the render-passes
  // New renderpasses requires re-creating render-targets
  //
  if (!initRenderPass() )               return false;
  if (!initFramebufferAndRelated() )    return false;
  return true;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::Initialize(NVK &nvk, int w, int h, float ssfact, int depthSamples_, int coverageSamples_, int tilesW, int tilesH, bool bOneFBOPerTile_)
{
    VkResult result;
    Finish();
    m_pnvk = &nvk;

    if(depthSamples_ >= 0)
        depthSamples = depthSamples_;
    if(coverageSamples_ >= 0)
        coverageSamples = coverageSamples_;
    if(tilesW > 0)
        tilesw = tilesW;
    if(tilesH > 0)
        tilesh = tilesH;
    if(ssfact >= 1.0)
    {
        scaleFactor = ssfact;
    }
    bValid = false;
    if(w > 0)
        width = w;
    if(h > 0)
        height = h;
    bufw = (int)(scaleFactor*(float)width);
    bufh = (int)(scaleFactor*(float)height);
    bOneFBOPerTile = bOneFBOPerTile_;
    //
    // other Vulkan stuff
    //
    //--------------------------------------------------------------------------
    // Command pool
    //
    VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = 0;
    result = nvk.createCommandPool(&cmdPoolInfo, NULL, &m_cmdPool);
    //--------------------------------------------------------------------------
    // descriptor set
    //
    // descriptor layout for general things (projection matrix; view matrix...)
    m_descriptorSetLayout = m_pnvk->createDescriptorSetLayout(
        NVK::DescriptorSetLayoutCreateInfo(NVK::DescriptorSetLayoutBinding
         //binding descriptorType,                              arraySize,  stageFlags
         (0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    1,          VK_SHADER_STAGE_FRAGMENT_BIT)
         (1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            1,          VK_SHADER_STAGE_FRAGMENT_BIT)
         (2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            1,          VK_SHADER_STAGE_FRAGMENT_BIT) )
    );
    //
    // PipelineLayout
    //
    m_pipelineLayout = nvk.createPipelineLayout(&m_descriptorSetLayout, 1);
    //
    // Create a sampler
    //
    m_sampler = nvk.createSampler(NVK::SamplerCreateInfo(
      VK_FILTER_LINEAR,               //magFilter 
      VK_FILTER_LINEAR,               //minFilter 
      VK_SAMPLER_MIPMAP_MODE_LINEAR,          //mipMode 
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressU 
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressV 
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressW 
      0.0,                                //mipLodBias 
      VK_FALSE,
      1,                                  //maxAnisotropy 
      VK_FALSE,                           //compareEnable
      VK_COMPARE_OP_NEVER,                //compareOp 
      0.0,                                //minLod 
      16.0,                               //maxLod 
      VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,  //borderColor
      VK_FALSE
    ));
    //--------------------------------------------------------------------------
    // MSAA state
    //
    ::VkSampleMask sampleMask = 0xFFFF;
    m_vkPipelineMultisampleStateCreateInfo = NVK::PipelineMultisampleStateCreateInfo(
      VK_SAMPLE_COUNT_1_BIT /*rasterSamples*/, VK_FALSE /*sampleShadingEnable*/, 1.0 /*minSampleShading*/, &sampleMask /*sampleMask*/, VK_FALSE, VK_FALSE);
    //--------------------------------------------------------------------------
    // Init 'pipelines'
    //
    m_vkPipelineRasterStateCreateInfo = NVK::PipelineRasterizationStateCreateInfo(
      VK_TRUE,            //depthClipEnable
      VK_FALSE,           //rasterizerDiscardEnable
      VK_POLYGON_MODE_FILL, //VkPolygonMode
      VK_CULL_MODE_NONE,  //cullMode
      VK_FRONT_FACE_COUNTER_CLOCKWISE,  //frontFace
      VK_FALSE,           //depthBiasEnable
      0.0,                //depthBias
      0.0,                //depthBiasClamp
      0.0,                //slopeScaledDepthBias
      1.0                 //lineWidth
    );
    m_vkPipelineColorBlendStateCreateInfo = NVK::PipelineColorBlendStateCreateInfo(
      VK_FALSE/*logicOpEnable*/,
      VK_LOGIC_OP_NO_OP,
      NVK::PipelineColorBlendAttachmentState(
        VK_FALSE                /*blendEnable*/,
        VK_BLEND_FACTOR_ZERO    /*srcColorBlendFactor*/,
        VK_BLEND_FACTOR_ZERO    /*dstColorBlendFactor*/,
        VK_BLEND_OP_ADD         /*colorBlendOp*/,
        VK_BLEND_FACTOR_ZERO    /*srcAlphaBlendFactor*/,
        VK_BLEND_FACTOR_ZERO    /*dstAlphaBlendFactor*/,
        VK_BLEND_OP_ADD /*alphaBlendOp*/,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT/*colorWriteMask*/),
      NVK::Float4()           //blendConst[4]
    );
    m_vkPipelineDepthStencilStateCreateInfo = NVK::PipelineDepthStencilStateCreateInfo(
      VK_FALSE,                   //depthTestEnable
      VK_FALSE,                   //depthWriteEnable
      VK_COMPARE_OP_NEVER,        //depthCompareOp
      VK_FALSE,                   //depthBoundsTestEnable
      VK_FALSE,                   //stencilTestEnable
      NVK::StencilOpState(),    //front
      NVK::StencilOpState(),    //back
      0.0f,                       //minDepthBounds
      1.0f                        //maxDepthBounds
    );

    //
    // Load SpirV shaders
    //
    if (vk::load_binary(std::string("GLSL_passthrough_vert.spv"), vsPassthrough))
      bValid = true;
    if (vk::load_binary(std::string("GLSL_ds1_frag.spv"), fsArray[0]))
      bValid = true;
    if (vk::load_binary(std::string("GLSL_ds2_frag.spv"), fsArray[1]))
      bValid = true;
    if (vk::load_binary(std::string("GLSL_ds3_frag.spv"), fsArray[2]))
      bValid = true;
    if (bValid == false)
    {
      LOGE("Failed loading some SPV files\n");
      nvk.utDestroy();
      bValid = false;
      return false;
    }


    //
    // Descriptor Pool: size is 3 to have enough for global; object and ...
    // TODO: try other VkDescriptorType
    //
    m_descPool = nvk.createDescriptorPool(NVK::DescriptorPoolCreateInfo(
        2, NVK::DescriptorPoolSize
            (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
            (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2) )
        );
    //
    // DescriptorSet allocation
    //
    nvk.allocateDescriptorSets( NVK::DescriptorSetAllocateInfo(m_descPool,1, &m_descriptorSetLayout), &m_descriptorSet);
    //
    // Buffers for general UBOs
    //
    vec2f texinfo(w,h);
    m_texInfo.Sz = sizeof(vec2f);
    m_texInfo.buffer        = nvk.utCreateAndFillBuffer(&m_cmdPool, m_texInfo.Sz, &texinfo, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_texInfo.bufferMem);
    //
    // Create buffer for fullscreen quad
    //
    static vec3f quad[] = {
    vec3f(-1.0, -1.0, 0.0),
    vec3f(0,0,0),
    vec3f( 1.0, -1.0, 0.0),
    vec3f(1,0,0),
    vec3f( 1.0,  1.0, 0.0),
    vec3f(1,1,0),
    vec3f(-1.0,  1.0, 0.0),
    vec3f(0,1,0) };
    m_quadBuffer.buffer        = nvk.utCreateAndFillBuffer(&m_cmdPool, 4*2*sizeof(vec3f), quad, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_quadBuffer.bufferMem);
    //
    // FBO and related resources
    //
    initRenderPass();
    initFramebufferAndRelated();

    bValid = true;
    return true;
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
VkCommandBuffer NVFBOBoxVK::Draw(DownSamplingTechnique technique, int tilex, int tiley)//, int windowW, int windowH, float *offset)
{
    if(tilex < 0)
        tilex = curtilex;
    else
        curtilex = tilex;
    if(tiley < 0)
        tiley = curtiley;
    else
        curtiley = tiley;

    if((scaleFactor > 1.0) || (tilesw > 1) || (tilesh > 1))
    {
      return m_cmdDownsample[technique].m_cmdbuffer;
    }
    return VK_NULL_HANDLE;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
VkRenderPass    NVFBOBoxVK::getScenePass()
{
    return m_scenePass;
}
VkFramebuffer   NVFBOBoxVK::getFramebuffer()
{
    return m_tileData[0].FBSS;
}
VkRect2D        NVFBOBoxVK::getViewRect()
{
    VkRect2D r;
    r.extent.height = bufh;
    r.extent.width = bufw;
    r.offset.x = 0;
    r.offset.y = 0;
    return r;
}
VkImage         NVFBOBoxVK::getColorImage()
{
    // if ever there was NO super-sampling, let's take directly the resolved image
    if(scaleFactor == 1.0)
        return m_tileData[0].color_texture_SS.img;
    // otherwise, take the result of down-sampling
    return m_tileData[0].color_texture_DS.img;
}
VkImage         NVFBOBoxVK::getColorImageSSMS()
{
    return m_tileData[0].color_texture_SSMS.img;
}
VkImage         NVFBOBoxVK::getDSTImageSSMS()
{
    return m_depth_texture_SSMS.img;
}

VkFramebuffer NVFBOBoxVK::GetFBO(int i)
{
    return depthSamples > 1 ? m_tileData[i].FBSS : m_tileData[i].FBDS;
}

