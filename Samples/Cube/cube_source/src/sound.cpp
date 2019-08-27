// sound.cpp: ak audio engine hooks

#include "cube.h"

#undef min
#undef max

#include <algorithm>
#include <list>

#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkModule.h>
#include <AK/MusicEngine/Common/AkMusicEngine.h>
#include <AK/Tools/Common/AkLock.h>
#include <AK/Tools/Common/AkMonitorError.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>
#include <AkDefaultIOHookBlocking.h>

#ifndef AK_OPTIMIZED
#include <AK/Comm/AkCommunication.h>
#endif

static const AkGameObjectID LISTENER_ID = 10001;

#include <AK/Plugin/AllPluginsRegistrationHelpers.h>

namespace AK
{
	void * AllocHook(size_t in_size)
	{
		return malloc(in_size);
	}

	void FreeHook(void * in_ptr)
	{
		free(in_ptr);
	}

#ifdef AK_WIN
	void * VirtualAllocHook(
		void * in_pMemAddress,
		size_t in_size,
		DWORD in_dwAllocationType,
		DWORD in_dwProtect
	)
	{
		return VirtualAlloc(in_pMemAddress, in_size, in_dwAllocationType, in_dwProtect);
	}

	void VirtualFreeHook(
		void * in_pMemAddress,
		size_t in_size,
		DWORD in_dwFreeType
	)
	{
		VirtualFree(in_pMemAddress, in_size, in_dwFreeType);
	}
#endif
}

CAkDefaultIOHookBlocking g_lowLevelIO;

using namespace AK;

AkAuxBusID g_PlayerEnvID = 0;
unsigned long g_envMAP[255];

//
// sound-related globals
//

extern string bnkname;								// name of map-specific bank

vector<baseent *> regent;							// entities registered as game objects

//
// forward declarations
//

void soundvol( int vol );
void musicvol( int vol );
void voicevol( int vol );

#define CHECK_SOUND_ENGINE if( !SoundEngine::IsInitialized() ) return;

#define CHECK_SOUND_ENGINE0 if( !SoundEngine::IsInitialized() ) return 0;

void akevent(char *name);


// register entity as game object in audio engine
void snd_registent( baseent * ent, char * in_name )
{
	snd_registptr( ent, in_name );
	regent.add( ent );
	AK::SoundEngine::RegisterGameObj(LISTENER_ID, "Listener (Default)");
	AK::SoundEngine::SetDefaultListeners(&LISTENER_ID, 1);
}

// unregister entity as game object in audio engine
void snd_unregistent( baseent * ent )
{
	CHECK_SOUND_ENGINE;

	for ( int i = 0; i < regent.length(); i++ )
	{
		if ( regent[i] == ent )
		{
			regent.remove( i );
			SoundEngine::UnregisterGameObj( (AkGameObjectID) ent );

			break;
		}
	}
}

// register entity as game object in audio engine
void snd_registptr( void * ent, char * in_name )
{
	CHECK_SOUND_ENGINE;

	if ( in_name == NULL )
	{
		static int cEntity = 0;
		char name[256];
		sprintf( name, "Entity #%i", (int) ++cEntity );

		SoundEngine::RegisterGameObj( (AkGameObjectID) ent, name );
	}
	else
	{
		SoundEngine::RegisterGameObj( (AkGameObjectID) ent, in_name );
	}
}

// unregister entity as game object in audio engine
void snd_unregistptr( void * ent )
{
	CHECK_SOUND_ENGINE;

	SoundEngine::UnregisterGameObj( (AkGameObjectID) ent );
}

// post a message in the wwise capture list
void snd_message( char * szMessage )
{
	Monitor::PostString( szMessage, Monitor::ErrorLevel_Message );
}

// query material id from name
int snd_getmaterialid( char * name )
{
	CHECK_SOUND_ENGINE0;

	return SoundEngine::GetIDFromString( name );
}

// set current material id of entity
void snd_setmaterial( void * ent, int materialid )
{
	CHECK_SOUND_ENGINE;
	
	SoundEngine::SetSwitch( SWITCHES::MATERIAL::GROUP, materialid, (AkGameObjectID) ent );
}

void snd_setfoot( void * ent, bool right )
{
	CHECK_SOUND_ENGINE;
	
	SoundEngine::SetSwitch( SWITCHES::FOOT::GROUP, right ? SWITCHES::FOOT::SWITCH::RIGHT : SWITCHES::FOOT::SWITCH::LEFT, (AkGameObjectID) ent );
}

// load map-specific bank file. assumes map loading code sets bnkname to the correct file path.
void snd_loadmapbank()
{
	CHECK_SOUND_ENGINE;

	AkBankID bankID;
	if ( bnkname[0] && SoundEngine::LoadBank(bnkname, AK_DEFAULT_POOL_ID, bankID) != AK_Success)
	{
		bnkname[0] = 0; // so we don't try to unload it
	}

	akevent( "Map_Loaded" );

}

// unload map-specific bank file
void snd_unloadmapbank()
{
	CHECK_SOUND_ENGINE;

	if ( !bnkname[0] )
		return;

	SoundEngine::UnloadBank( bnkname, NULL );
}

// initialize sound engine
void snd_init()
{
	// Initialize audio engine
	// Memory.
    AkMemSettings memSettings;
	
	memSettings.uMaxNumPools = 20;

    // Streaming.
    AkStreamMgrSettings stmSettings;
	StreamMgr::GetDefaultSettings( stmSettings );
	
    AkDeviceSettings deviceSettings;
	StreamMgr::GetDefaultDeviceSettings( deviceSettings );

	AkInitSettings l_InitSettings;
	AkPlatformInitSettings l_platInitSetings;
	SoundEngine::GetDefaultInitSettings( l_InitSettings );
	SoundEngine::GetDefaultPlatformInitSettings( l_platInitSetings );

	// Setting pool sizes for this game. Here, allow for user content; every game should determine its own optimal values.
	l_InitSettings.uDefaultPoolSize				=  2*1024*1024;
	l_platInitSetings.uLEngineDefaultPoolSize	=  4*1024*1024;

	AkMusicSettings musicInit;
	MusicEngine::GetDefaultInitSettings( musicInit );

	// Create and initialise an instance of our memory manager.
	if ( MemoryMgr::Init( &memSettings ) != AK_Success )
	{
		AKASSERT( !"Could not create the memory manager." );
		return;
	}

	// Create and initialise an instance of the default stream manager.
	if ( !StreamMgr::Create( stmSettings ) )
	{
		AKASSERT( !"Could not create the Stream Manager" );
		return;
	}

	// Create an IO device.
	if ( g_lowLevelIO.Init( deviceSettings ) != AK_Success )
	{
		AKASSERT( !"Cannot create streaming I/O device" );
		return;
	}
	
	// Initialize sound engine.
	if ( SoundEngine::Init( &l_InitSettings, &l_platInitSetings ) != AK_Success )
	{
		AKASSERT( !"Cannot initialize sound engine" );
		return;
	}

	// Initialize music engine.
	if ( MusicEngine::Init( &musicInit ) != AK_Success )
	{
		AKASSERT( !"Cannot initialize music engine" );
		return;
	}

	// load initialization and main soundbanks

#ifdef AK_WIN
	g_lowLevelIO.SetBasePath( L"soundbanks\\Windows\\" );
#else
	g_lowLevelIO.SetBasePath( AKTEXT( "soundbanks/Mac/" ) );
#endif

	AK::StreamMgr::SetCurrentLanguage( AKTEXT( "English(US)" ) );

#ifndef AK_OPTIMIZED
	// Initialize communication.
	AkCommSettings settingsComm;
	AK::Comm::GetDefaultInitSettings( settingsComm );
	AKPLATFORM::SafeStrCpy(settingsComm.szAppNetworkName, "Cube", AK_COMM_SETTINGS_MAX_STRING_SIZE);
	if ( AK::Comm::Init( settingsComm ) != AK_Success )
	{
		AKASSERT( !"Cannot initialize music communication" );
		return;
	}
#endif // AK_OPTIMIZED

	AkBankID bankID;
	AKRESULT retValue;
	retValue = SoundEngine::LoadBank( "Init.bnk", AK_DEFAULT_POOL_ID, bankID );
	assert(retValue == AK_Success);

	retValue = SoundEngine::LoadBank( "main.bnk", AK_DEFAULT_POOL_ID, bankID );
	assert(retValue == AK_Success);


	// initialize volume parameters to sensible default values

	musicvol( 255 );
	soundvol( 255 );
	voicevol( 255 );

	g_envMAP[0] = 0; //hardcode environment 0 to id=0
	for(int i=1; i<255; i++)
	{
		g_envMAP[i] = -1;
	}
};

// terminate sound engine
void snd_term()
{
	// Terminate audio engine

#ifndef AK_OPTIMIZED
	Comm::Term();
#endif // AK_OPTIMIZED

	MusicEngine::Term();

	SoundEngine::Term();

	g_lowLevelIO.Term();
	if ( IAkStreamMgr::Get() )
		IAkStreamMgr::Get()->Destroy();
		
	MemoryMgr::Term();
};

static void vecToAkVector( const vec & in_vec, AkVector & out_vector )
{
	out_vector.X = in_vec.x;
	out_vector.Y = in_vec.y;
	out_vector.Z = in_vec.z;
}

extern float rad(float x);

static void baseentToAkTransform(baseent * in_ent, AkTransform& out_transform)
{
	float fRadPitch = rad(in_ent->pitch);
	float fCosPitch = cos(fRadPitch);
	float fSinPitch = sin(fRadPitch);

	float fRadYaw = rad(in_ent->yaw - 90);
	float fCosYaw = cos(fRadYaw);
	float fSinYaw = sin(fRadYaw);

	out_transform.SetOrientation(
		fCosYaw * fCosPitch,
		fSinYaw * fCosPitch,
		fSinPitch,
		-fCosYaw * fSinPitch,
		-fSinYaw * fSinPitch,
		fCosPitch
	);

	out_transform.SetPosition(
		in_ent->o.x,
		in_ent->o.y,
		in_ent->o.z
	);
}

void snd_setpos( void * ent, float x, float y, float z )
{
	CHECK_SOUND_ENGINE;

	AkVector position = { x, y, z };
	AkVector front = { 0, 0, 1.0f };
	AkVector top = { 0, 1.0f, 0 };
	AkSoundPosition snd;
	snd.Set(position, front, top);
	SoundEngine::SetPosition( (AkGameObjectID) ent, snd );
}

void snd_stopall( void * ent )
{
	SoundEngine::StopAll( (AkGameObjectID) ent );
}

// update sound engine (called from main loop, once per frame)
void snd_update() 
{
	CHECK_SOUND_ENGINE;

	// set listener position

	AkTransform listener;
	baseentToAkTransform(player1, listener);

	SoundEngine::SetPosition(LISTENER_ID, listener);

	// set entities positions

	for ( int i = 0; i < regent.length(); i++ )
	{
		baseent * ent = regent[i];

		AkTransform snd;
		baseentToAkTransform(ent, snd);

		SoundEngine::SetPosition((AkGameObjectID)ent, snd);

#define CHECKSIZE 5

		uchar playerEnv = S((int)ent->o.x, (int)ent->o.y)->env;
		uchar envCount[256] = { 0 };

		for(int x=AkMax(0,(int)ent->o.x - CHECKSIZE); x<AkMin((int)ent->o.x + CHECKSIZE, ssize); x++)
		{
			for(int y=AkMax(0,(int)ent->o.y - CHECKSIZE); y<AkMin((int)ent->o.y + CHECKSIZE, ssize); y++)
			{
				//place all encountered environments in our EnvCount array
				sqr *currsqr = S(x, y);
				envCount[currsqr->env]++;
			}
		}

		AkAuxSendValue envVal[4];

		uchar envIndex = 0;
		for(int i=1; i<255; i++) //skip i=0
		{
			//find the first 4 environments with count>0
			if(envCount[i] > 0)
			{
				envVal[envIndex].listenerID = AK_INVALID_GAME_OBJECT;
				envVal[envIndex].auxBusID = g_envMAP[i];
				envVal[envIndex].fControlValue = (AkReal32)envCount[i] / ( ( CHECKSIZE * 2 ) * ( CHECKSIZE * 2 ) );
				envIndex++;
				if(envIndex >= 4)
					break;
			}
		}

		SoundEngine::SetGameObjectAuxSendValues( (AkGameObjectID) ent, &envVal[0], envIndex);
		if ( ent == player1 ) g_PlayerEnvID = g_envMAP[playerEnv]; //for debug
	}

	SoundEngine::RenderAudio();
};

// post a client event (attached to player, replicated to network peers)
void snd_clientevent(int event) 
{ 
	CHECK_SOUND_ENGINE;

 	addmsg(0, 2, SV_SOUND, (int) event ); 
	SoundEngine::PostEvent( event, (AkGameObjectID) player1 );
}

// set a wwise state
void snd_state(int stategroup, int state)
{
	CHECK_SOUND_ENGINE;

	SoundEngine::SetState( stategroup, state );
}

// set a wwise game parameter (at global scope)
void snd_gameparam(int param, float value)
{
	CHECK_SOUND_ENGINE;

	SoundEngine::SetRTPCValue( param, value );
}

static std::list<int> dummies; // currently playing dummies
static CAkLock dummies_lock;

static void dummy_callback( 
	AkCallbackType in_eType, 
	AkCallbackInfo* in_pCallbackInfo 
	)
{
	AkEventCallbackInfo * pInfo = (AkEventCallbackInfo *) in_pCallbackInfo;
	AkGameObjectID id = pInfo->gameObjID;

	dummies_lock.Lock();
	std::list<int>::iterator it = std::find( dummies.begin(), dummies.end(), pInfo->playingID );
	if ( it != dummies.end() )
	{
		dummies.erase( it );
		AK::SoundEngine::UnregisterGameObj( id );
	}
	dummies_lock.Unlock();
}

// post an event attached to the specified location in world
void snd_event(int event, vec *loc)
{
	CHECK_SOUND_ENGINE;

	if ( loc == NULL )
		SoundEngine::PostEvent( event, (AkGameObjectID) player1 );
	else
	{
		dummies_lock.Lock();
		dummies.push_back(0);
		int & dummy = dummies.back();
		dummies_lock.Unlock();

		SoundEngine::RegisterGameObj( (AkGameObjectID)&dummy, "PlayAtLocation" );

		AkVector front = { 0, 0, 1.0f };
		AkVector top = { 0, 1.0f, 0 };
		AkVector position;
		vecToAkVector(*loc, position);
		
		AkSoundPosition snd;
		snd.Set(position, front, top);

		SoundEngine::SetPosition( (AkGameObjectID)&dummy, snd );
		AkPlayingID id = SoundEngine::PostEvent( event, (AkGameObjectID)&dummy, AK_EndOfEvent, dummy_callback );
		if ( id != AK_INVALID_PLAYING_ID )
			dummy = id;
		else
		{
			SoundEngine::UnregisterGameObj( (AkGameObjectID)&dummy );
			dummies_lock.Lock();
			dummies.pop_back();
			dummies_lock.Unlock();
		}
	}
};

// post an event attached to the specified entity
void snd_event( int event, void * ent )
{
	CHECK_SOUND_ENGINE;

	SoundEngine::PostEvent( event, (AkGameObjectID) ent );
};

// post an event by name, attached to the specified location in world
void snd_event(char * name, vec *loc)
{
	CHECK_SOUND_ENGINE;

	AkUniqueID event = SoundEngine::GetIDFromString( name );
	if ( event != AK_INVALID_UNIQUE_ID )
		snd_event( (int) event, loc );
};

// post an event by name, attached to the specified entity
void snd_event(char * name, void * ent )
{
	CHECK_SOUND_ENGINE;

	SoundEngine::PostEvent( name, (AkGameObjectID) ent );
};

//
// sound-related scripting commands
//

// post an event on local player game object
void akevent(char *name)
{
	CHECK_SOUND_ENGINE;

	snd_event( name, player1 );
}
COMMAND(akevent, ARG_1STR);

// set the music volume (0-255)
void musicvol( int vol )
{
	CHECK_SOUND_ENGINE;

	SoundEngine::SetRTPCValue( GAME_PARAMETERS::MUSICVOLUME, (AkRtpcValue) vol );
}
COMMAND(musicvol, ARG_1INT);

// set the sound effects volume (0-255)
void soundvol( int vol )
{
	CHECK_SOUND_ENGINE;
	
	SoundEngine::SetRTPCValue( GAME_PARAMETERS::SFXVOLUME, (AkRtpcValue) vol );
}
COMMAND(soundvol, ARG_1INT);

// set the voice volume (0-255)
void voicevol( int vol )
{
	CHECK_SOUND_ENGINE;

	SoundEngine::SetRTPCValue( GAME_PARAMETERS::VOICEVOLUME, (AkRtpcValue) vol );
}
COMMAND(voicevol, ARG_1INT);

//
// deprecated scripting commands related to old music code
//

void sound(int n) 
{ 
	CHECK_SOUND_ENGINE;

	char notif[256];
	sprintf( notif, "Deprecated call: sound %d", n );
	Monitor::PostString( notif, Monitor::ErrorLevel_Message );
}
COMMAND(sound, ARG_1INT);

void music(char *name)
{
	CHECK_SOUND_ENGINE;

	char notif[256];
	sprintf( notif, "Deprecated call: music %s", name );
	Monitor::PostString( notif, Monitor::ErrorLevel_Message );
};

COMMAND(music, ARG_1STR);

int registersound(char *name)
{
	CHECK_SOUND_ENGINE0;

	char notif[256];
	sprintf( notif, "Deprecated call: registersound %s", name );
	Monitor::PostString( notif, Monitor::ErrorLevel_Message );

	return 0;
};
COMMAND(registersound, ARG_1EST);

void regenv(char *name)
{
	static int envID = 1; //start at 1
	unsigned long wiseID = SoundEngine::GetIDFromString(name);
	g_envMAP[envID++] = wiseID;
}

COMMAND(regenv, ARG_1EST);
