#include "../common/model-runtime.h"
#include <cassert>
#include <iostream>
struct Observer {
    int before=0,after=0;unsigned item=0,count=0;
    void before_purchase(unsigned i,unsigned n){++before;item=i;count=n;}
    void after_purchase(unsigned i,unsigned n){assert(i==item&&n==count);++after;}
};
struct HalfObserver {void before_purchase(unsigned,unsigned){assert(false);}};
int main() {
    using namespace plan4::runtime;
    static_assert(!observes_purchases<M0> && !observes_purchases<HalfObserver> && observes_purchases<Observer>);
    M0 m0;HalfObserver half;Observer observer;
    before_purchase(m0,7,2);after_purchase(m0,7,2);
    before_purchase(half,7,2);after_purchase(half,7,2);
    before_purchase(observer,7,2);after_purchase(observer,7,2);
    assert(observer.before==1&&observer.after==1&&observer.item==7&&observer.count==2);
    std::cout<<"purchase observer dispatch cases=3\n";
}
