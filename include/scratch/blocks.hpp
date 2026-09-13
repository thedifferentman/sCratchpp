#pragma once
#include "common.hpp"
#include <map>

namespace scratch {
Expr expr(const std::string& opcode, const Json& inputs = Json::object(), const Json& fields = Json::object());
Stmt stmt(const std::string& opcode, const Json& inputs = Json::object(), const Json& fields = Json::object());
Expr var(const std::string& name);
Expr arg(const std::string& name);
Expr add(Expr a, Expr b); Expr sub(Expr a, Expr b); Expr mul(Expr a, Expr b);
Expr div(Expr a, Expr b); Expr mod(Expr a, Expr b); Expr eq(Expr a, Expr b);
Expr lt(Expr a, Expr b); Expr gt(Expr a, Expr b); Expr land(Expr a, Expr b); Expr lor(Expr a, Expr b);
Expr lnot(Expr a); Expr floor_(Expr a);
Expr item(const std::string& list, Expr index);
Stmt set(const std::string& name, Expr value);
Stmt replace(const std::string& list, Expr index, Expr value);
Stmt append(const std::string& list, Expr value);
Stmt clear(const std::string& list);
Stmt iff(Expr condition, Script yes, Script no = {});
Stmt repeat(Expr count, Script body);
Stmt until(Expr condition, Script body);
Stmt call(const std::string& name, const std::vector<Expr>& args);
void extend(Script& dst, const Script& src);

class Project {
public:
    Json variables = Json::object();
    Json lists = Json::object();
    Json extensions = Json::array();
    // Extra SB3 members, e.g. licenses/SoftFloat.txt. Names use forward slashes;
    // values are UTF-8 text. These files are not runtime lists or project metadata.
    Json files = Json::object();
    // Sidecar-only information. Never serialized as Scratch blocks or variables.
    Json debug_map = Json::object();
    void procedure(const std::string& name, const std::vector<std::string>& params, Script body);
    void green_flag(Script body);
    Json build();
    void save(const std::string& path);
private:
    struct Procedure { std::string name; std::vector<std::string> params; Script body; };
    std::vector<Procedure> procedures_;
    std::vector<Script> hats_;
};
}
