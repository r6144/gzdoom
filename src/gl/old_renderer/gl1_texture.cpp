/*
** gltexture.cpp
** The texture classes for hardware rendering
** (Even though they are named 'gl' there is nothing hardware dependent
**  in this file. That is all encapsulated in the GLTexture class.)
**
**---------------------------------------------------------------------------
** Copyright 2004-2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "gl/gl_include.h"
#include "w_wad.h"
#include "m_png.h"
#include "r_draw.h"
#include "sbar.h"
#include "gi.h"
#include "cmdlib.h"
#include "stats.h"
#include "r_main.h"
#include "templates.h"
#include "sc_man.h"
#include "r_translate.h"
#include "colormatcher.h"

#include "gl/gl_struct.h"
#include "gl/gl_data.h"
#include "gl/gl_intern.h"
#include "gl/gl_framebuffer.h"
#include "gl/old_renderer/gl1_texture.h"
#include "gl/gl_functions.h"
#include "gl/old_renderer/gl1_shader.h"
#include "gl/gl_translate.h"
#include "gl/common/glc_texture.h"

EXTERN_CVAR(Bool, gl_render_precise)
EXTERN_CVAR(Int, gl_fogmode)
EXTERN_CVAR(Int, gl_lightmode)
EXTERN_CVAR(Bool, gl_precache)
EXTERN_CVAR(Bool, gl_texture_usehires)

extern bool HasGlobalBrightmap;
extern FRemapTable GlobalBrightmap;

namespace GLRendererOld
{

//===========================================================================
// 
// multi-format pixel copy with colormap application
// requires one of the previously defined conversion classes to work
//
//===========================================================================
template<class T>
void iCopyColors(unsigned char * pout, const unsigned char * pin, int cm, int count, int step)
{
	int i;
	int fac;

	switch(cm)
	{
	case CM_DEFAULT:
		for(i=0;i<count;i++)
		{
			pout[0]=T::R(pin);
			pout[1]=T::G(pin);
			pout[2]=T::B(pin);
			pout[3]=T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_INVERT:
		// Doom's inverted invulnerability map
		for(i=0;i<count;i++)
		{
			gl_InverseMap(T::Gray(pin), pout[0], pout[1], pout[2]);
			pout[3] = T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_GOLDMAP:
		// Heretic's golden invulnerability map
		for(i=0;i<count;i++)
		{
			gl_GoldMap(T::Gray(pin), pout[0], pout[1], pout[2]);
			pout[3] = T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_REDMAP:
		// Skulltag's red Doomsphere map
		for(i=0;i<count;i++)
		{
			gl_RedMap(T::Gray(pin), pout[0], pout[1], pout[2]);
			pout[3] = T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_GREENMAP:
		// Skulltags's Guardsphere map
		for(i=0;i<count;i++)
		{
			gl_GreenMap(T::Gray(pin), pout[0], pout[1], pout[2]);
			pout[3] = T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_GRAY:
		// this is used for colorization of blood.
		// To get the best results the brightness is taken from 
		// the most intense component and not averaged because that would be too dark.
		for(i=0;i<count;i++)
		{
			pout[0] = pout[1] = pout[2] = MAX(MAX(T::R(pin), T::G(pin)), T::B(pin));
			pout[3] = T::A(pin);
			pout+=4;
			pin+=step;
		}
		break;

	case CM_ICE:
		// Create the ice translation table, based on Hexen's.
		// Since this is done in True Color the purplish tint is fully preserved - even in Doom!
		for(i=0;i<count;i++)
		{
			int gray = T::Gray(pin)>>4;

			pout[0] = IcePalette[gray][0];
			pout[1] = IcePalette[gray][1];
			pout[2] = IcePalette[gray][2];
			pout[3] = 255;
			pout+=4;
			pin+=step;
		}
		break;

	case CM_SHADE:
		// Alpha shade uses the red channel for true color pics
		for(i=0;i<count;i++)
		{
			pout[0] = pout[1] =	pout[2] = 255;
			pout[3] = T::R(pin);
			pout+=4;
			pin+=step;
		}
		break;
	
	default:
		if (cm<=CM_DESAT31)
		{
			// Desaturated light settings.
			fac=cm-CM_DESAT0;
			for(i=0;i<count;i++)
			{
				gl_Desaturate(T::Gray(pin), T::R(pin), T::G(pin), T::B(pin), pout[0], pout[1], pout[2], fac);
				pout[3] = T::A(pin);
				pout+=4;
				pin+=step;
			}
		}
		break;
	}
}

typedef void (*CopyFunc)(unsigned char * pout, const unsigned char * pin, int cm, int count, int step);

static CopyFunc copyfuncs[]={
	iCopyColors<cRGB>,
	iCopyColors<cRGBA>,
	iCopyColors<cIA>,
	iCopyColors<cCMYK>,
	iCopyColors<cBGR>,
	iCopyColors<cBGRA>,
	iCopyColors<cI16>,
	iCopyColors<cRGB555>,
	iCopyColors<cPalEntry>
};

//===========================================================================
//
// True Color texture copy function
// This excludes all the cases that force downconversion to the
// base palette because they wouldn't be used anyway.
//
//===========================================================================
void FGLBitmap::CopyPixelDataRGB(int originx, int originy,
								const BYTE * patch, int srcwidth, int srcheight, int step_x, int step_y,
								int rotate, int ct, FCopyInfo *inf)
{
	if (ClipCopyPixelRect(Width, Height, originx, originy, patch, srcwidth, srcheight, step_x, step_y, rotate))
	{
		BYTE *buffer = GetPixels() + 4*originx + Pitch*originy;
		for (int y=0;y<srcheight;y++)
		{
			copyfuncs[ct](&buffer[y*Pitch], &patch[y*step_y], cm, srcwidth, step_x);
		}
	}
}

//===========================================================================
// 
// Creates one of the special palette translations for the given palette
//
//===========================================================================
void ModifyPalette(PalEntry * pout, PalEntry * pin, int cm, int count)
{
	int i;
	int fac;

	switch(cm)
	{
	case CM_DEFAULT:
		if (pin != pout)
			memcpy(pout, pin, count * sizeof(PalEntry));
		break;

	case CM_INVERT:
		// Doom's inverted invulnerability map
		for(i=0;i<count;i++)
		{
			int gray = (pin[i].r*77 + pin[i].g*143 + pin[i].b*37) >> 8;
			gl_InverseMap(gray, pout[i].r, pout[i].g, pout[i].b);
			pout[i].a = pin[i].a;
		}
		break;

	case CM_GOLDMAP:
		// Heretic's golden invulnerability map
		for(i=0;i<count;i++)
		{
			int gray = (pin[i].r*77 + pin[i].g*143 + pin[i].b*37) >> 8;
			gl_GoldMap(gray, pout[i].r, pout[i].g, pout[i].b);
			pout[i].a = pin[i].a;
		}
		break;

	case CM_REDMAP:
		// Skulltag's red Doomsphere map
		for(i=0;i<count;i++)
		{
			int gray = (pin[i].r*77 + pin[i].g*143 + pin[i].b*37) >> 8;
			gl_RedMap(gray, pout[i].r, pout[i].g, pout[i].b);
			pout[i].a = pin[i].a;
		}
		break;

	case CM_GREENMAP:
		// Skulltags's Guardsphere map
		for(i=0;i<count;i++)
		{
			int gray = (pin[i].r*77 + pin[i].g*143 + pin[i].b*37) >> 8;
			gl_GreenMap(gray, pout[i].r, pout[i].g, pout[i].b);
			pout[i].a = pin[i].a;
		}
		break;

	case CM_GRAY:
		// this is used for colorization of blood.
		// To get the best results the brightness is taken from 
		// the most intense component and not averaged because that would be too dark.
		for(i=0;i<count;i++)
		{
			pout[i].r = pout[i].g = pout[i].b = max(max(pin[i].r, pin[i].g), pin[i].b);
			pout[i].a = pin[i].a;
		}
		break;

	case CM_ICE:
		// Create the ice translation table, based on Hexen's.
		// Since this is done in True Color the purplish tint is fully preserved - even in Doom!
		for(i=0;i<count;i++)
		{
			int gray=(pin[i].r*77 + pin[i].g*143 + pin[i].b*37)>>12;

			pout[i].r = IcePalette[gray][0];
			pout[i].g = IcePalette[gray][1];
			pout[i].b = IcePalette[gray][2];
			pout[i].a = pin[i].a;
		}
		break;
	
	default:
		// Boom colormaps.
		if (cm>=CM_FIRSTCOLORMAP && cm<CM_FIRSTCOLORMAP+numfakecmaps)
		{
			if (count<=256)	// This does not work for raw image data because it assumes
							// the use of the base palette!
			{
				// CreateTexBuffer has already taken care of needed palette mapping so this
				// buffer is guaranteed to be in the base palette.
				byte * cmapp = &realcolormaps [NUMCOLORMAPS*256*(cm - CM_FIRSTCOLORMAP)];

				for(i=0;i<count;i++)
				{
					pout[i].r = GPalette.BaseColors[*cmapp].r;
					pout[i].g = GPalette.BaseColors[*cmapp].g;
					pout[i].b = GPalette.BaseColors[*cmapp].b;
					pout[i].a = pin[i].a;
					cmapp++;
				}
			}
			else if (pin != pout)
			{
				// Boom colormaps cannot be applied to hires texture replacements.
				// For those you have to set the colormap usage to  'blend'.
				memcpy(pout, pin, count * sizeof(PalEntry));
			}
		}
		else if (cm<=CM_DESAT31)
		{
			// Desaturated light settings.
			fac=cm-CM_DESAT0;
			for(i=0;i<count;i++)
			{
				int gray=(pin[i].r*77 + pin[i].g*143 + pin[i].b*36)>>8;
				gl_Desaturate(gray, pin[i].r, pin[i].g, pin[i].b, pout[i].r, pout[i].g, pout[i].b, fac);
				pout[i].a = pin[i].a;
			}
		}
		else if (pin!=pout)
		{
			memcpy(pout, pin, count * sizeof(PalEntry));
		}
		break;
	}
}


//===========================================================================
//
// Paletted to True Color texture copy function
//
//===========================================================================
void FGLBitmap::CopyPixelData(int originx, int originy, const BYTE * patch, int srcwidth, int srcheight, 
										int step_x, int step_y, int rotate, PalEntry * palette, FCopyInfo *inf)
{
	PalEntry penew[256];

	int x,y,pos,i;

	if (ClipCopyPixelRect(Width, Height, originx, originy, patch, srcwidth, srcheight, step_x, step_y, rotate))
	{
		BYTE *buffer = GetPixels() + 4*originx + Pitch*originy;

		// CM_SHADE is an alpha map with 0==transparent and 1==opaque
		if (cm == CM_SHADE) 
		{
			for(int i=0;i<256;i++) 
			{
				if (palette[i].a != 0)
					penew[i]=PalEntry(i, 255,255,255);
				else
					penew[i]=PalEntry(0,255,255,255);	// If the palette contains transparent colors keep them.
			}
		}
		else
		{
			// apply any translation. 
			// The ice and blood color translations are done directly
			// because that yields better results.
			switch(translation)
			{
			case CM_GRAY:
				ModifyPalette(penew, palette, CM_GRAY, 256);
				break;

			case CM_ICE:
				ModifyPalette(penew, palette, CM_ICE, 256);
				break;

			default:
			{
				PalEntry *ptrans = GLTranslationPalette::GetPalette(translation);
				if (ptrans)
				{
					for(i = 0; i < 256; i++)
					{
						penew[i] = (ptrans[i]&0xffffff) | (palette[i]&0xff000000);
					}
					break;
				}
			}

			case 0:
				memcpy(penew, palette, 256*sizeof(PalEntry));
				break;
			}
			if (cm!=0)
			{
				// Apply color modifications like invulnerability, desaturation and Boom colormaps
				ModifyPalette(penew, penew, cm, 256);
			}
		}
		// Now penew contains the actual palette that is to be used for creating the image.

		// convert the image according to the translated palette.
		// Please note that the alpha of the passed palette is inverted. This is
		// so that the base palette can be used without constantly changing it.
		// This can also handle full PNG translucency.
		for (y=0;y<srcheight;y++)
		{
			pos=(y*Pitch);
			for (x=0;x<srcwidth;x++,pos+=4)
			{
				int v=(unsigned char)patch[y*step_y+x*step_x];
				if (penew[v].a!=0)
				{
					buffer[pos]   = penew[v].r;
					buffer[pos+1] = penew[v].g;
					buffer[pos+2] = penew[v].b;
					buffer[pos+3] = penew[v].a;
				}
				/*
				else if (penew[v].a!=255)
				{
					buffer[pos  ] = (buffer[pos  ] * penew[v].a + penew[v].r * (1-penew[v].a)) / 255;
					buffer[pos+1] = (buffer[pos+1] * penew[v].a + penew[v].g * (1-penew[v].a)) / 255;
					buffer[pos+2] = (buffer[pos+2] * penew[v].a + penew[v].b * (1-penew[v].a)) / 255;
					buffer[pos+3] = clamp<int>(buffer[pos+3] + (( 255-buffer[pos+3]) * (255-penew[v].a))/255, 0, 255);
				}
				*/
			}
		}
	}
}


//===========================================================================
//
// The GL texture maintenance class
//
//===========================================================================
TArray<FGLTexture *> FGLTexture::gltextures;

//===========================================================================
//
// Constructor
//
//===========================================================================
FGLTexture::FGLTexture(FTexture * tx)
{
	tex = tx;

	glpatch=NULL;
	gltexture=NULL;

	HiresLump=-1;
	hirestexture = NULL;

	areacount = 0;
	areas = NULL;
	createWarped = false;

	bHasColorkey = false;

	tempScaleX = tempScaleY = FRACUNIT;

	for (int i=GLUSE_PATCH; i<=GLUSE_TEXTURE; i++)
	{
		Width[i] = tex->GetWidth();
		Height[i] = tex->GetHeight();
		LeftOffset[i] = tex->LeftOffset;
		TopOffset[i] = tex->TopOffset;
		RenderWidth[i] = tex->GetScaledWidth();
		RenderHeight[i] = tex->GetScaledHeight();
	}

	scalex = tex->xScale/(float)FRACUNIT;
	scaley = tex->yScale/(float)FRACUNIT;

	// a little adjustment to make sprites look better with texture filtering:
	// create a 1 pixel wide empty frame around them.
	if (tex->UseType == FTexture::TEX_Sprite || 
		tex->UseType == FTexture::TEX_SkinSprite || 
		tex->UseType == FTexture::TEX_Decal)
	{
		if (!tex->bWarped)
		{
			RenderWidth[GLUSE_PATCH]+=2;
			RenderHeight[GLUSE_PATCH]+=2;
			Width[GLUSE_PATCH]+=2;
			Height[GLUSE_PATCH]+=2;
			LeftOffset[GLUSE_PATCH]+=1;
			TopOffset[GLUSE_PATCH]+=1;
		}
	}

	if (!tex->gl_info.bBrightmapChecked)
	{
		// Check for brightmaps
		if ((gl.flags & RFL_GLSL) && tx->UseBasePalette() && HasGlobalBrightmap &&
			tx->UseType != FTexture::TEX_Autopage && tx->UseType != FTexture::TEX_Decal &&
			tx->UseType != FTexture::TEX_MiscPatch && tx->UseType != FTexture::TEX_FontChar &&
			tex->gl_info.Brightmap == NULL && tx->bWarped == 0
			) 
		{
			// May have one - let's check when we use this texture
			tex->gl_info.bBrightmapChecked = -1;
		}
		else
		{
			// does not have one so set the flag to 'done'
			tex->gl_info.bBrightmapChecked = 1;
		}
	}
	bIsTransparent = -1;

	if (tex->bHasCanvas) scaley=-scaley;

	index = gltextures.Push(this);

}

//===========================================================================
//
// Destructor
//
//===========================================================================

FGLTexture::~FGLTexture()
{
	Clean(true);
	if (areas) delete [] areas;
	if (hirestexture) delete hirestexture;

	for(unsigned i=0;i<gltextures.Size();i++)
	{
		if (gltextures[i]==this) 
		{
			gltextures.Delete(i);
			break;
		}
	}
}


//===========================================================================
//
// Sets a temporary scaling factor for this texture
//
//===========================================================================

void FGLTexture::SetWallScaling(fixed_t x, fixed_t y)
{
	if (x != tempScaleX)
	{
		fixed_t scale_x = FixedMul(x, tex->xScale);
		int foo = (Width[GLUSE_TEXTURE] << 17) / scale_x; 
		RenderWidth[GLUSE_TEXTURE] = (foo >> 1) + (foo & 1); 
		scalex = scale_x/(float)FRACUNIT;
		tempScaleX = x;
	}
	if (y != tempScaleY)
	{
		fixed_t scale_y = FixedMul(y, tex->yScale);
		int foo = (Width[GLUSE_TEXTURE] << 17) / scaley; 
		RenderHeight[GLUSE_TEXTURE] = (foo >> 1) + (foo & 1); 
		scaley = scale_y/(float)FRACUNIT;
		tempScaleY = y;
	}
}

//===========================================================================
//
//
//
//===========================================================================

fixed_t FGLTexture::RowOffset(fixed_t rowoffset) const
{
	if (tempScaleX == FRACUNIT)
	{
		if (scaley==1.f || tex->bWorldPanning) return rowoffset;
		else return quickertoint(rowoffset/scaley);
	}
	else
	{
		if (tex->bWorldPanning) return FixedDiv(rowoffset, tempScaleY);
		else return quickertoint(rowoffset/scaley);
	}
}

//===========================================================================
//
//
//
//===========================================================================

fixed_t FGLTexture::TextureOffset(fixed_t textureoffset) const
{
	if (tempScaleX == FRACUNIT)
	{
		if (scalex==1.f || tex->bWorldPanning) return textureoffset;
		else return quickertoint(textureoffset/scalex);
	}
	else
	{
		if (tex->bWorldPanning) return FixedDiv(textureoffset, tempScaleX);
		else return quickertoint(textureoffset/scalex);
	}
}


//===========================================================================
//
// Returns the size for which texture offset coordinates are used.
//
//===========================================================================

fixed_t FGLTexture::TextureAdjustWidth(ETexUse i) const
{
	if (tex->bWorldPanning) 
	{
		if (i == GLUSE_PATCH || tempScaleX == FRACUNIT) return RenderWidth[i];
		else return FixedDiv(Width[i], tempScaleX);
	}
	else return Width[i];
}


//===========================================================================
//
// GetRect
//
//===========================================================================

void FGLTexture::GetRect(GL_RECT * r, FGLTexture::ETexUse i) const
{
	r->left=-(float)GetScaledLeftOffset(i);
	r->top=-(float)GetScaledTopOffset(i);
	r->width=(float)TextureWidth(i);
	r->height=(float)TextureHeight(i);
}

//==========================================================================
//
// Checks for the presence of a hires texture replacement and loads it
//
//==========================================================================
unsigned char *FGLTexture::LoadHiresTexture(int *width, int *height, int cm)
{
	if (HiresLump==-1) 
	{
		bHasColorkey = false;
		HiresLump = CheckDDPK3(tex);
		if (HiresLump < 0) HiresLump = CheckExternalFile(tex, bHasColorkey);

		if (HiresLump >=0) 
		{
			hirestexture = FTexture::CreateTexture(HiresLump, FTexture::TEX_Any);
		}
	}
	if (hirestexture != NULL)
	{
		int w=hirestexture->GetWidth();
		int h=hirestexture->GetHeight();

		unsigned char * buffer=new unsigned char[w*(h+1)*4];
		memset(buffer, 0, w * (h+1) * 4);

		FGLBitmap bmp(buffer, w*4, w, h);
		bmp.SetTranslationInfo(cm);

		
		int trans = hirestexture->CopyTrueColorPixels(&bmp, 0, 0);
		CheckTrans(buffer, w*h, trans);

		if (bHasColorkey)
		{
			// This is a crappy Doomsday color keyed image
			// We have to remove the key manually. :(
			DWORD * dwdata=(DWORD*)buffer;
			for (int i=(w*h);i>0;i--)
			{
				if (dwdata[i]==0xffffff00 || dwdata[i]==0xffff00ff) dwdata[i]=0;
			}
		}
		*width = w;
		*height = h;
		return buffer;
	}
	return NULL;
}

//===========================================================================
// 
//	Finds gaps in the texture which can be skipped by the renderer
//  This was mainly added to speed up one area in E4M6 of 007LTSD
//
//===========================================================================
bool FGLTexture::FindHoles(const unsigned char * buffer, int w, int h)
{
	const unsigned char * li;
	int y,x;
	int startdraw,lendraw;
	int gaps[5][2];
	int gapc=0;


	// already done!
	if (areacount) return false;
	if (tex->UseType==FTexture::TEX_Flat) return false;	// flats don't have transparent parts
	areacount=-1;	//whatever happens next, it shouldn't be done twice!

	// large textures are excluded for performance reasons
	if (h>256) return false;	

	startdraw=-1;
	lendraw=0;
	for(y=0;y<h;y++)
	{
		li=buffer+w*y*4+3;

		for(x=0;x<w;x++,li+=4)
		{
			if (*li!=0) break;
		}

		if (x!=w)
		{
			// non - transparent
			if (startdraw==-1) 
			{
				startdraw=y;
				// merge transparent gaps of less than 16 pixels into the last drawing block
				if (gapc && y<=gaps[gapc-1][0]+gaps[gapc-1][1]+16)
				{
					gapc--;
					startdraw=gaps[gapc][0];
					lendraw=y-startdraw;
				}
				if (gapc==4) return false;	// too many splits - this isn't worth it
			}
			lendraw++;
		}
		else if (startdraw!=-1)
		{
			if (lendraw==1) lendraw=2;
			gaps[gapc][0]=startdraw;
			gaps[gapc][1]=lendraw;
			gapc++;

			startdraw=-1;
			lendraw=0;
		}
	}
	if (startdraw!=-1)
	{
		gaps[gapc][0]=startdraw;
		gaps[gapc][1]=lendraw;
		gapc++;
	}
	if (startdraw==0 && lendraw==h) return false;	// nothing saved so don't create a split list

	GL_RECT * rcs=new GL_RECT[gapc];

	for(x=0;x<gapc;x++)
	{
		// gaps are stored as texture (u/v) coordinates
		rcs[x].width=rcs[x].left=-1.0f;
		rcs[x].top=(float)gaps[x][0]/(float)h;
		rcs[x].height=(float)gaps[x][1]/(float)h;
	}
	areas=rcs;
	areacount=gapc;

	return false;
}


//===========================================================================
// 
// smooth the edges of transparent fields in the texture
// returns false when nothing is manipulated to save the work on further
// levels

// 28/10/2003: major optimization: This function was far too pedantic.
// taking the value of one of the neighboring pixels is fully sufficient
//
//===========================================================================
#ifdef WORDS_BIGENDIAN
#define MSB 0
#define SOME_MASK 0xffffff00
#else
#define MSB 3
#define SOME_MASK 0x00ffffff
#endif

#define CHKPIX(ofs) (l1[(ofs)*4+MSB]==255 ? (( ((DWORD*)l1)[0] = ((DWORD*)l1)[ofs]&SOME_MASK), trans=true ) : false)

bool FGLTexture::SmoothEdges(unsigned char * buffer,int w, int h, bool clampsides)
{
  int x,y;
  bool trans=buffer[MSB]==0; // If I set this to false here the code won't detect textures 
                                // that only contain transparent pixels.
  unsigned char * l1;

  if (h<=1 || w<=1) return false;  // makes (a) no sense and (b) doesn't work with this code!

  l1=buffer;


  if (l1[MSB]==0 && !CHKPIX(1)) CHKPIX(w);
  l1+=4;
  for(x=1;x<w-1;x++, l1+=4)
  {
    if (l1[MSB]==0 &&  !CHKPIX(-1) && !CHKPIX(1)) CHKPIX(w);
  }
  if (l1[MSB]==0 && !CHKPIX(-1)) CHKPIX(w);
  l1+=4;

  for(y=1;y<h-1;y++)
  {
    if (l1[MSB]==0 && !CHKPIX(-w) && !CHKPIX(1)) CHKPIX(w);
    l1+=4;
    for(x=1;x<w-1;x++, l1+=4)
    {
      if (l1[MSB]==0 &&  !CHKPIX(-w) && !CHKPIX(-1) && !CHKPIX(1)) CHKPIX(w);
    }
    if (l1[MSB]==0 && !CHKPIX(-w) && !CHKPIX(-1)) CHKPIX(w);
    l1+=4;
  }

  if (l1[MSB]==0 && !CHKPIX(-w)) CHKPIX(1);
  l1+=4;
  for(x=1;x<w-1;x++, l1+=4)
  {
    if (l1[MSB]==0 &&  !CHKPIX(-w) && !CHKPIX(-1)) CHKPIX(1);
  }
  if (l1[MSB]==0 && !CHKPIX(-w)) CHKPIX(-1);

  return trans;
}

//===========================================================================
// 
// Checks if the texture has a default brightmap and creates it if so
//
//===========================================================================
void FGLTexture::CreateDefaultBrightmap()
{
	const BYTE *texbuf = tex->GetPixels();
	const int white = ColorMatcher.Pick(255,255,255);

	int size = tex->GetWidth()*tex->GetHeight();
	for(int i=0;i<size;i++)
	{
		if (GlobalBrightmap.Remap[texbuf[i]] == white)
		{
			// Create a brightmap
			DPrintf("brightmap created for texture '%s'\n", tex->Name);
			tex->gl_info.Brightmap = new FBrightmapTexture(tex);
			tex->gl_info.bBrightmapChecked = 1;
			return;
		}
	}
	// No bright pixels found
	DPrintf("No bright pixels found in texture '%s'\n", tex->Name);
	tex->gl_info.bBrightmapChecked = 1;
}

//===========================================================================
// 
// Post-process the texture data after the buffer has been created
//
//===========================================================================

bool FGLTexture::ProcessData(unsigned char * buffer, int w, int h, int cm, bool ispatch)
{
	if (tex->bMasked && !tex->gl_info.bBrightmap) 
	{
		tex->bMasked=SmoothEdges(buffer, w, h, ispatch);
		if (tex->bMasked && !ispatch) FindHoles(buffer, w, h);
	}
	return true;
}


//===========================================================================
// 
//  Checks for transparent pixels if there is no simpler means to get
//  this information
//
//===========================================================================
void FGLTexture::CheckTrans(unsigned char * buffer, int size, int trans)
{
	if (bIsTransparent == -1) 
	{
		bIsTransparent = trans;
		if (trans == -1)
		{
			DWORD * dwbuf = (DWORD*)buffer;
			if (bIsTransparent == -1) for(int i=0;i<size;i++)
			{
				DWORD alpha = dwbuf[i]>>24;

				if (alpha != 0xff && alpha != 0)
				{
					bIsTransparent = 1;
					return;
				}
			}
		}
		bIsTransparent = 0;
	}
}

//===========================================================================
// 
//	Deletes all allocated resources
//
//===========================================================================

void FGLTexture::Clean(bool all)
{
	WorldTextureInfo::Clean(all);
	PatchTextureInfo::Clean(all);
	createWarped = false;
}

//===========================================================================
//
// FGLTexture::WarpBuffer
//
//===========================================================================

BYTE *FGLTexture::WarpBuffer(BYTE *buffer, int Width, int Height, int warp)
{
	DWORD *in = (DWORD*)buffer;
	DWORD *out = (DWORD*)new BYTE[4*Width*Height];
	float Speed = static_cast<FWarpTexture*>(tex)->GetSpeed();

	static_cast<FWarpTexture*>(tex)->GenTime = r_FrameTime;

	static DWORD linebuffer[256];	// anything larger will bring down performance so it is excluded above.
	DWORD timebase = DWORD(r_FrameTime*Speed*23/28);
	int xsize = Width;
	int ysize = Height;
	int xmask = xsize - 1;
	int ymask = ysize - 1;
	int ds_xbits;
	int i,x;

	if (warp == 1)
	{
		for(ds_xbits=-1,i=Width; i; i>>=1, ds_xbits++);

		for (x = xsize-1; x >= 0; x--)
		{
			int yt, yf = (finesine[(timebase+(x+17)*128)&FINEMASK]>>13) & ymask;
			const DWORD *source = in + x;
			DWORD *dest = out + x;
			for (yt = ysize; yt; yt--, yf = (yf+1)&ymask, dest += xsize)
			{
				*dest = *(source+(yf<<ds_xbits));
			}
		}
		timebase = DWORD(r_FrameTime*Speed*32/28);
		int y;
		for (y = ysize-1; y >= 0; y--)
		{
			int xt, xf = (finesine[(timebase+y*128)&FINEMASK]>>13) & xmask;
			DWORD *source = out + (y<<ds_xbits);
			DWORD *dest = linebuffer;
			for (xt = xsize; xt; xt--, xf = (xf+1)&xmask)
			{
				*dest++ = *(source+xf);
			}
			memcpy (out+y*xsize, linebuffer, xsize*sizeof(DWORD));
		}
	}
	else
	{
		int ybits;
		for(ybits=-1,i=ysize; i; i>>=1, ybits++);

		DWORD timebase = (r_FrameTime * Speed * 40 / 28);
		for (x = xsize-1; x >= 0; x--)
		{
			for (int y = ysize-1; y >= 0; y--)
			{
				int xt = (x + 128
					+ ((finesine[(y*128 + timebase*5 + 900) & 8191]*2)>>FRACBITS)
					+ ((finesine[(x*256 + timebase*4 + 300) & 8191]*2)>>FRACBITS)) & xmask;
				int yt = (y + 128
					+ ((finesine[(y*128 + timebase*3 + 700) & 8191]*2)>>FRACBITS)
					+ ((finesine[(x*256 + timebase*4 + 1200) & 8191]*2)>>FRACBITS)) & ymask;
				const DWORD *source = in + (xt << ybits) + yt;
				DWORD *dest = out + (x << ybits) + y;
				*dest = *source;
			}
		}
	}
	delete [] buffer;
	return (BYTE*)out;
}

//===========================================================================
// 
//	Initializes the buffer for the texture data
//
//===========================================================================

unsigned char * FGLTexture::CreateTexBuffer(ETexUse use, int _cm, int translation, int & w, int & h, bool allowhires)
{
	unsigned char * buffer;
	intptr_t cm = _cm;
	int W, H;


	// Textures that are already scaled in the texture lump will not get replaced
	// by hires textures
	if (gl_texture_usehires && allowhires && scalex==1.f && scaley==1.f)
	{
		buffer = LoadHiresTexture (&w, &h, _cm);
		if (buffer)
		{
			return buffer;
		}
	}

	W = w = Width[use];
	H = h = Height[use];


	buffer=new unsigned char[W*(H+1)*4];
	memset(buffer, 0, W * (H+1) * 4);

	FGLBitmap bmp(buffer, W*4, W, H);
	bmp.SetTranslationInfo(cm, translation);

	if (tex->bComplex)
	{
		FBitmap imgCreate;

		// The texture contains special processing so it must be composited using with the
		// base bitmap class and then be converted as a whole.
		if (imgCreate.Create(W, H))
		{
			memset(imgCreate.GetPixels(), 0, W * H * 4);
			int trans = 
				tex->CopyTrueColorPixels(&imgCreate, GetLeftOffset(use) - tex->LeftOffset, GetTopOffset(use) - tex->TopOffset);
			bmp.CopyPixelDataRGB(0, 0, imgCreate.GetPixels(), W, H, 4, W * 4, 0, CF_BGRA);
			CheckTrans(buffer, W*H, trans);
		}
	}
	else if (translation<=0)
	{
		int trans = 
			tex->CopyTrueColorPixels(&bmp, GetLeftOffset(use) - tex->LeftOffset, GetTopOffset(use) - tex->TopOffset);

		CheckTrans(buffer, W*H, trans);

	}
	else
	{
		// When using translations everything must be mapped to the base palette.
		// Since FTexture's method is doing exactly that by calling GetPixels let's use that here
		// to do all the dirty work for us. ;)
		tex->FTexture::CopyTrueColorPixels(&bmp, GetLeftOffset(use) - tex->LeftOffset, GetTopOffset(use) - tex->TopOffset);
	}

	// [BB] The hqnx upsampling (not the scaleN one) destroys partial transparency, don't upsamle textures using it.
	if ( bIsTransparent != 1 )
	{
		// [BB] Potentially upsample the buffer.
		buffer = gl_CreateUpsampledTextureBuffer ( tex, buffer, W, H, w, h );
	}

	if ((!(gl.flags & RFL_GLSL) || !gl_warp_shader) && tex->bWarped && W <= 256 && H <= 256)
	{
		buffer = WarpBuffer(buffer, W, H, tex->bWarped);
		createWarped = true;
	}

	return buffer;
}

//===========================================================================
// 
//	Gets texture coordinate info for world (wall/flat) textures
//  The wrapper class is there to provide a set of coordinate
//  functions to access the texture
//
//===========================================================================
const WorldTextureInfo * FGLTexture::GetWorldTextureInfo()
{

	if (tex->UseType==FTexture::TEX_Null) return NULL;		// Cannot register a NULL texture!
	if (!gltexture) gltexture=new GLTexture(Width[GLUSE_TEXTURE], Height[GLUSE_TEXTURE], true, true);
	if (gltexture) return (WorldTextureInfo*)this; 	
	return NULL;
}

//===========================================================================
// 
//	Gets texture coordinate info for sprites
//  The wrapper class is there to provide a set of coordinate
//  functions to access the texture
//
//===========================================================================
const PatchTextureInfo * FGLTexture::GetPatchTextureInfo()
{
	if (tex->UseType==FTexture::TEX_Null) return NULL;		// Cannot register a NULL texture!
	if (!glpatch) 
	{
		glpatch=new GLTexture(Width[GLUSE_PATCH], Height[GLUSE_PATCH], false, false);
	}
	if (glpatch) return (PatchTextureInfo*)this; 	
	return NULL;
}

//===========================================================================
// 
//	Checks if a shader needs to be used for this texture
//
//===========================================================================

void FGLTexture::SetupShader(int clampmode, int warped, int &cm, int translation)
{
	bool usebright;

	if (gl.flags & RFL_GLSL)
	{
		if (tex->gl_info.bBrightmapChecked == -1)
		{
			CreateDefaultBrightmap();
		}

		FTexture *brightmap = tex->gl_info.Brightmap;
		if (brightmap && gl_brightmap_shader && translation >= 0 &&
			cm >= CM_DEFAULT && cm <= CM_DESAT31 && gl_brightmapenabled)
		{
			FGLTexture *bmgltex = FGLTexture::ValidateTexture(brightmap);
			if (clampmode != -1) bmgltex->Bind(1, CM_DEFAULT, clampmode, 0);
			else bmgltex->BindPatch(1, CM_DEFAULT, 0);
			usebright = true;
		}
		else usebright = false;

		bool usecmshader = (tex->bHasCanvas || gl_colormap_shader) && cm > CM_DEFAULT && cm < CM_SHADE && gl_texturemode != TM_MASK;

		float warptime = warped? static_cast<FWarpTexture*>(tex)->GetSpeed() : 0.f;
		gl_SetTextureShader(warped, usecmshader? cm : CM_DEFAULT, usebright, warptime);
		if (usecmshader) cm = CM_DEFAULT;
	}
}

//===========================================================================
// 
//	Binds a texture to the renderer
//
//===========================================================================

const WorldTextureInfo * FGLTexture::Bind(int texunit, int cm, int clampmode, int translation)
{
	int usebright = false;

	translation = GLTranslationPalette::GetInternalTranslation(translation);

	if (GetWorldTextureInfo())
	{
		if (texunit == 0)
		{
			int warped = gl_warp_shader? tex->bWarped : 0;

			SetupShader(clampmode, warped, cm, translation);

			if (warped == 0)
			{
				// If this is a warped texture that needs updating
				// delete all system textures created for this
				if (tex->CheckModified() && !tex->bHasCanvas && HiresLump<0 && HiresLump!=-2)
				{
					gltexture->Clean(true);
				}
			}
			else if (createWarped)
			{
				Clean(true);
				GetWorldTextureInfo();
			}
		}

		// Bind it to the system.
		// clamping in x-direction may cause problems when rendering segs
		if (!gltexture->Bind(texunit, cm, translation, gl_render_precise? clampmode&GLT_CLAMPY : clampmode))
		{
			int w,h;

			// Create this texture
			unsigned char * buffer = CreateTexBuffer(GLUSE_TEXTURE, cm, translation, w, h);

			ProcessData(buffer, w, h, cm, false);
			if (!gltexture->CreateTexture(buffer, w, h, true, texunit, cm, translation)) 
			{
				// could not create texture
				delete buffer;
				return NULL;
			}
			delete buffer;
		}
		if (texunit == 0) gltexture->SetTextureClamp(gl_render_precise? clampmode&GLT_CLAMPY : clampmode);

		if (tex->bHasCanvas) static_cast<FCanvasTexture*>(tex)->NeedUpdate();
		return (WorldTextureInfo*)this; 
	}
	return NULL;
}

const WorldTextureInfo * FGLTexture::Bind(int cm, int clampmode, int translation)
{
	return Bind(0, cm, clampmode, translation);
}
//===========================================================================
// 
//	Binds a sprite to the renderer
//
//===========================================================================
const PatchTextureInfo * FGLTexture::BindPatch(int texunit, int cm, int translation)
{
	bool usebright = false;
	int transparm = translation;

	translation = GLTranslationPalette::GetInternalTranslation(translation);

	if (GetPatchTextureInfo())
	{
		if (texunit == 0)
		{
			int warped = gl_warp_shader? tex->bWarped : 0;

			SetupShader(-1, warped, cm, translation);

			if (warped == 0)
			{
				// If this is a warped texture that needs updating
				// delete all system textures created for this
				if (tex->CheckModified() && !tex->bHasCanvas && HiresLump<0 && HiresLump!=-2)
				{
					glpatch->Clean(true);
				}
			}
			else if (createWarped)
			{
				Clean(true);
				GetPatchTextureInfo();
			}
		}

		// Bind it to the system. For multitexturing this
		// should be the only thing that needs adjusting
		if (!glpatch->Bind(texunit, cm, translation, -1))
		{
			int w, h;

			// Create this texture
			unsigned char * buffer = CreateTexBuffer(GLUSE_PATCH, cm, translation, w, h, false);
			ProcessData(buffer, w, h, cm, true);
			if (!glpatch->CreateTexture(buffer, w, h, false, texunit, cm, translation)) 
			{
				// could not create texture
				delete buffer;
				return NULL;
			}
			delete buffer;
		}
		if (gl_render_precise)
		{
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		return (PatchTextureInfo*)this; 	
	}
	return NULL;
}

const PatchTextureInfo * FGLTexture::BindPatch(int cm, int translation)
{
	return BindPatch(0, cm, translation);
}

//==========================================================================
//
// Flushes all hardware dependent data
//
//==========================================================================

void FGLTexture::FlushAll()
{
	for(int i=gltextures.Size()-1;i>=0;i--)
	{
		gltextures[i]->Clean(true);
	}
}

//==========================================================================
//
// Deletes all hardware dependent data
//
//==========================================================================

void FGLTexture::DeleteAll()
{
	for(int i=gltextures.Size()-1;i>=0;i--)
	{
		gltextures[i]->tex->gl_info.GLTexture = NULL;
		delete gltextures[i];
	}
	gltextures.Clear();
}

//==========================================================================
//
// Gets a texture from the texture manager and checks its validity for
// GL rendering. 
//
//==========================================================================

FGLTexture * FGLTexture::ValidateTexture(FTexture * tex)
{
	if (tex	&& tex->UseType!=FTexture::TEX_Null)
	{
		FGLTexture *gltex = tex->gl_info.GLTexture;
		if (gltex == NULL) 
		{
			gltex = new FGLTexture(tex);
			tex->gl_info.GLTexture = gltex;
		}
		return gltex;
	}
	return NULL;
}

FGLTexture * FGLTexture::ValidateTexture(FTextureID no, bool translate)
{
	return FGLTexture::ValidateTexture(translate? TexMan(no) : TexMan[no]);
}


}