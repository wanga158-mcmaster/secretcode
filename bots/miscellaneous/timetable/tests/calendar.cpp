#include "calendar.h"
#include <cassert>
int main()
{
    tt::ItineraryCalendar job;
    job.issue(5,2,2,10,10,40,120);
    assert(job.group==5 && job.cursor==0 && job.crew==2);
    job.recover();
    assert(job.group==5 && job.phase==tt::JobPhase::recovering && job.cursor==0);
    assert(!job.release(7,tt::JobRelease::captured));
    assert(job.group==5); // another task's payout cannot release this commitment
    assert(job.release(5,tt::JobRelease::captured));
    assert(job.group==-1 && job.cursor==3 && job.phase==tt::JobPhase::idle);
    job.issue(8,3,1,130,135,140,180);
    assert(job.ready==135 && job.deadline==180);
    assert(job.release(8,tt::JobRelease::external_capture));
    assert(job.group==-1 && job.cursor==4);
}
