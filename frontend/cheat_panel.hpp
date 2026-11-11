#pragma once
#include "cheat_search.hpp"
#include "presentation.hpp"
#include "play_tools_ui.hpp"

namespace play_tools {
struct PanelWidget { int id; presentation::Rect rect; std::string text; bool selected = false; int font = 20; };
class CheatPanel {
public:
    CheatSearch search;
    SearchQuery query;
    std::string name, value, message = "Search names or values. Close and play,\nthen Refine to narrow previous results.";
    int tab = 2, scroll = 0, selected = 2000007;
    bool comparisons = false;
    static const char* comparison_name(Comparison comparison) {
        static const char* names[]{"Any / baseline", "Equals", "Not equal", "Greater than", "Less than", "Changed", "Unchanged", "Increased", "Decreased"};
        return names[static_cast<int>(comparison)];
    }
    std::vector<int> rows(wolf::Runtime& runtime, const Cheats& cheats) const {
        std::set<int> refs;
        if (tab == 1) for (const auto& [ref, old] : search.results) { (void)old; refs.insert(ref); }
        else if (tab == 2) { for (const auto& [ref, entry] : cheats.entries) if (entry.pinned) refs.insert(ref); }
        else {
            for (const auto& [ref, label] : cheats.names) { (void)label; refs.insert(ref); }
            for (int ref : runtime.numeric_variable_refs()) refs.insert(ref);
        }
        return {refs.begin(), refs.end()};
    }
    std::vector<PanelWidget> widgets(wolf::Runtime& runtime, const Cheats& cheats) {
        std::vector<PanelWidget> out;
        auto add = [&](int id, float x, float y, float w, float h, std::string text, bool active = false, int font = 20) {
            out.push_back({id,{x,y,w,h},std::move(text),active,font});
        };
        add(0,20,10,140,42,"Cheats",false,28);
        add(1,180,12,145,42,cheats.enabled ? "Cheats ON" : "Cheats OFF",cheats.enabled);
        add(2,340,12,175,42,cheats.show_pins ? "Pins HUD ON" : "Pins HUD OFF",cheats.show_pins);
        add(3,850,12,155,42,"Close / Play");
        add(4,20,72,360,46,name.empty() ? "Name or variable ID..." : name);
        add(5,390,72,170,46,value.empty() ? "Value (any)" : value);
        add(6,570,72,235,46,comparison_name(query.comparison),comparisons);
        add(7,815,72,190,46,"New search");
        add(8,815,128,190,44,"Refine results",tab==1);
        add(9,570,128,112,44,"Undo scan",false,16); add(10,692,128,113,44,"Clear filters",false,16);
        add(11,20,128,165,44,"All variables",tab==0); add(12,195,128,165,44,"Results ("+std::to_string(search.results.size())+")",tab==1);
        add(13,370,128,190,44,"Pinned variables",tab==2);
        auto visible = rows(runtime,cheats);
        scroll = std::clamp(scroll,0,std::max(0,static_cast<int>(visible.size())-6));
        add(0,20,184,680,28,std::to_string(visible.size())+" variables - swipe or use arrows to scroll",false,16);
        for (int i = 0; i < 6 && scroll+i < static_cast<int>(visible.size()); ++i) {
            const int ref = visible[scroll+i]; const auto entry = cheats.entries.find(ref);
            const bool pinned = entry != cheats.entries.end() && entry->second.pinned, frozen = entry != cheats.entries.end() && entry->second.frozen;
            add(1000+i,20,218+i*47,670,44,cheats.label(ref)+"\n#"+std::to_string(ref-2000000)+"    "+std::to_string(runtime.number(ref))+(frozen?"   [FROZEN]":"")+(pinned?"   [PIN]":""),ref==selected,14);
        }
        add(14,20,510,80,38,"Up"); add(15,110,510,80,38,"Down");
        add(0,210,510,480,46,message,false,15);
        add(0,715,185,290,74,cheats.label(selected)+"\nVariable "+std::to_string(selected-2000000),false,20);
        add(0,715,265,290,38,std::to_string(runtime.number(selected)),false,28);
        const auto entry = cheats.entries.find(selected);
        const bool frozen = entry != cheats.entries.end() && entry->second.frozen, pinned = entry != cheats.entries.end() && entry->second.pinned;
        add(16,715,318,140,48,"Set value"); add(17,865,318,140,48,frozen?"Unfreeze":"Freeze",frozen);
        add(18,715,376,140,48,pinned?"Unpin":"Pin",pinned); add(19,865,376,140,48,"Rename");
        add(20,715,434,140,48,"Refill"); add(21,865,434,140,48,"+100");
        add(0,715,494,290,58,"Rename pins to identify them.\n[FROZEN] locks the current value.",false,15);
        if (comparisons) for (int i=0;i<9;++i) add(100+i,570,124+i*43,235,41,comparison_name(static_cast<Comparison>(i)),static_cast<int>(query.comparison)==i,18);
        return out;
    }
    void scan(wolf::Runtime& runtime, const Cheats& cheats, bool refine) {
        query.name=name; query.value.reset();
        if (!value.empty()) {
            std::int32_t parsed=0; const auto read=std::from_chars(value.data(),value.data()+value.size(),parsed);
            if (read.ec!=std::errc{} || read.ptr!=value.data()+value.size()) throw std::runtime_error("Enter a whole number between -2147483648 and 2147483647");
            query.value=parsed;
        }
        search.scan(runtime,cheats,query,refine); tab=1; scroll=0;
        if (!search.results.empty()) selected=search.results.begin()->first;
        message=std::to_string(search.results.size())+" matches. Refine searches only these results.";
    }
};

using PanelPaint = std::function<presentation::Rect(const std::vector<PanelWidget>&)>;
inline void show_cheat_panel(SDL_Window* window, SDL_Renderer* renderer, wolf::Runtime& runtime, Cheats& cheats, CheatPanel& panel,
    const PanelPaint& paint, const PromptPaint& prompt, std::mutex& guard) {
    bool done=false, held=false, dragged=false; float start_x=0,start_y=0,last_y=0;
    while (!done) {
        auto widgets=panel.widgets(runtime,cheats); const auto area=paint(widgets);
        auto position=[&](int x,int y) {
            int ww=0,wh=0,pw=0,ph=0; SDL_GetWindowSize(window,&ww,&wh); SDL_GetRendererOutputSize(renderer,&pw,&ph);
            return std::pair<float,float>{(x*static_cast<float>(pw)/std::max(1,ww)-area.x)*1024/area.w,(y*static_cast<float>(ph)/std::max(1,wh)-area.y)*576/area.h};
        };
        SDL_Event event; if (!SDL_WaitEventTimeout(&event,16)) continue;
        if (event.type==SDL_QUIT || event.type==SDL_APP_WILLENTERBACKGROUND) { SDL_PushEvent(&event); break; }
        if (event.type==SDL_KEYDOWN && (event.key.keysym.sym==SDLK_ESCAPE || event.key.keysym.sym==SDLK_F12)) break;
        if (event.type==SDL_MOUSEWHEEL) { panel.scroll=std::max(0,panel.scroll-event.wheel.y*3); continue; }
        if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT) {
            const auto [x,y]=position(event.button.x,event.button.y); held=true; dragged=false; start_x=x; start_y=last_y=y; continue;
        }
        if (event.type==SDL_MOUSEMOTION && held) {
            const auto [x,y]=position(event.motion.x,event.motion.y);
            if (std::hypot(x-start_x,y-start_y)>8) dragged=true;
            if (dragged && start_x<700 && start_y>215 && start_y<510 && std::abs(y-last_y)>=23) {
                const int distance=static_cast<int>((last_y-y)/23); panel.scroll=std::max(0,panel.scroll+distance); last_y-=distance*23;
            }
            continue;
        }
        if (event.type!=SDL_MOUSEBUTTONUP || event.button.button!=SDL_BUTTON_LEFT || !held) continue;
        held=false; if (dragged) continue;
        const auto [x,y]=position(event.button.x,event.button.y);
        int action=0;
        for (auto it=widgets.rbegin();it!=widgets.rend();++it) if (it->id && presentation::contains(it->rect,x,y) && presentation::contains(it->rect,start_x,start_y)) { action=it->id; break; }
        if (panel.comparisons) {
            if (action>=100 && action<109) panel.query.comparison=static_cast<Comparison>(action-100);
            panel.comparisons=false; continue;
        }
        try {
            if (action==3) done=true;
            if (action==4) { if (auto text=prompt_text(window,"Search variable names or ID",panel.name,prompt)) panel.name=*text; }
            if (action==5) { if (auto text=prompt_text(window,"Search value (blank for any)",panel.value,prompt)) panel.value=*text; }
            if (action==6) panel.comparisons=true;
            if (action==7 || action==8) panel.scan(runtime,cheats,action==8);
            if (action==9) { panel.search.undo(); panel.tab=1; panel.scroll=0; }
            if (action==10) { panel.name.clear(); panel.value.clear(); panel.query.comparison=Comparison::Equal; }
            if (action>=11 && action<=13) { panel.tab=action-11; panel.scroll=0; }
            if (action==14) panel.scroll=std::max(0,panel.scroll-6);
            if (action==15) panel.scroll+=6;
            if (action>=1000 && action<1006) { const auto rows=panel.rows(runtime,cheats); panel.selected=rows.at(panel.scroll+action-1000); }
            const int ref=panel.selected;
            if (action==16) if (auto value=prompt_integer(window,"Set "+cheats.label(ref),runtime.number(ref),prompt)) {
                std::lock_guard<std::mutex> lock(guard); cheats.enabled=true; cheats.edit(runtime,ref,*value);
            }
            if (action==19) if (auto name=prompt_text(window,"Name this variable / pin",cheats.label(ref),prompt)) cheats.rename(ref,*name);
            if (action==1 || action==17 || action==18 || action==20 || action==21) {
                std::lock_guard<std::mutex> lock(guard);
                if (action==1) { cheats.enabled=!cheats.enabled; cheats.apply(runtime); }
                if (action==17) { cheats.enabled=true; cheats.freeze(runtime,ref,!cheats.entries[ref].frozen); }
                if (action==18) cheats.entries[ref].pinned=!cheats.entries[ref].pinned;
                if (action==20) {
                    int maximum=0; for (auto variable:quick_variables) if (variable.ref==ref) maximum=variable.maximum;
                    if (!maximum) panel.message="No known maximum. Use Set value.";
                    else { cheats.enabled=true; cheats.edit(runtime,ref,runtime.number(maximum)); }
                }
                if (action==21) { cheats.enabled=true; cheats.edit(runtime,ref,static_cast<int>(std::min<std::int64_t>(INT32_MAX,static_cast<std::int64_t>(runtime.number(ref))+100))); }
            }
            if (action==2) cheats.show_pins=!cheats.show_pins;
            if ((action>=16 && action<=21) || action==1 || action==2) cheats.persist();
        } catch (const std::exception& error) { panel.message=error.what(); }
    }
}
}
