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

#include <string.h>
#include <vector>

#include "nv_math/nv_math.h"
using namespace nv_math;

template <typename T, size_t N> 
inline size_t array_size(T(&x)[N]) 
{
	return N;
}

// 
// GLSL things
// NOTE: here we take advantage of the fact that NVIDIA driver can directly load GLSL code
// without going through SPIRV. This makes this post-processing class self sufficient
// ( No need to access external spv file...)
// 
static const char passThrough[] = 
    "#version 440 core\n"
    "layout(location=0) in  vec3 pos;\n"
    "layout(location=1) in  vec2 tc0;\n"
    "layout(location=1) out vec2 out_tc0;\n"
    "out gl_PerVertex {\n"
    "    vec4  gl_Position;\n"
    "};\n"
	"void main(void)\n"
	"{\n"
    "	gl_Position = vec4(pos,1);\n"
	"	out_tc0 = tc0;\n"
	"}\n";
static size_t passThrough_sz = array_size(passThrough);

static const char fs1[] = 
    "#version 440 core\n"
    "#extension GL_KHR_vulkan_glsl : enable\n"
    "layout(set=0, binding=0) uniform sampler2D texImage;\n"
    //"layout(set=0, binding = 0, rgba8) uniform imageBuffer im;\n"
    "layout(std140, set=0, binding=1) uniform texInfo {\n"
    "   vec2 texelSize;\n"
    "};\n"
    "layout(location=1) in  vec2 tc0;\n"
    "layout(location=0,index=0) out vec4 outColor;\n"
	"void main()\n"
	"{\n"
    "	outColor = texture(texImage, tc0.xy);\n"
	"}\n";
static size_t fs1_sz = array_size(fs1);

static const char fs2[] = 
    "#version 440 core\n"
    "#extension GL_KHR_vulkan_glsl : enable\n"
    "layout(set=0, binding=0) uniform sampler2D texImage;\n"
    "layout(std140, set=0, binding=1) uniform texInfo {\n"
    "   vec2 texelSize;\n"
    "};\n"
    "layout(location=1) in  vec2 tc0;\n"
    "layout(location=0,index=0) out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec4 tap0 = texture(texImage, tc0.xy);\n"
	"	vec4 tap1 = texture(texImage, tc0.xy + texelSize * vec2(  0.4,  0.9 ));\n"
	"	vec4 tap2 = texture(texImage, tc0.xy + texelSize * vec2( -0.4, -0.9 ));\n"
	"	vec4 tap3 = texture(texImage, tc0.xy + texelSize * vec2( -0.9,  0.4 ));\n"
	"	vec4 tap4 = texture(texImage, tc0.xy + texelSize * vec2(  0.9, -0.4 ));\n"
	"	outColor = 0.2 * ( tap0 + tap1 + tap2 + tap3 + tap4 );\n"
	"}\n";
static size_t fs2_sz = array_size(fs2);

static const char fs3[] = 
    "#version 440 core\n"
    "#extension GL_KHR_vulkan_glsl : enable\n"
    "layout(set=0, binding=0) uniform sampler2D texImage;\n"
    "layout(std140, set=0, binding=1) uniform texInfo {\n"
    "   vec2 texelSize;\n"
    "};\n"
    "layout(location=1) in  vec2 tc0;\n"
    "layout(location=0,index=0) out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec4 color, color2;\n"
	"	vec4 tap0 = texture(texImage, tc0.xy);\n"
	"	vec4 tap1 = texture(texImage, tc0.xy + texelSize * vec2(  0.4,  0.9 ));\n"
	"	vec4 tap2 = texture(texImage, tc0.xy + texelSize * vec2( -0.4, -0.9 ));\n"
	"	vec4 tap3 = texture(texImage, tc0.xy + texelSize * vec2( -0.9,  0.4 ));\n"
	"	vec4 tap4 = texture(texImage, tc0.xy + texelSize * vec2(  0.9, -0.4 ));\n"
	"	color = 0.2 * ( tap0 + tap1 + tap2 + tap3 + tap4 );\n"
	"	vec4 tap11 = texture(texImage, tc0.xy + texelSize * vec2(  0.4,  0.9 ));\n"
	"	vec4 tap21 = texture(texImage, tc0.xy + texelSize * vec2( -0.4, -0.9 ));\n"
	"	vec4 tap31 = texture(texImage, tc0.xy + texelSize * vec2( -0.9,  0.4 ));\n"
	"	vec4 tap41 = texture(texImage, tc0.xy + texelSize * vec2(  0.9, -0.4 ));\n"
	"	color2 = 0.2 * ( tap0 + tap11 + tap21 + tap31 + tap41 );\n"
	"	float mask = clamp(color2.w, 0.0, 1.0);\n"
	"	outColor.rgb = color.rgb * mask + color2.rgb * (1.0-mask);													\n"	
	"	outColor.w = mask;																			\n"
	"}\n";
static size_t fs3_sz = array_size(fs3);

static const char *fsArray[3] = {fs1, fs2, fs3 };
static size_t fsArray_sz[3] = { fs1_sz, fs2_sz, fs3_sz };

#ifdef USE_UNMANAGED
#  pragma managed(push,off)
#endif

///////////////////////////////////////////////////////////////////////////////
// VULKAN: NVK.h > vkfnptrinline.h > vulkannv.h > vulkan.h
//
#include "NVK.h"
#include "NVFBOBoxVK.h"

#define PRINTF(m) printf m

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
    deleteRT();
    if(m_sampler)
        m_nvk.vkDestroySampler(m_sampler);
    m_sampler = NULL;

    for(int i=0; i<3; i++)
    {
        if(m_cmdDownsample[i])
            m_nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdDownsample[i]);
        m_cmdDownsample[i] = NULL;
    }
	if(m_cmdPool)
        vkDestroyCommandPool(m_nvk.m_device, m_cmdPool, NULL); // destroys commands that are inside, obviously
    m_cmdPool = NULL;

    if(m_descriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_nvk.m_device, m_descriptorSetLayout, NULL); // general layout and objects layout
	m_descriptorSetLayout = 0;
	if(m_descriptorSet)
        vkFreeDescriptorSets(m_nvk.m_device, m_descPool, 1, &m_descriptorSet); // no really necessary: we will destroy the pool after that
    m_descriptorSet = NULL;

    if(m_descPool)
        vkDestroyDescriptorPool(m_nvk.m_device, m_descPool, NULL);
    m_descPool = NULL;

	if(m_pipelineLayout)
        vkDestroyPipelineLayout(m_nvk.m_device, m_pipelineLayout, NULL);
    m_pipelineLayout = NULL;

    for(int i=0; i<3; i++)
    {
        if(m_pipelines[i])
            vkDestroyPipeline(m_nvk.m_device, m_pipelines[i], NULL);
        m_pipelines[i] = NULL;
    }

    release(m_texInfo);
    release(m_quadBuffer);
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::deleteRT()
{
   if(m_scenePass)
        vkDestroyRenderPass(m_nvk.m_device, m_scenePass, NULL);
    m_scenePass = NULL;
   if(m_downsamplePass)
        vkDestroyRenderPass(m_nvk.m_device, m_downsamplePass, NULL);
    m_downsamplePass = NULL;

    //
	//loop in tiles
    //
	for(unsigned int i=0; i<m_tileData.size(); i++)
	{
        if(m_tileData[i].FBSS)
            vkDestroyFramebuffer(m_nvk.m_device, m_tileData[i].FBSS, NULL);
        m_tileData[i].FBSS = NULL;
        if(m_tileData[i].FBDS)
            vkDestroyFramebuffer(m_nvk.m_device, m_tileData[i].FBDS, NULL);
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
            m_nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdDownsample[i]);
        m_cmdDownsample[i] = NULL;
    }

    return true;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::initRT()
{
    deleteRT();
	bool multisample = depthSamples > 1;
	bool csaa = false;
	bool ret = true;
	if(bOneFBOPerTile)
		m_tileData.resize(tilesw*tilesh);
	else
		m_tileData.resize(1);
    //
    // Create the render passes for the scene-render
    //
    NVK::VkAttachmentReference color(0/*attachment*/, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL/*layout*/);
    NVK::VkAttachmentReference dst(1/*attachment*/, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL/*layout*/);
    NVK::VkAttachmentReference colorResolved(2/*attachment*/, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL/*layout*/);

    NVK::VkRenderPassCreateInfo rpinfo;
    if (multisample) 
    {
        //
        // Multisample case: have a color buffer as the resolve-target
        //
        rpinfo = NVK::VkRenderPassCreateInfo(
        NVK::VkAttachmentDescription
            (   VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)depthSamples,                             //format, samples
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
            )
            (   VK_FORMAT_D24_UNORM_S8_UINT, (VkSampleCountFlagBits)depthSamples,
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            )
            (   VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)1,                                        //format, samples
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
            ),
        NVK::VkSubpassDescription
        (   VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
            NULL,                           //inputAttachments
            &color,                         //colorAttachments
            &colorResolved,                 //resolveAttachments
            &dst,                           //depthStencilAttachment
            NULL,                           //preserveAttachments
            0                               //flags
        ),
        NVK::VkSubpassDependency(/*NONE*/)
    );
    } else {
        //
        // NON-Multisample case: no need for intermediate resolve target
        //
        rpinfo = NVK::VkRenderPassCreateInfo(
        NVK::VkAttachmentDescription
            (   VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,                                        //format, samples
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
            )
            (   VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            ),
        NVK::VkSubpassDescription
        (   VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
            NULL,                           //inputAttachments
            &color,                         //colorAttachments
            NULL,                           //resolveAttachments
            &dst,                           //depthStencilAttachment
            NULL,                           //preserveAttachments
            0                               //flags
        ),
        NVK::VkSubpassDependency(/*NONE*/)
    );
    }
    m_scenePass     = m_nvk.vkCreateRenderPass(rpinfo);
    //
    // Create the render pass for downsampling step: just a color buffer
    //
    rpinfo = NVK::VkRenderPassCreateInfo(
        NVK::VkAttachmentDescription
            (   VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,                                        //format, samples
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,          //loadOp, storeOp
                VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,  //stencilLoadOp, stencilStoreOp
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL //initialLayout, finalLayout
            ),
        NVK::VkSubpassDescription
        (   VK_PIPELINE_BIND_POINT_GRAPHICS,//pipelineBindPoint
            NULL,                           //inputAttachments
            &color,                         //colorAttachments
            NULL,                           //resolveAttachments
            NULL,                           //depthStencilAttachment
            NULL,                           //preserveAttachments
            0                               //flags
        ),
        NVK::VkSubpassDependency(/*NONE*/)
    );
    m_downsamplePass = m_nvk.vkCreateRenderPass(rpinfo);
    //
	//loop in tiles
    //
	for(unsigned int i=0; i<m_tileData.size(); i++)
	{
        //
		// init the texture that will also be the buffer to render to
		//
        m_tileData[i].color_texture_SS.img        = m_nvk.createImage2D(bufw, bufh, m_tileData[i].color_texture_SS.imgMem, VK_FORMAT_R8G8B8A8_UNORM);
        m_tileData[i].color_texture_SS.imgView    = m_nvk.vkCreateImageView(NVK::VkImageViewCreateInfo(
            m_tileData[i].color_texture_SS.img, // image
            VK_IMAGE_VIEW_TYPE_2D, //viewType
            VK_FORMAT_R8G8B8A8_UNORM, //format
            NVK::VkComponentMapping(),//channels
            NVK::VkImageSubresourceRange()//subresourceRange
            ) );
		//
		// Handle multisample FBO's first
		//
		if (multisample) 
		{
			// initialize color texture
            m_tileData[i].color_texture_SSMS.img        = m_nvk.createImage2D(bufw, bufh, m_tileData[i].color_texture_SSMS.imgMem, VK_FORMAT_R8G8B8A8_UNORM, (VkSampleCountFlagBits)depthSamples, (VkSampleCountFlagBits)coverageSamples);
            m_tileData[i].color_texture_SSMS.imgView    = m_nvk.vkCreateImageView(NVK::VkImageViewCreateInfo(
                m_tileData[i].color_texture_SSMS.img, // image
                VK_IMAGE_VIEW_TYPE_2D, //viewType
                VK_FORMAT_R8G8B8A8_UNORM, //format
                NVK::VkComponentMapping(),//channels
                NVK::VkImageSubresourceRange()//subresourceRange
                ) );

			// bind the multisampled depth buffer
			if(m_depth_texture_SSMS.img == 0)
            {
                m_depth_texture_SSMS.img      = m_nvk.createImage2D(bufw, bufh, m_depth_texture_SSMS.imgMem, VK_FORMAT_D24_UNORM_S8_UINT, (VkSampleCountFlagBits)depthSamples, (VkSampleCountFlagBits)(bCSAA ? coverageSamples:0));
                m_depth_texture_SSMS.imgView  = m_nvk.vkCreateImageView(NVK::VkImageViewCreateInfo(
                    m_depth_texture_SSMS.img, // image
                    VK_IMAGE_VIEW_TYPE_2D, //viewType
                    VK_FORMAT_D24_UNORM_S8_UINT, //format
                    NVK::VkComponentMapping(),//channels
                    NVK::VkImageSubresourceRange()//subresourceRange
                    ) );
            }
            //
            // create the framebuffer
            //
            m_tileData[i].FBSS = m_nvk.vkCreateFramebuffer(
                NVK::VkFramebufferCreateInfo
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
                m_depth_texture_SS.img      = m_nvk.createImage2D(bufw, bufh, m_depth_texture_SS.imgMem, VK_FORMAT_D24_UNORM_S8_UINT);
                m_depth_texture_SS.imgView  = m_nvk.vkCreateImageView(NVK::VkImageViewCreateInfo(
                    m_depth_texture_SS.img, // image
                    VK_IMAGE_VIEW_TYPE_2D, //viewType
                    VK_FORMAT_D24_UNORM_S8_UINT, //format
                    NVK::VkComponentMapping(),//channels
                    NVK::VkImageSubresourceRange()//subresourceRange
                    ) );
			}
            //
            // create the framebuffer
            //
            m_tileData[i].FBSS = m_nvk.vkCreateFramebuffer(
                NVK::VkFramebufferCreateInfo
                (   m_scenePass,            //renderPass
                    bufw, bufh, 1,          //width, height, layers
                (m_tileData[i].color_texture_SS.imgView) )
                (m_depth_texture_SS.imgView)
            );
		}
        //
        // create the framebuffer for downsampling
        //
        m_tileData[i].color_texture_DS.img        = m_nvk.createImage2D(width, height, m_tileData[i].color_texture_DS.imgMem, VK_FORMAT_R8G8B8A8_UNORM);
        m_tileData[i].color_texture_DS.imgView    = m_nvk.vkCreateImageView(NVK::VkImageViewCreateInfo(
            m_tileData[i].color_texture_DS.img, // image
            VK_IMAGE_VIEW_TYPE_2D, //viewType
            VK_FORMAT_R8G8B8A8_UNORM, //format
            NVK::VkComponentMapping(),//channels
            NVK::VkImageSubresourceRange()//subresourceRange
            ) );
        m_tileData[i].FBDS = m_nvk.vkCreateFramebuffer(
            NVK::VkFramebufferCreateInfo
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
    NVK::VkDescriptorImageInfo bufferImageViews = NVK::VkDescriptorImageInfo
        (m_sampler, m_tileData[0].color_texture_SS.imgView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // texImage sampler
    NVK::VkDescriptorBufferInfo descBuffer = NVK::VkDescriptorBufferInfo
        (m_texInfo.buffer, 0, m_texInfo.Sz);

    m_nvk.vkUpdateDescriptorSets(NVK::VkWriteDescriptorSet
        //(descSetDest,   binding, arrayIndex, VkDescriptorImageInfo/BufferInfo, kDescriptorType)
        (m_descriptorSet, 0,       0,          bufferImageViews,                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        (m_descriptorSet, 1,       0,          descBuffer,                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        );

    //
    // command buffer
    //
    for(int i=0; i<3; i++)
    {
        if(m_cmdDownsample[i])
            m_nvk.vkFreeCommandBuffer(m_cmdPool, m_cmdDownsample[i]);
        m_cmdDownsample[i] = m_nvk.vkAllocateCommandBuffer(m_cmdPool, true);
        {
            m_nvk.vkBeginCommandBuffer   (m_cmdDownsample[i], false, NVK::VkCommandBufferInheritanceInfo(m_downsamplePass, 0, m_tileData[0].FBDS, 0/*occlusionQueryEnable*/, 0/*queryFlags*/, 0/*pipelineStatistics*/) );

            VkRect2D viewRect = NVK::VkRect2D(NVK::VkOffset2D(0,0), NVK::VkExtent2D(width, height));
            float v2[2] = {1.0f/(float)bufw, 1.0f/(float)bufh };
            vkCmdUpdateBuffer       (m_cmdDownsample[i], m_texInfo.buffer, 0, sizeof(float)*2, (uint32_t*)&v2[0]);
            vkCmdBeginRenderPass    (m_cmdDownsample[i],
                NVK::VkRenderPassBeginInfo(m_downsamplePass, m_tileData[0].FBDS, viewRect,
                    NVK::VkClearValue(NVK::VkClearColorValue(0.8f,0.2f,0.2f,0.0f)) ), 
                VK_SUBPASS_CONTENTS_INLINE );
            vkCmdBindPipeline(m_cmdDownsample[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[i]); 
            vkCmdSetViewport(m_cmdDownsample[i], 0, 1, NVK::VkViewport(0,0,width, height, 0.0f, 1.0f) );
            vkCmdSetScissor( m_cmdDownsample[i], 0, 1, NVK::VkRect2D(0.0,0.0, width, height) );
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
bool NVFBOBoxVK::resize(int w, int h, float ssfact, int depthSamples_, int coverageSamples_)
{
	if(depthSamples_ >= 0)
		depthSamples = depthSamples_;
	if(coverageSamples_ >= 0)
		coverageSamples = coverageSamples;
	if(ssfact >= 1.0)
		scaleFactor = ssfact;
	if(w > 0)
		width = w;
	if(h > 0)
		height = h;
	bufw = (int)(scaleFactor*(float)width);
	bufh = (int)(scaleFactor*(float)height);

    bool multisample = depthSamples > 1;
	bool csaa = false;
	bool ret = true;

    return initRT();
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBoxVK::Initialize(NVK &nvk, int w, int h, float ssfact, int depthSamples_, int coverageSamples_, int tilesW, int tilesH, bool bOneFBOPerTile_)
{
    VkResult result;
	Finish();
    m_nvk = nvk;

	if(depthSamples_ >= 0)
		depthSamples = depthSamples_;
	if(coverageSamples_ >= 0)
		coverageSamples = coverageSamples;
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
    result = vkCreateCommandPool(m_nvk.m_device, &cmdPoolInfo, NULL, &m_cmdPool);
    //--------------------------------------------------------------------------
    // descriptor set
    //
    // descriptor layout for general things (projection matrix; view matrix...)
    m_descriptorSetLayout = m_nvk.vkCreateDescriptorSetLayout(
        NVK::VkDescriptorSetLayoutCreateInfo(NVK::VkDescriptorSetLayoutBinding
         //binding descriptorType,                              arraySize,  stageFlags
         (0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    1,          VK_SHADER_STAGE_FRAGMENT_BIT)
         (1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            1,          VK_SHADER_STAGE_FRAGMENT_BIT)
         (2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            1,          VK_SHADER_STAGE_FRAGMENT_BIT) )
    );
    //
    // PipelineLayout
    //
    m_pipelineLayout = nvk.vkCreatePipelineLayout(&m_descriptorSetLayout, 1);
    //
    // MSAA state
    //
    ::VkSampleMask sampleMask = 0xFFFF;
    NVK::VkPipelineMultisampleStateCreateInfo vkPipelineMultisampleStateCreateInfo(
        VK_SAMPLE_COUNT_1_BIT /*rasterSamples*/, VK_FALSE /*sampleShadingEnable*/, 1.0 /*minSampleShading*/, &sampleMask /*sampleMask*/, VK_FALSE, VK_FALSE);
    //--------------------------------------------------------------------------
    // Init 'pipelines'
    //
    NVK::VkPipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo(
            NVK::VkViewport(0.0f,0.0f,(float)width, (float)height,0.0f,1.0f),
            NVK::VkRect2DArray(0.0f,0.0f,(float)width, (float)height)
        );
    NVK::VkPipelineRasterizationStateCreateInfo vkPipelineRasterStateCreateInfo(
        VK_TRUE,            //depthClipEnable
        VK_FALSE,           //rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL, //VkPolygonMode
        VK_CULL_MODE_NONE,  //cullMode
        VK_FRONT_FACE_COUNTER_CLOCKWISE,  //frontFace
        VK_FALSE,           //depthBiasEnable
        0.0,                //depthBias
        0.0,                //depthBiasClamp
        0.0,                //slopeScaledDepthBias
        0.0                 //lineWidth
        );
    NVK::VkPipelineColorBlendStateCreateInfo vkPipelineColorBlendStateCreateInfo(
        VK_FALSE/*logicOpEnable*/,
        VK_LOGIC_OP_NO_OP, 
        NVK::VkPipelineColorBlendAttachmentState(
                VK_FALSE                /*blendEnable*/,
                VK_BLEND_FACTOR_ZERO    /*srcColorBlendFactor*/,
                VK_BLEND_FACTOR_ZERO    /*dstColorBlendFactor*/,
                VK_BLEND_OP_ADD         /*colorBlendOp*/,
                VK_BLEND_FACTOR_ZERO    /*srcAlphaBlendFactor*/,
                VK_BLEND_FACTOR_ZERO    /*dstAlphaBlendFactor*/,
                VK_BLEND_OP_ADD /*alphaBlendOp*/,
                VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT/*colorWriteMask*/),
        NVK::Float4()           //blendConst[4]
        );
    NVK::VkPipelineDepthStencilStateCreateInfo vkPipelineDepthStencilStateCreateInfo(
        VK_FALSE,                   //depthTestEnable
        VK_FALSE,                   //depthWriteEnable
        VK_COMPARE_OP_NEVER,        //depthCompareOp
        VK_FALSE,                   //depthBoundsTestEnable
        VK_FALSE,                   //stencilTestEnable
        NVK::VkStencilOpState(),    //front
        NVK::VkStencilOpState(),    //back
        0.0f,                       //minDepthBounds
        1.0f                        //maxDepthBounds
        );
    //
    // Create a sampler
    //
    m_sampler = nvk.vkCreateSampler(NVK::VkSamplerCreateInfo(
        VK_FILTER_LINEAR,               //magFilter 
        VK_FILTER_LINEAR,               //minFilter 
        VK_SAMPLER_MIPMAP_MODE_LINEAR,          //mipMode 
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressU 
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressV 
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,          //addressW 
        0.0,                                //mipLodBias 
        1,                                  //maxAnisotropy 
        VK_FALSE,                           //compareEnable
        VK_COMPARE_OP_NEVER,                //compareOp 
        0.0,                                //minLod 
        16.0,                               //maxLod 
        VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,  //borderColor
        VK_FALSE
    ) );
    //
    // GRID gfx pipelines
    //
    for(int i=0; i<3; i++)
    {
        m_pipelines[i] = nvk.vkCreateGraphicsPipeline(NVK::VkGraphicsPipelineCreateInfo
            (m_pipelineLayout,/*renderPass*/0,/*subpass*/0,/*basePipelineHandle*/0,/*basePipelineIndex*/0,/*flags*/0)
            (NVK::VkPipelineVertexInputStateCreateInfo(
                NVK::VkVertexInputBindingDescription    (0/*binding*/, 2*sizeof(vec3f)/*stride*/, VK_VERTEX_INPUT_RATE_VERTEX),
                NVK::VkVertexInputAttributeDescription  (0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, 0            /*offset*/ ) // pos
                                                        (1/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vec3f)/*offset*/ )
            ) )
            (NVK::VkPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, VK_FALSE/*primitiveRestartEnable*/) )
            (NVK::VkPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, nvk.vkCreateShaderModule(passThrough, passThrough_sz), "main") )
            (vkPipelineViewportStateCreateInfo)
            (vkPipelineRasterStateCreateInfo)
            (vkPipelineMultisampleStateCreateInfo)
            (NVK::VkPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, nvk.vkCreateShaderModule(fsArray[i], fsArray_sz[i]), "main") )
            (vkPipelineColorBlendStateCreateInfo)
            (vkPipelineDepthStencilStateCreateInfo)
            (NVK::VkPipelineDynamicStateCreateInfo(NVK::VkDynamicState
                (VK_DYNAMIC_STATE_VIEWPORT)(VK_DYNAMIC_STATE_SCISSOR) ) )
            );
    }
    //
    // Descriptor Pool: size is 3 to have enough for global; object and ...
    // TODO: try other VkDescriptorType
    //
    m_descPool = nvk.vkCreateDescriptorPool(NVK::VkDescriptorPoolCreateInfo(
        2, NVK::VkDescriptorPoolSize
            (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
            (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2) )
        );
    //
    // DescriptorSet allocation
    //
    nvk.vkAllocateDescriptorSets( NVK::VkDescriptorSetAllocateInfo(m_descPool,1, &m_descriptorSetLayout), &m_descriptorSet);
    //
    // Buffers for general UBOs
    //
    vec2f texinfo(w,h);
    m_texInfo.Sz = sizeof(vec2f);
    m_texInfo.buffer        = nvk.createAndFillBuffer(m_cmdPool, m_texInfo.Sz, &texinfo, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_texInfo.bufferMem);
    m_texInfo.bufferView    = nvk.vkCreateBufferView(m_texInfo.buffer, VK_FORMAT_UNDEFINED, m_texInfo.Sz );
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
    m_quadBuffer.buffer        = nvk.createAndFillBuffer(m_cmdPool, 4*2*sizeof(vec3f), quad, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_quadBuffer.bufferMem);
    m_quadBuffer.bufferView    = nvk.vkCreateBufferView(m_quadBuffer.buffer, VK_FORMAT_UNDEFINED, 2*sizeof(vec3f) );
	//
	// FBO and related resources
	//
	initRT();

    bValid = true;
	return true;
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void NVFBOBoxVK::Draw(DownSamplingTechnique technique, int tilex, int tiley)//, int windowW, int windowH, float *offset)
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
        m_nvk.vkQueueSubmit(NVK::VkSubmitInfo(0, NULL, 1, m_cmdDownsample + (int)technique, 0, NULL),  NULL);
	}
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

