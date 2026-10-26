#pragma once
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace plan3_runtime {
// Exactly bounded tile expansions. The SDK native field's limit is a distance,
// so it cannot establish this work bound. Incomplete fields stay unknown.
class StaticPaths {
public:
    static constexpr int unknown = -2, unreachable = -1;
    struct Field {
        cambc::bitboard roots;
        bool complete = false;
        std::unique_ptr<int[]> distance, next_tile, endpoint;
        std::vector<int> queue, roots_list;
        std::size_t head = 0;
        int prepared = 0;
    };
private:
    cambc::bitboard walls_;
    std::vector<std::uint8_t> wall_bits_;
    std::vector<Field> fields_;
    std::size_t next_ = 0;
    int width_ = 0, height_ = 0;
    unsigned work_ = 0;
    unsigned generation_ = 0;
    void guest_unit(Field& f,unsigned reserve) {
        const int tiles=width_*height_;
        if(f.prepared<tiles) {
            unsigned count=0;
            // Raw arrays are allocated without value initialization by add().
            // Initialize at most 256 tiles in this same shared work budget.
            while(f.prepared<tiles && count++<256) {
                if((count%16)==0 && cambc::fuel_remaining()<reserve)return;
                const int at=f.prepared++;
                f.distance[at]=f.next_tile[at]=f.endpoint[at]=-1;
            }
            if(f.prepared==tiles) {
                for(const int at:f.roots_list) {
                    f.distance[at]=0;f.queue.push_back(at);
                    f.next_tile[at]=f.endpoint[at]=at;
                }
                f.complete=f.queue.empty();
            }
            return;
        }
        unsigned count = 0;
        while (f.head < f.queue.size() && count++ < 256) {
            if ((count % 16) == 0 && cambc::fuel_remaining() < reserve) return;
            const int at = f.queue[f.head++], x = at % width_, y = at / width_;
            int predecessor = -1;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
                const int ni = ny * width_ + nx;
                if (f.distance[at]>0 && f.distance[ni] == f.distance[at]-1 &&
                    (predecessor < 0 || ni < predecessor)) predecessor = ni;
                if (f.distance[ni] < 0 && !((wall_bits_[ni/8]>>(ni%8))&1u)) {
                    f.distance[ni] = f.distance[at] + 1;
                    f.queue.push_back(ni);
                }
            }
            // All smaller-distance nodes have already been expanded. Cache
            // the exact descending route's endpoint without walking it again.
            if (predecessor >= 0) {
                f.next_tile[at] = predecessor;
                f.endpoint[at] = f.endpoint[predecessor];
            }
        }
        f.complete = f.head == f.queue.size();
    }
public:
    void initialize(cambc::bitboard walls) {
        walls_ = std::move(walls); width_ = cambc::map::width(); height_ = cambc::map::height();
        // No full-map guest unpack or native set-bit enumeration. The packed
        // public wall lookup is read only for neighbors in bounded BFS work.
        wall_bits_=walls_.export_linear_buffer();
    }
    int add(cambc::bitboard roots) {
        roots &= ~walls_;
        Field f; f.roots = std::move(roots);
        const int tiles=width_*height_;
        f.distance.reset(new int[tiles]);
        f.next_tile.reset(new int[tiles]);
        f.endpoint.reset(new int[tiles]);
        for (const auto p : f.roots.positions()) {
            const int at = p.y * width_ + p.x;
            f.roots_list.push_back(at);
        }
        std::sort(f.roots_list.begin(),f.roots_list.end());
        fields_.push_back(std::move(f));
        return static_cast<int>(fields_.size() - 1);
    }
    void begin_turn() { work_ = 0; }
    bool take_work(unsigned reserve=30000) {
        if (work_ >= 2 || cambc::fuel_remaining() < reserve) return false;
        ++work_; return true;
    }
    void advance(unsigned reserve=30000) {
        while (next_ < fields_.size() && take_work(reserve)) {
            auto& f = fields_[next_];
            guest_unit(f,reserve);
            if (f.complete) { ++next_; ++generation_; }
        }
    }
    bool prepare_idle(bool idle,unsigned owner_turn,bool registration_pending,bool urgent=false) {
        if(!idle || (!urgent && owner_turn%4!=0) || (!registration_pending && ready()))return false;
        advance();
        return true;
    }
    bool complete(int id) const {
        return id >= 0 && static_cast<std::size_t>(id) < fields_.size() && fields_[id].complete;
    }
    bool ready() const { return next_ == fields_.size(); }
    unsigned generation() const { return generation_; }
    int width() const { return width_; }
    const std::vector<int>& roots(int id) const {
        static const std::vector<int> empty;
        return id >= 0 && static_cast<std::size_t>(id) < fields_.size() ? fields_[id].roots_list : empty;
    }
    std::optional<cambc::vec2> endpoint(int id, cambc::vec2 p) const {
        if (get(id,p)<0) return std::nullopt;
        const int at = fields_[id].endpoint[p.y*width_+p.x];
        return at>=0 ? std::optional<cambc::vec2>{{at%width_,at/width_}} : std::nullopt;
    }
    std::optional<cambc::vec2> next_step(int id, cambc::vec2 p) const {
        if (get(id,p)<0) return std::nullopt;
        const int at = fields_[id].next_tile[p.y*width_+p.x];
        return at>=0 ? std::optional<cambc::vec2>{{at%width_,at/width_}} : std::nullopt;
    }
    int minimum_between(int id, int from_field) const {
        if (!complete(id)) return unknown;
        int best=unreachable;
        for (const int tile : roots(from_field)) {
            const int d=fields_[id].distance[tile];
            if (d>=0 && (best<0 || d<best)) best=d;
        }
        return best;
    }
    std::optional<cambc::vec2> closest_root(int roots_field, int distance_field) const {
        if (!complete(distance_field)) return std::nullopt;
        int best=-1, bd=-1;
        for (const int tile : roots(roots_field)) {
            const int d=fields_[distance_field].distance[tile];
            if (d>=0 && (bd<0 || d<bd)) { best=tile; bd=d; }
        }
        return best>=0 ? std::optional<cambc::vec2>{{best%width_,best/width_}} : std::nullopt;
    }
    int get(int id, cambc::vec2 p) const {
        if (!complete(id)) return unknown;
        const auto& f = fields_[id];
        if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) return unreachable;
        return f.distance[p.y * width_ + p.x];
    }
    int minimum(int id, const cambc::bitboard& from) const {
        if (!complete(id)) return unknown;
        int best = unreachable;
        for (const auto p : from.positions()) {
            const int d = get(id, p);
            if (d >= 0 && (best < 0 || d < best)) best = d;
        }
        return best;
    }
};
} // namespace plan3_runtime
