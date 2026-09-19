#include "scratch/blocks.hpp"
#include "scratch/platform.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>

using namespace scratch;
static void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
static void expect_error(Project& project, const char* message) {
    try { (void)project.build(); } catch (const Error&) { return; }
    throw Error(message);
}
static std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot read generated Unicode output path");
    return std::string(std::istreambuf_iterator<char>(input), {});
}
static std::map<std::string, std::string> zip_members(const std::string& zip) {
    const auto u16 = [&](std::size_t pos) {
        require(pos + 2 <= zip.size(), "Truncated ZIP integer");
        return static_cast<unsigned char>(zip[pos]) | (static_cast<unsigned char>(zip[pos + 1]) << 8);
    };
    const auto u32 = [&](std::size_t pos) {
        return static_cast<std::uint32_t>(u16(pos)) | (static_cast<std::uint32_t>(u16(pos + 2)) << 16);
    };
    std::map<std::string, std::string> result;
    std::size_t position = 0;
    while (u32(position) == 0x04034b50) {
        require(u16(position + 8) == 0, "Expected uncompressed ZIP entries");
        const auto size = u32(position + 18);
        const auto name_length = u16(position + 26);
        const auto extra_length = u16(position + 28);
        const auto payload = position + 30 + name_length + extra_length;
        require(payload + size <= zip.size(), "Truncated ZIP entry");
        result.emplace(zip.substr(position + 30, name_length), zip.substr(payload, size));
        position = payload + size;
    }
    require(u32(position) == 0x02014b50, "ZIP has no central directory");
    return result;
}
int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        Project project;
        project.variables["result"] = 0;
        project.lists["flags"] = Json::array({1, 0});
        project.procedure("accumulate", {"amount"}, {set("result", add(var("result"), arg("amount")))});
        project.procedure("main", {}, {
            set("result", 0), clear("output"),
            call("accumulate", {20}),
            // A list reporter must work directly in a boolean input socket.
            iff(item("flags", 1), {call("accumulate", {22})}, {set("result", -1)}),
            repeat(2, {append("output", var("result"))}),
            until(eq(var("result"), 44), {set("result", add(var("result"), 1))}),
            set("__status", "done")});
        project.green_flag({call("main", {})});
        const auto first = project.build();
        require(first == project.build(), "Build must be deterministic");
        const auto config_text=first["targets"][0]["comments"]["scrpp_tw_settings"]["text"].get<std::string>();
        require(config_text.find("\"framerate\":60")!=std::string::npos &&
                config_text.find("\"hq\":true")!=std::string::npos &&
                config_text.find("\"fencing\":false")!=std::string::npos &&
                config_text.find(" // _twconfig_")!=std::string::npos,"Missing default TW settings");
        Project custom_settings;
        custom_settings.turbowarp_settings={{"framerate",72.5},{"unlimited_clones",true},{"high_quality_pen",false}};
        const auto custom_text=custom_settings.build()["targets"][0]["comments"]["scrpp_tw_settings"]["text"].get<std::string>();
        require(custom_text.find("\"framerate\":72.5")!=std::string::npos &&
                custom_text.find("\"maxClones\":Infinity")!=std::string::npos,"TW overrides lost");
        for (const auto& invalid : std::vector<Json>{Json{{"framerate",true}},Json{{"framerate",251}},
                Json{{"high_quality_pen",1}},Json{{"stage_width",0}},Json{{"disable_compiler",true}}}) {
            Project bad;bad.turbowarp_settings=invalid;
            bool rejected=false;try{bad.build();}catch(const Error&){rejected=true;}
            require(rejected,"Invalid TW setting accepted");
        }
        require(first["targets"].size() == 2, "Exactly a stage and execution sprite are required");
        const auto& sprite = first["targets"][1];
        require(sprite["variables"].size() == 2, "Implicitly referenced variables were not collected");
        require(sprite["lists"].size() == 2, "Implicitly referenced lists were not collected");
        std::set<std::string> declared;
        for (const auto& v : sprite["variables"].items()) declared.insert(v.key());
        for (const auto& v : sprite["lists"].items()) declared.insert(v.key());
        unsigned definitions = 0, hats = 0, calls = 0;
        for (const auto& item : sprite["blocks"].items()) {
            const auto& block = item.value();
            if (block["opcode"] == "procedures_definition") ++definitions;
            if (block["opcode"] == "event_whenflagclicked") ++hats;
            if (block["opcode"] == "procedures_call") ++calls;
            if (block["opcode"] == "control_if" || block["opcode"] == "control_if_else") {
                const auto& condition=block.at("inputs").at("CONDITION");
                require(condition.at(0)==2 && condition.at(1).is_string(), "Boolean socket must contain a Boolean block");
                const auto child=sprite.at("blocks").at(condition.at(1).get<std::string>()).at("opcode").get<std::string>();
                require(child=="operator_not" || child=="operator_equals" || child=="operator_lt" || child=="operator_gt" || child=="operator_and" || child=="operator_or", "Numeric flags need explicit predicates for Blockly compatibility");
            }
            if (block.contains("mutation")) {
                require(block["mutation"]["warp"] == "true", "All procedures must run in warp mode");
                const auto ids = Json::parse(block["mutation"]["argumentids"].get<std::string>());
                for (const auto& id : ids) require(block["inputs"].contains(id.get<std::string>()), "Missing procedure argument input");
            }
            for (const auto& field : block["fields"].items())
                if (field.key() == "VARIABLE" || field.key() == "LIST")
                    require(declared.count(field.value()[1].get<std::string>()) != 0, "Undeclared symbol ID");
        }
        require(definitions == 2 && hats == 1 && calls == 3, "Unexpected procedure or hat count");
        Project pen;
        pen.green_flag({stmt("pen_clear")});
        require(pen.build()["extensions"] == Json::array({"pen"}), "Pen extension must be inferred");
        Project immutable;
        immutable.lists["font::data"] = Json::array({"glyph"});
        immutable.readonly_lists.insert("font::data");
        immutable.green_flag({set("glyph", item("font::data", 1))});
        (void)immutable.build();
        immutable.procedure("invalid", {}, {append("font::data", "new")});
        expect_error(immutable, "Writes to readonly resource lists must be rejected");
        Project events;
        events.resource_events.push_back({{"type", "wheel"}, {"queue", "events::wheel"},
            {"enabled", "events_enabled"}, {"capacity", 128}});
        events.green_flag({set("__scl_status", "running")});
        const auto event_project = events.build();
        require(event_project == events.build(), "Event lowering must be deterministic");
        require(event_project["targets"].size() == 2, "Events must not introduce another sprite");
        unsigned key_hats = 0, sensors = 0;
        for (const auto& block : event_project["targets"][1]["blocks"]) {
            if (block["opcode"] == "event_whenkeypressed") ++key_hats;
            if (block["opcode"] == "sensing_keypressed") ++sensors;
        }
        require(key_hats == 2 && sensors == 1, "Wheel hats must share the physical-key filter");
        events.resource_events.push_back({{"type", "keyboard"}, {"queue", "events::keyboard"},
            {"enabled", "events_enabled"}, {"capacity", 128}});
        const auto keyboard_project=events.build();
        std::map<std::string,unsigned> keys;
        for(const auto& block:keyboard_project["targets"][1]["blocks"])
            if(block["opcode"]=="event_whenkeypressed")++keys[block["fields"]["KEY_OPTION"][0].get<std::string>()];
        require(keys.size()==86,"Keyboard must include all supported ASCII and named keys");
        require(keys["up arrow"]==2 && keys["down arrow"]==2,"Keyboard and wheel collectors must coexist");
        require(keys["\\"]==1 && keys["\""]==1 && keys["A"]==1 && !keys.count("a"),
            "Symbols must remain literal and case-folded letters must not produce duplicate hats");
        require(keys["backspace"]==1 && keys["enter"]==1,"Named keys are missing");
        Project missing;
        missing.green_flag({call("missing", {})});
        expect_error(missing, "Undefined procedures must be rejected");
        Project arity;
        arity.procedure("f", {"x"}, {});
        arity.green_flag({call("f", {})});
        expect_error(arity, "Mismatched argument counts must be rejected");
        Project duplicate;
        duplicate.procedure("f", {}, {});
        duplicate.procedure("f", {}, {});
        expect_error(duplicate, "Duplicate procedures must be rejected");
        // Construct UTF-8 paths independent of the compiler's source codepage and
        // exercise the same UTF-8 string API as the command-line compiler.
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto unicode_directory = std::filesystem::temp_directory_path() /
            std::filesystem::u8path(std::string(u8"scratch-\u6d4b\u8bd5\u00b7-") + unique);
        std::filesystem::create_directory(unicode_directory);
        const auto unicode_sb3 = unicode_directory / std::filesystem::u8path(u8"\u7a0b\u5e8f\u00b7.sb3");
        const auto unicode_json = unicode_directory / std::filesystem::u8path(u8"\u7a0b\u5e8f\u00b7.json");
        project.files["licenses/SoftFloat.txt"] = "Test license text\nCopyright test\n";
        project.files[u8"licenses/\u8bf4\u660e.txt"] = u8"\u5b57\u8282\u8fd0\u7b97\n";
        require(project.build() == first, "Extra archive files must not alter project JSON or runtime state");
        project.save(unicode_sb3.u8string());
        project.save(unicode_json.u8string());
        const auto archive = read_file(unicode_sb3);
        const auto members = zip_members(archive);
        require(members.size() == 4, "Extra files were not added to SB3");
        require(members.at("licenses/SoftFloat.txt") == project.files["licenses/SoftFloat.txt"], "License contents changed");
        require(members.at(u8"licenses/\u8bf4\u660e.txt") == project.files[u8"licenses/\u8bf4\u660e.txt"], "UTF-8 member contents changed");
        require(Json::parse(members.at("project.json")) == first, "Packaged JSON changed");
        require(Json::parse(read_file(unicode_json)) == first, "Unicode JSON output did not round trip");
        project.save(unicode_sb3.u8string());
        require(read_file(unicode_sb3) == archive, "SB3 archive must be deterministic");
        for (const auto& invalid : {"project.json", "../outside.txt", "/absolute.txt", "C:/absolute.txt", "a//b", "a\\b"}) {
            Project invalid_files;
            invalid_files.files[invalid] = "bad";
            bool rejected = false;
            try { invalid_files.save(unicode_sb3.u8string()); } catch (const Error&) { rejected = true; }
            require(rejected, "Invalid or reserved extra archive member was accepted");
            require(read_file(unicode_sb3) == archive, "Invalid archive request truncated existing output");
        }
        std::filesystem::remove(unicode_sb3);
        std::filesystem::remove(unicode_json);
        std::filesystem::remove(unicode_directory);
        if (args.size() > 1) {
            const auto directory = std::filesystem::u8path(args[1]);
            std::filesystem::create_directories(directory);
            project.save((directory / "blocks-smoke.sb3").u8string());
            project.save((directory / "blocks-smoke.json").u8string());
        }
        std::cout << "blocks_test: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "blocks_test: " << error.what() << '\n';
        return 1;
    }
}
