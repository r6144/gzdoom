// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION: Door animation code (opening/closing)
//		[RH] Removed sliding door code and simplified for Hexen-ish specials
//
//-----------------------------------------------------------------------------



#include "doomdef.h"
#include "p_local.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "doomstat.h"
#include "r_state.h"
#include "c_console.h"
#include "gi.h"
#include "a_keys.h"
#include "i_system.h"
#include "sc_man.h"
#include "cmdlib.h"

IMPLEMENT_CLASS (DDoor)

DDoor::DDoor ()
{
}

void DDoor::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << m_Type
		<< m_TopDist
		<< m_BotSpot << m_BotDist << m_OldFloorDist
		<< m_Speed
		<< m_Direction
		<< m_TopWait
		<< m_TopCountdown
		<< m_LightTag;
}


//
// VERTICAL DOORS
//

//
// T_VerticalDoor
//
void DDoor::Tick ()
{
	EResult res;

	if (m_Sector->floorplane.d != m_OldFloorDist)
	{
		if (!m_Sector->floordata || !m_Sector->floordata->IsKindOf(RUNTIME_CLASS(DPlat)) ||
			!(barrier_cast<DPlat*>(m_Sector->floordata))->IsLift())
		{
			m_OldFloorDist = m_Sector->floorplane.d;
			m_BotDist = m_Sector->ceilingplane.PointToDist (m_BotSpot,
				m_Sector->floorplane.ZatPoint (m_BotSpot));
		}
	}

	switch (m_Direction)
	{
	case 0:
		// WAITING
		if (!--m_TopCountdown)
		{
			switch (m_Type)
			{
			case doorRaise:
				m_Direction = -1; // time to go back down
				DoorSound (false);
				break;
				
			case doorCloseWaitOpen:
				m_Direction = 1;
				DoorSound (true);
				break;
				
			default:
				break;
			}
		}
		break;
		
	case 2:
		//	INITIAL WAIT
		if (!--m_TopCountdown)
		{
			switch (m_Type)
			{
			case doorRaiseIn5Mins:
				m_Direction = 1;
				m_Type = doorRaise;
				DoorSound (true);
				break;
				
			default:
				break;
			}
		}
		break;
		
	case -1:
		// DOWN
		res = MoveCeiling (m_Speed, m_BotDist, -1, m_Direction, false);

		// killough 10/98: implement gradual lighting effects
		if (m_LightTag != 0 && m_TopDist != -m_Sector->floorplane.d)
		{
			EV_LightTurnOnPartway (m_LightTag, FixedDiv (m_Sector->ceilingplane.d + m_Sector->floorplane.d,
				m_TopDist + m_Sector->floorplane.d));
		}

		if (res == pastdest)
		{
			SN_StopSequence (m_Sector, CHAN_CEILING);
			switch (m_Type)
			{
			case doorRaise:
			case doorClose:
				m_Sector->ceilingdata = NULL;	//jff 2/22/98
				Destroy ();						// unlink and free
				break;
				
			case doorCloseWaitOpen:
				m_Direction = 0;
				m_TopCountdown = m_TopWait;
				break;
				
			default:
				break;
			}
		}
		else if (res == crushed)
		{
			switch (m_Type)
			{
			case doorClose:				// DO NOT GO BACK UP!
				break;
				
			default:
				m_Direction = 1;
				DoorSound (true);
				break;
			}
		}
		break;
		
	case 1:
		// UP
		res = MoveCeiling (m_Speed, m_TopDist, -1, m_Direction, false);
		
		// killough 10/98: implement gradual lighting effects
		if (m_LightTag != 0 && m_TopDist != -m_Sector->floorplane.d)
		{
			EV_LightTurnOnPartway (m_LightTag, FixedDiv (m_Sector->ceilingplane.d + m_Sector->floorplane.d,
				m_TopDist + m_Sector->floorplane.d));
		}

		if (res == pastdest)
		{
			SN_StopSequence (m_Sector, CHAN_CEILING);
			switch (m_Type)
			{
			case doorRaise:
				m_Direction = 0; // wait at top
				m_TopCountdown = m_TopWait;
				break;
				
			case doorCloseWaitOpen:
			case doorOpen:
				m_Sector->ceilingdata = NULL;	//jff 2/22/98
				Destroy ();						// unlink and free
				break;
				
			default:
				break;
			}
		}
		else if (res == crushed)
		{
			switch (m_Type)
			{
			case doorRaise:
			case doorRaiseIn5Mins:
				m_Direction = -1;
				DoorSound(false);
				break;

			default:
				break;
			}
		}
		break;
	}
}

// [RH] DoorSound: Plays door sound depending on direction and speed
void DDoor::DoorSound (bool raise) const
{
	int choice;

	// For multiple-selection sound sequences, the following choices are used:
	//  0  Opening
	//  1  Closing
	//  2  Opening fast
	//  3  Closing fast

	choice = !raise;

	if (m_Speed >= FRACUNIT*8)
	{
		choice += 2;
	}

	if (m_Sector->seqType >= 0)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, m_Sector->seqType, SEQ_DOOR, choice);
	}
	else if (m_Sector->SeqName != NAME_None)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, m_Sector->SeqName, choice);
	}
	else
	{
		const char *snd;

		switch (gameinfo.gametype)
		{
		default:	/* Doom and Hexen */
			snd = "DoorNormal";
			break;
			
		case GAME_Heretic:
			snd = "HereticDoor";
			break;

		case GAME_Strife:
			snd = "DoorSmallMetal";

			// Search the front top textures of 2-sided lines on the door sector
			// for a door sound to use.
			for (int i = 0; i < m_Sector->linecount; ++i)
			{
				const char *texname;
				line_t *line = m_Sector->lines[i];

				if (line->backsector == NULL)
					continue;

				FTexture *tex = TexMan[line->sidedef[0]->GetTexture(side_t::top)];
				texname = tex? tex->Name : NULL;
				if (texname != NULL && texname[0] == 'D' && texname[1] == 'O' && texname[2] == 'R')
				{
					switch (texname[3])
					{
					case 'S':
						snd = "DoorStone";
						break;

					case 'M':
						if (texname[4] == 'L')
						{
							snd = "DoorLargeMetal";
						}
						break;

					case 'W':
						if (texname[4] == 'L')
						{
							snd = "DoorLargeWood";
						}
						else
						{
							snd = "DoorSmallWood";
						}
						break;
					}
				}
			}
			break;
		}
		SN_StartSequence (m_Sector, CHAN_CEILING, snd, choice);
	}
}

DDoor::DDoor (sector_t *sector)
	: DMovingCeiling (sector)
{
}

// [RH] Merged EV_VerticalDoor and EV_DoLockedDoor into EV_DoDoor
//		and made them more general to support the new specials.

// [RH] SpawnDoor: Helper function for EV_DoDoor
DDoor::DDoor (sector_t *sec, EVlDoor type, fixed_t speed, int delay, int lightTag)
	: DMovingCeiling (sec),
  	  m_Type (type), m_Speed (speed), m_TopWait (delay), m_LightTag (lightTag)
{
	vertex_t *spot;
	fixed_t height;

	if (i_compatflags & COMPATF_NODOORLIGHT)
	{
		m_LightTag = 0;
	}

	switch (type)
	{
	case doorClose:
		m_Direction = -1;
		height = sec->FindLowestCeilingSurrounding (&spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4*FRACUNIT);
		DoorSound (false);
		break;

	case doorOpen:
	case doorRaise:
		m_Direction = 1;
		height = sec->FindLowestCeilingSurrounding (&spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4*FRACUNIT);
		if (m_TopDist != sec->ceilingplane.d)
			DoorSound (true);
		break;

	case doorCloseWaitOpen:
		m_TopDist = sec->ceilingplane.d;
		m_Direction = -1;
		DoorSound (false);
		break;

	case doorRaiseIn5Mins:
		m_Direction = 2;
		height = sec->FindLowestCeilingSurrounding (&spot);
		m_TopDist = sec->ceilingplane.PointToDist (spot, height - 4*FRACUNIT);
		m_TopCountdown = 5 * 60 * TICRATE;
		break;
	}

	if (!m_Sector->floordata || !m_Sector->floordata->IsKindOf(RUNTIME_CLASS(DPlat)) ||
		!(barrier_cast<DPlat*>(m_Sector->floordata))->IsLift())
	{
		height = sec->FindHighestFloorPoint (&m_BotSpot);
		m_BotDist = sec->ceilingplane.PointToDist (m_BotSpot, height);
	}
	else
	{
		height = sec->FindLowestCeilingPoint(&m_BotSpot);
		m_BotDist = sec->ceilingplane.PointToDist (m_BotSpot, height);
	}
	m_OldFloorDist = sec->floorplane.d;
}

bool EV_DoDoor (DDoor::EVlDoor type, line_t *line, AActor *thing,
				int tag, int speed, int delay, int lock, int lightTag)
{
	bool		rtn = false;
	int 		secnum;
	sector_t*	sec;

	if (lock != 0 && !P_CheckKeys (thing, lock, tag != 0))
		return false;

	if (tag == 0)
	{		// [RH] manual door
		if (!line)
			return false;

		// if the wrong side of door is pushed, give oof sound
		if (line->sidedef[1] == NULL)			// killough
		{
			S_Sound (thing, CHAN_VOICE, "*usefail", 1, ATTN_NORM);
			return false;
		}

		// get the sector on the second side of activating linedef
		sec = line->sidedef[1]->sector;
		secnum = int(sec-sectors);

		// if door already has a thinker, use it
		if (sec->PlaneMoving(sector_t::ceiling))
		{
			if (sec->ceilingdata->IsKindOf (RUNTIME_CLASS(DDoor)))
			{
				DDoor *door = barrier_cast<DDoor *>(sec->ceilingdata);

				// ONLY FOR "RAISE" DOORS, NOT "OPEN"s
				if (door->m_Type == DDoor::doorRaise && type == DDoor::doorRaise)
				{
					if (door->m_Direction == -1)
					{
						door->m_Direction = 1;	// go back up
						door->DoorSound (true);	// [RH] Make noise
					}
					else if (!(line->activation & (SPAC_Push|SPAC_MPush)))
						// [RH] activate push doors don't go back down when you
						//		run into them (otherwise opening them would be
						//		a real pain).
					{
						if (!thing->player || thing->player->isbot)
							return false;	// JDC: bad guys never close doors
											//Added by MC: Neither do bots.

						door->m_Direction = -1;	// start going down immediately

						// [RH] If this sector doesn't have a specific sound
						// attached to it, start the door close sequence.
						// Otherwise, just let the current one continue.
						// FIXME: This should be check if the sound sequence has separate up/down
						// paths, not if it was manually set.
						if ((sec->seqType < 0 && sec->SeqName == NAME_None) || SN_CheckSequence(sec, CHAN_CEILING) == NULL)
						{
							door->DoorSound (false);
						}
						return true;
					}
					else
					{
						return false;
					}
				}
			}
			return false;
		}
		if (new DDoor (sec, type, speed, delay, lightTag))
			rtn = true;
	}
	else
	{	// [RH] Remote door

		secnum = -1;
		while ((secnum = P_FindSectorFromTag (tag,secnum)) >= 0)
		{
			sec = &sectors[secnum];
			// if the ceiling already moving, don't start the door action
			if (sec->PlaneMoving(sector_t::ceiling))
				continue;

			if (new DDoor (sec, type, speed, delay, lightTag))
				rtn = true;
		}
				
	}
	return rtn;
}


//
// Spawn a door that closes after 30 seconds
//
void P_SpawnDoorCloseIn30 (sector_t *sec)
{
	fixed_t height;
	DDoor *door = new DDoor (sec);

	sec->special = 0;

	door->m_Sector = sec;
	door->m_Direction = 0;
	door->m_Type = DDoor::doorRaise;
	door->m_Speed = FRACUNIT*2;
	door->m_TopCountdown = 30 * TICRATE;
	height = sec->FindHighestFloorPoint (&door->m_BotSpot);
	door->m_BotDist = sec->ceilingplane.PointToDist (door->m_BotSpot, height);
	door->m_OldFloorDist = sec->floorplane.d;
	door->m_TopDist = sec->ceilingplane.d;
	door->m_LightTag = 0;
}

//
// Spawn a door that opens after 5 minutes
//
void P_SpawnDoorRaiseIn5Mins (sector_t *sec)
{
	sec->special = 0;
	new DDoor (sec, DDoor::doorRaiseIn5Mins, 2*FRACUNIT, TICRATE*30/7, 0);
}

// Strife's animated doors. Based on Doom's unused sliding doors, but significantly improved.

class DeletingDoorArray : public TArray<FDoorAnimation>
{
public:
	~DeletingDoorArray()
	{
		for(unsigned i=0;i<Size();i++)
		{
			FDoorAnimation *ani = &((*this)[i]);
			if (ani->TextureFrames != NULL) 
			{
				delete [] ani->TextureFrames;
				ani->TextureFrames = NULL;
			}
		}
	}
};

DeletingDoorArray DoorAnimations;

// EV_SlidingDoor : slide a door horizontally
// (animate midtexture, then set noblocking line)
//

//
// Return index into "DoorAnimations" array for which door type to use
//
static int P_FindSlidingDoorType (FTextureID picnum)
{
	unsigned int i;

	for (i = 0; i < DoorAnimations.Size(); ++i)
	{
		if (picnum == DoorAnimations[i].BaseTexture)
			return i;
	}

	return -1;
}

bool DAnimatedDoor::StartClosing ()
{
	FDoorAnimation &ani = DoorAnimations[m_WhichDoorIndex];

	// CAN DOOR CLOSE?
	if (m_Sector->touching_thinglist != NULL)
	{
		return false;
	}

	fixed_t topdist = m_Sector->ceilingplane.d;
	if (MoveCeiling (2048*FRACUNIT, m_BotDist, 0, -1, false) == crushed)
	{
		return false;
	}

	MoveCeiling (2048*FRACUNIT, topdist, 1);

	m_Line1->flags |= ML_BLOCKING;
	m_Line2->flags |= ML_BLOCKING;
	if (ani.CloseSound != NAME_None)
	{
		SN_StartSequence (m_Sector, CHAN_CEILING, ani.CloseSound, 1);
	}

	m_Status = Closing;
	m_Timer = m_Speed;
	return true;
}

void DAnimatedDoor::Tick ()
{
	FDoorAnimation &ani = DoorAnimations[m_WhichDoorIndex];

	switch (m_Status)
	{
	case Dead:
		m_Sector->ceilingdata = NULL;
		Destroy ();
		break;

	case Opening:
		if (!m_Timer--)
		{
			if (++m_Frame >= ani.NumTextureFrames)
			{
				// IF DOOR IS DONE OPENING...
				m_Line1->flags &= ~ML_BLOCKING;
				m_Line2->flags &= ~ML_BLOCKING;

				if (m_Delay == 0)
				{
					m_Sector->ceilingdata = NULL;
					Destroy ();
					break;
				}

				m_Timer = m_Delay;
				m_Status = Waiting;
			}
			else
			{
				// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
				m_Timer = m_Speed;

				m_Line1->sidedef[0]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line1->sidedef[1]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line2->sidedef[0]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line2->sidedef[1]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
			}
		}
		break;

	case Waiting:
		// IF DOOR IS DONE WAITING...
		if (!m_Timer--)
		{
			if (!StartClosing())
			{
				m_Timer = m_Delay;
			}
		}
		break;

	case Closing:
		if (!m_Timer--)
		{
			if (--m_Frame < 0)
			{
				// IF DOOR IS DONE CLOSING...
				MoveCeiling (2048*FRACUNIT, m_BotDist, -1);
				m_Sector->ceilingdata = NULL;
				Destroy ();
				// Unset blocking flags on lines that didn't start with them. Since the
				// ceiling is down now, we shouldn't need this flag anymore to keep things
				// from getting through.
				if (!m_SetBlocking1)
				{
					m_Line1->flags &= ~ML_BLOCKING;
				}
				if (!m_SetBlocking2)
				{
					m_Line2->flags &= ~ML_BLOCKING;
				}
				break;
			}
			else
			{
				// IF DOOR NEEDS TO ANIMATE TO NEXT FRAME...
				m_Timer = m_Speed;

				m_Line1->sidedef[0]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line1->sidedef[1]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line2->sidedef[0]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
				m_Line2->sidedef[1]->SetTexture(side_t::mid, ani.TextureFrames[m_Frame]);
			}
		}
		break;
	}
}

IMPLEMENT_CLASS (DAnimatedDoor)

DAnimatedDoor::DAnimatedDoor ()
{
}

DAnimatedDoor::DAnimatedDoor (sector_t *sec)
	: DMovingCeiling (sec)
{
}

void DAnimatedDoor::Serialize (FArchive &arc)
{
	Super::Serialize (arc);

	FTextureID basetex = DoorAnimations[m_WhichDoorIndex].BaseTexture;

	arc << m_Line1 << m_Line2
		<< m_Frame
		<< m_Timer
		<< m_BotDist
		<< m_Status
		<< m_Speed
		<< m_Delay
		<< basetex;
	if (SaveVersion < 2336)
	{
		m_SetBlocking1 = m_SetBlocking2 = true;
	}
	else
	{
		arc << m_SetBlocking1 << m_SetBlocking2;
	}

	if (arc.IsLoading())
	{
		m_WhichDoorIndex = P_FindSlidingDoorType (basetex);
		if (m_WhichDoorIndex == -1)
		{ // Oh no! The door animation doesn't exist anymore!
			m_WhichDoorIndex = 0;
		}
	}
}

DAnimatedDoor::DAnimatedDoor (sector_t *sec, line_t *line, int speed, int delay)
	: DMovingCeiling (sec)
{
	fixed_t topdist;
	FTextureID picnum;

	// The DMovingCeiling constructor automatically sets up an interpolation for us.
	// Stop it, since the ceiling is moving instantly here.
	StopInterpolation();
	m_WhichDoorIndex = P_FindSlidingDoorType (line->sidedef[0]->GetTexture(side_t::top));
	if (m_WhichDoorIndex < 0)
	{
		Printf ("EV_SlidingDoor: Textures are not defined for sliding door!");
		m_Status = Dead;
		return;
	}

	m_Line1 = line;
	m_Line2 = line;

	for (int i = 0; i < sec->linecount; ++i)
	{
		if (sec->lines[i] == line)
			continue;

		if (sec->lines[i]->sidedef[0]->GetTexture(side_t::top) == line->sidedef[0]->GetTexture(side_t::top))
		{
			m_Line2 = sec->lines[i];
			break;
		}
	}


	picnum = m_Line1->sidedef[0]->GetTexture(side_t::top);
	m_Line1->sidedef[0]->SetTexture(side_t::mid, picnum);
	m_Line2->sidedef[0]->SetTexture(side_t::mid, picnum);

	// don't forget texture scaling here!
	FTexture *tex = TexMan[picnum];
	topdist = tex ? tex->GetScaledHeight() : 64;

	topdist = m_Sector->ceilingplane.d - topdist * m_Sector->ceilingplane.c;

	m_Status = Opening;
	m_Speed = speed;
	m_Delay = delay;
	m_Timer = m_Speed;
	m_Frame = 0;
	m_SetBlocking1 = !!(m_Line1->flags & ML_BLOCKING);
	m_SetBlocking2 = !!(m_Line2->flags & ML_BLOCKING);
	m_Line1->flags |= ML_BLOCKING;
	m_Line2->flags |= ML_BLOCKING;
	m_BotDist = m_Sector->ceilingplane.d;
	MoveCeiling (2048*FRACUNIT, topdist, 1);
	if (DoorAnimations[m_WhichDoorIndex].OpenSound != NAME_None)
	{
		SN_StartSequence (m_Sector, CHAN_INTERIOR, DoorAnimations[m_WhichDoorIndex].OpenSound, 1);
	}
}

//============================================================================
//
// EV_SlidingDoor
//
//============================================================================

bool EV_SlidingDoor (line_t *line, AActor *actor, int tag, int speed, int delay)
{
	sector_t *sec;
	int secnum;
	bool rtn;

	secnum = -1;
	rtn = false;

	if (tag == 0)
	{
		// Manual sliding door
		sec = line->backsector;

		// Make sure door isn't already being animated
		if (sec->ceilingdata != NULL)
		{
			if (actor->player == NULL)
				return false;

			if (sec->ceilingdata->IsA (RUNTIME_CLASS(DAnimatedDoor)))
			{
				DAnimatedDoor *door = barrier_cast<DAnimatedDoor *>(sec->ceilingdata);
				if (door->m_Status == DAnimatedDoor::Waiting)
				{
					return door->StartClosing();
				}
			}
			return false;
		}
		if (P_FindSlidingDoorType (line->sidedef[0]->GetTexture(side_t::top)) >= 0)
		{
			new DAnimatedDoor (sec, line, speed, delay);
			return true;
		}
		return false;
	}

	while ((secnum = P_FindSectorFromTag (tag, secnum)) >= 0)
	{
		sec = &sectors[secnum];
		if (sec->ceilingdata != NULL)
		{
			continue;
		}

		for (int i = 0; tag != 0 && i < sec->linecount; ++i)
		{
			line = sec->lines[i];
			if (line->backsector == NULL)
			{
				continue;
			}
			if (P_FindSlidingDoorType (line->sidedef[0]->GetTexture(side_t::top)) >= 0)
			{
				rtn = true;
				new DAnimatedDoor (sec, line, speed, delay);
				break;
			}
		}
	}
	return rtn;
}

void P_ParseAnimatedDoor(FScanner &sc)
{
	const BITFIELD texflags = FTextureManager::TEXMAN_Overridable | FTextureManager::TEXMAN_TryAny;
	FDoorAnimation anim;
	TArray<FTextureID> frames;
	bool error = false;
	FTextureID v;

	sc.MustGetString();
	anim.BaseTexture = TexMan.CheckForTexture (sc.String, FTexture::TEX_Wall, texflags);

	if (!anim.BaseTexture.Exists())
	{
		error = true;
	}

	while (sc.GetString ())
	{
		if (sc.Compare ("opensound"))
		{
			sc.MustGetString ();
			anim.OpenSound = sc.String;
		}
		else if (sc.Compare ("closesound"))
		{
			sc.MustGetString ();
			anim.CloseSound = sc.String;
		}
		else if (sc.Compare ("pic"))
		{
			sc.MustGetString ();
			if (IsNum (sc.String))
			{
				v = anim.BaseTexture + (atoi(sc.String) - 1);
			}
			else
			{
				v = TexMan.CheckForTexture (sc.String, FTexture::TEX_Wall, texflags);
				if (!v.Exists() && anim.BaseTexture.Exists() && !error)
				{
					sc.ScriptError ("Unknown texture %s", sc.String);
				}
				frames.Push (v);
			}
		}
		else
		{
			sc.UnGet ();
			break;
		}
	}
	if (!error)
	{
		anim.TextureFrames = new FTextureID[frames.Size()];
		memcpy (anim.TextureFrames, &frames[0], sizeof(FTextureID) * frames.Size());
		anim.NumTextureFrames = frames.Size();
		DoorAnimations.Push (anim);
	}
}
