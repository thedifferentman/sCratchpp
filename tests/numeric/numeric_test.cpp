#include "scratch/numeric.hpp"
#include "scratch/platform.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include <random>

using namespace scratch;
namespace {
// An intentionally small, independent interpreter of actual Scratch opcodes.
// Generated code is exercised with floating-point arithmetic and Scratch's
// nonnegative modulo, not a second copy of the implementation's algorithms.
struct VM {
    std::map<std::string, double> vars;
    Json lists;
    std::uint64_t steps = 0;
    double eval(const Json& e) {
        if (e.is_number()) return e.get<double>();
        if (e.is_boolean()) return e.get<bool>();
        const auto op = e.at("op").get<std::string>();
        const auto& in = e.at("inputs");
        auto input = [&](const char* key) { return eval(in.at(key)); };
        if (op == "data_variable") return vars[e.at("fields").at("VARIABLE").at(0).get<std::string>()];
        if (op == "data_itemoflist") {
            auto index = input("INDEX");
            const auto& a = lists.at(e.at("fields").at("LIST").at(0).get<std::string>());
            if (index < 1 || index > a.size() || std::floor(index) != index) throw Error("invalid table index");
            return a.at(static_cast<std::size_t>(index-1)).get<double>();
        }
        if (op == "operator_add") return input("NUM1") + input("NUM2");
        if (op == "operator_subtract") return input("NUM1") - input("NUM2");
        if (op == "operator_multiply") return input("NUM1") * input("NUM2");
        if (op == "operator_divide") return input("NUM1") / input("NUM2");
        if (op == "operator_mod") {
            double a=input("NUM1"), b=input("NUM2"); return a-std::floor(a/b)*b;
        }
        if (op == "operator_equals") return input("OPERAND1") == input("OPERAND2");
        if (op == "operator_lt") return input("OPERAND1") < input("OPERAND2");
        if (op == "operator_gt") return input("OPERAND1") > input("OPERAND2");
        if (op == "operator_and") return input("OPERAND1") && input("OPERAND2");
        if (op == "operator_or") return input("OPERAND1") || input("OPERAND2");
        if (op == "operator_not") return !input("OPERAND");
        if (op == "operator_mathop" && e.at("fields").at("OPERATOR").at(0) == "floor") return std::floor(input("NUM"));
        throw Error("unimplemented interpreter expression: " + op);
    }
    void run(const Json& script) {
        for (const auto& s : script) {
            if (++steps > 50000000) throw Error("numeric operation exceeded finite execution budget");
            auto op=s.at("op").get<std::string>();
            if (op == "data_setvariableto") vars[s.at("fields").at("VARIABLE").at(0).get<std::string>()] = eval(s.at("inputs").at("VALUE"));
            else if (op == "control_if" || op == "control_if_else") {
                if (eval(s.at("inputs").at("CONDITION"))) run(s.at("branches").at("SUBSTACK"));
                else if (op == "control_if_else") run(s.at("branches").at("SUBSTACK2"));
            } else if (op == "control_repeat") {
                auto count=std::llround(eval(s.at("inputs").at("TIMES")));
                if (count < 0 || count > 100000) throw Error("invalid repeat bound");
                for (long long i=0;i<count;++i) run(s.at("branches").at("SUBSTACK"));
            } else throw Error("unimplemented interpreter statement: " + op);
        }
    }
    void input(const std::string& name, unsigned bits, std::uint64_t value) {
        for (unsigned i=0;i<(bits+7)/8;++i) vars[name+std::to_string(i)] = (value >> (8*i)) & 255;
    }
    std::uint64_t output(const Bytes& bytes) {
        if (bytes.size()>8) throw Error("test uint64 output too wide");
        std::uint64_t value=0;
        for (unsigned i=0;i<bytes.size();++i) {
            auto v=eval(bytes[i]);
            if (v<0 || v>255 || std::floor(v)!=v) throw Error("non-byte numeric result");
            value |= static_cast<std::uint64_t>(v) << (i*8);
        }
        return value;
    }
};
Bytes inputs(const std::string& name, unsigned bits) {
    Bytes a; for(unsigned i=0;i<(bits+7)/8;++i) a.push_back(var(name+std::to_string(i))); return a;
}
std::uint64_t mask(unsigned bits) { return bits==64 ? ~std::uint64_t(0) : (std::uint64_t(1)<<bits)-1; }
std::int64_t signed_value(std::uint64_t a,unsigned bits) {
    return static_cast<std::int64_t>(a | ((a & (std::uint64_t(1)<<(bits-1))) ? ~mask(bits) : 0));
}
std::uint64_t reference(const std::string& op,unsigned bits,std::uint64_t a,std::uint64_t b) {
    if(op=="add") return (a+b)&mask(bits);
    if(op=="sub") return (a-b)&mask(bits);
    if(op=="mul") return (a*b)&mask(bits);
    if(op=="and") return a&b;
    if(op=="or") return a|b;
    if(op=="xor") return a^b;
    if(op=="shl") return b>=bits ? 0 : (a<<b)&mask(bits);
    if(op=="lshr") return b>=bits ? 0 : a>>b;
    if(op=="ashr") return b>=bits ? 0 : static_cast<std::uint64_t>(signed_value(a,bits)>>b)&mask(bits);
    if(op=="udiv") return b ? a/b : 0;
    if(op=="urem") return b ? a%b : 0;
    auto sa=signed_value(a,bits), sb=signed_value(b,bits);
    if(op=="sdiv" || op=="srem") {
        if(!b) return 0;
        if(sa==std::numeric_limits<std::int64_t>::min() && sb==-1) return op=="sdiv" ? a : 0;
        return static_cast<std::uint64_t>(op=="sdiv" ? sa/sb : sa%sb)&mask(bits);
    }
    if(op=="eq") return a==b;
    if(op=="ne") return a!=b;
    if(op=="ult") return a<b;
    if(op=="ule") return a<=b;
    if(op=="ugt") return a>b;
    if(op=="uge") return a>=b;
    if(op=="slt") return sa<sb;
    if(op=="sle") return sa<=sb;
    if(op=="sgt") return sa>sb;
    if(op=="sge") return sa>=sb;
    throw Error("unknown reference opcode");
}
void expect(std::uint64_t actual,std::uint64_t expected,const std::string& context) {
    if(actual!=expected) throw Error(context+": actual="+std::to_string(actual)+" expected="+std::to_string(expected));
}
}

int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        Numeric numeric;
        const std::vector<unsigned> widths{1,2,7,8,9,16,17,31,32,33,63,64};
        const std::vector<std::string> binary_ops{"add","sub","mul","and","or","xor","shl","lshr","ashr","udiv","urem","sdiv","srem"};
        const std::vector<std::string> predicates{"eq","ne","ult","ule","ugt","uge","slt","sle","sgt","sge"};
        struct Case { std::string op; unsigned bits; NumericResult result; };
        std::vector<Case> cases;
        for(auto bits:widths) {
            for(auto& op:binary_ops) cases.push_back({op,bits,numeric.binary(op,bits,inputs("a",bits),inputs("b",bits))});
            for(auto& op:predicates) cases.push_back({op,bits,numeric.compare(op,bits,inputs("a",bits),inputs("b",bits))});
        }
        std::vector<Case> intrinsic_cases;
        for(auto bits:widths) for(const std::string op:{"ctpop","ctlz","cttz","bitreverse","abs","smin","smax","umin","umax"})
            intrinsic_cases.push_back({op,bits,numeric.intrinsic("llvm."+op+".i"+std::to_string(bits),bits,{inputs("a",bits),inputs("b",bits)})});
        for(auto bits:widths) if(bits%16==0)
            intrinsic_cases.push_back({"bswap",bits,numeric.intrinsic("llvm.bswap.i"+std::to_string(bits),bits,{inputs("a",bits)})});
        for(auto bits:widths) for(const std::string op:{"fshl","fshr"})
            intrinsic_cases.push_back({op,bits,numeric.intrinsic("llvm."+op+".i"+std::to_string(bits),bits,{inputs("a",bits),inputs("b",bits),inputs("k",bits)})});
        Project project; numeric.install(project);
        VM vm; vm.lists=project.lists;
        std::mt19937_64 random(0x5c12a7c);
        unsigned count=0;
        for(const auto& c:cases) {
            Json script=c.result.code;
            auto max=mask(c.bits), sign=std::uint64_t(1)<<(c.bits-1);
            std::vector<std::uint64_t> special{0,1,max,sign,sign-1,max>>1};
            for(unsigned i=0;i<72;++i) {
                auto a=i<36 ? special[i/6] : random()&max;
                auto b=i<36 ? special[i%6] : random()&max;
                if((c.op=="shl" || c.op=="lshr" || c.op=="ashr") && i>=36 && i<64) b=(i-36)%c.bits;
                vm.input("a",c.bits,a); vm.input("b",c.bits,b); vm.steps=0; vm.run(script);
                expect(vm.output(c.result.bytes),reference(c.op,c.bits,a,b),c.op+" i"+std::to_string(c.bits)+" "+std::to_string(a)+","+std::to_string(b));
                ++count;
            }
        }
        for(const auto& c:intrinsic_cases) {
            Json script=c.result.code;
            for(unsigned i=0;i<32;++i) {
                auto a=(i==0 ? 0 : i==1 ? mask(c.bits) : random()&mask(c.bits)), b=random()&mask(c.bits);
                auto k=random()&mask(c.bits);
                vm.input("a",c.bits,a); vm.input("b",c.bits,b); vm.input("k",c.bits,k); vm.steps=0; vm.run(script);
                std::uint64_t expected=0;
                if(c.op=="ctpop") for(unsigned bit=0;bit<c.bits;++bit) expected+=(a>>bit)&1;
                if(c.op=="ctlz") { while(expected<c.bits && !((a>>(c.bits-expected-1))&1)) ++expected; }
                if(c.op=="cttz") { while(expected<c.bits && !((a>>expected)&1)) ++expected; }
                if(c.op=="bitreverse") for(unsigned bit=0;bit<c.bits;++bit) expected|=((a>>bit)&1)<<(c.bits-bit-1);
                if(c.op=="bswap") for(unsigned byte=0;byte<c.bits/8;++byte) expected|=((a>>(byte*8))&255)<<((c.bits/8-byte-1)*8);
                if(c.op=="abs") expected=signed_value(a,c.bits)<0 ? (-a)&mask(c.bits) : a;
                if(c.op=="umin") expected=a<b ? a:b;
                if(c.op=="umax") expected=a>b ? a:b;
                if(c.op=="smin") expected=signed_value(a,c.bits)<signed_value(b,c.bits) ? a:b;
                if(c.op=="smax") expected=signed_value(a,c.bits)>signed_value(b,c.bits) ? a:b;
                if(c.op=="fshl") { auto shift=k%c.bits; expected=shift ? ((a<<shift)|(b>>(c.bits-shift)))&mask(c.bits) : a; }
                if(c.op=="fshr") { auto shift=k%c.bits; expected=shift ? ((b>>shift)|(a<<(c.bits-shift)))&mask(c.bits) : b; }
                expect(vm.output(c.result.bytes),expected&mask(c.bits),c.op+" i"+std::to_string(c.bits)); ++count;
            }
        }
        for(auto from:widths) for(auto to:widths) for(const std::string op:{"trunc","zext","sext","ptrtoint","inttoptr"}) {
            if((op=="trunc" && to>from) || ((op=="zext" || op=="sext") && to<from)) continue;
            auto c=numeric.cast(op,from,to,inputs("a",from)); Json script=c.code;
            for(unsigned i=0;i<8;++i) {
                auto a=(i==0 ? mask(from) : random()&mask(from)); vm.input("a",from,a); vm.steps=0; vm.run(script);
                auto expected=(op=="sext" ? static_cast<std::uint64_t>(signed_value(a,from)) : a)&mask(to);
                expect(vm.output(c.bytes),expected,op+" i"+std::to_string(from)+" to i"+std::to_string(to)); ++count;
            }
        }
        // Overflow and saturation reference arithmetic uses a <= 32-bit input,
        // leaving a genuinely wider uint64/int64 host type for the oracle.
        for(unsigned bits:{1u,7u,8u,9u,16u,17u,31u,32u})
            for(const std::string op:{"uadd","usub","umul","sadd","ssub","smul"})
                for(bool saturation:{false,true}) {
                    if(saturation && op.substr(1)=="mul") continue;
                    auto name="llvm."+op+(saturation ? ".sat.i" : ".with.overflow.i")+std::to_string(bits);
                    auto c=numeric.intrinsic(name,bits,{inputs("a",bits),inputs("b",bits)}); Json script=c.code;
                    for(unsigned i=0;i<64;++i) {
                        auto a=(i==0 ? mask(bits) : random()&mask(bits)),b=(i==0 ? 1 : random()&mask(bits));
                        vm.input("a",bits,a); vm.input("b",bits,b); vm.steps=0; vm.run(script);
                        bool ov=false; std::uint64_t expected=0;
                        if(op[0]=='s') {
                            auto sa=signed_value(a,bits),sb=signed_value(b,bits);
                            auto full=op.substr(1)=="add" ? sa+sb : op.substr(1)=="sub" ? sa-sb : sa*sb;
                            auto low=-(std::int64_t(1)<<(bits-1)),high=(std::int64_t(1)<<(bits-1))-1;
                            ov=full<low || full>high;
                            if(saturation) full=std::max(low,std::min(high,full));
                            expected=static_cast<std::uint64_t>(full)&mask(bits);
                        } else {
                            auto full=op.substr(1)=="add" ? a+b : op.substr(1)=="sub" ? a-b : a*b;
                            ov=op.substr(1)=="sub" ? a<b : full>mask(bits);
                            expected=saturation && ov ? (op.substr(1)=="sub" ? 0 : mask(bits)) : full&mask(bits);
                        }
                        Bytes value=c.bytes;
                        if(!saturation) { expect(static_cast<std::uint64_t>(vm.eval(value.back())),ov,name+" flag"); value.pop_back(); }
                        expect(vm.output(value),expected,name+" value a="+std::to_string(a)+" b="+std::to_string(b)); ++count;
                    }
                }
        if(args.size() > 1) {
            std::ifstream stream(std::filesystem::u8path(args[1]));
            if(!stream) throw Error("cannot open wide numeric fixtures");
            Json fixtures; stream>>fixtures;
            std::map<std::pair<unsigned,std::string>,NumericResult> generated;
            for(const auto& row:fixtures) {
                unsigned bits=row.at("bits");
                for(const auto& expected:row.at("expected").items()) {
                    const auto& op=expected.key();
                    auto key=std::make_pair(bits,op);
                    auto found=generated.find(key);
                    if(found==generated.end()) found=generated.emplace(key,numeric.binary(op,bits,inputs("a",bits),inputs("b",bits))).first;
                    for(unsigned i=0;i<(bits+7)/8;++i) {
                        vm.vars["a"+std::to_string(i)]=row.at("a").at(i).get<double>();
                        vm.vars["b"+std::to_string(i)]=row.at(op=="shl" || op=="lshr" || op=="ashr" ? "k":"b").at(i).get<double>();
                    }
                    vm.steps=0; vm.run(Json(found->second.code));
                    for(unsigned i=0;i<found->second.bytes.size();++i)
                        expect(static_cast<std::uint64_t>(vm.eval(found->second.bytes[i])),expected.value().at(i).get<std::uint64_t>(),"wide "+op+" i"+std::to_string(bits)+" byte "+std::to_string(i));
                    ++count;
                }
            }
        }
        if(args.size() > 2) {
            Project smoke; numeric.install(smoke);
            smoke.variables["__test_failures"]=0;
            smoke.variables["__test_done"]=0;
            smoke.lists["__test_actual"]=Json::array();
            smoke.lists["__test_expected"]=Json::array();
            Script script;
            for(const auto& c:cases) {
                if(c.bits!=64 && c.bits!=17) continue;
                if(c.op!="add" && c.op!="sub" && c.op!="mul" && c.op!="and" && c.op!="or" && c.op!="xor" &&
                   c.op!="shl" && c.op!="lshr" && c.op!="ashr" && c.op!="udiv" && c.op!="urem" &&
                   c.op!="sdiv" && c.op!="srem" && c.op!="slt") continue;
                auto a=mask(c.bits)-4, b=std::uint64_t(3);
                if(c.op=="mul" && c.bits==64) a=b=0xffffffffu;
                for(unsigned i=0;i<(c.bits+7)/8;++i) {
                    script.push_back(set("a"+std::to_string(i),(a>>(i*8))&255));
                    script.push_back(set("b"+std::to_string(i),(b>>(i*8))&255));
                }
                extend(script,c.result.code);
                auto expected=reference(c.op,c.bits,a,b);
                for(unsigned i=0;i<c.result.bytes.size();++i) {
                    auto byte=(expected>>(i*8))&255;
                    smoke.lists["__test_expected"].push_back(byte);
                    script.push_back(append("__test_actual",c.result.bytes[i]));
                    script.push_back(iff(lnot(eq(c.result.bytes[i],byte)),{set("__test_failures",add(var("__test_failures"),1))}));
                }
            }
            script.push_back(set("__test_done",1));
            smoke.procedure("numeric smoke",{},script);
            smoke.green_flag({call("numeric smoke",{})});
            smoke.save(args[2]);
        }
        std::cout<<"PASS "<<count<<" numeric cases (actual Scratch AST, integer boundaries and randomized inputs)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
