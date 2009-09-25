#ifndef __GL_FUNCT
#define __GL_FUNCT

#include "v_palette.h"

class AActor;
struct MapData;



// External entry points for the GL renderer
void gl_PreprocessLevel();
void gl_CleanLevelData();
void gl_LinkLights();
void gl_SetActorLights(AActor *);
void gl_DeleteAllAttachedLights();
void gl_RecreateAllAttachedLights();
void gl_ParseDefs();
void gl_SetFogParams(int _fogdensity, PalEntry _outsidefogcolor, int _outsidefogdensity, int _skyfog);
void gl_CheckNodes(MapData * map);
bool gl_LoadGLNodes(MapData * map);



extern int currentrenderer;

#endif
