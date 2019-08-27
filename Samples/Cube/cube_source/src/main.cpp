// main.cpp: initialisation & main loop

#include "cube.h"
#include <AK/Tools/Common/AkPlatformFuncs.h>

bool bWantToQuit = false;
void quit()                     // normal exit
{
    writeservercfg();
	bWantToQuit = true;
};

void fatal(const char *s, const char *o)    // failure exit
{
    sprintf_sd(msg)("%s%s\n", s, o);
	exit(1);
};

void *alloc(int s)              // for some big chunks... most other allocs use the memory pool
{
    void *b = calloc(1,s);
    if(!b) fatal("out of memory!");
    return b;
};

int scr_w = 1024;
int scr_h = 768;
bool dedicated = false;
int  maxcl = 4;
int uprate = 0;
char *sdesc = "", *ip = "", *master = NULL, *passwd = "";

#ifdef _DEBUG
bool fs = false;
#else
bool fs = true;
#endif

COMMAND(quit, ARG_NONE);

VARF(	gamespeed, 10, 100, 1000,
		if(multiplayer())
		gamespeed = 100	);

VARP(minmillis, 0, 10, 1000);

int islittleendian = 1;
int framesinmap = 0;

void Cube_Arg( char * arg )
{
	char *a = arg + 2;
	if ( arg[0] == '-' ) switch( arg[1] )
	{
		case 'd': dedicated = true; break;
		case 't': fs     = false; break;
		case 'w': scr_w  = atoi(a); break;
		case 'h': scr_h  = atoi(a); break;
		case 'u': uprate = atoi(a); break;
		case 'n': sdesc  = a; break;
		case 'i': ip     = a; break;
		case 'm': master = a; break;
		case 'p': passwd = a; break;
		case 'c': maxcl  = atoi(a); break;
	    default:  conoutf("unknown commandline option");
	}
	else conoutf("unknown commandline argument");
}

bool Cube_Init()
{
	#define log(s) conoutf("init: %s", s)
    log("net");


	if(enet_initialize()<0) fatal("Unable to initialise network module");
    log("sound");
    snd_init();

	player1 = newdynent( M_NONE, "Local Player" ); // this needs to happen after snd_init ( since it registers a game object )
    initclient();

    initserver(dedicated, uprate, sdesc, ip, master, passwd, maxcl);  // never returns if dedicated
	log("world");
	empty_world(7, true);

	log("gl");
	gl_init(scr_w, scr_h);

	int xs, ys;
	log("basetex");
	if(!installtex(2,  path(newstring("data" S_PATH "newchars.jpg")), xs, ys) ||
       !installtex(3,  path(newstring("data" S_PATH "martin" S_PATH "base.jpg")), xs, ys) ||
       !installtex(6,  path(newstring("data" S_PATH "martin" S_PATH "ball1.jpg")), xs, ys) ||
       !installtex(7,  path(newstring("data" S_PATH "martin" S_PATH "smoke.jpg")), xs, ys) ||
       !installtex(8,  path(newstring("data" S_PATH "martin" S_PATH "ball2.jpg")), xs, ys) ||
       !installtex(9,  path(newstring("data" S_PATH "martin" S_PATH "ball3.jpg")), xs, ys) ||
       !installtex(4,  path(newstring("data" S_PATH "explosion.jpg")), xs, ys) ||
       !installtex(5,  path(newstring("data" S_PATH "items.jpg")), xs, ys) ||
       !installtex(1,  path(newstring("data" S_PATH "crosshair.jpg")), xs, ys)) fatal("could not find core textures (hint: run cube from the parent of the bin directory)");

    Cube_KeyRepeat(false);
	log("cfg");
	
    newmenu("frags\tpj\tping\tteam\tname");
    newmenu("ping\tplr\tserver");
    exec("data" S_PATH "keymap.cfg");
    exec("data" S_PATH "menus.cfg");
    exec("data" S_PATH "prefabs.cfg");
    exec("data" S_PATH "sounds.cfg");
    exec("servers.cfg");
    if(!execfile("config.cfg")) execfile("data" S_PATH "defaults.cfg");
    exec("autoexec.cfg");
	
	log("localconnect");
	localconnect();
	changemap("metl3");		// if this map is changed, also change depthcorrect()
	return true;
}

void Cube_Term()
{
	stop();
	disconnect(true);
    writecfg();
    cleangl();
    snd_term();
    cleanupserver();
}

bool Cube_Tick()
{
	static float timeStart = 0, timeEnd = 0; // for speed throttling

	float millis = Cube_GetTicks()*gamespeed/100;
    if(millis-lastmillis>200)
		lastmillis = millis-200;
    else if(millis-lastmillis<1)
		lastmillis = millis-1;

	timeEnd = Cube_GetTicks();

	// speed throttling -- only works when vertical sync is off
    if( ( minmillis-(timeEnd - timeStart) ) >= 1.0f ) 
	{
		Cube_Delay((unsigned int) ( minmillis-(timeEnd - timeStart) ));
	}

	timeStart = Cube_GetTicks();

    cleardlights();
    updateworld(millis);

	if(!demoplayback) serverslice((int)time(NULL), 0);
    snd_update();

    static float fps = 30.0f;
    fps = (1000.0f/curtime+fps*10)/11;
    computeraytable(player1->o.x, player1->o.y);

	// the next two operations block when vertical sync is on
	readdepth(scr_w, scr_h);
	Cube_SwapBuffers();

	if(framesinmap++<5)	// cheap hack to get rid of initial sparklies, even when triple buffering etc.
    {
		player1->yaw += 5;
		gl_drawframe(scr_w, scr_h, fps);
		player1->yaw -= 5;
    };

    gl_drawframe(scr_w, scr_h, fps);
	return !bWantToQuit;
}

AkInt64 timerFreq, timerInitialCnt;
SDL_Window* displayWindow = NULL;

void Cube_PasteTextFromClipboard( string buf )
{
}

void Cube_SetGamma(float r, float g, float b)
{
	return;
}

void Cube_SwapBuffers()
{
	SDL_GL_SwapWindow( displayWindow );
}

void Cube_KeyRepeat( bool on )
{
}

void Cube_Delay( unsigned int ms )
{
    AKPLATFORM::AkSleep( ms );
}

float Cube_GetTicks()
{
	AkInt64 timerCnt;
	AKPLATFORM::PerformanceCounter( &timerCnt );

	return (float) ( (double) (timerCnt - timerInitialCnt) / (double) ( timerFreq / 1000 ) );
}

// WinMain

extern int scr_w;
extern int scr_h;
extern bool saycommandon;

#ifdef AK_WIN

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int iCmdShow)
{
	// parse command-line

	char * token = strtok( lpCmdLine, " " );
	while( token != NULL )
	{
		Cube_Arg( token );
		token = strtok( NULL, " " );
	}

#else

int main( int argc, char * argv[] )
{
    for ( int i = 1; i < argc; ++i )
        Cube_Arg( argv[i] );
#endif

	// get initial timer values
	AKPLATFORM::PerformanceFrequency( &timerFreq );
	AKPLATFORM::PerformanceCounter( &timerInitialCnt );
	
	SDL_Init(SDL_INIT_VIDEO);
 
	displayWindow = SDL_CreateWindow( "Cube", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, scr_w, scr_h, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);

	SDL_GLContext glcontext = SDL_GL_CreateContext(displayWindow);

 	Cube_Init();

	SDL_SetRelativeMouseMode(SDL_TRUE);

	// program main loop
	bool skipnexttext = false;
	bool quit = false;
	while ( !quit )
	{
		SDL_Event sdlEvent;

		while ( SDL_PollEvent( &sdlEvent ) ) 
		{
			switch ( sdlEvent.type )
			{
			case SDL_QUIT:
				quit = true;
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				keypress( -sdlEvent.button.button, sdlEvent.type == SDL_MOUSEBUTTONDOWN );
				break;

			case SDL_MOUSEWHEEL:
				if ( sdlEvent.wheel.y > 0 )
					keypress( -4, true );
				else
					keypress( -5, true );
				break;

            case SDL_MOUSEMOTION:
                mousemove(sdlEvent.motion.xrel, sdlEvent.motion.yrel);
                break;
			
			case SDL_KEYDOWN:
				{
					bool saycommandon_prev = saycommandon;
					keypress( (int) sdlEvent.key.keysym.sym, true );
					if ( saycommandon && !saycommandon_prev )
						skipnexttext = true;
				}
				break;
			case SDL_KEYUP:
				keypress( (int) sdlEvent.key.keysym.sym, false );
				break;
			case SDL_TEXTINPUT:
				if ( !skipnexttext )
				{
					for ( int i = 0, c = strlen( sdlEvent.text.text ); i < c; ++i )
						charpress( sdlEvent.text.text[i] );
				}
				skipnexttext = false;
				break;
/*			case WM_CHAR:
				charpress( (char) wParam );
				return 0;
*/
			}
		}

		if ( !Cube_Tick() )
			quit = true;
	}

	Cube_Term();

	SDL_GL_DeleteContext(glcontext);

	return (int) 0; // msg.wParam;
}
