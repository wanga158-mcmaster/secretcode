// Adapted from authoritative .cache/plan10-c01-current-rebase fixtures; no assertion changes.
#include "../catalyst-forge/src/c-policy.h"
#include <cassert>
#include <cstdio>
#ifdef NDEBUG
#error "C01 current caller assertions must remain enabled"
#endif
// Begin unchanged authoritative partial-current.inc
namespace partial_current {
using namespace p10;
using Forge=p10_c_forge::CPolicy;

// Builder copied from frozen .cache/plan10-b10-escape/native-caller.cpp.
// Owner40 is saved replay input: all map groups retain truthful visibility and
// base geometry; the west ray is freshly observed free, not a synthetic exit.
// Later worlds below are explicit caller inputs, not executed engine actions.
static World native_world(){World w;w.round=40;w.self.id=0;w.geometry_complete=w.observation_complete=w.actor_observation_complete=true;w.opposition_complete=false;
{Actor a;
a.id=0;a.type=static_cast<Champion>(0);a.visible=true;a.ally=true;a.position={35,2};a.hp=1000;a.max_hp=1000;a.strength=40;a.focus=100;a.range=1;
a.alive=(2073u&1u)!=0;
a.can_move=(2073u&8u)!=0;
a.can_attack=(2073u&16u)!=0;
a.can_cast=(2073u&2048u)!=0;
{auto&b=a.abilities[0];b.ready=a.can_cast&&true;b.cooldown=a.cooldown[0]=0;b.max_cooldown=8;b.targets_camps=(8u&1u)!=0;b.targets_self=(8u&8u)!=0;b.targets_ground=(8u&16u)!=0;b.exclusive=(0u&1u)!=0;
b.reset=true;
}
{auto&b=a.abilities[1];b.ready=a.can_cast&&true;b.cooldown=a.cooldown[1]=0;b.max_cooldown=8;b.targets_camps=(8u&1u)!=0;b.targets_self=(8u&8u)!=0;b.targets_ground=(8u&16u)!=0;b.exclusive=(0u&1u)!=0;
b.shield=true;b.range=4;b.modifier=2.5;
b.duration=5;
}
w.self=a;}
{Actor a;
a.id=2;a.type=static_cast<Champion>(2);a.visible=true;a.ally=true;a.position={35,1};a.hp=1000;a.max_hp=1000;a.strength=70;a.focus=100;a.range=1;
a.alive=(2073u&1u)!=0;
a.can_move=(2073u&8u)!=0;
a.can_attack=(2073u&16u)!=0;
a.can_cast=(2073u&2048u)!=0;
{auto&b=a.abilities[0];b.ready=a.can_cast&&true;b.cooldown=a.cooldown[0]=0;b.max_cooldown=8;b.targets_camps=(8u&1u)!=0;b.targets_self=(8u&8u)!=0;b.targets_ground=(8u&16u)!=0;b.exclusive=(0u&1u)!=0;
b.reset=true;
}
{auto&b=a.abilities[1];b.ready=a.can_cast&&true;b.cooldown=a.cooldown[1]=0;b.max_cooldown=8;b.targets_camps=(16u&1u)!=0;b.targets_self=(16u&8u)!=0;b.targets_ground=(16u&16u)!=0;b.exclusive=(0u&1u)!=0;
b.dash=true;b.range=2;b.radius=0;b.modifier=0;b.duration=0;
}
w.allies.push_back(a);}
{Group g;g.id=118;g.observed=true;g.complete=true;g.living=true;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=66;g.gold=270;
g.route_known=true;g.travel=0;g.to_bank=g.to_spawn=-1;g.approach=w.self.position;
{Monster m;m.id=50;m.group=118;m.position={36,2};m.observed=true;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=22;m.group=118;m.position={37,2};m.observed=true;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=54;m.group=118;m.position={36,3};m.observed=true;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=18;m.group=118;m.position={37,3};m.observed=true;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=52;m.group=118;m.position={36,4};m.observed=true;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=20;m.group=118;m.position={37,4};m.observed=true;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=144;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=15;g.gold=110;
{Monster m;m.id=14;m.group=144;m.position={21,3};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=46;m.group=144;m.position={22,3};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=151;g.observed=true;g.complete=true;g.living=true;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=22;g.gold=110;
{Monster m;m.id=48;m.group=151;m.position={28,3};m.observed=true;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=16;m.group=151;m.position={29,3};m.observed=true;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=44;m.group=151;m.position={30,3};m.observed=true;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=174;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=4;g.gold=60;
{Monster m;m.id=34;m.group=174;m.position={10,4};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=0;m.group=174;m.position={10,5};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=299;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=8;m.group=299;m.position={12,7};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=28;m.group=299;m.position={12,8};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=383;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=4;g.gold=80;
{Monster m;m.id=30;m.group=383;m.position={14,9};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=2;m.group=383;m.position={15,9};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=32;m.group=383;m.position={15,10};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=427;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=26;m.group=427;m.position={17,10};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=10;m.group=427;m.position={17,11};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=552;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=11;g.gold=60;
{Monster m;m.id=4;m.group=552;m.position={19,13};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=36;m.group=552;m.position={20,13};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=38;m.group=552;m.position={19,14};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=637;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=24;m.group=637;m.position={22,15};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=12;m.group=637;m.position={22,16};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=762;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=18;g.gold=40;
{Monster m;m.id=6;m.group=762;m.position={24,18};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=42;m.group=762;m.position={25,18};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=40;m.group=762;m.position={24,19};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=885;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=18;g.gold=40;
{Monster m;m.id=41;m.group=885;m.position={24,21};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=7;m.group=885;m.position={24,22};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=43;m.group=885;m.position={25,22};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1006;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=13;m.group=1006;m.position={22,24};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=25;m.group=1006;m.position={22,25};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1085;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=11;g.gold=60;
{Monster m;m.id=39;m.group=1085;m.position={19,26};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=5;m.group=1085;m.position={19,27};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=37;m.group=1085;m.position={20,27};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1206;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=11;m.group=1206;m.position={17,29};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=27;m.group=1206;m.position={17,30};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1245;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=4;g.gold=80;
{Monster m;m.id=33;m.group=1245;m.position={15,30};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=31;m.group=1245;m.position={14,31};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=3;m.group=1245;m.position={15,31};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1324;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=0;g.gold=40;
{Monster m;m.id=29;m.group=1324;m.position={12,32};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=9;m.group=1324;m.position={12,33};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1445;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=50;g.respawn_remaining=-1;g.gems=4;g.gold=60;
{Monster m;m.id=1;m.group=1445;m.position={10,35};m.observed=false;m.alive=true;m.awake=false;m.hp=1350;m.max_hp=1350;m.strength=20;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=35;m.group=1445;m.position={10,36};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1512;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=66;g.gold=270;
{Monster m;m.id=53;m.group=1512;m.position={36,36};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=21;m.group=1512;m.position={37,36};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=55;m.group=1512;m.position={36,37};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=19;m.group=1512;m.position={37,37};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=51;m.group=1512;m.position={36,38};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=23;m.group=1512;m.position={37,38};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1538;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=15;g.gold=110;
{Monster m;m.id=15;m.group=1538;m.position={21,37};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=47;m.group=1538;m.position={22,37};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
{Group g;g.id=1545;g.observed=false;g.complete=false;g.living=false;g.cleared=false;g.respawning=false;g.respawn=60;g.respawn_remaining=-1;g.gems=22;g.gold=110;
{Monster m;m.id=49;m.group=1545;m.position={28,37};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=17;m.group=1545;m.position={29,37};m.observed=false;m.alive=true;m.awake=false;m.hp=2400;m.max_hp=2400;m.strength=60;m.range=4;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
{Monster m;m.id=45;m.group=1545;m.position={30,37};m.observed=false;m.alive=true;m.awake=false;m.hp=120;m.max_hp=120;m.strength=0;m.range=3;
g.monsters.push_back(m);g.spawns.push_back(m.position);g.hp+=m.hp;}
g.position=g.spawns.front();w.groups.push_back(g);}
w.visible_open_tiles.push_back({34,2});
w.visible_open_tiles.push_back({33,2});
w.visible_open_tiles.push_back({32,2});
w.visible_open_tiles.push_back({31,2});
return w;}

static Group& group(World&w,int id){auto it=std::find_if(w.groups.begin(),w.groups.end(),[&](const auto&g){return g.id==id;});assert(it!=w.groups.end());return *it;}
static void awake(World&w,int id,bool value){for(auto&m:group(w,id).monsters)m.awake=value;}
static void cleared(World&w,int id){auto&g=group(w,id);g.observed=g.complete=g.cleared=true;g.living=g.respawning=false;g.hp=0;for(auto&m:g.monsters){m.observed=true;m.alive=false;m.hp=0;}}
static World local_world(){auto w=native_world();w.spawn={3,1};w.observation_complete=false;return w;}
static void paid_reset(World&w){
 // Exact paid prefix response: Varg hit50/reset/hit50 splashes54/22/18.
 // Runtime receipts credit the primary target's paid damage, not splash copies.
 for(auto&m:group(w,118).monsters){m.awake=true;if(m.id==50||m.id==54||m.id==22||m.id==18)m.hp-=80;}
 auto&g=group(w,118);g.hp=0;for(const auto&m:g.monsters)g.hp+=std::max(0.,m.hp);
 w.round=41;w.self.hp=820;w.self.can_move=w.self.can_attack=w.self.can_cast=true;
 w.self.cooldown[0]=7;w.self.abilities[0].cooldown=7;w.self.abilities[0].ready=false;
 w.receipts={{Receipt::Kind::attack,40,118,w.self.id,0,0,0,40},
             {Receipt::Kind::cast,40,118,w.self.id},
             {Receipt::Kind::attack,40,118,w.self.id,0,0,0,40}};
}
static Directive prefix(Forge&p,World&w,Arm arm=Arm::active){
 const auto d=p.step(w,arm);
 assert(d.task==Task::attack&&d.group==118&&d.monster==50&&d.cast_slot==0&&d.second_monster==50);
 assert(d.reserve_reset&&d.strict_tank&&d.local_safety_verified&&fresh_certificate(w,d));
 assert(p.partial_pending&&!p.partial_escaping&&!p.partial_returning&&!p.partial_acknowledged&&p.fresh_traces==2&&p.critical_continuation());
 assert(p.partial_round==40&&p.partial_source==118&&p.partial_target==50&&p.partial_endpoint==Point(32,2));
 return d;
}
static void actual_caller_and_return(){
 {Forge approach;auto pose=local_world();pose.self.position={34,1};auto d=approach.step(pose,Arm::active);
  assert(d.task==Task::inspect&&d.group==118&&approach.fresh_traces==0&&!approach.partial_pending&&!d.safety_verified);}
 auto w=local_world();assert(w.self.position==Point(35,2)&&w.self.hp==1000&&w.self.strength==40);
 assert(group(w,151).complete&&!group(w,151).monsters[1].awake&&!group(w,144).complete);
 Forge p;p10_mobile::call_diagnostics={};prefix(p,w);
 assert(p10_mobile::call_diagnostics.calls==0); // prefix precedes full-clear warmup
 auto same=p.step(w,Arm::active);assert(same.task==Task::wait&&p.partial_pending&&p.fresh_traces==0&&!p.partial_acknowledged);
 paid_reset(w);
 auto d=p.step(w,Arm::active);assert(d.task==Task::move&&d.destination==Point(34,2)&&fresh_certificate(w,d));
 assert(p.partial_acknowledged&&p.partial_escaping&&!p.partial_pending&&p.fresh_traces==1&&p.critical_continuation());
 ++w.round;w.receipts.clear();w.self.position={34,2};w.self.hp=640;
 d=p.step(w,Arm::active);assert(d.task==Task::move&&d.destination==Point(33,2)&&fresh_certificate(w,d)&&p.fresh_traces==1);
 ++w.round;w.self.position={33,2};w.self.hp=460;
 d=p.step(w,Arm::active);assert(d.task==Task::move&&d.destination==Point(32,2)&&fresh_certificate(w,d)&&p.fresh_traces==1);
 ++w.round;w.self.position={32,2};
 d=p.step(w,Arm::active);assert(d.task==Task::recover&&!p.partial_escaping&&p.partial_returning&&p.fresh_traces==1&&p.critical_continuation());
 ++w.round;d=p.step(w,Arm::active);assert(d.task==Task::recover&&p.partial_returning&&p.fresh_traces==0&&p.critical_continuation());
 // Actual map teamA spawn rectangle is [3,6)x[1,4). Arrival does not imply heal.
 ++w.round;w.self.position={3,1};w.self.at_spawn=true;w.self.hp=980;
 d=p.step(w,Arm::active);assert(d.task==Task::recover&&p.partial_returning&&p.fresh_traces==0&&p.critical_continuation());
 ++w.round;w.self.hp=1000;d=p.step(w,Arm::active);assert(!p.partial_returning&&!p.partial_escaping&&p.fresh_traces<=2&&!p.critical_continuation());
 // A later fresh sortie must not retain the last sortie's ACK.
 w=local_world();w.round=50;d=p.step(w,Arm::active);
 assert(d.task==Task::attack&&p.partial_pending&&!p.partial_acknowledged&&!p.partial_escaping&&p.fresh_traces==2);
}
static void no_ack_is_not_execution(){
 const std::array<std::vector<Receipt>,6> wrong{{
  {},{{Receipt::Kind::attack,40,118,0,0,0,0,0}},
  {{Receipt::Kind::attack,40,118,2,0,0,0,40}},
  {{Receipt::Kind::attack,40,151,0,0,0,0,40}},
  {{Receipt::Kind::attack,39,118,0,0,0,0,40}},
  {{Receipt::Kind::cast,40,118,0,0,0,0,40}}
 }};
 for(const auto&receipts:wrong){Forge p;auto w=local_world();prefix(p,w);++w.round;w.receipts=receipts;const auto d=p.step(w,Arm::active);
  assert(d.task==Task::wait&&!p.partial_pending&&!p.partial_acknowledged&&!p.partial_escaping&&!p.partial_returning&&p.fresh_traces==0);}
 // No owned attack ACK is needed to flee a freshly observed danger caused by
 // another actor. The caller must retain that distinction.
 Forge p;auto w=local_world();prefix(p,w);++w.round;awake(w,118,true);w.self.hp=820;
 auto d=p.step(w,Arm::active);assert(d.task==Task::move&&d.destination==Point(34,2)&&p.partial_escaping&&!p.partial_acknowledged);
}
static void awake_flip_and_unknown(){
 Forge p;auto w=local_world();prefix(p,w);paid_reset(w);awake(w,151,true);
 auto d=p.step(w,Arm::active);assert(d.task==Task::recover&&p.partial_acknowledged&&p.partial_failed&&p.partial_escaping&&p.partial_returning&&p.fresh_traces==2&&!d.safety_verified);
 // This changed input restores a real currently sleeping neighbor. No old HP,
 // awake flag or trajectory is reused after the failed exit.
 ++w.round;w.receipts.clear();w.self.hp=640;awake(w,151,false);
 d=p.step(w,Arm::active);assert(d.task==Task::move&&d.destination==Point(34,2)&&fresh_certificate(w,d)&&!p.partial_failed&&p.partial_escaping&&p.fresh_traces==1);
 auto unknown=local_world();group(unknown,151).complete=false;Forge blocked;
 d=blocked.step(unknown,Arm::active);assert(d.task!=Task::attack&&d.task!=Task::cast&&!d.safety_verified&&!blocked.partial_pending&&blocked.fresh_traces<=2);
}
static void custody_and_payout_precedence(){
 // Another actor can finish the source while our emitted prefix was skipped.
 Forge pending;auto w=local_world();prefix(pending,w);++w.round;cleared(w,118);
 auto d=pending.step(w,Arm::active);assert(d.task==Task::capture&&d.group==118&&!pending.partial_pending&&!pending.partial_escaping&&!pending.partial_returning&&pending.fresh_traces==0);
 // Source custody also cancels a return intent left by a failed fresh exit.
 Forge failed;w=local_world();prefix(failed,w);paid_reset(w);awake(w,151,true);failed.step(w,Arm::active);
 assert(failed.partial_failed&&failed.partial_returning&&failed.partial_escaping);
 ++w.round;w.receipts.clear();cleared(w,118);d=failed.step(w,Arm::active);
 assert(d.task==Task::capture&&d.group==118&&!failed.partial_pending&&!failed.partial_escaping&&!failed.partial_returning&&failed.fresh_traces==0);
 ++w.round;d=failed.step(w,Arm::active);assert(d.task==Task::capture&&d.group==118&&!failed.partial_returning);
 Forge paid;w=local_world();prefix(paid,w);paid_reset(w);awake(w,151,true);paid.step(w,Arm::active);
 ++w.round;w.self.bag=66;w.own_gold=270;w.own_payout=true;w.last_payout_group=118;w.last_payout_round=w.round;
 w.receipts={{Receipt::Kind::payout,w.round,118,w.self.id,0,66,270}};
 d=paid.step(w,Arm::active);assert(d.task==Task::bank&&paid.funded&&!paid.partial_pending&&!paid.partial_escaping&&paid.partial_returning&&paid.fresh_traces==0);
 Forge loaded;w=local_world();prefix(loaded,w);++w.round;w.self.bag=7;d=loaded.step(w,Arm::active);assert(d.task==Task::bank&&!loaded.partial_acknowledged);
}
static int fuel40000(){return 40000;}
static int fuel47000(){return 47000;}
static int fuel50000(){return 50000;}
static int fuel55000(){return 55000;}
static int fuel65000(){return 65000;}
static int fuel_reads=0,high_reads=0;
static int declining_fuel(){return ++fuel_reads<=high_reads?50000:40000;}
static void fuel_and_stun(){
 auto w=local_world();Forge tight;tight.set_fuel_reader(fuel47000);auto d=tight.step(w,Arm::active);
 assert(d.task==Task::attack&&d.monster==50&&d.cast_slot<0&&d.second_monster<0&&tight.partial_pending&&tight.fresh_traces==1&&fresh_certificate(w,d));
 for(auto arm:{Arm::active,Arm::control}){Forge observed;observed.set_fuel_reader(fuel55000);auto frame=w;frame.action_reserve=41000;auto one=observed.step(frame,arm);
  assert(one.task==Task::attack&&one.cast_slot<0&&one.second_monster<0&&observed.fresh_traces==1&&observed.partial_pending&&fresh_certificate(frame,one));}
 Forge enough;enough.set_fuel_reader(fuel65000);prefix(enough,w);
 Forge low;low.set_fuel_reader(fuel40000);d=low.step(w,Arm::active);
 assert(d.task!=Task::attack&&d.task!=Task::cast&&!d.safety_verified&&!low.partial_pending&&low.fresh_traces==0);
 auto stunned=w;stunned.self.can_move=stunned.self.can_attack=stunned.self.can_cast=false;
 for(auto&a:stunned.self.abilities)a.ready=false;Forge no_action;d=no_action.step(stunned,Arm::active);
 assert(d.task!=Task::attack&&d.task!=Task::cast&&!d.safety_verified&&!no_action.partial_pending&&no_action.fresh_traces<=2);
 Forge escaping;w=local_world();prefix(escaping,w);paid_reset(w);
 w.self.can_move=w.self.can_attack=w.self.can_cast=false;for(auto&a:w.self.abilities)a.ready=false;
 d=escaping.step(w,Arm::active);assert(d.task==Task::recover&&escaping.partial_acknowledged&&escaping.partial_failed&&escaping.partial_escaping&&escaping.fresh_traces<=2&&!d.safety_verified);
}
static void shield_fallback_and_missing_inputs(){
 auto w=local_world();w.self.cooldown[0]=7;w.self.abilities[0].cooldown=7;w.self.abilities[0].ready=false;
 Forge p;const auto d=p.step(w,Arm::active);
 assert(d.task==Task::cast&&d.cast_slot==1&&d.second_monster==50&&d.reserve_reset&&d.strict_tank&&fresh_certificate(w,d));
 assert(p.partial_pending&&!p.partial_acknowledged&&p.partial_endpoint==Point(32,2)&&p.fresh_traces==2);
 p10_partial::Options o;o.prefix=p10_partial::Prefix::shield_basic;o.slot=1;o.target=50;
 auto b=p10_mobile::budget_for(w,fuel50000);const auto shield=p10_partial::partial_work(w,group(w,118),b,o);
 // Certified Task::cast stops if its shield fails. That branch holds without
 // awakening the sleeping source: an optimistic fallback basic would cost HP.
 assert(shield.feasible&&shield.failed_cast_branch&&shield.failed_cast_hp==1000&&shield.retained_damage==160&&shield.hp==710);
 assert(shield.casts==1&&shield.basics==1&&shield.phases==6&&b.spent==2&&fresh_certificate(w,shield.first));
 auto stale_actors=w;stale_actors.actor_observation_complete=false;auto stale=p10_mobile::budget_for(stale_actors,fuel50000);
 const auto no_actor_frame=p10_partial::partial_work(stale_actors,group(stale_actors,118),stale,o);
 assert(!no_actor_frame.feasible&&!no_actor_frame.first.safety_verified&&!no_actor_frame.first.local_safety_verified&&stale.spent==0);
 auto missing_details=w;missing_details.self.combat_details_complete=false;auto missing=p10_mobile::budget_for(missing_details,fuel50000);
 const auto no_self_details=p10_partial::partial_work(missing_details,group(missing_details,118),missing,o);
 assert(!no_self_details.feasible&&!no_self_details.first.safety_verified&&!no_self_details.first.local_safety_verified&&missing.spent==0);
}
static void complete_budget_and_fresh_retry(){
 auto w=local_world();p10_partial::Options o;o.prefix=p10_partial::Prefix::reset_basic;o.slot=0;o.target=50;o.settle=0;
 auto b=p10_mobile::budget_for(w,fuel50000);const auto r=p10_partial::partial_work(w,group(w,118),b,o);
 assert(r.feasible&&r.failed_cast_branch&&r.retained_damage==320&&r.hp==460&&r.moves==3&&r.phases==6&&r.phase.size()==6&&b.spent==2&&fresh_certificate(w,r.first));
 assert(r.phase[0].incoming==180&&r.phase[1].incoming==180&&r.phase[2].incoming==180);
 assert(r.phase[4].incoming==0&&r.phase[5].incoming==0); // two forced settle phases
 auto capped=p10_partial::partial_work(w,group(w,118),b,o);assert(!capped.feasible&&!capped.first.safety_verified&&capped.failure==p10_partial::Failure::trace_budget&&b.spent==2);
 auto one_left=p10_mobile::budget_for(w,fuel50000);one_left.spent=1;auto refused=p10_partial::partial_work(w,group(w,118),one_left,o);
 assert(!refused.feasible&&!refused.first.safety_verified&&refused.failure==p10_partial::Failure::trace_budget&&one_left.spent==1);
 fuel_reads=0;high_reads=12;auto falling=p10_mobile::budget_for(w,declining_fuel);auto stopped=p10_partial::partial_work(w,group(w,118),falling,o);
 assert(fuel_reads>high_reads&&falling.spent>0&&falling.spent<=2&&!stopped.feasible&&!stopped.first.safety_verified&&stopped.failure==p10_partial::Failure::fuel);
 auto fresh=p10_mobile::budget_for(w,fuel50000);auto retried=p10_partial::partial_work(w,group(w,118),fresh,o);
 assert(retried.feasible&&retried.retained_damage==320&&retried.hp==460&&fresh.spent==2&&fresh_certificate(w,retried.first));
 // Reset readiness must not discard a source-clearing first basic. Its
 // emitted action has no imaginary second target or cast receipt.
 auto small=local_world();auto&single=group(small,118);single.monsters.resize(1);single.monsters[0].hp=single.monsters[0].max_hp=40;
 auto tiny=p10_mobile::budget_for(small,fuel50000);auto killed=p10_partial::partial_work(small,single,tiny,o);
 assert(killed.feasible&&killed.kills==1&&killed.basics==1&&killed.casts==0&&killed.retained_damage==40&&tiny.spent==2);
 assert(killed.first.task==Task::attack&&killed.first.cast_slot<0&&killed.first.second_monster<0&&fresh_certificate(small,killed.first));
}
void run(){
 actual_caller_and_return();no_ack_is_not_execution();awake_flip_and_unknown();custody_and_payout_precedence();fuel_and_stun();shield_fallback_and_missing_inputs();complete_budget_and_fresh_retry();
 std::puts("partial-work: actual Forge caller, paid ACK, fresh exits/return, custody, local unknowns, fuel/stun and two-trace cap PASS; no engine action/game or income claim");
}


}

// End authoritative partial-current.inc
// Begin unchanged authoritative bootstrap-current.inc
namespace bootstrap_current {
using namespace p10;
static World frame(){
 World w;w.round=40;w.max_rounds=2000;w.spawn={38,2};w.fuel=100000;w.self.id=0;w.self.type=Champion::varg;w.self.position={35,2};w.self.hp=w.self.max_hp=1000;w.self.strength=40;w.self.focus=100;w.self.range=1;w.self.visible=w.self.alive=w.self.ally=true;w.self.can_attack=w.self.can_cast=w.self.can_move=true;
 auto&a=w.self.abilities[0];a.reset=a.ready=a.targets_self=true;a.max_cooldown=8;
 auto&s=w.self.abilities[1];s.shield=s.ready=s.targets_self=true;s.modifier=2.5;s.duration=5;s.max_cooldown=8;
 Group g;g.id=18;g.position={36,2};g.gems=132;g.gold=30;g.travel=0;g.to_bank=g.to_spawn=3;g.observed=g.complete=g.living=g.route_known=g.program_available=true;g.approach={35,2};
 for(int y=1;y<=5;++y)for(int x=35;x<=38;++x)if(x==35||x==38||y==1||y==5)g.approach_pads.push_back({x,y});
 const int ids[3][2]={{50,22},{54,18},{52,20}};for(int y=2;y<=4;++y)for(int x=36;x<=37;++x){Monster m;m.id=ids[y-2][x-36];m.group=18;m.position={x,y};m.hp=m.max_hp=x==36?120:2400;m.strength=x==36?0:60;m.range=x==36?3:4;m.observed=m.alive=true;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 Group small;small.id=19;small.position={34,2};small.gems=8;small.travel=0;small.to_bank=small.to_spawn=3;small.approach={35,2};small.approach_pads={{35,2},{34,1},{34,3},{33,2}};small.observed=small.complete=small.living=small.route_known=small.program_available=true;Monster m;m.id=99;m.group=19;m.position={34,2};m.hp=m.max_hp=40;m.observed=m.alive=true;small.monsters={m};small.spawns={m.position};small.hp=40;w.groups={g,small};return w;
}
static int no_headroom(){return 40200;}
void run(){
 p10_mobile::default_cache={};auto w=frame();p10_c_forge::CPolicy p;auto first=p.step(w,Arm::active);
 // The failed local prefix and long-clear refusal use this owner's two slots.
 // Reselection names the real alternate, whose proof starts next owner.
 assert(p.refusal_events==1&&p.fresh_traces==2&&first.group==19&&first.task==Task::inspect&&!first.safety_verified);
 ++w.round;assert(p.rejected(w,w.groups[0]));auto next=p.step(w,Arm::active);assert(next.group==19&&next.task!=Task::inspect&&p.refusal_events==1&&p.fresh_traces<=2);
 // Changed real source HP/capital invalidates its old refusal. No forecast
 // result, helper damage or wallet is carried into the new physical frame.
 auto changed=w;changed.groups[0].monsters[1].hp-=40;assert(!p.rejected(changed,changed.groups[0]));changed=w;changed.self.strength+=5;assert(!p.rejected(changed,changed.groups[0]));
 // A transient fuel refusal cannot poison the source or select a fictitious
 // productive continuation. The card can retry when a fresh budget arrives.
 p10_c_forge::CPolicy low;low.set_fuel_reader(no_headroom);auto wait=low.step(w,Arm::active);assert(wait.task==Task::inspect&&low.refusal_events==0&&low.fresh_traces==0);
 std::puts("Forge actual caller: measured long-clear refusal reselects; fresh premises and temporary fuel PASS");
}

}

// End authoritative bootstrap-current.inc
using namespace p10;
using Forge=p10_c_forge::CPolicy;
static_assert(Forge::observation_headroom==18000&&Forge::observe_open_rays&&!Forge::observe_open_tiles&&!Forge::observe_ally_details&&Forge::observe_ally_detail_types==0&&Forge::owns_capital);
static World capital_world(){World w;w.round=10;w.max_rounds=2000;w.self.id=2;w.self.type=Champion::vendetta;w.self.visible=w.self.alive=w.self.ally=w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.position={1,1};w.self.hp=w.self.max_hp=1000;w.self.strength=100;w.self.focus=100;w.self.range=1;w.self.strength_growth=5;w.self.strength_shards=12;w.self.at_spawn=true;w.own_gold=120;w.to_spawn=0;Group g;g.id=60;g.position={2,1};g.observed=g.complete=g.route_known=g.living=g.program_available=true;g.gems=100;g.respawn=10;g.to_bank=g.to_spawn=1;g.travel=0;g.approach=w.self.position;g.approach_pads={w.self.position,{1,2},{2,2}};Monster m;m.id=600;m.group=60;m.position={2,1};m.hp=m.max_hp=240;m.observed=m.alive=true;g.monsters={m};g.spawns={m.position};g.hp=240;w.groups={g};w.visible_open_tiles={{1,1},{1,2},{2,2}};return w;}
static void purchase(World&w){++w.round;w.own_purchase=true;w.receipts={{Receipt::Kind::purchase,w.round,-1,w.self.id}};w.own_gold-=w.shard_cost;w.self.strength+=w.self.strength_growth;++w.self.strength_shards;}
static void capital_menu(){
 p10_mobile::default_cache={};auto w=capital_world();Forge p,c;auto d=p.step(w,Arm::active);auto no=c.step(w,Arm::control);assert(d.task==Task::buy&&d.buy_count==1&&d.item==Item::monster_strength&&d.template_id==6101&&p.capital_packet==4&&p.wallet.issued==1&&p.fresh_traces==0);assert(no.task!=Task::buy&&c.wallet.issued==0);
 for(int i=0;i<3;++i){purchase(w);d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.buy_count==1&&p.wallet.confirmed==i+1&&p.wallet.issued==i+2&&!p.critical_continuation());}
 purchase(w);d=p.step(w,Arm::active);assert(d.task==Task::attack&&p.wallet.confirmed==4&&p.wallet.issued==4&&p.partial_pending&&p.critical_continuation());
 // The real already-cleared/payout/bank frame closes retained work. The next
 // full source is a new admission, not an invented damage or income receipt.
 ++w.round;w.own_purchase=false;w.own_payout=true;w.last_payout_group=60;w.last_payout_round=w.round;w.self.bag=100;w.receipts={{Receipt::Kind::payout,w.round,60,w.self.id,0,100,40}};d=p.step(w,Arm::active);assert(d.task==Task::bank&&p.funded&&!p.partial_pending);
 ++w.round;w.own_payout=false;w.own_bank_completed=true;w.last_bank_round=w.round;w.self.bag=0;w.own_gold=90;w.receipts={{Receipt::Kind::bank,w.round,-1,w.self.id,0,100,0}};w.groups[0].id=61;w.groups[0].monsters[0].id=610;w.groups[0].monsters[0].group=61;w.groups[0].hp=w.groups[0].monsters[0].hp=w.groups[0].monsters[0].max_hp=270;d=p.step(w,Arm::active);assert(d.task==Task::buy&&p.wallet.issued==5&&w.self.strength_shards>10);
 auto immediate=capital_world();immediate.self.strength_growth=100;immediate.groups[0].hp=immediate.groups[0].monsters[0].hp=immediate.groups[0].monsters[0].max_hp=600;immediate.own_gold=30;Forge strong;assert(strong.step(immediate,Arm::control).task==Task::buy&&strong.capital_packet==1);
 // The shared item menu cannot fall through to B's legacy one-shard shopping.
 auto poor=capital_world();poor.own_gold=30;poor.self.strength_shards=0;Forge weak;assert(weak.step(poor,Arm::control).task!=Task::buy&&weak.wallet.issued==0);
 auto absent=poor;absent.groups.clear();Forge empty;assert(empty.step(absent,Arm::active).task==Task::wait&&empty.wallet.issued==0);auto late=capital_world();late.max_rounds=late.round+10;Forge terminal;assert(terminal.step(late,Arm::active).task!=Task::buy);
 std::puts("C01 current rebase: funded recurring active and immediate-control same item menu, real receipts, old cap exceeded, no B/Runtime independent shopping bypass PASS");
}
static int fuel40000(){return 40000;}
static void funded_critical_precedence(){
 auto w=capital_world();Forge probe;assert(probe.capital(w,Arm::active).task==Task::buy);
 for(Arm arm:{Arm::active,Arm::control}){
  auto waiting=w;Forge pending;pending.partial_pending=true;pending.partial_round=waiting.round;pending.partial_source=60;pending.partial_target=600;auto d=pending.step(waiting,arm);assert(d.task==Task::wait&&pending.critical_continuation()&&pending.wallet.issued==0&&pending.capital_events==0&&pending.fresh_traces==0);
  Forge healing;healing.partial_returning=true;auto hurt=w;hurt.self.hp=500;d=healing.step(hurt,arm);assert(d.task==Task::recover&&healing.partial_returning&&healing.critical_continuation()&&healing.wallet.issued==0&&healing.capital_events==0&&healing.fresh_traces==0);
  auto away=w;away.self.at_spawn=false;Forge returning;returning.partial_returning=true;d=returning.step(away,arm);assert(d.task==Task::recover&&returning.partial_returning&&returning.wallet.issued==0&&returning.capital_events==0);
  // Same snapshot is explicitly capital-profitable, but a current pending ACK
  // and its required fresh exit always run first, even with actual cash.
  auto native=partial_current::local_world();Forge paid;partial_current::prefix(paid,native,arm);partial_current::paid_reset(native);native.own_gold=300;native.self.strength_growth=5;native.groups[0].to_bank=native.groups[0].to_spawn=3;d=paid.step(native,arm);assert(d.task==Task::move&&d.destination==Point(34,2)&&paid.partial_acknowledged&&paid.partial_escaping&&paid.critical_continuation()&&paid.wallet.issued==0&&paid.capital_events==0&&fresh_certificate(native,d));
  Forge low;low.partial_returning=true;low.set_fuel_reader(fuel40000);d=low.step(hurt,arm);assert(d.task==Task::recover&&low.partial_returning&&low.wallet.issued==0&&low.fresh_traces==0);
  // Wallet receipts are acknowledged even when the required work yields early.
  auto channel=w;channel.self.channel=Channel::tribute;channel.own_purchase=true;channel.receipts={{Receipt::Kind::purchase,7,-1,channel.self.id}};Forge receipt;receipt.partial_returning=true;d=receipt.step(channel,arm);assert(d.task==Task::wait&&receipt.wallet.confirmed==1&&receipt.wallet.last_purchase==7&&receipt.capital_events==0);
  auto custody=w;custody.capture_active=true;custody.productive_group=60;Forge holder;holder.partial_escaping=true;holder.partial_source=60;d=holder.step(custody,arm);assert(d.task==Task::capture&&d.group==60&&!holder.critical_continuation()&&holder.wallet.issued==0);
 }
 // A healed arrival can resume its own capital service; an arrival alone cannot.
 Forge healed;healed.partial_returning=true;auto d=healed.step(w,Arm::active);assert(d.task==Task::buy&&!healed.critical_continuation()&&healed.wallet.issued==1);
 std::puts("C01 funded critical precedence: pending ACK, certified escape, return, actual healing, channel/custody and low fuel precede optional capital in both arms PASS");
}
static void held_source_and_reset_kill(){
 auto w=capital_world();w.own_gold=0;w.self.at_spawn=false;w.groups[0].hp=w.groups[0].monsters[0].hp=w.groups[0].monsters[0].max_hp=40;auto rich=w.groups[0];rich.id=62;rich.gems=10000;rich.position=rich.monsters[0].position={10,10};rich.monsters[0].id=620;rich.monsters[0].group=62;rich.spawns={rich.position};w.groups.push_back(rich);Forge held;held.funded=true;held.source=60;auto d=held.step(w,Arm::active);assert(d.group==60&&held.source==60); // no re-ranking away from the live owned source
 auto small=partial_current::local_world();auto&g=partial_current::group(small,118);g.monsters.resize(1);g.monsters[0].hp=g.monsters[0].max_hp=40;g.hp=40;g.spawns={g.monsters[0].position};Forge p;d=p.step(small,Arm::active);assert(d.task==Task::attack&&d.cast_slot<0&&d.second_monster<0&&p.partial_pending&&fresh_certificate(small,d));
 std::puts("C01 current mechanics: held-source rank preserved, source-killing basic retains safe suffix without imaginary reset PASS");
}
int main(){partial_current::run();bootstrap_current::run();capital_menu();funded_critical_precedence();held_source_and_reset_kill();std::puts("PASS C01 current copied rebase; source completion only, native fuel and strength unqualified");}
