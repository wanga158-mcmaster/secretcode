#include "../src/residual_probe.h"
#include "../src/residual_routes.h"
#include "../src/residual_words.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
using namespace residual::probe;
unsigned checks{},failures{};
void check(bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
Words decode(const Line& line){Words result{};
    for(std::size_t i=0;i<8;++i)for(unsigned j=0;j<8;++j){const char c=line[i*8+j];
        check((c>='0'&&c<='9')||(c>='a'&&c<='f'),"only lower ASCII hex");
        const auto value=static_cast<U32>(c<='9'?c-'0':c-'a'+10);result[i]=(result[i]<<4)|value;}
    check(line[64]=='\n',"exact LF at byte64");return result;
}
}
int main(){
    {
        reset();check(role()==none,"role remains unknown before existing identity read");
        before(7,1,3,1);clock(7,7);returned(7,1,1);budget(7,42000,41000);
        check(!record().first.set,"routine cold Budget failure before epoch8 is ignored");
        unsigned writes=0;auto writer=[&](const char*,std::size_t n){++writes;check(n==65,"one fixed-length write");};
        check(!offer(7,1000,50000,writer)&&writes==0,"startup breadcrumbs do not spend packet slots");
        check(!select(0)&&role()==none,"only named Marcella role may select");check(select(1)&&select(1),"selection is idempotent");
        check(!offer(8,1000,50000,writer)&&writes==0,"first failure packet is reserved, not first three startup entries");
        check(record().before_site==1&&record().world_stage==3,"preselection breadcrumbs retained");
    }
    {
        reset();select(1);before(9,4,7,1);clock(9,10);returned(9,5,2);
        check(record().first.set&&record().first.kind==Failure::Stale,"actual changed clock creates sticky Stale");
        check(record().first.expected==9&&record().first.actual==10&&record().first.site==4,"failure binds cached expected/actual and before site");
        budget(9,28000,27000);
        check(record().first.kind==Failure::Stale&&record().first.site==4,"later budget return does not overwrite first failure");
        Line captured{};unsigned writes=0;
        auto writer=[&](const char* bytes,std::size_t size){++writes;check(size==captured.size(),"single callback size65");
            for(std::size_t i=0;i<size;++i)captured[i]=bytes[i];
            before(10,8,13,0); // source record can change; captured bytes cannot.
        };
        check(!offer(10,28000,43999,writer)&&writes==0,"diagnostic reserve cannot borrow production floor");
        check(offer(10,28000,44000,writer)&&writes==1&&record().packets==1,"exact original floor plus16K admits one write");
        const auto words=decode(captured);
        check(words[0]==magic&&words[1]==9&&words[2]==10,"snapshot precedes writer mutation");
        check((words[3]&255)==4&&((words[3]>>8)&15)==7&&((words[3]>>12)&15)==1,"before context packed exactly");
        check(((words[3]>>16)&255)==5&&((words[3]>>24)&15)==2&&((words[3]>>28)&7)==1,"latest returned context and selected role packed separately");
        check((words[4]&65535)==10&&((words[4]>>16)&255)==4&&((words[4]>>24)&3)==2&&((words[4]>>26)&3)==1,"sticky failure and packet ordinal decoded");
        check(words[5]==(9u|(10u<<16))&&words[6]==28000&&words[7]==44000,"failure dates and actual admitting budget sample");
        check(!offer(10,28000,90000,writer)&&writes==1,"same actual epoch cannot spend next packet slot");
        clock(11,11);before(11,2,8,1);check(offer(11,28000,50000,writer),"next eligible actual observation emits packet2");
        clock(12,12);check(offer(12,28000,50000,writer),"next eligible actual observation emits packet3");
        clock(13,13);check(!offer(13,28000,50000,writer)&&writes==3&&record().packets==3,"hard three-packet transport cap");
    }
    {
        reset();select(1);before(8,2,3,1);clock(8,8);budget(8,42000,42000);
        check(record().first.kind==Failure::Budget&&record().first.status==budget_status,"strict production gate treats equal fuel as Budget refusal");
        before(9,7,9,0);clock(9,9);returned(9,7,0);budget(9,28000,80000);
        check(record().first.expected==8&&record().first.actual==8&&record().first.site==2,"first budget failure survives later successful observations");
        const auto first=snapshot();check(((first[4]>>24)&3)==1&&((first[4]>>28)&15)==14,"Budget kind distinct from actual Ready return status");
        unsigned writes=0;auto writer=[&](const char*,std::size_t){++writes;};
        check(!offer(10,28000,80000,writer)&&writes==0,"a cached mismatching actual clock cannot authorize transport");
        check(!offer(9,28000,80000,writer)&&writes==0,"even a fresh affordable Budget record cannot consume a transport slot");
    }
    {
        reset();select(1);before(8,1,3,1);clock(8,8);budget(8,42000,41000);
        check(record().first.kind==Failure::Budget,"post-startup first Budget record retained initially");
        unsigned writes=0;auto writer=[&](const char*,std::size_t){++writes;};
        check(!offer(8,28000,50000,writer)&&writes==0,"first eligible Budget record reserves every slot for Stale");
        before(9,101,7,1);clock(9,10);returned(9,101,2);
        check(record().first.kind==Failure::Stale&&record().first.expected==9&&record().first.actual==10&&record().first.site==101,"first Stale replaces all fields of prior Budget failure");
        check(offer(10,28000,50000,writer)&&writes==1,"first actual Stale boundary emits packet1");
        before(11,107,13,1);clock(11,12);returned(11,107,2);
        check(record().first.expected==9&&record().first.actual==10&&record().first.site==101,"later stale observer sites cannot overwrite first Stale");
        check(offer(12,28000,50000,writer)&&writes==2,"second packet retains first Stale despite later last-before context");
    }
    {
        reset();before(10,6,4,1);const auto status=residual::WordResult::Status::Stale;
        returned(10,6,static_cast<unsigned>(status),status==residual::WordResult::Status::Stale);
        check(record().first.kind==Failure::Stale&&record().first.actual==none,"Stale return can retain unknown actual rather than invent it");
        select(1);unsigned writes=0;
        check(offer(10,0,16000,[&](const char*,std::size_t){++writes;})&&writes==1,"explicit supplied fresh sample can transport unknown earlier clock");
        reset();select(1);before(8,1,0,1);clock(8,8);budget(8,40000,39999);
        const auto saved=record().first;
        mark_failure(9,9,static_cast<unsigned>(Failure::Explicit),3);
        check(record().first.site==saved.site&&record().first.kind==saved.kind,"explicit later failure cannot replace sticky first failure");
    }
    {
        reset();select(1);unsigned writes=0;auto writer=[&](const char*,std::size_t n){++writes;check(n==65,"Stale-only packet preserves65-byte transport");};
        for(int e=1;e<=80;++e){before(e,1,3,1);clock(e,e);budget(e,42000,41000);
            check(!offer(e,28000,80000,writer),"many routine Budget deferrals never emit despite ample diagnostic reserve");}
        check(writes==0&&record().packets==0&&record().first.kind==Failure::Budget,"Budget-only history leaves all three packet slots unused");
        const auto unsupported=residual::routes::Status::Unsupported;
        check(static_cast<unsigned>(unsupported)==2,"actual route enum value2 is Unsupported");
        returned(80,4,static_cast<unsigned>(unsupported));
        check(record().first.kind==Failure::Budget&&record().returned_status==2&&!offer(80,28000,80000,writer),"untyped route Unsupported2 is only a breadcrumb and cannot manufacture Stale");
        before(90,2,13,1);clock(90,91);const auto need=residual::ReadStatus::NeedCompute;
        returned(90,2,static_cast<unsigned>(need),need==residual::ReadStatus::Stale);
        check(record().first.kind==Failure::Stale&&record().first.expected==90&&record().first.actual==91&&record().returned_status==1,"real clock mismatch wins even when callee returns NeedCompute");
        check(offer(91,28000,80000,writer)&&writes==1,"first genuine clock boundary receives the first packet");
        returned(91,4,static_cast<unsigned>(unsupported));
        check(!offer(91,28000,80000,writer)&&writes==1,"same-epoch route result cannot consume a second packet");
        clock(92,92);check(offer(92,28000,80000,writer)&&writes==2,"next eligible epoch receives packet2");
        clock(93,93);check(offer(93,28000,80000,writer)&&writes==3,"next eligible epoch receives packet3");
        clock(94,94);check(!offer(94,28000,80000,writer)&&writes==3,"Stale-only history still obeys hard max3");
        check(record().first.expected==90&&record().first.actual==91&&record().first.site==2,"subsequent statuses and clocks preserve FIRST real Stale");
    }
    {
        reset();select(1);before(70000,300,20,19);clock(70000,70001);returned(70000,301,18);
        budget(70000,std::numeric_limits<U64>::max(),1);const auto words=snapshot();
        check(record().overflow&&((words[3]>>31)&1)==1,"numeric/packed overflow is explicit");
        check((words[3]&255)==255&&((words[3]>>8)&15)==15&&((words[3]>>12)&15)==15,"out-of-range compact context becomes unknown, never truncates");
        check((words[4]&65535)==65535&&words[5]==none&&words[6]==none,"out-of-scope failure epochs and full-width budget become unknown");
        unsigned writes=0;auto writer=[&](const char*,std::size_t){++writes;};
        check(!offer(70001,std::numeric_limits<U64>::max(),std::numeric_limits<U64>::max(),writer)&&writes==0,"reserve addition cannot overflow");
        Words arbitrary{0u,1u,0x01234567u,0x89abcdefu,0xffffffffu,0x80000000u,0x0000000fu,0xf0000000u};
        check(decode(encode(arbitrary))==arbitrary,"manual codec roundtrips every full32-bit word");
    }
    {
        reset();select(1);before(8,3,2,1);clock(8,9);budget(8,100,100);
        bool threw=false;
        try{offer(9,100,20000,[](const char*,std::size_t){throw 7;});}catch(int){threw=true;}
        check(threw&&record().packets==0&&record().last_emission_epoch==none,"failed writer leaves exception handling and transport receipt to caller");
        reset();check(role()==none&&!record().first.set&&record().packets==0,"explicit diagnostic reset clears all scalar state");
    }
    std::cout<<"residual_probe "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
