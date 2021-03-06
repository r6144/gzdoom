/*
** gl_setup.cpp
** Initializes the data structures required by the GL renderer to handle
** a level
**
**---------------------------------------------------------------------------
** Copyright 2005 Christoph Oelckers
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
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
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

#include "gl/system/gl_system.h"
#include "doomtype.h"
#include "colormatcher.h"
#include "r_translate.h"
#include "i_system.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "c_dispatch.h"
#include "r_sky.h"
#include "sc_man.h"
#include "w_wad.h"
#include "gi.h"
#include "p_setup.h"
#include "g_level.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/dynlights/gl_glow.h"
#include "gl/utility/gl_clock.h"
#include "gl/gl_functions.h"

void InitGLRMapinfoData();
void gl_InitData();

//==========================================================================
//
// This must be available before spawning the actors so
// gl_PreprocessLevel is too late.
//
//==========================================================================

void gl_InitSegs()
{
	// set up the extra data in case the map was loaded with regular node that might pass as GL nodes.
	if (glsegextras == NULL)
	{
		glsegextras = new glsegextra_t[numsegs];
		for(int i=0;i<numsegs;i++)
		{
			glsegextras[i].PartnerSeg = DWORD_MAX;
		}
		for (int i=0; i<numsubsectors; i++)
		{
			int seg = int(subsectors[i].firstline-segs);
			for(DWORD j=0;j<subsectors[i].numlines;j++)
			{
				glsegextras[j+seg].Subsector = &subsectors[i];
			}
		}
	}
	
	for(int i=0;i<numsegs;i++)
	{
		seg_t *seg = &segs[i];

		// Account for ZDoom space optimizations that cannot be done for GL
		unsigned int partner= glsegextras[i].PartnerSeg;
		if (partner < numsegs)  seg->PartnerSeg = &segs[partner];
		else seg->PartnerSeg = NULL;
	}
}

//==========================================================================
//
// prepare subsectors for GL rendering
// - analyze rendering hacks using open sectors
// - assign a render sector (for self referencing sectors)
// - calculate a bounding box
//
//==========================================================================

inline void M_ClearBox (fixed_t *box)
{
	box[BOXTOP] = box[BOXRIGHT] = INT_MIN;
	box[BOXBOTTOM] = box[BOXLEFT] = INT_MAX;
}

inline void M_AddToBox(fixed_t* box,fixed_t x,fixed_t y)
{
	if (x<box[BOXLEFT]) box[BOXLEFT] = x;
	if (x>box[BOXRIGHT]) box[BOXRIGHT] = x;
	if (y<box[BOXBOTTOM]) box[BOXBOTTOM] = y;
	if (y>box[BOXTOP]) box[BOXTOP] = y;
}

static void SpreadHackedFlag(subsector_t * sub)
{
	// The subsector pointer hasn't been set yet!
	for(DWORD i=0;i<sub->numlines;i++)
	{
		seg_t * seg = sub->firstline + i;

		if (seg->PartnerSeg)
		{
			subsector_t * sub2 = seg->PartnerSeg->Subsector();

			if (!(sub2->hacked&1) && sub2->render_sector == sub->render_sector)
			{
				sub2->hacked|=1;
				SpreadHackedFlag (sub2);
			}
		}
	}
}


static void PrepareSectorData()
{
	int 				i;
	size_t				/*ii,*/ jj;
	TArray<subsector_t *> undetermined;
	subsector_t *		ss;

	// look up sector number for each subsector
	for (i = 0; i < numsubsectors; i++)
	{
		ss = &subsectors[i];
		seg_t *seg = ss->firstline;

		M_ClearBox(ss->bbox);
		for(jj=0; jj<ss->numlines; jj++)
		{
			M_AddToBox(ss->bbox,seg->v1->x, seg->v1->y);
			seg++;
		}
	}

	// now group the subsectors by sector
	subsector_t ** subsectorbuffer = new subsector_t * [numsubsectors];

	for(i=0, ss=subsectors; i<numsubsectors; i++, ss++)
	{
		ss->render_sector->subsectorcount++;
	}

	for (i=0; i<numsectors; i++) 
	{
		sectors[i].subsectors = subsectorbuffer;
		subsectorbuffer += sectors[i].subsectorcount;
		sectors[i].subsectorcount = 0;
	}
	
	for(i=0, ss = subsectors; i<numsubsectors; i++, ss++)
	{
		ss->render_sector->subsectors[ss->render_sector->subsectorcount++]=ss;
	}

	// marks all malformed subsectors so rendering tricks using them can be handled more easily
	for (i = 0; i < numsubsectors; i++)
	{
		if (subsectors[i].sector == subsectors[i].render_sector)
		{
			seg_t * seg = subsectors[i].firstline;
			for(DWORD j=0;j<subsectors[i].numlines;j++)
			{
				if (!(subsectors[i].hacked&1) && seg[j].linedef==0 && 
						seg[j].PartnerSeg!=NULL && 
						subsectors[i].render_sector != seg[j].PartnerSeg->Subsector()->render_sector)
				{
					DPrintf("Found hack: (%d,%d) (%d,%d)\n", seg[j].v1->x>>16, seg[j].v1->y>>16, seg[j].v2->x>>16, seg[j].v2->y>>16);
					subsectors[i].hacked|=1;
					SpreadHackedFlag(&subsectors[i]);
				}
				if (seg[j].PartnerSeg==NULL) subsectors[i].hacked|=2;	// used for quick termination checks
			}
		}
	}
}

//==========================================================================
//
// Some processing for transparent door hacks using a floor raised by 1 map unit
// - This will be used to lower the floor of such sectors by one map unit
//
//==========================================================================
static void PrepareTransparentDoors(sector_t * sector)
{
	bool solidwall=false;
	int notextures=0;
	int nobtextures=0;
	int selfref=0;
	int i;
	sector_t * nextsec=NULL;

#ifdef _MSC_VER
#ifdef _DEBUG
	if (sector-sectors==2)
	{
		__asm nop
	}
#endif
#endif

	P_Recalculate3DFloors(sector);
	if (sector->subsectorcount==0) return;

	sector->transdoorheight=sector->GetPlaneTexZ(sector_t::floor);
	sector->transdoor= !(sector->e->XFloor.ffloors.Size() || sector->heightsec || sector->floorplane.a || sector->floorplane.b);

	if (sector->transdoor)
	{
		for (i=0; i<sector->linecount; i++)
		{
			if (sector->lines[i]->frontsector==sector->lines[i]->backsector) 
			{
				selfref++;
				continue;
			}

			sector_t * sec=getNextSector(sector->lines[i], sector);
			if (sec==NULL) 
			{
				solidwall=true;
				continue;
			}
			else
			{
				nextsec=sec;

				int side = sector->lines[i]->sidedef[0]->sector == sec;

				if (sector->GetPlaneTexZ(sector_t::floor)!=sec->GetPlaneTexZ(sector_t::floor)+FRACUNIT) 
				{
					sector->transdoor=false;
					return;
				}
				if (!sector->lines[i]->sidedef[1-side]->GetTexture(side_t::top).isValid()) notextures++;
				if (!sector->lines[i]->sidedef[1-side]->GetTexture(side_t::bottom).isValid()) nobtextures++;
			}
		}
		if (sector->GetTexture(sector_t::ceiling)==skyflatnum)
		{
			sector->transdoor=false;
			return;
		}

		if (selfref+nobtextures!=sector->linecount)
		{
			sector->transdoor=false;
		}

		if (selfref+notextures!=sector->linecount)
		{
			// This is a crude attempt to fix an incorrect transparent door effect I found in some
			// WolfenDoom maps but considering the amount of code required to handle it I left it in.
			// Do this only if the sector only contains one-sided walls or ones with no lower texture.
			if (solidwall)
			{
				if (solidwall+nobtextures+selfref==sector->linecount && nextsec)
				{
					sector->heightsec=nextsec;
					sector->heightsec->MoreFlags=0;
				}
				sector->transdoor=false;
			}
		}
	}
}


//==========================================================================
//
// 
//
//==========================================================================
static void AddToVertex(const sector_t * sec, TArray<int> & list)
{
	int secno = int(sec-sectors);

	for(unsigned i=0;i<list.Size();i++)
	{
		if (list[i]==secno) return;
	}
	list.Push(secno);
}

//==========================================================================
//
// Attach sectors to vertices - used to generate vertex height lists
//
//==========================================================================
static void InitVertexData()
{
	TArray<int> * vt_sectorlists;

	int i,j,k;
	unsigned int l;

	vt_sectorlists = new TArray<int>[numvertexes];


	for(i=0;i<numlines;i++)
	{
		line_t * line = &lines[i];

		for(j=0;j<2;j++)
		{
			vertex_t * v = j==0? line->v1 : line->v2;

			for(k=0;k<2;k++)
			{
				sector_t * sec = k==0? line->frontsector : line->backsector;

				if (sec)
				{
					extsector_t::xfloor &x = sec->e->XFloor;

					AddToVertex(sec, vt_sectorlists[v-vertexes]);
					if (sec->heightsec) AddToVertex(sec->heightsec, vt_sectorlists[v-vertexes]);

					for(l=0;l<x.ffloors.Size();l++)
					{
						F3DFloor * rover = x.ffloors[l];
						if(!(rover->flags & FF_EXISTS)) continue;
						if (rover->flags&FF_NOSHADE) continue; // FF_NOSHADE doesn't create any wall splits 

						AddToVertex(rover->model, vt_sectorlists[v-vertexes]);
					}
				}
			}
		}
	}

	for(i=0;i<numvertexes;i++)
	{
		int cnt = vt_sectorlists[i].Size();

		vertexes[i].dirty = true;
		vertexes[i].numheights=0;
		if (cnt>1)
		{
			vertexes[i].numsectors= cnt;
			vertexes[i].sectors=new sector_t*[cnt];
			vertexes[i].heightlist = new float[cnt*2];
			for(int j=0;j<cnt;j++)
			{
				vertexes[i].sectors[j] = &sectors[vt_sectorlists[i][j]];
			}
		}
		else
		{
			vertexes[i].numsectors=0;
		}
	}

	delete [] vt_sectorlists;
}

//==========================================================================
//
// Group segs to sidedefs
//
//==========================================================================

static void GetSideVertices(int sdnum, FVector2 *v1, FVector2 *v2)
{
	line_t *ln = sides[sdnum].linedef;
	if (ln->sidedef[0] == &sides[sdnum]) 
	{
		v1->X = ln->v1->fx;
		v1->Y = ln->v1->fy;
		v2->X = ln->v2->fx;
		v2->Y = ln->v2->fy;
	}
	else
	{
		v2->X = ln->v1->fx;
		v2->Y = ln->v1->fy;
		v1->X = ln->v2->fx;
		v1->Y = ln->v2->fy;
	}
}

static int STACK_ARGS segcmp(const void *a, const void *b)
{
	seg_t *A = *(seg_t**)a;
	seg_t *B = *(seg_t**)b;
	return xs_RoundToInt(FRACUNIT*(A->sidefrac - B->sidefrac));
}

static void PrepareSegs()
{
	int *segcount = new int[numsides];
	int realsegs = 0;

	// Get floatng point coordinates of vertices
	for(int i = 0; i < numvertexes; i++)
	{
		vertexes[i].fx = FIXED2FLOAT(vertexes[i].x);
		vertexes[i].fy = FIXED2FLOAT(vertexes[i].y);
		vertexes[i].dirty = true;
	}

	// count the segs
	memset(segcount, 0, numsides * sizeof(int));
	
	// set up the extra data in case the map was loaded with regular node that might pass as GL nodes.
	if (glsegextras == NULL)
	{
		glsegextras = new glsegextra_t[numsegs];
		for(int i=0;i<numsegs;i++)
		{
			glsegextras[i].PartnerSeg = DWORD_MAX;
		}
		for (int i=0; i<numsubsectors; i++)
		{
			int seg = int(subsectors[i].firstline-segs);
			for(DWORD j=0;j<subsectors[i].numlines;j++)
			{
				glsegextras[j+seg].Subsector = &subsectors[i];
			}
		}
	}
	
	for(int i=0;i<numsegs;i++)
	{
		seg_t *seg = &segs[i];

		if (seg->sidedef == NULL) continue;	// miniseg
		int sidenum = int(seg->sidedef - sides);

		realsegs++;
		segcount[sidenum]++;
		FVector2 sidestart, sideend, segend(seg->v2->fx, seg->v2->fy);
		GetSideVertices(sidenum, &sidestart, &sideend);

		sideend -=sidestart;
		segend -= sidestart;

		seg->sidefrac = float(segend.Length() / sideend.Length());
	}

	// allocate memory
	sides[0].segs = new seg_t*[realsegs];
	sides[0].numsegs = 0;

	for(int i = 1; i < numsides; i++)
	{
		sides[i].segs = sides[i-1].segs + segcount[i-1];
		sides[i].numsegs = 0;
	}
	delete [] segcount;

	// assign the segs
	for(int i=0;i<numsegs;i++)
	{
		seg_t *seg = &segs[i];
		if (seg->sidedef != NULL) seg->sidedef->segs[seg->sidedef->numsegs++] = seg;
	}

	// sort the segs
	for(int i = 0; i < numsides; i++)
	{
		if (sides[i].numsegs > 1) qsort(sides[i].segs, sides[i].numsegs, sizeof(seg_t*), segcmp);
	}
}

//==========================================================================
//
// Initialize the level data for the GL renderer
//
//==========================================================================

void gl_PreprocessLevel()
{
	int i;

	static bool datadone=false;


	if (!datadone)
	{
		datadone=true;
		gl_InitData();
	}

	PrepareSegs();
	PrepareSectorData();
	InitVertexData();
	for(i=0;i<numsectors;i++) 
	{
		sectors[i].dirty = true;
		sectors[i].sectornum = i;
		PrepareTransparentDoors(&sectors[i]);
	}

	gl_InitPortals();

	if (GLRenderer != NULL) 
	{
		GLRenderer->SetupLevel();
	}

#if 0
	gl_CreateSections();
#endif

	InitGLRMapinfoData();
}



//==========================================================================
//
// Cleans up all the GL data for the last level
//
//==========================================================================
void gl_CleanLevelData()
{
	// Dynamic lights must be destroyed before the sector information here is deleted.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	AActor * mo=it.Next();
	while (mo)
	{
		AActor * next = it.Next();
		mo->Destroy();
		mo=next;
	}

	if (vertexes != NULL)
	{
		for(int i = 0; i < numvertexes; i++) if (vertexes[i].numsectors > 0)
		{
			if (vertexes[i].sectors != NULL)
			{
				delete [] vertexes[i].sectors;
				vertexes[i].sectors = NULL;
			}
			if (vertexes[i].heightlist != NULL)
			{
				delete [] vertexes[i].heightlist;
				vertexes[i].heightlist = NULL;
			}
		}
	}

	if (sides && sides[0].segs)
	{
		delete [] sides[0].segs;
		sides[0].segs = NULL;
	}
	if (sectors && sectors[0].subsectors) 
	{
		delete [] sectors[0].subsectors;
		sectors[0].subsectors = NULL;
	}
	if (gamenodes && gamenodes!=nodes)
	{
		delete [] gamenodes;
		gamenodes = NULL;
		numgamenodes = 0;
	}
	if (gamesubsectors && gamesubsectors!=subsectors)
	{
		delete [] gamesubsectors;
		gamesubsectors = NULL;
		numgamesubsectors = 0;
	}
}

