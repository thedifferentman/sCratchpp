#include "scratch/resources.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <utility>

namespace {
using scratch::Json;
struct TemporaryPack {
    std::filesystem::path directory;
    TemporaryPack() {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<100;++attempt) {
            auto candidate=std::filesystem::temp_directory_path()/
                ("scrpp-resource-link-"+std::to_string(stamp)+"-"+std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)){directory=candidate;return;}
        }
        throw scratch::Error("Cannot create temporary resource test directory");
    }
    ~TemporaryPack(){std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    std::string write(const Json& unit) const {
        const auto path=directory/"resources.json";
        std::ofstream stream(path,std::ios::binary);
        stream<<unit.dump();
        if(!stream)throw scratch::Error("Cannot write temporary resource manifest");
        return path.u8string();
    }
};
void require(bool condition,const std::string& message) {
    if(!condition)throw scratch::Error(message);
}
void rejects(const std::function<void()>& action,const std::string& diagnostic) {
    try {action();}
    catch(const scratch::Error& error) {
        require(std::string(error.what()).find(diagnostic)!=std::string::npos,
                "Unexpected diagnostic: "+std::string(error.what()));
        return;
    }
    throw scratch::Error("Expected rejection containing: "+diagnostic);
}
Json pack() {
    return {{"schemaVersion",1},{"package","demo"},{"costumes",Json::array()},
            {"lists",Json::array()},{"events",Json::array()}};
}
Json list(const std::string& name,Json items,bool readonly=false) {
    return {{"name","demo::"+name},{"items",std::move(items)},{"readonly",readonly}};
}
Json wheel() {
    return {{"type","wheel"},{"queue","demo::wheel"},{"enabled","events_enabled"},{"capacity",128}};
}
const Json& serialized_list(const Json& project,const std::string& name) {
    for(const auto& value:project["targets"][1]["lists"].items())
        if(value.value()[0]==name)return value.value()[1];
    throw scratch::Error("Serialized list missing: "+name);
}
void test_resource_lists() {
    TemporaryPack files;
    const auto original=Json::array({" leading and trailing ","", "42",u8"中文😀",u8"\u2028\u2029", "last\r"});
    auto unit=pack();
    unit["lists"].push_back(list("font",original,true));
    unit["lists"].push_back(list("mutable",Json::array({"initial"})));
    unit["events"].push_back(wheel());
    scratch::Project project;
    project.lists["demo::font"]=Json::array(); // Assembly may have declared it first.
    scratch::link_resources(project,{files.write(unit)});
    require(project.lists["demo::font"]==original,"List initialization altered text data");
    require(project.readonly_lists.count("demo::font")==1,"Readonly declaration was lost");
    require(!project.readonly_lists.count("demo::mutable"),"Mutable list became readonly");
    require(project.lists["demo::wheel"].empty(),"Wheel queue must start empty");
    require(project.variables["events_enabled"]==0,"Event enable variable must default to zero");
    require(project.resource_events==unit["events"],"Wheel metadata did not reach Project");
    auto keyboard_unit=pack();
    keyboard_unit["events"]=Json::array({{{"type","keyboard"},{"queue","demo::keys"},
        {"enabled","keys_enabled"},{"capacity",64}}});
    scratch::Project keyboard_project;
    scratch::link_resources(keyboard_project,{files.write(keyboard_unit)});
    require(keyboard_project.resource_events==keyboard_unit["events"],"Keyboard metadata did not reach Project");
    // Verify actual SB3 project serialization, not merely the preparation schema.
    const auto serialized=Json::parse(project.build().dump());
    require(serialized_list(serialized,"demo::font")==original,"SB3 serialization altered font rows");
    require(serialized_list(serialized,"demo::mutable")==Json::array({"initial"}),"Mutable initial items were lost");
    require(serialized["targets"].size()==2,"Resources must not create another execution sprite");

    const auto reject_unit=[&](Json bad,const std::string& diagnostic) {
        rejects([&]{scratch::Project target;scratch::link_resources(target,{files.write(bad)});},diagnostic);
    };
    rejects([&]{scratch::Project target;target.lists["demo::font"]=Json::array({"existing"});
                scratch::link_resources(target,{files.write(unit)});},"existing nonempty list");
    auto bad=unit;bad["lists"].push_back(bad["lists"][0]);
    reject_unit(bad,"Duplicate initialized list");
    bad=unit;bad["lists"][0]["name"]="other::font";
    reject_unit(bad,"Invalid qualified list");
    bad=unit;bad["lists"][0]["items"]=Json::array({7});
    reject_unit(bad,"must be strings");
    bad=unit;bad["lists"][0]["items"]=Json::array({"before\bafter"});
    reject_unit(bad,"U+0008");
    bad=unit;bad["lists"][0]["readonly"]="true";
    reject_unit(bad,"Invalid initialized list");
    bad=pack();auto full=Json::array();for(unsigned i=0;i<200000;++i)full.push_back("");
    bad["lists"].push_back(list("large",full));
    scratch::Project limit;
    scratch::link_resources(limit,{files.write(bad)});
    require(limit.lists["demo::large"].size()==200000,"Maximum valid list capacity rejected");
    bad["lists"][0]["items"].push_back("");
    reject_unit(bad,"exceeds 200000");

    bad=unit;bad["events"][0]["queue"]="demo::font";
    reject_unit(bad,"readonly event queue");
    bad=unit;bad["events"].push_back(wheel());
    reject_unit(bad,"Duplicate or readonly event queue");
    for(const int capacity:{0,200001}) {
        bad=unit;bad["events"][0]["capacity"]=capacity;
        reject_unit(bad,"Invalid event queue capacity");
    }
    bad=unit;bad["events"][0]["capacity"]=true;
    reject_unit(bad,"Invalid input event");
    bad=unit;bad["events"][0]["type"]="callback";
    reject_unit(bad,"Invalid input event");
    bad=unit;bad["events"][0]["enabled"]="bad\nvariable";
    reject_unit(bad,"Invalid event enable variable");
    // Previous prepared costume packs need not contain the newly added fields.
    auto legacy=pack();legacy.erase("lists");legacy.erase("events");
    scratch::Project target;scratch::link_resources(target,{files.write(legacy)});
}
}

int main() {
    try {
        const std::pair<const char*,const char*> vectors[]={
            {"","d41d8cd98f00b204e9800998ecf8427e"},
            {"a","0cc175b9c0f1b6a831c399e269772661"},
            {"abc","900150983cd24fb0d6963f7d28e17f72"},
            {"message digest","f96b697d7cb7938d525a2f31aaf161d0"},
            {"abcdefghijklmnopqrstuvwxyz","c3fcd3d76192e4007dfb496cca67e13b"},
            {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789","d174ab98d277d9f5a5611c2c9f419d9f"},
            {"12345678901234567890123456789012345678901234567890123456789012345678901234567890","57edf4a22be3c955ac49da2e2107b67a"}};
        for(const auto& vector:vectors)
            if(scratch::resource_md5(vector.first)!=vector.second)throw scratch::Error("Scratch resource MD5 differs from RFC 1321 test vector");
        test_resource_lists();
        std::cout<<"Resource identifiers: 7 RFC 1321 vectors; initialized lists, wheel metadata, "
                   "SB3 roundtrip, capacity and rejection checks passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
