#include "../catalyst-forge/src/b-policy.h"
#include <cassert>
#include <cstdio>
using namespace p10;
using Forge=p10_b_forge::BPolicy;

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
 // B15's saved owner40 is one tile short of an attack pad. A full-clear
 // forecast at this pose starved the existing guarded inspection movement.
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
int main(){
 actual_caller_and_return();no_ack_is_not_execution();awake_flip_and_unknown();custody_and_payout_precedence();fuel_and_stun();shield_fallback_and_missing_inputs();complete_budget_and_fresh_retry();
 auto unavailable=local_world();unavailable.self.equipment_details_complete=false;auto budget=p10_mobile::budget_for(unavailable,fuel50000);
 const auto refused=p10_partial::partial_work(unavailable,group(unavailable,118),budget);
 assert(!refused.feasible&&!refused.first.safety_verified&&budget.spent==0);
 std::puts("partial-work: actual Forge caller, paid ACK, fresh exits/return, custody, local unknowns, fuel/stun and two-trace cap PASS; no engine action/game or income claim");
}

