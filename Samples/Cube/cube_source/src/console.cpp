// console.cpp: the console buffer, its display, and command line control

#include "cube.h"
#include <ctype.h>

struct cline { char *cref; float outtime; };
vector<cline> conlines;

const int ndraw = 5;
const int WORDWRAP = 80;
int conskip = 0;

bool saycommandon = false;
string commandbuf;

void setconskip(int n)
{
    conskip += n;
    if(conskip<0) conskip = 0;
};

COMMANDN(conskip, setconskip, ARG_1INT);

void conline(const char *sf, bool highlight)        // add a line to the console buffer
{
    cline cl;
    cl.cref = conlines.length()>100 ? conlines.pop().cref : newstringbuf("");   // constrain the buffer size
    cl.outtime = lastmillis;                        // for how long to keep line on screen
    conlines.insert(0,cl);
    if(highlight)                                   // show line in a different colour, for chat etc.
    {
        cl.cref[0] = '\f';
        cl.cref[1] = 0;
        strcat_s(cl.cref, sf);
    }
    else
    {
        strcpy_s(cl.cref, sf);
    };
    puts(cl.cref);
    #ifndef WIN32
        fflush(stdout);
    #endif
};

void conoutf(const char *s, ...)
{
    sprintf_sdv(sf, s);
    s = sf;
    int n = 0;
    while(strlen(s)>WORDWRAP)                       // cut strings to fit on screen
    {
        string t;
        strn0cpy(t, s, WORDWRAP+1);
        conline(t, n++!=0);
        s += WORDWRAP;
    };
    conline(s, n!=0);
};

void renderconsole()                                // render buffer taking into account time & scrolling
{
    int nd = 0;
    char *refs[ndraw];
    loopv(conlines) if(conskip ? i>=conskip-1 || i>=conlines.length()-ndraw : lastmillis-conlines[i].outtime<20000)
    {
        refs[nd++] = conlines[i].cref;
        if(nd==ndraw) break;
    };
    loopj(nd)
    {
        draw_text(refs[j], FONTH/3, (FONTH/4*5)*(nd-j-1)+FONTH/3, 2);
    };
};

// keymap is defined externally in keymap.cfg

struct keym { int code; char *name; char *action; } keyms[256];
int numkm = 0;                                     

void keymap(char *code, char *key, char *action)
{
    keyms[numkm].code = atoi(code);
    keyms[numkm].name = newstring(key);
    keyms[numkm++].action = newstringbuf(action);
};

COMMAND(keymap, ARG_3STR);

void bindkey(char *key, char *action)
{
    for(char *x = key; *x; x++) *x = toupper(*x);
    loopi(numkm) if(strcmp(keyms[i].name, key)==0)
    {
        strcpy_s(keyms[i].action, action);
        return;
    };
    conoutf("unknown key \"%s\"", key);   
};

COMMANDN(bind, bindkey, ARG_2STR);

void saycommand(char *init)                         // turns input to the command line on or off
{
    saycommandon = (init!=NULL);
    if(!editmode) Cube_KeyRepeat(saycommandon);
    if(!init) init = "";
    strcpy_s(commandbuf, init);
};

void mapmsg(char *s) { strn0cpy(hdr.maptitle, s, 128); };

COMMAND(saycommand, ARG_VARI);
COMMAND(mapmsg, ARG_1STR);

void pasteconsole()
{
	Cube_PasteTextFromClipboard( commandbuf );
};

cvector vhistory;
int histpos = 0;

void history(int n)
{
    static bool rec = false;
    if(!rec && n>=0 && n<vhistory.length())
    {
        rec = true;
        execute(vhistory[vhistory.length()-n-1]);
        rec = false;
    };
};

COMMAND(history, ARG_1INT);

void charpress( char ch )
{
	if ( saycommandon )
	{
		resetcomplete();
		
		char add[] = { ch, 0 }; 
		strcat_s(commandbuf, add); 
	}
}

bool keypress(int code, bool isdown)
{
	if(saycommandon)                                // keystrokes go to commandline
    {
        if(isdown)
        {
            switch(code)
            {
                case SDLK_RETURN:
                    return true;

                case SDLK_BACKSPACE:
                case SDLK_LEFT:
                {
                    for(int i = 0; commandbuf[i]; i++) if(!commandbuf[i+1]) commandbuf[i] = 0;
                    resetcomplete();
                    return true;
                };
                    
                case SDLK_UP:
                    if(histpos) strcpy_s(commandbuf, vhistory[--histpos]);
                    return true;
                
                case SDLK_DOWN:
                    if(histpos<vhistory.length()) strcpy_s(commandbuf, vhistory[histpos++]);
                    return true;
                    
                case SDLK_TAB:
                    complete(commandbuf);
                    return true;

                case SDLK_v:
                    if(SDL_GetModState()&(KMOD_LCTRL|KMOD_RCTRL)) { pasteconsole(); return true; };

            };
        }
        else
        {
            if(code==SDLK_RETURN)
            {
                if(commandbuf[0])
                {
                    if(vhistory.empty() || strcmp(vhistory.last(), commandbuf))
                    {
                        vhistory.add(newstring(commandbuf));  // cap this?
                    };
                    histpos = vhistory.length();
                    if(commandbuf[0]=='/') execute(commandbuf, true);
                    else toserver(commandbuf);
                };
                saycommand(NULL);

				return true;
            }
            else if(code==SDLK_ESCAPE)
            {
                saycommand(NULL);

				return true;
            };
        };
    }
    else if(!menukey(code, isdown))                 // keystrokes go to menu
	{
        loopi(numkm) if(keyms[i].code==code)        // keystrokes go to game, lookup in keymap and execute
        {
            string temp;
            strcpy_s(temp, keyms[i].action);
            execute(temp, isdown); 
            return true;
        };
    };
	return false;
};

char *getcurcommand()
{
    return saycommandon ? commandbuf : NULL;
};

void writebinds(FILE *f)
{
    loopi(numkm)
    {
        if(*keyms[i].action) fprintf(f, "bind \"%s\" [%s]\n", keyms[i].name, keyms[i].action);
    };
};