#ifndef __R_BLEND_H
#define __R_BLEND_H

/*
** r_blend.h
** Constants and types for specifying texture blending.
**
**---------------------------------------------------------------------------
** Copyright 2008 Randy Heit
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

// Legacy render styles
enum ERenderStyle
{
	STYLE_None,				// Do not draw
	STYLE_Normal,			// Normal; just copy the image to the screen
	STYLE_Fuzzy,			// Draw silhouette using "fuzz" effect
	STYLE_SoulTrans,		// Draw translucent with amount in r_transsouls
	STYLE_OptFuzzy,			// Draw as fuzzy or translucent, based on user preference
	STYLE_Stencil,			// Fill image interior with alphacolor
	STYLE_Translucent,		// Draw translucent
	STYLE_Add,				// Draw additive
	STYLE_Shaded,			// Treat patch data as alpha values for alphacolor
	STYLE_TranslucentStencil,

	STYLE_Count
};

// Flexible render styles (most possible combinations are supported in software)
enum ERenderOp
{
	STYLEOP_None,			// Do not draw
	STYLEOP_Add,			// Add source to destination
	STYLEOP_Sub,			// Subtract source from destination
	STYLEOP_RevSub,			// Subtract destination from source
	STYLEOP_Fuzz,			// Draw fuzzy on top of destination - ignores alpha and color
	STYLEOP_FuzzOrAdd,		// Draw fuzzy or add, based on user preference
	STYLEOP_FuzzOrSub,		// Draw fuzzy or subtract, based on user preference
	STYLEOP_FuzzOrRevSub,	// Draw fuzzy or reverse subtract, based on user preference
};

enum ERenderAlpha
{
	STYLEALPHA_Zero,		// Blend factor is 0.0
	STYLEALPHA_One,			// Blend factor is 1.0
	STYLEALPHA_Src,			// Blend factor is alpha
	STYLEALPHA_InvSrc,		// Blend factor is 1.0 - alpha
};

enum ERenderFlags
{
	// Use value of transsouls as alpha.
	STYLEF_TransSoulsAlpha = 1,

	// Force alpha to 1. Not the same as STYLEALPHA_One, since that also
	// ignores alpha from the texture.
	STYLEF_Alpha1 = 2,

	// Use red component from grayscale/RGB texture as alpha. If the texture
	// is paletted, the palette is ignored and it is treated as grayscale.
	// This should generally be combined with STYLEF_ColorIsFixed, since that's
	// all the software renderer supports, but hardware acceleration can do
	// them separately should you want to do that for some reason.
	STYLEF_RedIsAlpha = 4,

	// Ignore texture for RGB output. Color comes from fillcolor for actors
	// or DTA_FillColor for DrawTexture().
	STYLEF_ColorIsFixed = 8,

	// Invert source color, either the texture color or the fixed color.
	STYLEF_InvertSource = 16,

	// Invert overlay color. This is the fade for actors and DTA_ColorOverlay
	// for DrawTexture().
	STYLEF_InvertOverlay = 32,

	// Actors only: Ignore sector fade and fade to black. To fade to white,
	// combine this with STYLEF_InvertOverlay.
	STYLEF_FadeToBlack = 64,
};

union FRenderStyle
{
	struct
	{
		BYTE BlendOp;	// Of ERenderOp type
		BYTE SrcAlpha;	// Of ERenderAlpha type
		BYTE DestAlpha;	// Of ERenderAlpha type
		BYTE Flags;
	};
	uint32 AsDWORD;

	inline FRenderStyle &operator= (ERenderStyle legacy);
	operator uint32() const { return AsDWORD; }
	bool operator==(const FRenderStyle &o) const { return AsDWORD == o.AsDWORD; }
	void CheckFuzz();
	bool IsVisible(fixed_t alpha) const throw();
private:
	// Code that compares an actor's render style with a legacy render
	// style value should be updated. Making these conversion operators
	// private will catch those cases.
	operator ERenderStyle() const { return STYLE_Normal; }
	operator int() const { return STYLE_Normal; }
};

extern FRenderStyle LegacyRenderStyles[STYLE_Count];

inline FRenderStyle &FRenderStyle::operator= (ERenderStyle legacy)
{
	if (legacy < STYLE_None || legacy >= STYLE_Count)
	{
		legacy = STYLE_None;
	}
	*this = LegacyRenderStyles[legacy];
	return *this;
}

class FArchive;

FArchive &operator<< (FArchive &arc, FRenderStyle &style);

#endif
