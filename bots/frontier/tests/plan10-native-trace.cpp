#include "../common/src/trace.h"
#include <cassert>
#include <limits>
#include <string>
#ifdef NDEBUG
#error Trace regression requires assertions enabled
#endif
int main(){
 p10::TraceLine line("p10 prediction actor=",-1," round=",2000," predicted=[",1,",",6,"] gems=",std::numeric_limits<long long>::min());
 assert(std::string_view(line.bytes,line.size)=="p10 prediction actor=-1 round=2000 predicted=[1,6] gems=-9223372036854775808\n");
 const std::string long_reason(1000,'x');p10::TraceLine bounded("p10 error ",long_reason);
 assert(bounded.size==sizeof(bounded.bytes)&&bounded.bytes[bounded.size-1]=='\n');
 assert(std::string_view(bounded.bytes,10)=="p10 error ");
 p10::trace("PASS bounded native trace: exact signed integers, fixed storage, complete lines");
}
