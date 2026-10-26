#include "../common/spatial-evidence.h"
#include "../common/nav.h"
int main() {
    plan4::PublicNavigation nav;plan4::Information info;plan4::Menu menu;plan4::runtime::SpatialEvidence evidence;
    std::vector<cambc::camp> camps;std::vector<cambc::champion> enemies;
    evidence.start(info,menu,nav.topology(),camps,enemies,plan4::PublicNavigation::route_prefix,&nav,0,40000);
}
