#pragma once
#include "native_service_navigation.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace mosaic::native_service {

enum class ServiceField : std::uint8_t {service,egress};

// Two persistent borrowers, not a copied/swapped Navigation scratch buffer.
// Selecting one is constant work. begin() has the same bounded root-copy/
// comparison contract as Navigation. Their common board must be immutable;
// retire_all() BEFORE editing shared storage, then begin with its new semantic
// intent. Retiring one context does not release another context's borrow.
template<std::size_t MaxCells=16384> class ServiceFields {
public:
    using NavigationType=Navigation<MaxCells>;
    constexpr ServiceFields()noexcept=default;
    ServiceFields(const ServiceFields&)=delete;
    ServiceFields& operator=(const ServiceFields&)=delete;
    ServiceFields(ServiceFields&&)=delete;
    ServiceFields& operator=(ServiceFields&&)=delete;

    bool begin(ServiceField which,int width,int height,std::span<const std::uint8_t> board,
               std::span<const Point> roots,std::uint64_t semantic_intent)noexcept {
        if(!valid(which))return false;
        auto& context=contexts_[index(which)];
        bool same=context.active&&context.field.status()!=NavigationStatus::idle&&
            context.field.status()!=NavigationStatus::failed&&context.width==width&&context.height==height&&
            context.board==board.data()&&context.intent==semantic_intent&&
            roots.size()<=NavigationType::max_roots&&context.root_count==roots.size();
        for(std::size_t i=0;same&&i<roots.size();++i)same=context.roots[i]==roots[i];
        if(!same&&next_generation_==std::numeric_limits<std::uint64_t>::max()){
            retire(which);return false; // never reuse a published identity
        }
        if(!context.field.begin(width,height,board,roots,semantic_intent)){
            forget(context);return false;
        }
        if(same)return true;
        context.width=width;context.height=height;context.board=board.data();context.intent=semantic_intent;
        context.root_count=roots.size();
        for(std::size_t i=0;i<roots.size();++i)context.roots[i]=roots[i];
        context.active=true;context.generation=++next_generation_;return true;
    }
    NavigationProgress step(ServiceField which,unsigned units)noexcept {
        if(!valid(which))return {NavigationStatus::failed,0,false};
        auto& context=contexts_[index(which)];
        const auto progress=context.field.step(units);
        if(progress.status==NavigationStatus::failed)forget(context);
        return progress;
    }
    // Admission precedes EACH real Navigation leaf, rather than pricing a
    // whole cap from one callback. Denial retains this exact field/cursor for
    // a later paid input; the caller owns the callback and leaf cost bounds.
    // If admission retires/rebinds this context, do not spend its old grant on
    // the replacement. Progress already performed still counts as spent work.
    template<class Admit> NavigationProgress step_guarded(ServiceField which,unsigned cap,Admit&& admit) {
        if(!valid(which))return {NavigationStatus::failed,0,false};
        auto& context=contexts_[index(which)];
        const auto generation=context.generation;
        NavigationProgress total{context.field.status(),0,false};
        while(total.units<cap&&total.status==NavigationStatus::preparing){
            const bool allowed=bool(admit());
            total.status=context.field.status();
            if(!allowed||!context.active||context.generation!=generation||
               total.status!=NavigationStatus::preparing)break;
            const auto leaf=step(which,1);
            total.units+=leaf.units;total.progressed|=leaf.progressed;total.status=leaf.status;
            if(!leaf.units||!leaf.progressed)break;
        }
        return total;
    }
    void retire(ServiceField which)noexcept {
        if(!valid(which))return;
        auto& context=contexts_[index(which)];context.field.retire();forget(context);
    }
    void retire_all()noexcept {retire(ServiceField::service);retire(ServiceField::egress);}

    // A read-only reference keeps all mutation through this wrapper, so its
    // generation cannot silently outlive an external retire()/begin().
    [[nodiscard]] const NavigationType& navigation(ServiceField which)const {
        if(!valid(which))throw std::invalid_argument("invalid service field");
        return contexts_[index(which)].field;
    }
    [[nodiscard]] bool active(ServiceField which)const noexcept {
        return valid(which)&&contexts_[index(which)].active;
    }
    [[nodiscard]] std::uint64_t generation(ServiceField which)const noexcept {
        return active(which)?contexts_[index(which)].generation:0;
    }
    [[nodiscard]] std::uint64_t semantic_intent(ServiceField which)const noexcept {
        return active(which)?contexts_[index(which)].intent:0;
    }
    [[nodiscard]] NavigationStatus status(ServiceField which)const noexcept {
        return valid(which)?contexts_[index(which)].field.status():NavigationStatus::failed;
    }
    [[nodiscard]] NavigationStatus status(ServiceField which,Point at)const noexcept {
        return valid(which)?contexts_[index(which)].field.status(at):NavigationStatus::failed;
    }
    [[nodiscard]] bool available(ServiceField which,Point at)const noexcept {
        return valid(which)&&contexts_[index(which)].field.available(at);
    }
    [[nodiscard]] std::optional<int> distance(ServiceField which,Point at)const noexcept {
        return valid(which)?contexts_[index(which)].field.distance(at):std::nullopt;
    }
    unsigned nominees(ServiceField which,Point from,std::span<const Point> excluded,std::span<Point> out)const noexcept {
        return valid(which)?contexts_[index(which)].field.nominees(from,excluded,out):0;
    }

private:
    struct Context {
        // Default initialization leaves Navigation scratch untouched. A
        // static constinit ServiceFields value{} may legitimately zero it.
        NavigationType field;
        std::array<Point,NavigationType::max_roots> roots{};
        const std::uint8_t* board{};std::size_t root_count{};
        int width{},height{};std::uint64_t intent{},generation{};bool active{};
    };
    std::array<Context,2> contexts_;std::uint64_t next_generation_{};
    static constexpr bool valid(ServiceField which)noexcept {
        return which==ServiceField::service||which==ServiceField::egress;
    }
    static constexpr std::size_t index(ServiceField which)noexcept {
        return which==ServiceField::egress?1u:0u;
    }
    static constexpr void forget(Context& context)noexcept {
        context.active=false;context.board=nullptr;context.root_count=0;context.width=context.height=0;
        context.intent=context.generation=0;
    }
};

} // namespace mosaic::native_service
