#include "../risk-common/scenarios.h"
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <numeric>

namespace risk=plan3_risk;
unsigned checks=0,oracle_cases=0;
void require(bool value,const char* expression,int line) {
    ++checks;
    if(!value){std::fprintf(stderr,"risk test:%d: %s\n",line,expression);std::exit(1);}
}
#define CHECK(value) require(bool(value),#value,__LINE__)
bool equal(risk::Fraction value,std::uint64_t numerator,std::uint32_t denominator) {
    const auto a=std::gcd(value.numerator,std::uint64_t(value.denominator));
    const auto b=std::gcd(numerator,std::uint64_t(denominator));
    return value.numerator/a==numerator/b && value.denominator/a==denominator/b;
}
risk::Input input(unsigned worlds=2,unsigned jobs=2) {
    risk::Input value;value.worldCount=worlds;value.jobCount=jobs;
    for(unsigned w=0;w<worlds;++w)value.probabilityWeights[w]=1;
    value.radius={1,4};value.budget={1,2};
    for(unsigned j=0;j<jobs;++j) {
        auto& job=value.jobs[j];job.key={0,std::int32_t(j+1),-1,7,-1};
        job.complete=true;job.outcomeCount=worlds;
        for(unsigned w=0;w<worlds;++w)job.outcomes[w]={0,true,true,false};
    }
    return value;
}
void empty(const risk::Result& value,risk::State state,risk::Reason reason) {
    CHECK(value.state==state && value.reason==reason && value.rowCount==0 && value.denominator==1);
    CHECK(!value.ambiguity.present && !value.belief.present && !value.minimax.present && !value.ruin.present);
    for(const auto& row:value.rows)CHECK(!row.ruinAdmitted && !row.nominal.numerator && !row.worst.numerator);
}
void certificates(const risk::Input& source,const risk::Result& value) {
    CHECK(value.state==risk::State::known && value.rowCount==source.jobCount);
    std::uint32_t weight=0;for(unsigned w=0;w<source.worldCount;++w)weight+=source.probabilityWeights[w];
    const auto d=weight*source.radius.denominator;CHECK(value.denominator==d);
    for(unsigned j=0;j<source.jobCount;++j) {
        const auto& row=value.rows[j];CHECK(row.key==source.jobs[j].key);
        for(unsigned mode=0;mode<2;++mode) {
            const auto& q=mode?row.upperLossProbabilities:row.worstProbabilities;
            std::uint64_t total=0,difference=0,expectation=0;
            for(unsigned w=0;w<risk::maximum_worlds;++w) {
                if(w>=source.worldCount){CHECK(q[w]==0);continue;}
                CHECK(q[w]<=d);total+=q[w];
                const auto p=source.probabilityWeights[w]*source.radius.denominator;
                difference+=q[w]>p?q[w]-p:p-q[w];
                expectation+=std::uint64_t(q[w])*(mode?unsigned(source.jobs[j].outcomes[w].loss):source.jobs[j].outcomes[w].value);
            }
            CHECK(total==d && difference*source.radius.denominator<=std::uint64_t(2)*source.radius.numerator*d);
            CHECK(expectation==(mode?row.upperLoss.numerator:row.worst.numerator));
        }
        CHECK(row.ruinAdmitted==(row.upperLoss.numerator*source.budget.denominator<=std::uint64_t(source.budget.numerator)*d));
    }
}
void hand_tests() {
    auto value=input();
    value.jobs[0].outcomes[0]={10,true,true,true};
    value.jobs[1].outcomes[0].value=value.jobs[1].outcomes[1].value=4;
    auto result=risk::evaluate(value);certificates(value,result);
    CHECK(result.ambiguity.row==1 && result.belief.row==0 && result.minimax.row==1 && result.ruin.row==1);
    CHECK(equal(result.rows[0].nominal,5,1) && equal(result.rows[0].worst,5,2));
    CHECK(equal(result.rows[0].nominalLoss,1,2) && equal(result.rows[0].upperLoss,3,4));
    CHECK(result.rows[0].worstProbabilities[0]==2 && result.rows[0].worstProbabilities[1]==6);
    CHECK(result.rows[0].upperLossProbabilities[0]==6 && result.rows[0].upperLossProbabilities[1]==2);
    value.budget={3,4};result=risk::evaluate(value);CHECK(result.ruin.present && result.ruin.row==0);
    value.budget={2,3};result=risk::evaluate(value);CHECK(result.ruin.row==1);
    value.radius={0,1};result=risk::evaluate(value);CHECK(result.ambiguity.row==result.belief.row);certificates(value,result);
    value.radius={1,1};result=risk::evaluate(value);CHECK(result.ambiguity.row==result.minimax.row);certificates(value,result);
    value=input();value.jobs[0].key[1]=9;value.jobs[1].key[1]=2;
    result=risk::evaluate(value);CHECK(result.ambiguity.row==1 && result.belief.row==1 && result.minimax.row==1 && result.ruin.row==1);
    value.budget={0,1};for(auto& job:value.jobs)for(auto& outcome:job.outcomes)outcome.loss=true;
    result=risk::evaluate(value);CHECK(result.state==risk::State::known && !result.ruin.present && result.belief.present);
    value=input();value.probabilityWeights={1,0,0,0};value.jobs[0].outcomes[1].known=false;
    empty(risk::evaluate(value),risk::State::unavailable,risk::Reason::unknown_outcome);
    value.jobs[1].outcomes[0].value=risk::maximum_value+1;
    empty(risk::evaluate(value),risk::State::invalid,risk::Reason::value_bound);
    value=input();value.jobs[0].complete=false;empty(risk::evaluate(value),risk::State::unavailable,risk::Reason::incomplete);
    value=input();value.jobs[0].outcomeCount=1;empty(risk::evaluate(value),risk::State::unavailable,risk::Reason::incomplete);
    value=input();value.jobs[0].outcomes[0].lossKnown=false;empty(risk::evaluate(value),risk::State::unavailable,risk::Reason::unknown_outcome);
    value=input();value.jobs[0].outcomeCount=3;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::outcome_count);
    value=input();value.worldCount=0;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::world_count);
    value=input();value.worldCount=5;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::world_count);
    value=input();value.jobCount=0;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::job_count);
    value=input();value.jobCount=29;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::job_count);
    value=input();value.probabilityWeights={0,0,0,0};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::weights);
    value=input();value.probabilityWeights={4096,1,0,0};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::weights);
    value=input();value.radius={1,0};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::radius);
    value=input();value.radius={4097,4097};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::radius);
    value=input();value.radius={2,1};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::radius);
    value=input();value.budget={2,1};empty(risk::evaluate(value),risk::State::invalid,risk::Reason::budget);
    value=input();value.jobs[1].key=value.jobs[0].key;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::duplicate_job);
    value=input();value.jobs[0].key[0]=6;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::job_id);
    value=input();value.jobs[0].key[1]=-2;empty(risk::evaluate(value),risk::State::invalid,risk::Reason::job_id);
    value=input(4,1);value.probabilityWeights={4096,0,0,0};value.radius=value.budget={4096,4096};
    for(auto& outcome:value.jobs[0].outcomes)outcome={risk::maximum_value,true,true,true};
    result=risk::evaluate(value);certificates(value,result);
    CHECK(result.denominator==16777216 && result.rows[0].nominal.numerator==16777216000000000000ULL);
    CHECK(result.rows[0].worst.numerator==result.rows[0].nominal.numerator && result.ruin.present);
    value=input(4,28);for(unsigned j=0;j<28;++j)for(auto& outcome:value.jobs[j].outcomes)outcome.value=j;
    result=risk::evaluate(value);CHECK(result.rowCount==28 && result.ambiguity.row==27);certificates(value,result);
    value.jobCount=1;value.jobs[27].key[0]=-8;result=risk::evaluate(value);CHECK(result.state==risk::State::known && result.rowCount==1);
}
plan3::InformationState snapshot() {
    plan3::InformationState info;info.round=20;info.roundLimit=300;info.winningGems=1000;
    info.health=info.maximumHealth=100;return info;
}
plan3::JobForecast forecast(int id,int reward=10,int tau=5) {
    plan3::JobForecast job;job.id={plan3::JobKind::cycle,id,-1,7,-1};
    for(unsigned w=0;w<4;++w) {
        plan3::Outcome o;o.response=static_cast<plan3::ResponseWorld>(w);
        o.deltaS=reward;o.tau=tau;o.terminal.health=o.terminal.maximumHealth=100;
        if(reward)o.conversions.push_back({tau,reward});job.outcomes.push_back(o);
    }
    return job;
}
void mapping_tests() {
    auto info=snapshot();plan3::ControllerContext context;risk::Model model;model.valueUnits=1;
    std::vector<plan3::JobForecast> jobs{forecast(9),forecast(2,8)};
    auto mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.state==risk::State::known && mapped.input.jobCount==2 && mapped.canonicalCount==2);
    CHECK(mapped.input.jobs[0].key[1]==2 && mapped.input.jobs[1].key[1]==9);
    CHECK(mapped.input.probabilityWeights==(std::array<std::uint32_t,4>{1024,1024,1024,1024}));
    jobs[0].outcomes[0].terminal.loops.push_back({4,10,10,3,true,plan3::Provenance::assumed});
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[1].outcomes[0].value==46);
    model.family=risk::Family::ruin;mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.input.jobs[1].outcomes[0].value==10);model.family=risk::Family::ambiguity;

    // All-outcome Robust control must not average away the bad lifecycle.
    jobs={forecast(1,0),forecast(2,8)};
    for(auto& job:jobs)for(unsigned w=0;w<4;++w) {
        auto alternative=job.outcomes[w];alternative.lifecycle=1;
        alternative.deltaS=job.id.firstGroup==1?20:8;alternative.conversions={{5,alternative.deltaS}};
        alternative.conditionalLifecycleWeight=job.outcomes[w].conditionalLifecycleWeight=0.5;
        job.outcomes.push_back(alternative);
    }
    mapped=risk::map_scenarios(info,jobs,context,model);const auto result=risk::evaluate(mapped.input);
    CHECK(mapped.state==risk::State::known && result.minimax.key[1]==1);
    CHECK(risk::full_minimax(mapped).key[1]==2 && mapped.fullMinimaxValues[0]==0 && mapped.fullMinimaxValues[1]==8);
    jobs[0].outcomes[4].healthLoss=100;
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);
    jobs[0].outcomes.pop_back();mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.input.jobCount==1 && mapped.input.jobs[0].key[1]==2 && mapped.omittedUnknown==1 && mapped.candidatePrefix);
    jobs={forecast(1),forecast(2)};jobs[0].outcomes[0].conversions[0].gems=9;
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobCount==1 && mapped.omittedUnknown==1);
    jobs[0].outcomes[0].conversions[0].gems=-1;
    CHECK(risk::map_scenarios(info,jobs,context,model).omittedUnknown==1);
    jobs[0].outcomes[0].conversions[0].gems=10;jobs[0].outcomes[0].healthLoss=std::numeric_limits<double>::infinity();
    CHECK(risk::map_scenarios(info,jobs,context,model).omittedUnknown==1);
    jobs={forecast(1)};jobs[0].outcomes.pop_back();mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.state==risk::State::unavailable && !mapped.input.jobCount && mapped.omittedUnknown==1);
    CHECK(!risk::full_minimax(mapped).present);

    info.carriedGems=10;model.family=risk::Family::ruin;
    jobs={forecast(1,10,150)};jobs[0].id.kind=plan3::JobKind::bank;
    mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.state==risk::State::known && !mapped.input.jobs[0].outcomes[0].value && !mapped.input.jobs[0].outcomes[0].loss);
    jobs={forecast(1,0)};for(auto& o:jobs[0].outcomes)o.terminal.bag=10;
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(!mapped.input.jobs[0].outcomes[0].loss);
    jobs[0].outcomes[0].terminal.bag=9;
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);
    info.carriedGems=0;jobs={forecast(1)};jobs[0].outcomes[0].healthLoss=info.health;
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);

    // Cumulative damage across explicit healing phases is not one fatal hit.
    jobs={forecast(1)};
    for(auto& outcome:jobs[0].outcomes) {outcome.healthLoss=150;outcome.minimumHealth=20;}
    mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.state==risk::State::known);
    for(const auto& outcome:mapped.input.jobs[0].outcomes)CHECK(!outcome.loss);
    CHECK(risk::evaluate(mapped.input).ruin.present);
    jobs[0].outcomes[0].minimumHealth=0; // Healed terminal HP still cannot erase a death.
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);
    jobs[0].outcomes[0].minimumHealth.reset();
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);
    for(const auto invalid:{-1.0,101.0,std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        jobs[0].outcomes[0].minimumHealth=invalid;
        mapped=risk::map_scenarios(info,jobs,context,model);
        CHECK(mapped.state==risk::State::unavailable && mapped.omittedUnknown==1 && !mapped.input.jobCount);
    }
    jobs={forecast(1,0)};info.carriedGems=10;
    for(auto& outcome:jobs[0].outcomes) {outcome.minimumHealth=20;outcome.terminal.bag=9;}
    mapped=risk::map_scenarios(info,jobs,context,model);CHECK(mapped.input.jobs[0].outcomes[0].loss);
    info.carriedGems=0;jobs={forecast(1)};

    std::array<std::uint32_t,4> counts{};
    CHECK(risk::probability_counts({0.25,0.25,0.25,0.25},3,counts));
    CHECK(counts==(std::array<std::uint32_t,4>{1,1,1,0}));
    CHECK(!risk::probability_counts({0.2,0.2,0.2,0.2},4096,counts));
    CHECK(!risk::probability_counts({1,0,0,-0.01},4096,counts));
    CHECK(!risk::probability_counts({1,0,0,0},4097,counts));
    for(unsigned cut=0;cut<=6;++cut) {
        unsigned reads=0;const auto limited=risk::map_scenarios(info,jobs,context,model,[&] {return reads++!=cut;});
        if(cut<6)CHECK(limited.state==risk::State::unavailable && !limited.input.jobCount);
        else CHECK(limited.state==risk::State::known);
    }
    // Stable FIRST duplicate and canonical physical IDs are the shared menu's.
    jobs={forecast(7,3),forecast(7,99)};mapped=risk::map_scenarios(info,jobs,context,model);
    CHECK(mapped.input.jobCount==1 && mapped.input.jobs[0].outcomes[0].value==3);
}
void read_unsigned(std::FILE* file,unsigned& value){CHECK(std::fscanf(file,"%u",&value)==1);}
void read_value(std::FILE* file,std::uint64_t& value){CHECK(std::fscanf(file,"%" SCNu64,&value)==1);}
void oracle_tests(const char* path) {
    std::FILE* file=std::fopen(path,"r");CHECK(file!=nullptr);
    unsigned count=0;read_unsigned(file,count);CHECK(count>0 && count<=100000);
    for(unsigned index=0;index<count;++index) {
        risk::Input value;read_unsigned(file,value.worldCount);read_unsigned(file,value.jobCount);
        CHECK(value.worldCount<=4 && value.jobCount<=28);
        for(auto& weight:value.probabilityWeights)read_unsigned(file,weight);
        read_unsigned(file,value.radius.numerator);read_unsigned(file,value.radius.denominator);
        read_unsigned(file,value.budget.numerator);read_unsigned(file,value.budget.denominator);
        for(unsigned j=0;j<value.jobCount;++j) {
            auto& job=value.jobs[j];job.complete=true;job.outcomeCount=value.worldCount;
            for(auto& part:job.key){int parsed=0;CHECK(std::fscanf(file,"%d",&parsed)==1);part=parsed;}
            for(auto& outcome:job.outcomes){read_value(file,outcome.value);outcome.known=outcome.lossKnown=true;}
            for(auto& outcome:job.outcomes){unsigned loss=0;read_unsigned(file,loss);CHECK(loss<=1);outcome.loss=bool(loss);}
        }
        const auto result=risk::evaluate(value);certificates(value,result);
        unsigned denominator=0;read_unsigned(file,denominator);CHECK(result.denominator==denominator);
        unsigned arm=0;
        for(const auto* choice:{&result.ambiguity,&result.belief,&result.minimax,&result.ruin}) {
            int row=0;CHECK(std::fscanf(file,"%d",&row)==1);
            CHECK(choice->present==(row>=0));
            if(row>=0){const auto& selected=result.rows[unsigned(row)];
                const auto score=arm==0?selected.worst:arm==2?selected.minimum:selected.nominal;
                CHECK(choice->row==unsigned(row) && choice->key==value.jobs[unsigned(row)].key &&
                      choice->score.denominator==denominator && choice->score.numerator==score.numerator);}
            ++arm;
        }
        for(unsigned j=0;j<value.jobCount;++j) {
            const auto& row=result.rows[j];
            for(const auto* score:{&row.nominal,&row.worst,&row.minimum,&row.nominalLoss,&row.upperLoss}) {
                std::uint64_t expected=0;read_value(file,expected);CHECK(score->numerator==expected && score->denominator==denominator);
            }
            for(const auto* q:{&row.worstProbabilities,&row.upperLossProbabilities})
                for(const auto actual:*q){unsigned expected=0;read_unsigned(file,expected);CHECK(actual==expected);}
        }
        ++oracle_cases;
    }
    unsigned extra=0;CHECK(std::fscanf(file,"%u",&extra)==EOF);CHECK(std::fclose(file)==0);
}
int main(int argc,char** argv) {
    CHECK(argc<=2);hand_tests();mapping_tests();if(argc==2)oracle_tests(argv[1]);
    std::printf("RISK_RESULT {\"checks\":%u,\"oracleCases\":%u,\"nativeCalls\":0}\n",checks,oracle_cases);
}
