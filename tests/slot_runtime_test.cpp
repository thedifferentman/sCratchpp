#include "scratch/slot_memory.hpp"
#include "scratch/slot_numeric.hpp"
#include "scratch/platform.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>

using namespace scratch;
namespace {
constexpr const char* memory = "__scl_memory";
using Values = std::vector<unsigned>;
std::uint64_t mask(unsigned bits) { return bits == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << bits) - 1; }
std::int64_t signed_value(std::uint64_t n, unsigned bits) {
    return static_cast<std::int64_t>(n | ((n & (std::uint64_t(1) << (bits-1))) ? ~mask(bits) : 0));
}
Values bytes(std::uint64_t n, unsigned bits) {
    Values result((bits+7)/8, 0);
    for (unsigned i=0; i<result.size() && i<8; ++i) result[i]=unsigned((n>>(i*8))&255);
    return result;
}
std::uint64_t reference(const std::string& op, unsigned bits, std::uint64_t a, std::uint64_t b) {
    if (op=="add") return (a+b)&mask(bits);
    if (op=="sub") return (a-b)&mask(bits);
    if (op=="mul") return (a*b)&mask(bits);
    if (op=="and") return a&b;
    if (op=="or") return a|b;
    if (op=="xor") return a^b;
    if (op=="shl") return b>=bits ? 0 : (a<<b)&mask(bits);
    if (op=="lshr") return b>=bits ? 0 : a>>b;
    if (op=="ashr") return b>=bits ? 0 : static_cast<std::uint64_t>(signed_value(a,bits)>>b)&mask(bits);
    if (op=="udiv") return b ? a/b : 0;
    if (op=="urem") return b ? a%b : 0;
    const auto sa=signed_value(a,bits), sb=signed_value(b,bits);
    if (op=="sdiv" || op=="srem") {
        if (!b) return 0;
        if (sa==std::numeric_limits<std::int64_t>::min() && sb==-1) return op=="sdiv" ? a : 0;
        return static_cast<std::uint64_t>(op=="sdiv" ? sa/sb : sa%sb)&mask(bits);
    }
    if (op=="eq") return a==b;
    if (op=="ne") return a!=b;
    if (op=="ult") return a<b;
    if (op=="ule") return a<=b;
    if (op=="ugt") return a>b;
    if (op=="uge") return a>=b;
    if (op=="slt") return sa<sb;
    if (op=="sle") return sa<=sb;
    if (op=="sgt") return sa>sb;
    if (op=="sge") return sa>=sb;
    throw Error("unhandled slot-test reference operation");
}
void write(Script& body, unsigned address, const Values& value) {
    for (unsigned i=0; i<value.size(); ++i) body.push_back(replace(memory,address+i,value[i]));
}
void direct(const Stmt& operation, unsigned arguments) {
    if (operation.at("op")!="procedures_call" || operation.at("args").size()!=arguments)
        throw Error("slot operation must be one direct call with one argument per address");
    for (const auto& value:operation.at("args"))
        if (!value.is_number()) throw Error("numeric call site unexpectedly reads/packs source bytes");
}
struct Tests {
    Project project;
    Script main;
    Json cases=Json::array();
    unsigned next=0;
    Tests() {
        project.lists[memory]=std::vector<unsigned>(1024,0);
        project.lists["__test_actual"]=Json::array();
        project.lists["__test_expected"]=Json::array();
        project.variables["__test_done"]=0;
        project.variables["__scl_status"]="running";
    }
    void check(Script& body,unsigned address,const Values& expected) {
        for (unsigned i=0; i<expected.size(); ++i) {
            body.push_back(append("__test_actual",item(memory,address+i)));
            project.lists["__test_expected"].push_back(expected[i]);
        }
    }
    void add_case(const std::string& label,Script body,unsigned checks) {
        const auto name="slot test "+std::to_string(next++);
        cases.push_back(Json{{"label",label},{"checks",checks}});
        project.procedure(name,{},std::move(body));
        main.push_back(call(name,{}));
    }
};

void numeric_tests(Tests& tests,SlotNumeric& numeric) {
    for (unsigned bits:{1u,17u,64u}) {
        for (const std::string op:{"add","sub","mul","udiv","urem","sdiv","srem","and","or","xor","shl","lshr","ashr",
                                  "eq","ne","ult","ule","ugt","uge","slt","sle","sgt","sge"}) {
            const bool comparison=op=="eq" || op=="ne" || op=="ult" || op=="ule" || op=="ugt" || op=="uge" ||
                                  op=="slt" || op=="sle" || op=="sgt" || op=="sge";
            const auto a=mask(bits)-1;
            const auto b=(op=="shl" || op=="lshr" || op=="ashr") ? std::uint64_t(bits/2) : std::uint64_t(3)&mask(bits);
            std::string cached;
            for (unsigned destination:{300u,100u,200u,101u}) {
                Script body;
                write(body,100,bytes(a,bits)); write(body,200,bytes(b,bits));
                auto operation=comparison ? numeric.compare(op,bits,destination,100,200) : numeric.binary(op,bits,destination,100,200);
                direct(operation,3);
                if (!cached.empty() && cached!=operation.at("callee")) throw Error("slot signature was not reused across destinations");
                cached=operation.at("callee").get<std::string>();
                body.push_back(operation);
                const auto expected=bytes(reference(op,bits,a,b),comparison?1:bits);
                tests.check(body,destination,expected);
                tests.add_case(op+" i"+std::to_string(bits)+" destination "+std::to_string(destination),body,unsigned(expected.size()));
            }
        }
    }
    {
        Script body;
        write(body,100,{1,2,3,4,5,6,7,8,9});
        auto operation=numeric.binary("add",64,102,100,101); direct(operation,3); body.push_back(operation);
        tests.check(body,102,{3,5,7,9,11,13,15,17});
        tests.add_case("overlapping lhs, rhs and destination",body,8);
    }
    struct Boundary { const char* op; unsigned bits; Values lhs,rhs,expected; };
    for (const Boundary& test:std::vector<Boundary>{
            {"sdiv",64,{0,0,0,0,0,0,0,128},{255,255,255,255,255,255,255,255},{0,0,0,0,0,0,0,128}},
            {"srem",64,{0,0,0,0,0,0,0,128},{255,255,255,255,255,255,255,255},{0,0,0,0,0,0,0,0}},
            {"udiv",17,{255,255,1},{0,0,0},{0,0,0}},
            {"srem",17,{255,255,1},{0,0,0},{0,0,0}},
            {"add",9,{255,1},{1,0},{0,0}},
            {"sdiv",9,{255,1},{2,0},{0,0}},
            {"udiv",65,{0,0,0,0,0,0,0,0,1},{3,0,0,0,0,0,0,0,0},{85,85,85,85,85,85,85,85,0}},
            {"urem",65,{0,0,0,0,0,0,0,0,1},{3,0,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0,0}},
            {"sdiv",65,{0,0,0,0,0,0,0,0,1},{255,255,255,255,255,255,255,255,1},{0,0,0,0,0,0,0,0,1}}}) {
        Script body; write(body,100,test.lhs); write(body,200,test.rhs);
        const auto operation=numeric.binary(test.op,test.bits,101,100,200); direct(operation,3); body.push_back(operation);
        tests.check(body,101,test.expected);
        tests.add_case(std::string(test.op)+" boundary i"+std::to_string(test.bits),body,unsigned(test.expected.size()));
    }
    struct Cast { const char* op; unsigned from,to; Values input,expected; };
    Values all257(33,255); all257.back()=1;
    Values zext257(33,0); zext257[0]=255; zext257[1]=255; zext257[2]=1;
    for (const Cast& cast:std::vector<Cast>{
            {"sext",1,257,{1},all257}, {"zext",17,257,{255,255,1},zext257},
            {"trunc",257,17,all257,{255,255,1}}, {"sext",17,64,{0,0,1},{0,0,255,255,255,255,255,255}},
            {"trunc",64,1,bytes(0xffffffffffffffffULL,64),{1}},
            {"ptrtoint",64,17,bytes(0x123456789ULL,64),{137,103,1}},
            {"inttoptr",17,64,{255,255,1},{255,255,1,0,0,0,0,0}}}) {
        for (unsigned destination:{99u,100u,101u}) {
            Script body; write(body,100,cast.input);
            auto operation=numeric.cast(cast.op,cast.from,cast.to,destination,100); direct(operation,2); body.push_back(operation);
            tests.check(body,destination,cast.expected);
            tests.add_case(std::string(cast.op)+" "+std::to_string(cast.from)+" to "+std::to_string(cast.to)+" overlap "+std::to_string(destination),body,unsigned(cast.expected.size()));
        }
    }
    struct Intrinsic { const char* name; unsigned bits; std::vector<Values> inputs; Values expected; };
    for (const Intrinsic& intrinsic:std::vector<Intrinsic>{
            {"llvm.uadd.with.overflow.i17",17,{{255,255,1},{1,0,0}},{0,0,0,1}},
            {"llvm.ssub.with.overflow.i17",17,{{0,0,1},{1,0,0}},{255,255,0,1}},
            {"llvm.sadd.sat.i17",17,{{255,255,0},{1,0,0}},{255,255,0}},
            {"llvm.usub.sat.i17",17,{{0,0,0},{1,0,0}},{0,0,0}},
            {"llvm.fshl.i17",17,{{255,255,1},{0,0,0},{1,0,0}},{254,255,1}},
            {"llvm.fshr.i17",17,{{255,255,1},{0,0,0},{1,0,0}},{0,0,1}},
            {"llvm.ctlz.i17",17,{{0,1,0},{0}},{8,0,0}},
            {"llvm.cttz.i17",17,{{0,1,0},{0}},{8,0,0}},
            {"llvm.ctpop.i64",64,{{255,255,255,255,255,255,255,255}},{64,0,0,0,0,0,0,0}},
            {"llvm.bitreverse.i17",17,{{1,0,0}},{0,0,1}},
            {"llvm.bswap.i64",64,{{1,2,3,4,5,6,7,8}},{8,7,6,5,4,3,2,1}},
            {"llvm.abs.i17",17,{{255,255,1},{0}},{1,0,0}},
            {"llvm.smin.i17",17,{{255,255,1},{1,0,0}},{255,255,1}},
            {"llvm.umax.i17",17,{{255,255,1},{1,0,0}},{255,255,1}}}) {
        Script body; std::vector<Expr> slots; std::vector<unsigned> lengths;
        for (unsigned i=0; i<intrinsic.inputs.size(); ++i) {
            const auto address=100+i*100; write(body,address,intrinsic.inputs[i]);
            slots.push_back(address); lengths.push_back(unsigned(intrinsic.inputs[i].size()));
        }
        const auto operation=numeric.intrinsic(intrinsic.name,intrinsic.bits,101,slots,lengths);
        direct(operation,unsigned(slots.size()+1)); body.push_back(operation);
        tests.check(body,101,intrinsic.expected);
        tests.add_case(std::string(intrinsic.name)+" overlapping destination",body,unsigned(intrinsic.expected.size()));
    }
    // A subsequent invocation must not mutate the previous destination slot.
    Script body; write(body,100,bytes(0xffffffffULL,64)); write(body,200,bytes(1,64));
    body.push_back(numeric.binary("add",64,300,100,200));
    body.push_back(numeric.binary("add",64,400,200,200));
    tests.check(body,300,bytes(0x100000000ULL,64)); tests.check(body,400,bytes(2,64));
    tests.add_case("persistent destinations across repeated helper",body,16);
}

// Deliberately bit-at-a-time: the runtime implementation uses byte displacement
// and residual arithmetic, so this oracle does not duplicate its algorithm.
Values reference_constant_shift(const std::string& op,unsigned bits,unsigned amount,const Values& source) {
    Values result((bits+7)/8,0);
    if (amount>=bits) return result; // The existing deterministic poison fallback.
    const auto bit=[&](unsigned index) { return (source[index/8]>>(index%8))&1u; };
    for (unsigned output=0; output<bits; ++output) {
        unsigned value=0;
        if (op=="shl") { if (output>=amount) value=bit(output-amount); }
        else if (output+amount<bits) value=bit(output+amount);
        else if (op=="ashr") value=bit(bits-1);
        if (value) result[output/8]|=1u<<(output%8);
    }
    return result;
}

void constant_shift_tests(Tests& tests,SlotNumeric& numeric) {
    for (unsigned bits:{1u,9u,17u,32u,64u,65u,257u}) {
        Values source((bits+7)/8);
        for (unsigned i=0;i<source.size();++i) source[i]=((i*73+19)^0xa5u)&255u;
        const unsigned topbits=(bits-1)%8+1;
        source.back()=(source.back()&((1u<<topbits)-1))|(1u<<(topbits-1));
        const std::set<unsigned> amounts{0u,1u,7u,8u,bits-1,bits,bits+1};
        for (const std::string op:{"shl","lshr","ashr"}) for (unsigned amount:amounts) {
            Script body; std::string cached;
            const auto expected=reference_constant_shift(op,bits,amount,source);
            for (unsigned destination:{99u,100u,101u}) {
                write(body,100,source);
                auto operation=numeric.shift_constant(op,bits,amount,destination,100); direct(operation,2);
                if (!cached.empty() && cached!=operation.at("callee")) throw Error("constant-shift signature did not reuse its procedure");
                cached=operation.at("callee").get<std::string>();
                body.push_back(operation); tests.check(body,destination,expected);
            }
            tests.add_case(op+" constant "+std::to_string(amount)+" i"+std::to_string(bits)+" three overlapping destinations",
                           body,unsigned(expected.size()*3));
            if (op=="ashr" && (amount==1 || amount==bits-1)) {
                Values positive=source; positive.back()&=~(1u<<(topbits-1));
                const auto positive_expected=reference_constant_shift(op,bits,amount,positive);
                Script positive_body;
                for (unsigned destination:{99u,100u,101u}) {
                    write(positive_body,100,positive);
                    auto operation=numeric.shift_constant(op,bits,amount,destination,100); direct(operation,2);
                    positive_body.push_back(operation); tests.check(positive_body,destination,positive_expected);
                }
                tests.add_case("positive ashr constant "+std::to_string(amount)+" i"+std::to_string(bits)+" three overlapping destinations",
                               positive_body,unsigned(positive_expected.size()*3));
            }
        }
    }
}

void memory_tests(Tests& tests,SlotMemory& runtime) {
    for (unsigned count:{1u,2u,4u,8u,13u}) {
        Values source(count); for (unsigned i=0;i<count;++i) source[i]=i+1;
        for (unsigned destination:{99u,100u,101u}) {
            Script body; write(body,100,source); const auto operation=runtime.copy(destination,100,count); direct(operation,count==13?3:2);
            body.push_back(operation); tests.check(body,destination,source);
            tests.add_case("copy "+std::to_string(count)+" bytes overlap "+std::to_string(destination),body,count);
        }
    }
    {
        Script body; write(body,99,{7,7,7,7,7,7,7});
        body.push_back(runtime.fill(100,511,Expr(5))); tests.check(body,99,{7,255,255,255,255,255,7});
        tests.add_case("dynamic fill normalizes byte and preserves guards",body,7);
    }
    {
        Script body; write(body,400,bytes(600,64)); write(body,600,{255,255,255,255});
        body.push_back(runtime.load(401,400,3,17)); tests.check(body,401,{255,255,1});
        tests.add_case("i17 load destination overlaps encoded pointer",body,3);
    }
    {
        Script body; write(body,400,bytes(600,64)); write(body,602,{1,2,3,4,5,6,7,8});
        body.push_back(runtime.store(400,602,8)); tests.check(body,600,{1,2,3,4,5,6,7,8});
        tests.add_case("store guest destination overlaps source",body,8);
    }
    {
        Script body; write(body,400,bytes(600,64)); write(body,200,{255,255,255}); write(body,600,{7,7,7,7});
        body.push_back(runtime.store(400,200,3,17)); tests.check(body,600,{255,255,1,7});
        tests.add_case("i17 store masks final byte and preserves next byte",body,4);
    }
    {
        Script body; write(body,400,bytes(199,64)); write(body,199,{7,255,255});
        body.push_back(runtime.store(400,200,2,9)); tests.check(body,199,{255,1,255});
        tests.add_case("i9 store snapshots top byte before overlapping destination writes",body,3);
    }
    {
        Script body; write(body,400,bytes(600,64)); write(body,500,bytes(700,64));
        const auto first=runtime.decode(400,body); const auto second=runtime.decode(500,body);
        body.push_back(append("__test_actual",first)); tests.project.lists["__test_expected"].push_back(600);
        body.push_back(append("__test_actual",second)); tests.project.lists["__test_expected"].push_back(700);
        tests.add_case("decoded addresses survive later decode",body,2);
    }
    {
        Script body; write(body,400,bytes(602,64)); write(body,500,bytes(600,64)); write(body,800,bytes(6,64));
        write(body,600,{1,2,3,4,5,6,7,8});
        body.push_back(runtime.memmove_slot(400,500,800,8)); tests.check(body,600,{1,2,1,2,3,4,5,6});
        body.push_back(runtime.memmove_slot(500,400,800,8)); tests.check(body,600,{1,2,3,4,5,6,5,6});
        tests.add_case("guest memmove slot count both overlap directions",body,16);
    }
    {
        Script body; write(body,400,bytes(~std::uint64_t(0),64)); write(body,500,bytes(0x100000000ULL,64)); write(body,800,bytes(0,64));
        body.push_back(runtime.memcpy_slot(400,500,800,8)); body.push_back(runtime.memmove_slot(500,400,800,8));
        body.push_back(runtime.memset_slot(400,255,800,8)); body.push_back(runtime.check_access(0,0));
        tests.check(body,800,bytes(0,64)); tests.add_case("zero count skips both invalid pointers and access checks",body,8);
    }
}

void trap_project(const std::filesystem::path& directory,const std::string& name,unsigned pointer,unsigned length,bool high_byte) {
    Project project; project.lists[memory]=std::vector<unsigned>(1024,0);
    project.variables["__scl_status"]="running"; project.variables["__test_done"]=0;
    SlotMemory runtime(project,8,32,1024); Script body;
    auto encoded=bytes(pointer,64); if (high_byte) encoded[7]=1;
    write(body,100,encoded); body.push_back(runtime.load(200,100,length)); body.push_back(set("__test_done",1));
    project.procedure("trap case",{},body); project.green_flag({call("trap case",{})});
    project.save((directory/name).u8string());
}
}

int main(int argc,char** argv) {
    try {
        const auto args=platform::arguments(argc,argv);
        if (args.size()!=2) throw Error("usage: slot-runtime-test output-directory");
        const auto directory=std::filesystem::u8path(args[1]); std::filesystem::create_directories(directory);
        Tests tests; SlotMemory memory_runtime(tests.project,8,1,1024); SlotNumeric numeric(tests.project);
        numeric_tests(tests,numeric); constant_shift_tests(tests,numeric); memory_tests(tests,memory_runtime); numeric.install(tests.project);
        tests.main.push_back(set("__test_done",1)); tests.main.push_back(set("__scl_status","done"));
        tests.project.procedure("slot runtime tests",{},tests.main); tests.project.green_flag({call("slot runtime tests",{})});
        tests.project.files["slot-runtime-expected.json"]=Json{{"cases",tests.cases},{"bytes",tests.project.lists["__test_expected"].size()}}.dump(2);
        const auto built=tests.project.build();
        for (const auto& target:built.at("targets")) for (const auto& block:target.at("blocks"))
            if (block.at("opcode")=="procedures_prototype" && block.at("mutation").at("warp")!="true")
                throw Error("slot runtime procedure is not warp");
        tests.project.save((directory/"slot-runtime.sb3").u8string());
        trap_project(directory,"slot-high-pointer.sb3",600,1,true);
        trap_project(directory,"slot-range-end.sb3",1023,4,false);
        trap_project(directory,"slot-code-address.sb3",31,1,false);
        std::cout<<"PASS direct slot calls, helper reuse and warp structure; generated "<<tests.cases.size()
                 <<" cases / "<<tests.project.lists["__test_expected"].size()<<" byte checks and three trap projects\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
