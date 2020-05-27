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

#include "nvpwindow.hpp"

#if defined(_MSC_VER)
#  include <windows.h>
#  include <direct.h>
#endif
#include <assert.h>
#include <float.h>
#include <math.h>

#include <string.h>
#include <vector>




#ifdef USE_UNMANAGED
#  pragma managed(push,off)
#endif

#include "NVFBOBox.h"
#include <nvgl/extensions_gl.hpp>

//#include "Logging.h"
//#include "glError.h"

#include <map>

#include "helper_fbo.h"

/////////////////////////////////////////////
// 
// Methods
//
/////////////////////////////////////////////

NVFBOBox::NVFBOBox() : 
  bOneFBOPerTile(false),
  scaleFactor(1.0),
  depthSamples(0), coverageSamples(0),
  bValid(false),
  vpx(0), vpy(0), vpw(0), vph(0),
  bCSAA(false),
  bufw(0), bufh(0),
  width(0), height(0),
  curtilex(0), curtiley(0),
  //color_texture(0),  
  depth_texture(0),
  //fb(0),  fbms(0),  
  depth_texture_ms(0),  
  //color_texture_ms(0),
  pngData(NULL),
  pngDataTile(NULL),
  pngDataSz(0)
{
}
NVFBOBox::~NVFBOBox()
{
	pngDataSz = 0;
	if(pngData)
		delete []pngData;
	pngData = NULL;
	if(pngDataTile)
		delete []pngDataTile;
	pngDataTile = NULL;
}
void NVFBOBox::Finish()
{
	pngDataSz = 0;
	if(pngData)
		delete []pngData;
	pngData = NULL;
	if(pngDataTile)
		delete []pngDataTile;
	pngDataTile = NULL;
	for(unsigned int i=0; i<tileData.size(); i++)
	{
		if(tileData[i].color_texture_ms)
		  glDeleteTextures(1, &tileData[i].color_texture_ms);
		if(tileData[i].color_texture)
		  glDeleteTextures(1, &tileData[i].color_texture);
		if(tileData[i].fb)
		  glDeleteFramebuffers(1, &tileData[i].fb);
		if(tileData[i].fbms)
		  glDeleteFramebuffers(1, &tileData[i].fbms);
	}
	tileData.clear();

	if(depth_texture_ms) 
	  glDeleteTextures(1, &depth_texture_ms);
	if(depth_texture)
	  glDeleteTextures(1, &depth_texture);

	depth_texture_ms=0;
	depth_texture=0;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBox::initRT()
{
	bool multisample = depthSamples > 1;
	bool csaa = (coverageSamples > depthSamples) && (has_GL_NV_texture_multisample);
	bool ret = true;
	if(bOneFBOPerTile)
		tileData.resize(tilesw*tilesh);
	else
		tileData.resize(1);
	//loop in tiles
	for(unsigned int i=0; i<tileData.size(); i++)
	{
		//
		// init the texture that will also be the buffer to render to
		//
        tileData[i].color_texture = texture::createRGBA8(bufw, bufh, 1);
        tileData[i].fb = fbo::create();
        fbo::attachTexture2D(tileData[i].fb, tileData[i].color_texture, 0, 1);
		//
		// Handle multisample FBO's first
		//
		if (multisample)
		{
            //now handle the FBO in MS resolution
            tileData[i].fbms = fbo::create();
			// initialize color texture
            tileData[i].color_texture_ms = texture::createRGBA8(bufw, bufh, depthSamples, coverageSamples);
            fbo::attachTexture2D(tileData[i].fbms, tileData[i].color_texture_ms, 0, depthSamples);

			// bind the multisampled depth buffer
			if(depth_texture_ms == 0)
                depth_texture_ms = texture::createDST(bufw, bufh, depthSamples, bCSAA ? coverageSamples:0);
            fbo::attachDSTTexture2D(tileData[i].fbms, depth_texture_ms, depthSamples);
			fbo::CheckStatus();

		} // if (multisample)
		else // Depth buffer created without the need to resolve MSAA
		{
			// Create it one for many FBOs
			if(depth_texture == 0)
			{
                depth_texture = texture::createDST(bufw, bufh, 1, 0);
			}
            fbo::attachDSTTexture2D(tileData[i].fb, depth_texture, 1);
		}

	} // for i
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return ret;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBox::resize(int w, int h, float ssfact, int depthSamples_, int coverageSamples_)
{
	if(depthSamples_ >= 0)
		depthSamples = depthSamples_;
	if(coverageSamples_ >= 0)
    coverageSamples = coverageSamples_;
	if(ssfact >= 1.0)
		scaleFactor = ssfact;
	if(w > 0)
		width = w;
	if(h > 0)
		height = h;
	bufw = (int)(scaleFactor*(float)width);
	bufh = (int)(scaleFactor*(float)height);

    bool multisample = depthSamples > 1;
	bool csaa = (coverageSamples > depthSamples) && (has_GL_NV_texture_multisample);
	bool ret = true;

	if(depth_texture)
	{
        texture::deleteTexture(depth_texture);
        depth_texture = 0;
	}
	if(depth_texture_ms)
    {
        texture::deleteTexture(depth_texture_ms);
        depth_texture_ms = 0;
    }
	//loop in tiles
	for(unsigned int i=0; i<tileData.size(); i++)
	{
        if(tileData[i].color_texture_ms)
            texture::deleteTexture(tileData[i].color_texture_ms);
        texture::deleteTexture(tileData[i].color_texture);
        if(tileData[i].fbms)
        {
            fbo::detachColorTexture(tileData[i].fbms, 0, depthSamples);
            fbo::detachDSTTexture(tileData[i].fbms, depthSamples);
        }
        if(tileData[i].fb)
        {
            fbo::detachColorTexture(tileData[i].fb, 0, depthSamples);
            fbo::detachDSTTexture(tileData[i].fb, depthSamples);
        }
        tileData[i].color_texture = texture::createRGBA8(bufw, bufh, 1);
        fbo::attachTexture2D(tileData[i].fb, tileData[i].color_texture, 0, 1);
		if (multisample) 
		{
			// initialize color texture
            tileData[i].color_texture_ms = texture::createRGBA8(bufw, bufh, depthSamples, coverageSamples);
            fbo::attachTexture2D(tileData[i].fbms, tileData[i].color_texture_ms, 0, depthSamples);
	        if(depth_texture_ms == 0)
                depth_texture_ms = texture::createDST(bufw, bufh, depthSamples, bCSAA ? coverageSamples:0);
            fbo::attachDSTTexture2D(tileData[i].fbms, depth_texture_ms, depthSamples);
			fbo::CheckStatus();

		} // if (multisample)
		else // Depth buffer created without the need to resolve MSAA
		{
	        if(depth_texture == 0)
                depth_texture = texture::createDST(bufw, bufh, 1, 0);
            fbo::attachDSTTexture2D(tileData[i].fb, depth_texture, 1);
			fbo::CheckStatus();
		}
			
	} // for i
	return ret; // TODO: return false if failed...
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBox::Initialize(int w, int h, float ssfact, int depthSamples_, int coverageSamples_, int tilesW, int tilesH, bool bOneFBOPerTile_)
{
	Finish();

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
	// FBO
	//
	initRT();

	// 
	// GLSL things
	// 
	const char passThrough[] = 
		"void main(void)\n"
		"{\n"
		"	gl_Position = gl_Vertex;\n"
		"	gl_TexCoord[0].xy = gl_MultiTexCoord0.xy;\n"
		"}\n";
	downsampling[0].cleanup();
	//downsampling[0].addVertexShader("../EffectBox/GLSLShaders/passThrough.glsl");
	downsampling[0].addVertexShaderFromString(passThrough);
	//downsampling[0].addFragmentShader("../EffectBox/GLSLShaders/downSample1.glsl");
	downsampling[0].addFragmentShaderFromString(
		"uniform sampler2D texImage;\n"
		"void main()\n"
		"{\n"
        "	gl_FragColor = texture2D(texImage, gl_TexCoord[0].xy);\n"
		"}\n"
		);
	downsampling[1].cleanup();
	//downsampling[1].addVertexShader("../EffectBox/GLSLShaders/passThrough.glsl");
	downsampling[1].addVertexShaderFromString(passThrough);
	//downsampling[1].addFragmentShader("../EffectBox/GLSLShaders/downSample2.glsl");
	downsampling[1].addFragmentShaderFromString(
		"uniform sampler2D	texImage;\n"
		"uniform vec2		texelSize;\n"
		"void main()\n"
		"{\n"
		"	vec4 tap0 = texture2D(texImage, gl_TexCoord[0].xy);\n"
		"	vec4 tap1 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  0.4,  0.9 ));\n"
		"	vec4 tap2 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -0.4, -0.9 ));\n"
		"	vec4 tap3 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -0.9,  0.4 ));\n"
		"	vec4 tap4 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  0.9, -0.4 ));\n"
		"	gl_FragColor = 0.2 * ( tap0 + tap1 + tap2 + tap3 + tap4 );\n"
		"}\n"
		);
	downsampling[2].cleanup();
	//downsampling[2].addVertexShader("../EffectBox/GLSLShaders/passThrough.glsl");
	downsampling[2].addVertexShaderFromString(passThrough);
	//downsampling[2].addFragmentShader("../EffectBox/GLSLShaders/downSample3.glsl");
	downsampling[2].addFragmentShaderFromString(
		"uniform sampler2D	texImage;\n"
		"uniform vec2		texelSize;\n"
		"void main()\n"
		"{\n"
		"	vec4 color, color2;\n"
		"	vec4 tap0 = texture2D(texImage, gl_TexCoord[0].xy);\n"
		"	vec4 tap1 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  0.4,  0.9 ));\n"
		"	vec4 tap2 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -0.4, -0.9 ));\n"
		"	vec4 tap3 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -0.9,  0.4 ));\n"
		"	vec4 tap4 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  0.9, -0.4 ));\n"
		"	color = 0.2 * ( tap0 + tap1 + tap2 + tap3 + tap4 );\n"
		"	vec4 tap11 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  0.9,  1.9 ));\n"
		"	vec4 tap21 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -0.9, -1.9 ));\n"
		"	vec4 tap31 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2( -1.9,  0.9 ));\n"
		"	vec4 tap41 = texture2D(texImage, gl_TexCoord[0].xy + texelSize * vec2(  1.9, -0.9 ));\n"
		"	color2 = 0.2 * ( tap0 + tap11 + tap21 + tap31 + tap41 );\n"
		"	float mask = clamp(color2.w, 0.0, 1.0);\n"
        "	gl_FragColor.rgb = color.rgb * mask + color2.rgb * (1.0-mask);													\n"	
		"	gl_FragColor.w = mask;																			\n"
		"}\n"
		);
	bValid = true;
	return true;
}

void NVFBOBox::MakeResourcesResident()
{
    GLuint64 handle;
	for(unsigned int i=0; i<tileData.size(); i++)
	{
		if(tileData[i].color_texture_ms)
        {
            handle = glGetTextureHandleARB(tileData[i].color_texture_ms);
            glMakeTextureHandleResidentARB(handle);
        }
		if(tileData[i].color_texture)
        {
            handle = glGetTextureHandleARB(tileData[i].color_texture);
            glMakeTextureHandleResidentARB(handle);
        }
	}
	if(depth_texture_ms) 
    {
        handle = glGetTextureHandleARB(depth_texture_ms);
        glMakeTextureHandleResidentARB(handle);
    }
	if(depth_texture)
    {
        handle = glGetTextureHandleARB(depth_texture);
        glMakeTextureHandleResidentARB(handle);
    }
}

#if 0
#define FULLSCRQUAD(x,y)\
  float ww = 2.0f/(float)tilesw;\
  float hh = 2.0f/(float)tilesh;\
  float xx = (x*ww)-1;\
  float yy = (y*hh)-1;\
  glBegin(GL_QUADS);\
  glTexCoord2f(0,0);\
  glVertex4f(xx, yy, 0.0,1);\
  glTexCoord2f(1,0);\
  glVertex4f(xx+ww, yy,0.0,1);\
  glTexCoord2f(1,1);\
  glVertex4f(xx+ww, yy+hh,0.0,1);\
  glTexCoord2f(0,1);\
  glVertex4f(xx, yy+hh,0.0,1);\
  glEnd();
#else
#define FULLSCRQUAD(x,y)
#endif
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
bool NVFBOBox::ResolveAA(DownSamplingTechnique technique=DS1, int tilex=0, int tiley=0)
{
	if(!bValid)
		return false;
	if(tilex < 0)
		tilex = curtilex;
	else
		curtilex = tilex;
	if(tiley < 0)
		tiley = curtiley;
	else
		curtiley = tiley;

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL);
	//
	// if this FBO is multisampled, resolve it, so it can be displayed
	// the blit will allow the multisampled buffer to be stretched to a normal buffer at res bufw/bufh
	//
	int i = bOneFBOPerTile ? tilesw * tiley + tilex : 0;
    bool toBackBuffer = false;
	if( depthSamples > 1 )
	{
		glBindFramebuffer( GL_READ_FRAMEBUFFER, tileData[i].fbms);
		// scalefactor == 1.0 means that we simply need to blit things with no filtering to the backbuffer
		// We also want to not have any tiling...
        if((scaleFactor ==1.0) && (tilesw == 1) && (tilesh == 1))
            toBackBuffer = true;
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 
			toBackBuffer ? 0 : tileData[i].fb);
		glBlitFramebuffer( 0, 0, bufw, bufh, 0, 0, bufw, bufh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
    return toBackBuffer;
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void NVFBOBox::Draw(DownSamplingTechnique technique, int tilex, int tiley, int windowW, int windowH, float *offset)
{
	if(tilex < 0)
		tilex = curtilex;
	else
		curtilex = tilex;
	if(tiley < 0)
		tiley = curtiley;
	else
		curtiley = tiley;

	bool toBackBuffer = ResolveAA(technique, tilex, tiley);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// we will go through a fullscreen quad with shader if tiling or scalefactor > 1
	if((scaleFactor > 1.0) || (tilesw > 1) || (tilesh > 1))
	{
        assert(toBackBuffer == false);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		if(technique < 0) //Fallback
			technique = DS1;
		downsampling[technique].bindShader();
		downsampling[technique].bindTexture(GL_TEXTURE_2D, "texImage", tileData[bOneFBOPerTile ? tilesw * tiley + tilex : 0].color_texture, 0);
		float v2[2] = { 1.0f/(float)bufw, 1.0f/(float)bufh };
		downsampling[technique].setUniformVector("texelSize", v2, 2);
		glDepthMask(false);
        //PRINT_GL_ERROR;

		//
		// During this full screen pass, we will down-sample the buffer and
		// eventually filter it
		//
		//FULLSCRQUAD(tilex, tiley);
		float pw = 2.0f/(float)windowW;
		float ph = 2.0f/(float)windowH;
		float ww = (width * pw);
		float hh = (height * ph);
		float xx = ((((float)tilex - 0.5f*(float)tilesw)*(float)width)  * pw);
		float yy = ((((float)tiley - 0.5f*(float)tilesh)*(float)height)  * ph);
		if(offset)
		{
			xx += offset[0];
			yy += offset[1];
		}
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL);
		glBegin(GL_QUADS);
		glTexCoord2f(0,0);
		glVertex4f(xx, yy, 0.0,1);
		glTexCoord2f(1,0);
		glVertex4f(xx+ww, yy,0.0,1);
		glTexCoord2f(1,1);
		glVertex4f(xx+ww, yy+hh,0.0,1);
		glTexCoord2f(0,1);
		glVertex4f(xx, yy+hh,0.0,1);
		glEnd();
        //PRINT_GL_ERROR;

		downsampling[technique].unbindShader();
	glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
	}
	else if(!toBackBuffer)// simple case : copy the data to the backbuffer
	{
		//
		// WARNING: CREATES a MEMORY LEAK!!!
		//
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL);
		glBindFramebuffer( GL_READ_FRAMEBUFFER, tileData[0].fb);
		// scalefactor == 1.0 means that we simply need to blit things with no filtering to the backbuffer
		// We also want to not have any tiling...
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer( 0, 0, bufw, bufh, 0, 0, bufw, bufh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void NVFBOBox::ActivateBuffer(int tilex, int tiley, GLenum target)
{
	if(!bValid)
		return;
	if(tilex < 0)
		tilex = curtilex;
	else
		curtilex = tilex;
	if(tiley < 0)
		tiley = curtiley;
	else
		curtiley = tiley;

	int i = bOneFBOPerTile ? tilesw * tiley + tilex : 0;
	glBindFramebuffer(target, depthSamples > 1 ? tileData[i].fbms : tileData[i].fb);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if(GL_FRAMEBUFFER == target)
	{
		glViewport(0, 0, bufw, bufh);
	}
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void NVFBOBox::Activate(int tilex, int tiley, float m_frustum[][4])
{
	if(!bValid)
		return;
	if(tilex < 0)
		tilex = curtilex;
	else
		curtilex = tilex;
	if(tiley < 0)
		tiley = curtiley;
	else
		curtiley = tiley;

	//
	// Bind the framebuffer to render on : can be either Multisampled one or normal one
	//
	int i = bOneFBOPerTile ? tilesw * tiley + tilex : 0;
	glBindFramebuffer(GL_FRAMEBUFFER, depthSamples > 1 ? tileData[i].fbms : tileData[i].fb);

	glPushAttrib(GL_VIEWPORT_BIT); 
	glEnable(GL_MULTISAMPLE);
	glViewport(0, 0, bufw, bufh);
	//
	// Change the projection matrix
	//
	// Do this only if needed : when in tiling mode
	if((tilesw > 1)||(tilesh > 1))
	{
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		float proj[16];
		float matTiling[16] = {
			(float)tilesw,0.0f			,0.0f,0.0f,
			0.0f		 ,(float)tilesh	,0.0f,0.0f,
			0.0f		 ,0.0f			,1.0f,0.0f,
			(float)tilesw -1.0f - 2.0f*(float)tilex, 
			(float)tilesh -1.0f - 2.0f*(float)tiley, 
			0,1
		};
		glGetFloatv(GL_PROJECTION_MATRIX, proj);
		glLoadMatrixf(matTiling);
		glMultMatrixf(proj);
		glMatrixMode(GL_MODELVIEW);
		// Clipping plane computation according to the tile we are in
		float mview[16];
		float clip[16];
		tilex = tilesw-1-tilex;
		tiley = tilesh-1-tiley;
		glGetFloatv(GL_MODELVIEW_MATRIX, mview);
		glPushMatrix();
		glLoadMatrixf(proj);
		glMultMatrixf(mview);
		glGetFloatv(GL_MODELVIEW_MATRIX, clip);
		glPopMatrix();
		float	clipedges[6][2] = {
			{-1.f, 1.f-(2.f*(float)tilex/(float)tilesw)},
			{1.f,  (2.f*((float)tilex+1.f)/(float)tilesw) - 1.f},
			{-1.f, 1.f-(2.f*(float)tiley/(float)tilesh)},
			{1.f,  (2.f*((float)tiley+1.f)/(float)tilesh) - 1.f},
			{-1.f, 1.0f}, {1.f, 1.0f} };
		for(int i = 0; i < 6; ++i)
		{
			int axis = i / 2;

			for(int j = 0; j < 4; ++j)
				m_frustum[i][j] = 
						clipedges[i][0]*clip[j*4+axis] // Directional vector of plane eq.
						+ clipedges[i][1]*clip[j*4+3]; // offset of plane eq.

			//Normalize the plane
			float invLength = 1.f / sqrtf((m_frustum[i][0]*m_frustum[i][0])+(m_frustum[i][1]*m_frustum[i][1])+(m_frustum[i][2]*m_frustum[i][2]));
			
			for(int j = 0; j < 4; ++j)
				m_frustum[i][j] *= invLength;
		}

	}
}
/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void NVFBOBox::Deactivate()
{
	if(!bValid)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glPopAttrib();
	// Do this only if needed : when in tiling mode
	if((tilesw > 1)||(tilesh > 1))
	{
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	}
}

/*-------------------------------------------------------------------------
  # of tiles in W and H - doesn't return the width and height...
  -------------------------------------------------------------------------*/
int NVFBOBox::getTilesW()
{
	return tilesw;
}
int NVFBOBox::getTilesH()
{
	return tilesh;
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
#ifdef USEPNG
#include "png.h"
void pngWriteFn( png_structp png_ptr, png_bytep data, png_size_t length) {
	FILE* fp = (FILE*)png_get_io_ptr(png_ptr);

	if (!data)
		png_error( png_ptr, "Attempt to write to null file pointer");

	fwrite( data, length, 1, fp);
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void pngFlushFn( png_structp png_ptr) {
	FILE* fp = (FILE*)png_get_io_ptr(png_ptr);

	fflush(fp);
}

/*-------------------------------------------------------------------------

  -------------------------------------------------------------------------*/
void errorFn (png_structp p, png_const_charp pchr)
{
	LOGE("Error : %s\n", pchr);
}
#endif
/*-------------------------------------------------------------------------

- Note: DownSamplingTechnique technique currently not used...
In fact, the kernel filtering for downsampling is not used at all when in tiling mode.
The downsampling is only used when doing a Fullscreen quad rendering in a render target...
This is only done in Draw(), For now. Not sure this is needed for our tiling stuff.
However, MSAA is working as expected.
  -------------------------------------------------------------------------*/
void NVFBOBox::PngWriteData(DownSamplingTechnique technique, int tilex, int tiley)
{
#ifdef USEPNG
	int bit_depth = 8;
	int color_type = PNG_COLOR_TYPE_RGB;
	int row_bytes = bufw * 3 * (bit_depth >> 3);

	if(tilex < 0)
		tilex = curtilex;
	else
		curtilex = tilex;
	if(tiley < 0)
		tiley = curtiley;
	else
		curtiley = tiley;

	if((pngDataSz < (row_bytes * bufh * tilesw * tilesh))||(!pngData))
	{
		if(pngData) delete []pngData;
		pngData = new GLubyte[row_bytes * bufh * tilesw * tilesh];
		//zeromemory(pngData, row_bytes * bufh * tilesw * tilesh);
		pngDataSz = row_bytes * bufh * tilesw * tilesh;
	}
	if(!pngDataTile)
	{
		pngDataTile = new GLubyte[row_bytes * bufh];
		//zeromemory(pngData, row_bytes * bufh * tilesh);
	}
	// Shall we do ResolveAA(DownSamplingTechnique technique=DS1, int tilex=0, int tiley=0) ?
	// Right now : previous Draw() call made it...
	glBindFramebuffer(GL_FRAMEBUFFER, tileData[bOneFBOPerTile ? tilex + tiley*tilesw : 0].fb);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glReadPixels(0,0,bufw,bufh,GL_RGB,GL_UNSIGNED_BYTE, pngDataTile);
	// Maybe we should put back the state that currently was before I changed it...
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	for(int y=0; y<bufh; y++)
	{
		memcpy(pngData + tilex*row_bytes + (bufh*tiley+y)*row_bytes*tilesw, 
			pngDataTile + y*row_bytes, row_bytes);
	}
#endif
}
/*-------------------------------------------------------------------------
//  writePng
//
//	Image saver function for png files. The code is heavily
//  based on the example png save code distributed with libPNG
  -------------------------------------------------------------------------*/
#if 1
static int counter = 0;
bool NVFBOBox::PngWriteFile( const char *file)
{
#ifdef USEPNG
	int bit_depth = 8;
	int color_type = PNG_COLOR_TYPE_RGB;
	int row_bytes = tilesw * bufw * 3 * (bit_depth >> 3);
	if((pngDataSz < (row_bytes * bufh * tilesh))||(!pngData))
		return false;
	char tmpname[100];
	sprintf(tmpname, "%s_%d.png", file, counter++);
	FILE *fp = fopen( tmpname, "wb");

	if ( !fp)
		return false;

	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, &errorFn, NULL);
	
	if (!png_ptr) {
		fclose(fp);
		return false; /* out of memory */
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, NULL);
		fclose(fp);
		return false; /* out of memory */
	}


	// setjmp() is used for error handling with libPNG, if something goes wrong it is coming back here

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		return false;
	}

	// Need to override the standard I/O methods since libPNG may be linked against a different run-time
	png_set_write_fn( png_ptr, fp, pngWriteFn, pngFlushFn);

	png_set_IHDR(png_ptr, info_ptr, tilesw*bufw, tilesh*bufh, bit_depth, color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png_ptr, info_ptr);
	for ( int ii = 0;  ii < tilesh*bufh; ii++) 
	{
		png_write_row(png_ptr, pngData + (tilesh*bufh - 1 - ii)*row_bytes);
	}

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	fclose(fp);

	return true;
#else
	return false;
#endif
}

unsigned int NVFBOBox::GetFBO(int i)
{
    return depthSamples > 1 ? tileData[i].fbms : tileData[i].fb;
}

#endif
