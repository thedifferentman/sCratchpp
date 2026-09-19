#include "scratch/resources.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace scratch {
namespace {
std::string read_file(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw Error("Cannot read resource file: " + file.u8string());
    std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    if (stream.bad()) throw Error("Failed reading resource file: " + file.u8string());
    return bytes;
}
std::filesystem::path asset_path(const std::filesystem::path& manifest, const std::string& name) {
    if (name.empty() || name.find('\\') != std::string::npos || name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        throw Error("Invalid resource-unit asset path: " + name);
    const auto relative=std::filesystem::u8path(name);
    if (relative.is_absolute()) throw Error("Resource-unit asset paths must be relative: " + name);
    for(const auto& part:relative)if(part=="..")throw Error("Resource-unit asset path escapes its package: " + name);
    const auto base=std::filesystem::weakly_canonical(manifest.parent_path());
    const auto resolved=std::filesystem::weakly_canonical(base/relative);
    auto candidate=resolved.begin();
    for(auto part=base.begin();part!=base.end();++part,++candidate)
        if(candidate==resolved.end() || *part!=*candidate)throw Error("Resource-unit asset resolves outside its package: " + name);
    return resolved;
}
}

std::string resource_md5(const std::string& bytes) {
    // RFC 1321. Used as the Scratch asset identifier, never for authentication.
    static constexpr std::array<uint32_t,64> k={
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static constexpr unsigned shifts[4][4]={{7,12,17,22},{5,9,14,20},{4,11,16,23},{6,10,15,21}};
    std::string input=bytes;
    input.push_back(static_cast<char>(0x80));
    while(input.size()%64!=56)input.push_back(0);
    const uint64_t bits=static_cast<uint64_t>(bytes.size())*8;
    for(unsigned i=0;i<8;++i)input.push_back(static_cast<char>(bits>>(i*8)));
    uint32_t h[4]={0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    for(size_t at=0;at<input.size();at+=64) {
        uint32_t words[16]={};
        for(unsigned i=0;i<64;++i)words[i/4]|=uint32_t(static_cast<unsigned char>(input[at+i]))<<((i%4)*8);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
        for(unsigned i=0;i<64;++i) {
            uint32_t f,g;
            if(i<16){f=(b&c)|(~b&d);g=i;}
            else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}
            else if(i<48){f=b^c^d;g=(3*i+5)%16;}
            else{f=c^(b|~d);g=(7*i)%16;}
            const uint32_t sum=a+f+k[i]+words[g];const unsigned s=shifts[i/16][i%4];
            a=d;d=c;c=b;b+=((sum<<s)|(sum>>(32-s)));
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;
    }
    static const char hex[]="0123456789abcdef";std::string result;
    for(auto word:h)for(unsigned i=0;i<4;++i){const unsigned byte=(word>>(i*8))&255;result+=hex[byte>>4];result+=hex[byte&15];}
    return result;
}

void link_resources(Project& project, const std::vector<std::string>& manifests) {
    std::set<std::string> packages;
    std::map<std::string,std::string> names;
    std::set<std::string> list_names, event_queues;
    for(const auto& filename:manifests) {
        const auto manifest=std::filesystem::absolute(std::filesystem::u8path(filename));
        Json unit;
        try {unit=Json::parse(read_file(manifest));}
        catch(const std::exception& e){throw Error("Resource manifest '"+filename+"': "+e.what());}
        if(!unit.is_object() || unit.value("schemaVersion",0)!=1 || !unit.contains("package") || !unit["package"].is_string() ||
            !unit.contains("costumes") || !unit["costumes"].is_array())throw Error("Invalid prepared resource manifest: "+filename);
        const std::string package=unit["package"];
        if(package.empty() || package.find("::")!=std::string::npos || package.find('\0')!=std::string::npos)
            throw Error("Invalid resource package name: "+package);
        if(!packages.insert(package).second)throw Error("Duplicate resource package: "+package);
        const auto qualified = [&](const std::string& name, const char* kind) {
            const auto prefix=package+"::";
            if(name.size()<=prefix.size() || name.compare(0,prefix.size(),prefix)!=0 ||
                name.find("::",prefix.size())!=std::string::npos ||
                std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))
                throw Error(std::string("Invalid qualified ")+kind+" name: "+name);
        };
        if(unit.contains("lists") && !unit["lists"].is_array())throw Error("Resource lists must be an array: "+package);
        for(const auto& list:unit.value("lists",Json::array())) {
            if(!list.is_object() || !list.contains("name") || !list["name"].is_string() ||
                !list.contains("items") || !list["items"].is_array() ||
                (list.contains("readonly") && !list["readonly"].is_boolean()))
                throw Error("Invalid initialized list in resource package: "+package);
            const std::string name=list["name"];
            qualified(name,"list");
            if(!list_names.insert(name).second)throw Error("Duplicate initialized list: "+name);
            const auto& items=list["items"];
            if(items.size()>200000)throw Error("Resource list exceeds 200000 items: "+name);
            for(const auto& item:items)
                if(!item.is_string() || item.get_ref<const std::string&>().find('\b')!=std::string::npos)
                    throw Error("Resource list items must be strings without U+0008: "+name);
            if(project.lists.contains(name) && !project.lists[name].empty())
                throw Error("Resource list conflicts with an existing nonempty list: "+name);
            project.lists[name]=items;
            if(list.value("readonly",false))project.readonly_lists.insert(name);
        }
        if(unit.contains("events") && !unit["events"].is_array())throw Error("Resource events must be an array: "+package);
        for(const auto& event:unit.value("events",Json::array())) {
            if(!event.is_object() || (event.value("type","")!="wheel" && event.value("type","")!="keyboard") ||
                !event.contains("queue") || !event["queue"].is_string() ||
                !event.contains("enabled") || !event["enabled"].is_string() ||
                !event.contains("capacity") || !event["capacity"].is_number_integer())
                throw Error("Invalid input event in resource package: "+package);
            const std::string queue=event["queue"], enabled=event["enabled"];
            qualified(queue,"event queue");
            if(enabled.empty() || std::any_of(enabled.begin(),enabled.end(),[](unsigned char c){return c<32;}))
                throw Error("Invalid event enable variable: "+enabled);
            const auto capacity=event["capacity"].get<int64_t>();
            if(capacity<1 || capacity>200000)throw Error("Invalid event queue capacity: "+queue);
            if(!event_queues.insert(queue).second || project.readonly_lists.count(queue))
                throw Error("Duplicate or readonly event queue: "+queue);
            if(!project.lists.contains(queue))project.lists[queue]=Json::array();
            if(!project.variables.contains(enabled))project.variables[enabled]=0;
            project.resource_events.push_back(event);
        }
        for(const auto& costume:unit["costumes"]) {
            if(!costume.is_object())throw Error("Invalid costume in resource package: "+package);
            const std::string name=costume.value("name","");
            const auto prefix=package+"::";
            if(name.size()<=prefix.size() || name.compare(0,prefix.size(),prefix)!=0 ||
                name.find("::",prefix.size())!=std::string::npos || name.find('\0')!=std::string::npos)
                throw Error("Invalid qualified costume name: "+name);
            if(!names.emplace(name,filename).second)throw Error("Duplicate costume: "+name);
            if(costume.value("dataFormat","")!="svg" || costume.value("bitmapResolution",0)!=1)
                throw Error("Prepared costumes must use SVG at resolution 1: "+name);
            for(const auto* field:{"rotationCenterX","rotationCenterY"})
                if(!costume.contains(field) || !costume[field].is_number() || !std::isfinite(costume[field].get<double>()))
                    throw Error("Invalid rotation center for costume: "+name);
            const auto bytes=read_file(asset_path(manifest,costume.value("path","")));
            const auto id=resource_md5(bytes), asset=id+".svg";
            if(costume.value("assetId","")!=id || costume.value("md5ext","")!=asset)
                throw Error("Resource content hash mismatch: "+name+"; rebuild its resource unit");
            const auto found=project.assets.find(asset);
            if(found!=project.assets.end() && found->second!=bytes)throw Error("Resource hash collision: "+name);
            project.assets.emplace(asset,bytes);
            project.costumes.push_back({{"name",name},{"assetId",id},{"md5ext",asset},{"dataFormat","svg"},
                {"bitmapResolution",1},{"rotationCenterX",costume["rotationCenterX"]},{"rotationCenterY",costume["rotationCenterY"]}});
        }
    }
}
}
