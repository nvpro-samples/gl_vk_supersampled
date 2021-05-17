#pragma once
/*
 * Copyright (c) 2016-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2016-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef GL_FRAMEBUFFER_EXT
#    define GL_FRAMEBUFFER_EXT                0x8D40
typedef unsigned int GLenum;
#endif

class NVFBOBoxVK
{
protected:
    NVK  *m_pnvk;
    struct ImgO {
        VkImage          img;
        VkImageView      imgView;
        VkDeviceMemory   imgMem;
        size_t           Sz;
    };
    struct BufO {
        VkBuffer        buffer;
        VkDeviceMemory  bufferMem;
        size_t          Sz;
    };
    void release(ImgO &imgo) { 
        if(imgo.imgView) vkDestroyImageView(m_pnvk->m_device, imgo.imgView, NULL);
        if(imgo.img)     vkDestroyImage(m_pnvk->m_device, imgo.img, NULL);
        if(imgo.imgMem)  
          m_pnvk->freeMemory(imgo.imgMem);
        memset(&imgo, 0, sizeof(ImgO));
    }
    void release(BufO &bufo) { 
        if(bufo.buffer)       vkDestroyBuffer(m_pnvk->m_device, bufo.buffer, NULL);
        if(bufo.bufferMem)    
          m_pnvk->freeMemory(bufo.bufferMem);
        memset(&bufo, 0, sizeof(BufO));
    }

    NVK::PipelineMultisampleStateCreateInfo   m_vkPipelineMultisampleStateCreateInfo;
    NVK::PipelineRasterizationStateCreateInfo m_vkPipelineRasterStateCreateInfo;
    NVK::PipelineColorBlendStateCreateInfo    m_vkPipelineColorBlendStateCreateInfo;
    NVK::PipelineDepthStencilStateCreateInfo  m_vkPipelineDepthStencilStateCreateInfo;
public:
    enum DownSamplingTechnique
    {
        DS1 = 0,
        DS2 = 1,
        DS3 = 2,
        NONE = 3
    };

    NVFBOBoxVK(); 
    ~NVFBOBoxVK();

    virtual bool Initialize(NVK &nvk, int w, int h, float ssfact, int depthSamples, int coverageSamples=-1, int tilesW=1, int tilesH=1, bool bOneFBOPerTile=true);
    virtual bool setMSAA(int depthSamples_ = -1, int coverageSamples_ = -1);
    virtual bool resize(int w, int h, float ssfact=-1);
    virtual void Finish();

    virtual int getWidth() { return width; }
    virtual int getHeight() { return height; }
    virtual int getBufferWidth() { return bufw; }
    virtual int getBufferHeight() { return bufh; }
    virtual float getSSFactor() { return scaleFactor; }

    VkRenderPass    getScenePass();
    VkFramebuffer   getFramebuffer();
    VkRect2D        getViewRect();
    VkImage         getColorImage();
    VkImage         getColorImageSSMS();
    VkImage         getDSTImageSSMS();
    VkCommandBuffer getCmdBufferDownSample();
    //virtual void Activate(int tilex=0, int tiley=0, float m_frustum[][4]=NULL);
    virtual VkCommandBuffer Draw(DownSamplingTechnique technique, int tilex=0, int tiley=0);

    virtual VkFramebuffer GetFBO(int i=0);

protected:

  bool          bValid;
  bool          bCSAA;

  int           vpx, vpy, vpw, vph;
  int           width, height;
  int           bufw, bufh;
  int           curtilex, curtiley;
  float        scaleFactor;
  int          depthSamples, coverageSamples;

  int           tilesw, tilesh;
  bool            bOneFBOPerTile;
    //
    // Vulkan stuff
    //
    VkRenderPass                m_scenePass;        // pass for rendering into the super-sampled buffers
    VkRenderPass                m_downsamplePass;   // pass for the downsampling step
    NVK::CommandBuffer            m_cmdDownsample[3]; // command for the downsampling step
    NVK::CommandPool            m_cmdPool;
    VkDescriptorPool            m_descPool;

    VkDescriptorSetLayout       m_descriptorSetLayout; // general layout and objects layout
    VkDescriptorSet             m_descriptorSet;    // descriptor set for general part

    VkPipelineLayout            m_pipelineLayout;

    VkPipeline                  m_pipelines[3]; // 3 pipelines for 3 different modes of down-sampling
    //
    // resources
    //
    BufO                        m_quadBuffer;   // buffer for fullscreen quad
    BufO                        m_texInfo;      // buffer for uniforms to pass to shaders for downsampling
    ImgO                        m_depth_texture_SS;    // DST texture after downsampling
    ImgO                        m_depth_texture_SSMS; // DST texture where the scene gets rendered
    //ImgO                        m_testTex;
    VkSampler                   m_sampler;
    struct TileData
    {
        VkFramebuffer   FBDS;
        VkFramebuffer   FBSS;
        ImgO    color_texture_DS;
        ImgO    color_texture_SS;
        ImgO    color_texture_SSMS;
    };
    std::vector<TileData> m_tileData;   // images where the scene gets rendered

    int      pngDataSz;      // size of allocated memory
    unsigned char *pngData;      // temporary data for the full image (many tiles)
    unsigned char *pngDataTile; // temporary data from a tile
    bool    initFramebufferAndRelated();
    bool    initRenderPass();
    bool    deleteFramebufferAndRelated();
    bool    deleteRenderPass();
};
