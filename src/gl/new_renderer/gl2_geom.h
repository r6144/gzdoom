
#ifndef __GL2_GEOM_H
#define __GL2_GEOM_H


#include "gl/common/glc_structs.h"
#include "gl2_vertex.h"

struct FDynamicColormap;

namespace GLRendererNew
{

struct FGLSubsectorPlaneRenderData
{
	int mIndex;
	TArray<FVertex3D> mVertices;
	FPrimitive3D mPrimitive;
};


struct FGLSectorPlaneRenderData
{
	secplane_t *mPlane;
	bool mUpside;
	bool mDownside;
	TArray<FGLSubsectorPlaneRenderData> mSubsectorData;
};

struct FGLSectorRenderData
{
	sector_t *mSector;
	TArray<FGLSectorPlaneRenderData> mPlanes[3];

	void CreatePlane(int in_area, sector_t *sec, GLSectorPlane &splane, 
					 int lightlevel, FDynamicColormap *cm, 
					 float alpha, bool additive, int whichplanes, bool opaque);

	void Init(int sector);
	void Invalidate();
	void Validate(int in_area);
};



}


#endif