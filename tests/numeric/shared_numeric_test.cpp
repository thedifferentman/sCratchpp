#include "scratch/numeric.hpp"
#include "scratch/platform.hpp"
#include <cstdint>
#include <iostream>
#include <limits>

using namespace scratch;
namespace {
std::uint64_t mask(unsigned bits) { return bits == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << bits) - 1; }
std::int64_t signed_value(std::uint64_t n, unsigned bits) {
    return static_cast<std::int64_t>(n | ((n & (std::uint64_t(1) << (bits-1))) ? ~mask(bits) : 0));
}
Bytes bytes(std::uint64_t n, unsigned bits) {
    Bytes result;
    for (unsigned i = 0; i < (bits+7)/8; ++i) result.push_back((n >> (i*8)) & 255);
    return result;
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
    throw Error("unknown test reference opcode");
}
}

int main(int argc,char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        if(args.size()!=2) throw Error("usage: shared-numeric-test output.sb3");
        Numeric numeric(true);
        Project project;
        project.variables["__test_failures"]=0;
        project.variables["__test_done"]=0;
        project.variables["__test_checks"]=0;
        project.lists["__test_actual"]=Json::array();
        project.lists["__test_expected"]=Json::array();
        Script script;
        auto check=[&](const Bytes& actual,const Bytes& expected) {
            if(actual.size()!=expected.size()) throw Error("shared numeric result size mismatch");
            for(std::size_t i=0;i<actual.size();++i) {
                project.lists["__test_expected"].push_back(expected[i]);
                script.push_back(append("__test_actual",actual[i]));
                script.push_back(iff(lnot(eq(actual[i],expected[i])),{set("__test_failures",add(var("__test_failures"),1))}));
                script.push_back(set("__test_checks",add(var("__test_checks"),1)));
            }
        };
        auto reused=[&](const NumericResult& first,const NumericResult& second) {
            if(first.code.empty() || second.code.empty() || first.code[0].at("op")!="procedures_call" ||
               first.code[0].at("callee")!=second.code[0].at("callee"))
                throw Error("identical numeric signatures did not reuse their helper");
            if(first.bytes==second.bytes) throw Error("separate helper calls share unsnapshotted outputs");
        };
        for(unsigned bits:{1u,8u,17u,32u,64u}) {
            for(const std::string op:{"add","sub","mul","and","or","xor","shl","lshr","ashr","udiv","urem","sdiv","srem",
                                      "eq","ne","ult","ule","ugt","uge","slt","sle","sgt","sge"}) {
                bool comparison=op=="eq" || op=="ne" || op=="ult" || op=="ule" || op=="ugt" || op=="uge" ||
                                op=="slt" || op=="sle" || op=="sgt" || op=="sge";
                auto a=mask(bits)-1, b=std::uint64_t(3)&mask(bits), c=std::uint64_t(123)&mask(bits), d=std::uint64_t(5)&mask(bits);
                auto first=comparison ? numeric.compare(op,bits,bytes(a,bits),bytes(b,bits)) : numeric.binary(op,bits,bytes(a,bits),bytes(b,bits));
                auto second=comparison ? numeric.compare(op,bits,bytes(c,bits),bytes(d,bits)) : numeric.binary(op,bits,bytes(c,bits),bytes(d,bits));
                reused(first,second);
                extend(script,first.code); extend(script,second.code);
                // Check the first result only AFTER another call of the same helper.
                check(first.bytes,bytes(reference(op,bits,a,b),comparison?1:bits));
                check(second.bytes,bytes(reference(op,bits,c,d),comparison?1:bits));
            }
        }
        for(const std::string op:{"sext","zext","trunc","ptrtoint","inttoptr","bitcast"}) {
            unsigned from=op=="trunc" ? 64:17, to=op=="trunc" ? 17:op=="bitcast" ? 17:64;
            auto first=numeric.cast(op,from,to,bytes(mask(from),from));
            auto second=numeric.cast(op,from,to,bytes(1,from));
            reused(first,second); extend(script,first.code); extend(script,second.code);
            check(first.bytes,bytes(op=="sext" ? mask(to) : mask(from)&mask(to),to));
            check(second.bytes,bytes(1,to));
        }
        for(const std::string name:{"ctpop","ctlz","cttz","bitreverse","bswap","abs","smax","smin","umax","umin","fshl","fshr","uadd.with.overflow","sadd.sat"}) {
            const auto a=bytes(0xfffe,16), b=bytes(3,16), c=bytes(5,16);
            std::vector<Bytes> first_args{a,b}, second_args{b,c};
            if(name=="ctpop" || name=="bitreverse" || name=="bswap") { first_args={a};second_args={b}; }
            if(name=="ctlz" || name=="cttz" || name=="abs") { first_args={a,Bytes{0}};second_args={b,Bytes{0}}; }
            if(name=="fshl" || name=="fshr") { first_args={a,b,c};second_args={b,c,a}; }
            auto first=numeric.intrinsic("llvm."+name+".i16",16,first_args);
            auto second=numeric.intrinsic("llvm."+name+".i16",16,second_args);
            reused(first,second); extend(script,first.code); extend(script,second.code);
            Bytes expected_first,expected_second;
            if(name=="ctpop") {expected_first=bytes(15,16);expected_second=bytes(2,16);}
            if(name=="ctlz") {expected_first=bytes(0,16);expected_second=bytes(14,16);}
            if(name=="cttz") {expected_first=bytes(1,16);expected_second=bytes(0,16);}
            if(name=="bitreverse") {expected_first=bytes(32767,16);expected_second=bytes(49152,16);}
            if(name=="bswap") {expected_first=bytes(65279,16);expected_second=bytes(768,16);}
            if(name=="abs") {expected_first=bytes(2,16);expected_second=bytes(3,16);}
            if(name=="smax") {expected_first=bytes(3,16);expected_second=bytes(5,16);}
            if(name=="smin") {expected_first=bytes(65534,16);expected_second=bytes(3,16);}
            if(name=="umax") {expected_first=bytes(65534,16);expected_second=bytes(5,16);}
            if(name=="umin") {expected_first=bytes(3,16);expected_second=bytes(3,16);}
            if(name=="fshl") {expected_first=bytes(((65534u<<5)|(3u>>11))&65535,16);expected_second=bytes(((3u<<14)|(5u>>2))&65535,16);}
            if(name=="fshr") {expected_first=bytes(((3u>>5)|(65534u<<11))&65535,16);expected_second=bytes(((5u>>14)|(3u<<2))&65535,16);}
            if(name=="uadd.with.overflow") {expected_first={1,0,1};expected_second={8,0,0};}
            if(name=="sadd.sat") {expected_first=bytes(1,16);expected_second=bytes(8,16);}
            check(first.bytes,expected_first);check(second.bytes,expected_second);
        }
        // Feed one shared result to another call and then to the same operation.
        auto sum=numeric.binary("add",64,bytes(0xffffffffu,64),bytes(1,64)); extend(script,sum.code);
        auto product=numeric.binary("mul",64,sum.bytes,sum.bytes);extend(script,product.code);
        auto final=numeric.binary("add",64,sum.bytes,product.bytes);extend(script,final.code);
        check(final.bytes,bytes(0x100000000ULL,64));
        script.push_back(set("__test_done",1));
        project.procedure("shared numeric smoke",{},script);
        project.green_flag({call("shared numeric smoke",{})});
        numeric.install(project);
        auto serialized=project.build();
        std::size_t helpers=0;
        for(const auto& block:serialized.at("targets").at(1).at("blocks").items())
            if(block.value().at("opcode")=="procedures_definition") ++helpers;
        if(helpers!=136) throw Error("unexpected shared helper count: "+std::to_string(helpers));
        project.save(args[1]);
        std::cout<<"PASS shared helper reuse and result isolation; generated "<<helpers-1<<" helpers and "
                 <<project.lists["__test_expected"].size()<<" real-VM byte checks\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
