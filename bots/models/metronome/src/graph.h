#pragma once
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::metronome {
// Public, immutable terrain only. A field's initialized bitmap and queue prefix
// survive owner turns; no physical forecast or dynamic occupant survives them.
class Graph {
    struct Field {
        Point target{};
        std::unique_ptr<int[]> distance,queue;
        std::unique_ptr<unsigned char[]> seen;
        int head=0,tail=0,neighbor=0;
        std::size_t cleared=0;
        bool seeded=false;
        bool has(int key)const{return (seen[key/8]&(1u<<(key%8)))!=0;}
        void add(int key,int d){seen[key/8]|=static_cast<unsigned char>(1u<<(key%8));distance[key]=d;queue[tail++]=key;}
    };
    std::array<Field,16> fields_{};
    runtime::PublicTopology topology_{};
    std::uint64_t identity_=0;
    int visits_=0;
    int index(Point p)const{return p.y*topology_.width+p.x;}
    bool inside(Point p)const{return p.x>=0&&p.y>=0&&p.x<topology_.width&&p.y<topology_.height;}
    static bool budget(WorkBudget w){return !w.can_refine||w.can_refine(w.context);}
public:
    bool bind(runtime::PublicTopology topology,std::uint64_t identity){
        if(!identity||!topology.complete||!topology.walkable||topology.width<1||topology.height<1||
            std::int64_t(topology.width)*topology.height>4096)return false;
        if(identity_!=identity||topology.width!=topology_.width||topology.height!=topology_.height||
            topology.walkable!=topology_.walkable||topology.context!=topology_.context){fields_={};identity_=identity;}
        topology_=topology;return true;
    }
    void begin_turn(){visits_=0;}
    int vertices()const{return visits_;}
    int cached(Point a,Point b)const{
        if(!inside(a)||!inside(b))return -1;
        if(a==b)return 0;
        for(const auto& f:fields_)if(f.seeded&&(f.target==a||f.target==b)){
            const int key=index(f.target==a?b:a);if(f.has(key))return f.distance[key];
        }
        return -1;
    }
    bool pending(Point a,Point b)const{
        if(cached(a,b)>=0)return false;
        for(const auto& f:fields_)if(f.distance&&(f.target==a||f.target==b))return !f.seeded||f.head<f.tail;
        for(const auto& f:fields_)if(!f.distance)return true;
        return false; // Fixed capacity is explicit missing work, never eviction.
    }
    int advance(Point a,Point b,int quota,WorkBudget work){
        if(quota<0||quota>32||!inside(a)||!inside(b)||!budget(work))return -1;
        const int known=cached(a,b);if(known>=0)return known;
        if(!topology_.walkable(a,topology_.context)||!topology_.walkable(b,topology_.context))return -1;
        Field* field=nullptr;
        for(auto& f:fields_)if(f.distance&&(f.target==a||f.target==b)){field=&f;break;}
        if(!field)for(auto& f:fields_)if(!f.distance){field=&f;f.target=b;break;}
        if(!field)return -1;
        auto& f=*field;const auto count=std::size_t(topology_.width)*topology_.height,bytes=(count+7)/8;
        if(!f.distance){if(!budget(work))return -1;f.distance.reset(new int[count]);}
        if(!f.queue){if(!budget(work))return -1;f.queue.reset(new int[count]);}
        if(!f.seen){if(!budget(work))return -1;f.seen.reset(new unsigned char[bytes]);}
        while(f.cleared<bytes){if(!budget(work))return -1;const auto n=std::min(std::size_t(32),bytes-f.cleared);
            std::fill_n(f.seen.get()+f.cleared,n,static_cast<unsigned char>(0));f.cleared+=n;}
        if(!f.seeded){if(!budget(work))return -1;f.add(index(f.target),0);f.seeded=true;}
        const int key=index(f.target==a?b:a),stop=visits_+quota;
        while(!f.has(key)&&f.head<f.tail&&visits_<stop){
            if(!budget(work))return -1;
            ++visits_;
            const int current=f.queue[f.head];const Point p{current%topology_.width,current/topology_.width};
            while(f.neighbor<9){const int dx=f.neighbor%3-1,dy=f.neighbor/3-1;++f.neighbor;
                if(!dx&&!dy)continue;
                const Point next{p.x+dx,p.y+dy};
                if(!inside(next)||!topology_.walkable(next,topology_.context))continue;
                const int child=index(next);if(!f.has(child))f.add(child,f.distance[current]+1);
            }
            f.neighbor=0;++f.head;
        }
        return f.has(key)?f.distance[key]:-1;
    }
};
} // namespace plan4::metronome
