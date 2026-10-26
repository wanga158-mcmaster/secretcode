#pragma once
#include <algorithm>
namespace tt {

enum class JobPhase { idle, travelling, clearing, capturing, recovering };
enum class JobRelease { captured, external_capture, denied, expired, assistance_finished };

struct ItineraryCalendar {
    int cursor = 0, group = -1, rank = -1, crew = 1;
    int issued = -1, ready = -1, assembly_deadline = -1, deadline = -1;
    JobPhase phase = JobPhase::idle;
    void issue(int g, int route_rank, int bodies, int round, int available, int assembly, int finish)
    {
        group=g; rank=route_rank; crew=bodies; issued=round; ready=available;
        assembly_deadline=std::max(round+1,assembly); deadline=std::max(assembly_deadline,finish);
        phase=JobPhase::travelling;
    }
    void recover() { if(group>=0) phase=JobPhase::recovering; }
    bool release(int g, JobRelease)
    {
        if(group!=g) return false;
        cursor=std::max(0,rank+1); group=-1; phase=JobPhase::idle;
        return true;
    }
};

} // namespace tt
