// weapon.cpp: all shooting and effects code

#include "cube.h"

struct guninfo { int eventfire, eventend, eventhit, attackdelay, damage, projspeed, part, kickamount; char *name; };

const int MONSTERDAMAGEFACTOR = 4;
const int SGRAYS = 20;
const float SGSPREAD = 2;
vec sg[SGRAYS];

guninfo guns[NUMGUNS] =
{
	{ 1,	0,	0,	 250,  50, 0,   0,  1, "fist"		},
    { 1,	0,	0,	1400,  10, 0,   0, 20, "shotgun"	},  // *SGRAYS
    { 1,	0,	0,	 100,  30, 0,   0,  7, "chaingun"	},
    { 1,	1,	1,	 800, 120, 80,  0, 10, "rocket"		},
    { 1,	0,	0,	1500, 100, 0,   0, 30, "rifle"      },
    { 1,	1,	1,	 200,  20, 50,  4,  1, "fireball"   },
    { 1,	1,	1,	 200,  40, 30,  6,  1, "iceball"    },
    { 1,	1,	1,	 200,  30, 160, 7,  1, "slimeball"  },
    { 1,	0,	0,	 250,  50, 0,   0,  1, "bite"       },
};

void selectgun(int a, int b, int c)
{
    if(a<-1 || b<-1 || c<-1 || a>=NUMGUNS || b>=NUMGUNS || c>=NUMGUNS) return;
    int s = player1->gunselect;
    if(a>=0 && s!=a && player1->ammo[a]) s = a;
    else if(b>=0 && s!=b && player1->ammo[b]) s = b;
    else if(c>=0 && s!=c && player1->ammo[c]) s = c;
    else if(s!=GUN_RL && player1->ammo[GUN_RL]) s = GUN_RL;
    else if(s!=GUN_CG && player1->ammo[GUN_CG]) s = GUN_CG;
    else if(s!=GUN_SG && player1->ammo[GUN_SG]) s = GUN_SG;
    else if(s!=GUN_RIFLE && player1->ammo[GUN_RIFLE]) s = GUN_RIFLE;
    else s = GUN_FIST;
    if(s!=player1->gunselect) snd_clientevent(AK::EVENTS::WEAPLOAD);
    player1->gunselect = s;
    //conoutf("%s selected", (int)guns[s].name);
};

int reloadtime(int gun) { return guns[gun].attackdelay; };

void weapon(char *a1, char *a2, char *a3)
{
    selectgun(a1[0] ? atoi(a1) : -1,
              a2[0] ? atoi(a2) : -1,
              a3[0] ? atoi(a3) : -1);
};

COMMAND(weapon, ARG_3STR);

void createrays(vec &from, vec &to)             // create random spread of rays for the shotgun
{
    vdist(dist, dvec, from, to);
    float f = dist*SGSPREAD/1000;
    loopi(SGRAYS)
    {
        #define RNDD (rnd(101)-50)*f
        vec r = { RNDD, RNDD, RNDD };
        sg[i] = to;
        vadd(sg[i], r); 
    };
};

bool intersect(dynent *d, vec &from, vec &to)   // if lineseg hits entity bounding box
{
    vec v = to, w = d->o, *p; 
    vsub(v, from);
    vsub(w, from);
    float c1 = dotprod(w, v);

    if(c1<=0) p = &from;
    else
    {
        float c2 = dotprod(v, v);
        if(c2<=c1) p = &to;
        else
        {
            float f = c1/c2;
            vmul(v, f);
            vadd(v, from);
            p = &v;
        };
    };

    return p->x <= d->o.x+d->radius
        && p->x >= d->o.x-d->radius
        && p->y <= d->o.y+d->radius
        && p->y >= d->o.y-d->radius
        && p->z <= d->o.z+d->aboveeye
        && p->z >= d->o.z-d->eyeheight;
};

char *playerincrosshair()
{
	if(demoplayback) return NULL;
    loopv(players)
    {
        dynent *o = players[i];
        if(!o) continue; 
        if(intersect(o, player1->o, worldpos)) return o->name;
    };
    return NULL;
};

struct projectile 
	: public baseent
{ 
	vec to; 
	float speed; 
	dynent *owner; 
	int gun; 
	bool local; 
};

vector<projectile *> projs;

projectile * newprojectile(vec &from, vec &to, float speed, bool local, dynent *owner, int gun)
{
    projectile *p = (projectile *)gp()->alloc(sizeof(projectile));

	p->o = from;

	// calculate velocity

	p->vel = to;
	vsub( p->vel, from );

	{ // normalize vector
		float len = (float)sqrt(dotprod(p->vel,p->vel));
		vdiv( p->vel, len );
	}

	vmul( p->vel, speed * 2 ); // boost value for doppler

    p->yaw = -(float)atan2(from.x - to.x, from.y - to.y)/PI*180+180;
	p->roll = 0;
	p->pitch = (float)atan2(from.z - to.z, sqrt(dotprod(from,to))) *180/PI; 
    p->to = to;
    p->speed = speed;
    p->local = local;
    p->owner = owner;
    p->gun = gun;

	snd_registent( p, NULL );

	return p;
};

void zapprojectile( projectile *& proj )
{
	snd_unregistent( proj );

	if(proj) gp()->dealloc(proj, sizeof(projectile));
    proj = NULL;
}

void projreset() 
{ 
	loopv(projs) zapprojectile( projs[i] );
	projs.setsize(0);
}

void hit(int target, int damage, dynent *d, dynent *at)
{
    if(d==player1) selfdamage(damage, at==player1 ? -1 : -2, at);
	else if (d->GetMonsterState()) monsterpain(d, damage, at);
    else
	{ 
		addmsg(1, 4, SV_DAMAGE, target, damage, d->lifesequence); 
		snd_event(AK::EVENTS::PAIN, d); 
	};
    particle_splash(3, damage, 1000, d->o);
	demodamage(damage, d->o);
};

const float RL_RADIUS = 5;
const float RL_DAMRAD = 7;   // hack

void radialeffect(dynent *o, vec &v, int cn, int qdam, dynent *at)
{
    if(o->state!=CS_ALIVE) return;
    vdist(dist, temp, v, o->o);
    dist -= 2; // account for eye distance imprecision
    if(dist<RL_DAMRAD) 
    {
        if(dist<0) dist = 0;
        int damage = (int)(qdam*(1-(dist/RL_DAMRAD)));
        hit(cn, damage, o, at);
        vmul(temp, (RL_DAMRAD-dist)*damage/800);
        vadd(o->vel, temp);
    };
};

void splash(projectile *p, vec &v, vec &vold, int notthisplayer, int notthismonster, int qdam)
{
    particle_splash(0, 50, 300, v);

	if ( guns[p->gun].eventend )
	{
		char name[256];
		sprintf( name, "End_%s_%s", guns[p->gun].name, p->owner == player1 ? "Player" : "Monster" );
		snd_event( name, p );
	}
    
	if ( guns[p->gun].eventhit )
	{
		char name[256];
		sprintf( name, "Hit_%s_%s", guns[p->gun].name, p->owner == player1 ? "Player" : "Monster" );
		snd_event( name, p );
	}

    if(p->gun==GUN_RL)
    {
        newsphere(v, RL_RADIUS, 0);
        dodynlight(vold, v, 0, 0, p->owner);
        if(!p->local) return;
        radialeffect(player1, v, -1, qdam, p->owner);
        loopv(players)
        {
            if(i==notthisplayer) continue;
            dynent *o = players[i];
            if(!o) continue; 
            radialeffect(o, v, i, qdam, p->owner);
        };
        dvector &mv = getmonsters();
        loopv(mv) if(i!=notthismonster) radialeffect(mv[i], v, i, qdam, p->owner);
    };
};

// return true when projectile should die
inline bool projdamage(dynent *o, projectile *p, vec &v, int i, int im, int qdam)
{
    if(o->state!=CS_ALIVE) 
		return false;

    if( intersect(o, p->o, v) )
    {
        splash(p, v, p->o, i, im, qdam);
        hit(i, qdam, o, p->owner);

		return true;
    }; 

	return false;
};

void moveprojectiles(float time)
{
	for( int i = 0; i<projs.length(); )  
    {
        projectile *p = projs[i];
        int qdam = guns[p->gun].damage*(p->owner->quadmillis ? 4 : 1);
		if (p->owner->GetMonsterState()) qdam /= MONSTERDAMAGEFACTOR;
        vdist(dist, v, p->o, p->to);
        float dtime = dist*1000/p->speed;
        if(time>dtime) dtime = time;
        vmul(v, time/dtime);
        vadd(v, p->o)

		bool alive = true; // projectile is alive

        if(p->local)
        {
            loopv(players)
            {
                dynent *o = players[i];
                if(!o) continue; 
				if ( projdamage(o, p, v, i, -1, qdam) )
				{
					alive = false;
					break;
				}
            };
            if(p->owner!=player1)
			{
				alive = !projdamage(player1, p, v, -1, -1, qdam);
			}
            dvector &mv = getmonsters();
            loopv(mv) 
			{
				if(!vreject(mv[i]->o, v, 10.0f) && mv[i]!=p->owner) 
				{
					if ( projdamage(mv[i], p, v, -1, i, qdam) )
					{
						alive = false;
						break;
					}
				}
			}
        };

		if ( alive )
		{
            if(time==dtime) // time expired: hit the world
			{
				splash(p, v, p->o, -1, -1, qdam);
				alive = false;
			}
            else
            {
                if(p->gun==GUN_RL) 
				{ 
					dodynlight(p->o, v, 0, 255, p->owner);
					particle_splash(5, 2, 200, v); 
				}
                else 
				{ 
					particle_splash(1, 1, 200, v); 
					particle_splash(guns[p->gun].part, 1, 1, v); 
				};
            }
		}

		if ( alive )
		{
			p->o = v;
			i++;
        }
		else
		{
			zapprojectile( p );
			projs.remove(i);
		}
    };
};

void shootv(int gun, vec &from, vec &to, dynent *d, bool local)     // create visual effect from a shot
{
    int pspeed = 25;
	projectile * p = NULL;
    switch(gun)
    {
        case GUN_FIST:
            break;

        case GUN_SG:
        {
            loopi(SGRAYS) particle_splash(0, 5, 200, sg[i]);
            break;
        };

        case GUN_CG:
            particle_splash(0, 100, 250, to);
            break;

        case GUN_RL:
        case GUN_FIREBALL:
        case GUN_ICEBALL:
        case GUN_SLIMEBALL:
			pspeed = guns[gun].projspeed;
			if (d->GetMonsterState()) pspeed /= 2;
			p = newprojectile(from, to, (float)pspeed, local, d, gun);
			projs.add( p );
            break;

        case GUN_RIFLE: 
            particle_splash(0, 50, 200, to);
            particle_trail(1, 500, from, to);
            break;
    };

	if ( guns[gun].eventfire )
	{
		char name[256];
		sprintf( name, "Fire_%s_%s", guns[gun].name, d == player1 ? "Player" : "Monster" );

		if ( p )
			snd_event( name, p );
		else
			snd_event( name, d );
	}
};

void hitpush(int target, int damage, dynent *d, dynent *at, vec &from, vec &to)
{
    hit(target, damage, d, at);
    vdist(dist, v, from, to);
    vmul(v, damage/dist/50);
    vadd(d->vel, v);
};

void raydamage(dynent *o, vec &from, vec &to, dynent *d, int i)
{
    if(o->state!=CS_ALIVE) return;
    int qdam = guns[d->gunselect].damage;
    if(d->quadmillis) qdam *= 4;
	if (d->GetMonsterState()) qdam /= MONSTERDAMAGEFACTOR;
    if(d->gunselect==GUN_SG)
    {
        int damage = 0;
        loop(r, SGRAYS) if(intersect(o, from, sg[r])) damage += qdam;
        if(damage) hitpush(i, damage, o, d, from, to);
    }
    else if(intersect(o, from, to)) hitpush(i, qdam, o, d, from, to);
};

void shoot(dynent *d, vec &targ)
{
    float attacktime = lastmillis-d->lastaction;
    if(attacktime<d->gunwait) return;
    d->gunwait = 0;
    if(!d->attacking) return;
    d->lastaction = lastmillis;
    d->lastattackgun = d->gunselect;
    if(!d->ammo[d->gunselect])
	{ 
		snd_clientevent(AK::EVENTS::NOAMMO); 
		d->gunwait = 250; d->lastattackgun = -1; return; 
	};
    if(d->gunselect) d->ammo[d->gunselect]--;
    vec from = d->o;
    vec to = targ;
    from.z -= 0.2f;    // below eye
    
    vdist(dist, unitv, from, to);
    vdiv(unitv, dist);
    vec kickback = unitv;
    vmul(kickback, guns[d->gunselect].kickamount*-0.01f);
    vadd(d->vel, kickback);
    if(d->pitch<80.0f) d->pitch += guns[d->gunselect].kickamount*0.05f;
    

    if(d->gunselect==GUN_FIST || d->gunselect==GUN_BITE) 
    {
        vmul(unitv, 3); // punch range
        to = from;
        vadd(to, unitv);
    };   
    if(d->gunselect==GUN_SG) createrays(from, to);

    if(d->quadmillis && attacktime>200) snd_clientevent(AK::EVENTS::PUPFIRE);
    shootv(d->gunselect, from, to, d, true);
	if (!d->GetMonsterState()) addmsg(1, 8, SV_SHOT, d->gunselect, (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF), (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF));
	d->gunwait = guns[d->gunselect].attackdelay;

    if(guns[d->gunselect].projspeed) return;
    
    loopv(players)
    {
        dynent *o = players[i];
        if(!o) continue; 
        raydamage(o, from, to, d, i);
    };

    dvector &v = getmonsters();
    loopv(v) if(v[i]!=d) raydamage(v[i], from, to, d, -2);

	if (d->GetMonsterState()) raydamage(player1, from, to, d, -1);
};
