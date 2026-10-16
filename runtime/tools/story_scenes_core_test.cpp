#include "mods/story_scenes_core.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
using namespace story_scenes;
struct Fake final:Driver {
    unsigned acquired=0,entered=0,updated=0,finished=0,released=0,complete_at=2;
    bool acquire_ok=true,enter_ok=true,finish_ok=true,update_ok=true;
    StopReason reason=StopReason::Aborted;
    bool acquire()override{++acquired;return acquire_ok;}
    bool enter(size_t)override{++entered;return enter_ok;}
    StepResult update(size_t,unsigned elapsed)override{++updated;return !update_ok?StepResult::Failed:elapsed>=complete_at?StepResult::Complete:StepResult::Running;}
    bool finish()override{++finished;return finish_ok;}
    void release(StopReason r)override{++released;reason=r;}
};
int main(){
    static_assert(!std::is_copy_constructible_v<Player>);
    {Player p;Fake f;assert(!p.start({},f,1,false));assert(!p.start({{1,false}},f,1,false));assert(!p.start({{1,true},{0,true}},f,1,false));assert(!p.start({{0,false},{0,true}},f,1,false));assert(!p.start({{18001,false},{0,true}},f,1,false));assert(!p.start({{2,false},{0,true}},f,1,true));assert(f.acquired==0);}
    {Player p;Fake f;f.acquire_ok=false;assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.acquired==1&&f.released==0);}
    {Player p;Fake f;assert(p.start({{2,false},{0,true}},f,1,false));assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.acquired==1);p.tick(1);assert(p.step()==0&&p.elapsed()==1);p.tick(1);assert(p.step()==1&&p.active());p.tick(1);assert(!p.active()&&f.finished==1&&f.released==1&&f.reason==StopReason::Finished);p.stop();p.tick(1);assert(f.released==1);}
    {Player p;Fake f;f.complete_at=18000;assert(p.start({{18000,false},{0,true}},f,1,false));for(unsigned n=0;n<18000;++n)p.tick(1);assert(p.active());p.tick(1);assert(!p.active()&&f.finished==1&&f.reason==StopReason::Finished);}
    {Player p;Fake f;f.enter_ok=false;assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.released==1&&f.finished==0&&f.reason==StopReason::Failed);}
    {Player p;Fake f;f.update_ok=false;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);assert(f.released==1&&f.finished==0&&f.reason==StopReason::Failed);}
    {Player p;Fake f;f.complete_at=3;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);p.tick(1);assert(f.released==1&&f.finished==0&&f.reason==StopReason::TimedOut);}
    for(auto reason:{StopReason::Skipped,StopReason::Aborted,StopReason::StateLoaded,StopReason::Interrupted}){
        Player p;Fake f;assert(p.start({{0,true}},f,7,false));
        if(reason==StopReason::Skipped)p.tick(7,false,true);
        else if(reason==StopReason::Aborted)p.stop();
        else if(reason==StopReason::StateLoaded)p.tick(8,false,true);
        else p.tick(7,true,true);
        assert(!p.active()&&f.released==1&&f.finished==0&&f.reason==reason);p.stop();assert(f.released==1);
    }
    {Player p;Fake f;f.finish_ok=false;assert(p.start({{0,true}},f,7,false));p.tick(7);assert(f.finished==1&&f.released==1&&f.reason==StopReason::Failed);}
    {Player p;Fake f;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);p.stop();assert(p.start({{2,false},{0,true}},f,2,false));p.tick(2);p.tick(2);p.tick(2);assert(f.released==2&&f.finished==1);}
    std::puts("story scene lifecycle tests passed");
}
