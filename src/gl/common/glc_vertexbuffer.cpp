/*
** glc_vertexbuffer.cpp
** Vertex buffer handling.
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

#include "gl/gl_include.h"
#include "doomtype.h"
#include "p_local.h"
#include "gl/common/glc_renderer.h"
#include "gl/common/glc_data.h"
#include "gl/common/glc_vertexbuffer.h"




//==========================================================================
//
// Create / destroy the VBO
//
//==========================================================================

FVertexBuffer::FVertexBuffer()
{
	vbo_id = 0;
	if (gl.flags&RFL_VBO)
	{
		gl.GenBuffers(1, &vbo_id);
	}
}
	
FVertexBuffer::~FVertexBuffer()
{
	if (gl.flags&RFL_VBO)
	{
		gl.DeleteBuffers(1, &vbo_id);
	}
}

//==========================================================================
//
// Initialize a single vertex
//
//==========================================================================

void FVBOVertex::SetFlatVertex(vertex_t *vt, const secplane_t & plane)
{
	x = vt->fx;
	y = vt->fy;
	z = plane.ZatPoint(vt->fx, vt->fy);
	u = vt->fx/64.f;
	v = -vt->fy/64.f;
	w = dc = df = 0;
}

//==========================================================================
//
// Find a 3D floor
//
//==========================================================================

static F3DFloor *Find3DFloor(sector_t *target, sector_t *model)
{
	for(unsigned i=0; i<target->e->XFloor.ffloors.Size(); i++)
	{
		F3DFloor *ffloor = target->e->XFloor.ffloors[i];
		if (ffloor->model == model) return ffloor;
	}
	return NULL;
}

//==========================================================================
//
// Creates the vertices for one plane in one subsector
//
//==========================================================================

int FVertexBuffer::CreateSubsectorVertices(subsector_t *sub, const secplane_t &plane, int floor)
{
	int idx = vbo_shadowdata.Reserve(sub->numlines);
	for(int k=0; k<sub->numlines; k++, idx++)
	{
		vbo_shadowdata[idx].SetFlatVertex(segs[sub->firstline+k].v1, plane);
		if (sub->sector->transdoor && floor) vbo_shadowdata[idx].z -= 1.f;
	}
	return idx;
}

//==========================================================================
//
// Creates the vertices for one plane in one subsector
//
//==========================================================================

int FVertexBuffer::CreateSectorVertices(sector_t *sec, const secplane_t &plane, int floor)
{
	int rt = vbo_shadowdata.Size();
	// First calculate the vertices for the sector itself
	for(int j=0; j<sec->subsectorcount; j++)
	{
		subsector_t *sub = sec->subsectors[j];
		CreateSubsectorVertices(sub, plane, floor);
	}
	return rt;
}

//==========================================================================
//
//
//
//==========================================================================

int FVertexBuffer::CreateVertices(int h, sector_t *sec, const secplane_t &plane, int floor)
{
	// First calculate the vertices for the sector itself
	sec->vboshadowheight[h] = sec->vboheight[h] = sec->GetPlaneTexZ(h);
	sec->vboindex[h] = CreateSectorVertices(sec, plane, floor);

	// Next are all sectors using this one as heightsec
	TArray<sector_t *> &fakes = sec->e->FakeFloor.Sectors;
	for (unsigned g=0; g<fakes.Size(); g++)
	{
		sector_t *fsec = fakes[g];
		fsec->vboindex[2+h] = CreateSectorVertices(fsec, plane, false);
	}

	// and finally all attached 3D floors
	TArray<sector_t *> &xf = sec->e->XFloor.attached;
	for (unsigned g=0; g<xf.Size(); g++)
	{
		sector_t *fsec = xf[g];
		F3DFloor *ffloor = Find3DFloor(fsec, sec);

		if (ffloor != NULL && ffloor->flags & FF_RENDERPLANES)
		{
			bool dotop = (ffloor->top.model == sec) && (ffloor->top.isceiling == h);
			bool dobottom = (ffloor->bottom.model == sec) && (ffloor->bottom.isceiling == h);

			if (dotop || dobottom)
			{
				if (dotop) ffloor->top.vindex = vbo_shadowdata.Size();
				if (dobottom) ffloor->bottom.vindex = vbo_shadowdata.Size();
	
				CreateSectorVertices(fsec, plane, false);
			}
		}
	}
	sec->vbocount[h] = vbo_shadowdata.Size() - sec->vboindex[h];
	return sec->vboindex[h];
}


//==========================================================================
//
//
//
//==========================================================================

void FVertexBuffer::CreateFlatVBO()
{
	for (int h = sector_t::floor; h <= sector_t::ceiling; h++)
	{
		for(int i=0; i<numsectors;i++)
		{
			CreateVertices(h, &sectors[i], sectors[i].GetSecPlane(h), h == sector_t::floor);
		}
	}

	// We need to do a final check for Vavoom water and FF_FIX sectors.
	// No new vertices are needed here. The planes come from the actual sector
	for(int i=0; i<numsectors;i++)
	{
		for(unsigned j=0;j<sectors[i].e->XFloor.ffloors.Size(); j++)
		{
			F3DFloor *ff = sectors[i].e->XFloor.ffloors[j];

			if (ff->top.model == &sectors[i])
			{
				ff->top.vindex = sectors[i].vboindex[ff->top.isceiling];
			}
			if (ff->bottom.model == &sectors[i])
			{
				ff->bottom.vindex = sectors[i].vboindex[ff->top.isceiling];
			}
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

void FVertexBuffer::UpdatePlaneVertices(sector_t *sec, int plane)
{
	secplane_t &splane = sec->GetSecPlane(plane);
	FVBOVertex *vt = &vbo_shadowdata[sec->vboindex[plane]];
	for(int i=0; i<sec->vbocount[plane]; i++, vt++)
	{
		vt->z = splane.ZatPoint(vt->x, vt->y);
	}
}

//==========================================================================
//
//
//
//==========================================================================

void FVertexBuffer::CreateVBO()
{
	vbo_shadowdata.Clear();
	if (vbo_id > 0)
	{
		CreateFlatVBO();
		gl.BindBuffer(GL_ARRAY_BUFFER, vbo_id);
		gl.BufferData(GL_ARRAY_BUFFER, vbo_shadowdata.Size() * sizeof(FVBOVertex), &vbo_shadowdata[0], GL_DYNAMIC_DRAW);
	}
	else 
	{
		// set all VBO info to invalid values so that we can save some checks in the rendering code
		for(int i=0;i<numsectors;i++)
		{
			sectors[i].vboindex[3] = sectors[i].vboindex[2] = 
			sectors[i].vboindex[1] = sectors[i].vboindex[0] = -1;
			sectors[i].vboheight[1] = sectors[i].vboheight[0] = FIXED_MIN;
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

void FVertexBuffer::BindVBO()
{
	if (vbo_id > 0)
	{
		gl.BindBuffer(GL_ARRAY_BUFFER, vbo_id);
		glVertexPointer(3,GL_FLOAT, sizeof(FVBOVertex), &VTO->x);
		glTexCoordPointer(2,GL_FLOAT, sizeof(FVBOVertex), &VTO->u);
		gl.EnableClientState(GL_VERTEX_ARRAY);
		gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	}
}

