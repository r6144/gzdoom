#include "i_musicinterns.h"
#include "c_dispatch.h"
#include "i_music.h"
#include "i_system.h"

#include "templates.h"
#include "v_text.h"
#include "menu/menu.h"

static DWORD	nummididevices;
static bool		nummididevicesset;

#ifdef _WIN32
	   UINT		mididevice;

CUSTOM_CVAR (Int, snd_mididevice, -1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	UINT oldmididev = mididevice;

	if (!nummididevicesset)
		return;

	if ((self >= (signed)nummididevices) || (self < -5))
	{
		Printf ("ID out of range. Using default device.\n");
		self = 0;
		return;
	}
	else if (self < 0)
	{
		mididevice = 0;
	}
	else
	{
		mididevice = self;
	}

	// If a song is playing, move it to the new device.
	if (oldmididev != mididevice && currSong != NULL && currSong->IsMIDI())
	{
		MusInfo *song = currSong;
		if (song->m_Status == MusInfo::STATE_Playing)
		{
			I_StopSong (song);
			I_PlaySong (song, song->m_Looping);
		}
	}
}

void I_InitMusicWin32 ()
{
	nummididevices = midiOutGetNumDevs ();
	nummididevicesset = true;
	snd_mididevice.Callback ();
}

void I_ShutdownMusicWin32 ()
{
	// Ancient bug a saw on NT 4.0 and an old version of FMOD 3: If waveout
	// is used for sound and a MIDI is also played, then when I quit, the OS
	// tells me a free block was modified after being freed. This is
	// apparently a synchronization issue between two threads, because if I
	// put this Sleep here after stopping the music but before shutting down
	// the entire sound system, the error does not happen. Observed with a
	// Vortex 2 (may Aureal rest in peace) and an Audigy (damn you, Creative!).
	// I no longer have a system with NT4 drivers, so I don't know if this
	// workaround is still needed or not.
	if (OSPlatform == os_WinNT4)
	{
		Sleep(50);
	}
}

#ifdef HAVE_FLUIDSYNTH
#define NUM_DEF_DEVICES 4
#else
#define NUM_DEF_DEVICES 3
#endif
void I_BuildMIDIMenuList (FOptionValues *opt)
{
	int p;
	FOptionValues::Pair *pair = &opt->mValues[opt->mValues.Reserve(NUM_DEF_DEVICES)];
#ifdef HAVE_FLUIDSYNTH
	pair[0].Text = "FluidSynth";
	pair[0].Value = -5.0;
	p = 1;
#else
	p = 0;
#endif
	pair[p].Text = "OPL Synth Emulation";
	pair[p].Value = -3.0;
	pair[p+1].Text = "TiMidity++";
	pair[p+1].Value = -2.0;
	pair[p+2].Text = "FMOD";
	pair[p+2].Value = -1.0;


	for (DWORD id = 0; id < nummididevices; ++id)
	{
		MIDIOUTCAPS caps;
		MMRESULT res;

		res = midiOutGetDevCaps (id, &caps, sizeof(caps));
		assert(res == MMSYSERR_NOERROR);
		if (res == MMSYSERR_NOERROR)
		{
			pair = &opt->mValues[opt->mValues.Reserve(1)];
			pair->Text = caps.szPname;
			pair->Value = (float)id;
		}
	}
}

static void PrintMidiDevice (int id, const char *name, WORD tech, DWORD support)
{
	if (id == snd_mididevice)
	{
		Printf (TEXTCOLOR_BOLD);
	}
	Printf ("% 2d. %s : ", id, name);
	switch (tech)
	{
	case MOD_MIDIPORT:		Printf ("MIDIPORT");		break;
	case MOD_SYNTH:			Printf ("SYNTH");			break;
	case MOD_SQSYNTH:		Printf ("SQSYNTH");			break;
	case MOD_FMSYNTH:		Printf ("FMSYNTH");			break;
	case MOD_MAPPER:		Printf ("MAPPER");			break;
	case MOD_WAVETABLE:		Printf ("WAVETABLE");		break;
	case MOD_SWSYNTH:		Printf ("SWSYNTH");			break;
	}
	if (support & MIDICAPS_CACHE)
	{
		Printf (" CACHE");
	}
	if (support & MIDICAPS_LRVOLUME)
	{
		Printf (" LRVOLUME");
	}
	if (support & MIDICAPS_STREAM)
	{
		Printf (" STREAM");
	}
	if (support & MIDICAPS_VOLUME)
	{
		Printf (" VOLUME");
	}
	Printf (TEXTCOLOR_NORMAL "\n");
}

CCMD (snd_listmididevices)
{
	UINT id;
	MIDIOUTCAPS caps;
	MMRESULT res;

#ifdef HAVE_FLUIDSYNTH
	PrintMidiDevice (-5, "FluidSynth", MOD_SWSYNTH, 0);
#endif
	PrintMidiDevice (-3, "Emulated OPL FM Synth", MOD_FMSYNTH, 0);
	PrintMidiDevice (-2, "TiMidity++", 0, MOD_SWSYNTH);
	PrintMidiDevice (-1, "FMOD", 0, MOD_SWSYNTH);
	if (nummididevices != 0)
	{
		for (id = 0; id < nummididevices; ++id)
		{
			res = midiOutGetDevCaps (id, &caps, sizeof(caps));
			if (res == MMSYSERR_NODRIVER)
				strcpy (caps.szPname, "<Driver not installed>");
			else if (res == MMSYSERR_NOMEM)
				strcpy (caps.szPname, "<No memory for description>");
			else if (res != MMSYSERR_NOERROR)
				continue;

			PrintMidiDevice (id, caps.szPname, caps.wTechnology, caps.dwSupport);
		}
	}
}

#else

// Everything but Windows uses this code.

CUSTOM_CVAR(Int, snd_mididevice, -1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < -5)
		self = -5;
	else if (self > -1)
		self = -1;
}

#ifdef HAVE_FLUIDSYNTH
#define NUM_DEF_DEVICES 4
#else
#define NUM_DEF_DEVICES 3
#endif
void I_BuildMIDIMenuList (FOptionValues *opt)
{
	int p;
	FOptionValues::Pair *pair = &opt->mValues[opt->mValues.Reserve(NUM_DEF_DEVICES)];
#ifdef HAVE_FLUIDSYNTH
	pair[0].Text = "FluidSynth";
	pair[0].Value = -5.0;
	p = 1;
#else
	p = 0;
#endif
	pair[p].Text = "OPL Synth Emulation";
	pair[p].Value = -3.0;
	pair[p+1].Text = "TiMidity++";
	pair[p+1].Value = -2.0;
	pair[p+2].Text = "FMOD";
	pair[p+2].Value = -1.0;
}

CCMD (snd_listmididevices)
{
#ifdef HAVE_FLUIDSYNTH
	Printf("%s-5. FluidSynth\n", -5 == snd_mididevice ? TEXTCOLOR_BOLD : "");
#endif
	Printf("%s-3. Emulated OPL FM Synth\n", -3 == snd_mididevice ? TEXTCOLOR_BOLD : "");
	Printf("%s-2. TiMidity++\n", -2 == snd_mididevice ? TEXTCOLOR_BOLD : "");
	Printf("%s-1. FMOD\n", -1 == snd_mididevice ? TEXTCOLOR_BOLD : "");
}
#endif
