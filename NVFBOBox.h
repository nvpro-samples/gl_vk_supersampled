#pragma once
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
//
// Implementation of different antialiasing methods.
// - typical MSAA
// - CSAA
// - Hardware AA mixed with FBO for supersampling pass
//   - simple downsampling
//   - downsampling with 1 or 2 kernel filters
//
// AABox is the class that will handle everything related to supersampling through 
// an offscreen surface defined thanks to FBO
// Basic use is :
//
//  Initialize()
//  ...
//  Activate(int x=0, int y=0)
//	Draw the scene (so, in the offscreen supersampled buffer)
//  Deactivate()
//  Draw() : downsample to backbuffer
//  ...
//  Finish()
//
//
// Copyright (c) NVIDIA Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "GLSLShader.h"

#ifndef GL_FRAMEBUFFER_EXT
#	define GL_FRAMEBUFFER_EXT				0x8D40
typedef unsigned int GLenum;
#endif

class NVFBOBox
{
public:
	enum DownSamplingTechnique
	{
		DS1 = 0,
		DS2 = 1,
		DS3 = 2,
		NONE = 3
	};

    NVFBOBox(); 
	~NVFBOBox();

	virtual bool Initialize(int w, int h, float ssfact, int depthSamples, int coverageSamples=-1, int tilesW=1, int tilesH=1, bool bOneFBOPerTile=true);
    virtual bool resize(int w, int h, float ssfact=-1, int depthSamples_=-1, int coverageSamples_=-1);
    virtual void MakeResourcesResident();
	virtual void Finish();

	virtual int getTilesW();
	virtual int getTilesH();

	virtual int getWidth() { return width; }
	virtual int getHeight() { return height; }
	virtual int getBufferWidth() { return bufw; }
	virtual int getBufferHeight() { return bufh; }
	virtual float getSSFactor() { return scaleFactor; }

	virtual void ActivateBuffer(int tilex, int tiley, GLenum target = GL_FRAMEBUFFER);
	virtual void Activate(int tilex=0, int tiley=0, float m_frustum[][4]=NULL);
	virtual bool ResolveAA(DownSamplingTechnique technique, int tilex, int tiley);
	virtual void Deactivate();
	virtual void Draw(DownSamplingTechnique technique, int tilex, int tiley, int windowW, int windowH, float *offset);

	virtual bool PngWriteFile( const char *file);
	virtual void PngWriteData(DownSamplingTechnique technique, int tilex, int tiley);

    virtual unsigned int GetFBO(int i=0);

protected:

  bool		  bValid;
  bool		  bCSAA;

  int		   vpx, vpy, vpw, vph;
  int		   width, height;
  int		   bufw, bufh;
  int		   curtilex, curtiley;
  float			scaleFactor;
  int			depthSamples, coverageSamples;

  int		   tilesw, tilesh;
  bool			bOneFBOPerTile;

  GLSLShader downsampling[3];

  // Let's assume we only want to keep separate data for colors. Depth can be shared
  GLuint		depth_texture;
  GLuint		depth_texture_ms;
  struct TileData
  {
	  GLuint		fb;
	  GLuint		fbms;
	  GLuint		color_texture_ms;
	  GLuint		color_texture;
  };
  std::vector<TileData> tileData;

	GLint  pngDataSz;	  // size of allocated memory
	GLubyte *pngData;	  // temporary data for the full image (many tiles)
	GLubyte *pngDataTile; // temporary data from a tile

  bool		  initRT();
};
