#include "scratch/backend.hpp"
#include "scratch/numeric.hpp"
#include "scratch/assembly.hpp"
#include "scratch/slot_memory.hpp"
#include "scratch/slot_numeric.hpp"
#include "scratch/constants.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace scratch {
namespace {
constexpr const char* MEMORY = "__scl_memory";
constexpr const char* SP = "__scl_sp";
constexpr const char* HP = "__scl_heap";
constexpr const char* STATUS = "__scl_status";
unsigned size_of(const Json& t) {
    const uint64_t n=t.value("size",uint64_t(0));
    if(n>200000)throw Error("value storage size exceeds the Scratch memory capacity");
    return static_cast<unsigned>(n);
}
unsigned bits_of(const Json& t) { return t.value("bits", size_of(t) * 8); }
unsigned align_up(unsigned n, unsigned a) {
    a=std::max(1u,a);const uint64_t result=(uint64_t(n)+a-1)/a*a;
    if(result>200000)throw Error("aligned layout exceeds the Scratch memory capacity");
    return static_cast<unsigned>(result);
}
bool starts(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
Bytes literal(uint64_t n, unsigned bytes) {
    Bytes b; for (unsigned i=0; i<bytes; ++i) { b.push_back(i<8 ? (n & 255) : 0); n >>= 8; } return b;
}
Bytes zeros(unsigned n) { return Bytes(n, Expr(0)); }
struct Slot { unsigned offset; Json type; };
struct Function {
    const Json* source = nullptr;
    std::string name, entry, body;
    unsigned address = 0, frame_size = 0, outgoing = 0, outgoing_size = 0;
    std::map<std::string, Slot> slots;
    std::map<std::string, unsigned> block_ids;
    std::map<std::string, const Json*> blocks;
    std::vector<unsigned> arg_offsets;
};

class Compiler {
    const Json& module;
    BackendOptions options;
    Project project;
    Numeric numeric{true};
    SlotMemory slot_memory;
    SlotNumeric slot_numeric;
    bool building_runtime=false;
    Expr runtime_destination;
    std::map<std::string,std::string> runtime_operations;
    std::map<std::string,unsigned> constant_slots;
    std::vector<std::pair<unsigned,Json>> constant_initializers;
    unsigned pointer_bytes, serial = 0, data_start = 0, heap_start = 0, boot_args = 0, boot_result = 0, tail_args = 0, tail_size = 16;
    std::map<std::string, Function> functions;
    std::map<std::string, uint64_t> symbols;
    std::map<std::string, Json> aliases;
    std::map<std::pair<std::string,std::string>,unsigned> block_addresses;
    std::map<std::string, const Json*> declarations;
    unsigned argv_address=0;
    std::vector<std::pair<unsigned,std::string>> argument_strings;
    Function* current = nullptr;
    std::string current_block;
    std::string temp(const std::string& hint = "t") { return "__scl_"+hint+std::to_string(serial++); }
    Expr save(Expr e, Script& out, const std::string& hint="t") { auto n=temp(hint); out.push_back(set(n,e)); return var(n); }
    void error(const std::string& why) const {
        throw Error((current ? "function '"+current->name+"', block '"+current_block+"': " : "") + why);
    }
    Script trap(const std::string& why) {
        return slot_memory.trap(why);
    }
    static unsigned script_weight(const Json& value) {
        unsigned count=value.is_object() && value.contains("op") ? 1 : 0;
        if(value.is_object()) {
            for(const auto& child:value.items())if(child.key()!="debug_point")count+=script_weight(child.value());
        }else if(value.is_array())for(const auto& child:value)count+=script_weight(child);
        return count;
    }
    static bool contains_ssa_reference(const Json& value) {
        if(value.is_object() && value.value("kind","")=="ref")return true;
        if(value.is_structured())for(const auto& child:value)if(contains_ssa_reference(child))return true;
        return false;
    }
    Script outline_script(Script code) {
        // Large STL functions can overwhelm TurboWarp's JS compiler when one
        // Scratch procedure contains tens of thousands of reporter blocks.
        // Helpers share the existing LLVM frame; they do not allocate another
        // virtual frame or change phi, return, recursion or musttail semantics.
        constexpr unsigned limit=512;
        for(auto& statement:code)if(statement.contains("branches")) {
            for(auto& branch:statement["branches"])
                branch=outline_script(branch.get<Script>());
        }
        unsigned total=0;for(const auto& statement:code)total+=script_weight(statement);
        if(total<=limit)return code;
        Script calls,part;unsigned weight=0;
        auto flush=[&] {
            if(part.empty())return;
            const auto name=temp("chunk");
            project.procedure(name,{"frame","args","result"},std::move(part));
            calls.push_back(call(name,{arg("frame"),arg("args"),arg("result")}));
            part.clear();weight=0;
        };
        for(auto& statement:code) {
            const auto size=script_weight(statement);
            if(weight && weight+size>limit)flush();
            weight+=size;part.push_back(std::move(statement));
        }
        flush();return calls;
    }
    Bytes read(Expr address, unsigned count) {
        Bytes b; for(unsigned i=0;i<count;++i) b.push_back(item(MEMORY,i ? add(address,i) : address)); return b;
    }
    void write(Expr address, const Bytes& bytes, Script& out) {
        if(bytes.empty())return;
        if(std::all_of(bytes.begin(),bytes.end(),[](const Expr& b){return b.is_number() && b==0;})) {
            out.push_back(slot_memory.fill(address,0,static_cast<unsigned>(bytes.size())));return;
        }
        for(unsigned i=0;i<bytes.size();++i) out.push_back(replace(MEMORY,i?add(address,i):address,bytes[i]));
    }
    Bytes snapshot(const Bytes& b, Script& out) { Bytes r; for(const auto& x:b) r.push_back(save(x,out,"byte")); return r; }
    Expr frame() { return arg("frame"); }
    Expr address_of(const std::string& id) {
        if(!current || !current->slots.count(id)) error("unknown SSA value '"+id+"'");
        return add(frame(),current->slots.at(id).offset);
    }
    Expr operand_slot(const Json& operand,Script&) {
        const auto kind=operand.value("kind","");
        if(kind=="ref")return address_of(operand.at("id"));
        if(kind=="runtime_slot")return operand.at("address");
        const auto encoded=constant_value(operand);
        const auto key=Json(encoded).dump();
        auto found=constant_slots.find(key);
        if(found!=constant_slots.end())return found->second;
        const unsigned count=size_of(operand.at("type"));
        unsigned address=align_up(heap_start,operand.at("type").value("align",1u));
        if(uint64_t(address)+count+256>=options.memory_size)error("constant pool exceeds configured memory capacity");
        heap_start=address+std::max(1u,count);
        constant_slots[key]=address;constant_initializers.push_back({address,{{"kind","bytes"},{"type",operand.at("type")},{"bytes",encoded}}});
        return address;
    }
    void slot_copy(Expr destination,Expr source,unsigned bytes,Script& out) {
        if(bytes && destination!=source)out.push_back(slot_memory.copy(destination,source,bytes));
    }
    void share_instruction(const Json& original,Script& out) {
        Json model=original;model.erase("id");model.erase("debug");model.erase("debug_origin");
        const std::string op=model.at("op");
        if((op=="gep" || op=="getelementptr") && model.at("type").value("kind","")!="vector") {
            Json fixed=model;fixed["kind"]="constexpr";fixed["operands"]=Json::array({{{"kind","zero"},{"type",model.at("type")}}});
            fixed["gep"]=Json::array();Json dynamic=Json::array();
            for(const auto& step:model.at("gep")) {
                if(step.contains("offset") || step.at("index").value("kind","")!="ref")fixed["gep"].push_back(step);
                else dynamic.push_back(step);
            }
            auto offset=constant_value(fixed);
            if(std::any_of(offset.begin(),offset.end(),[](unsigned b){return b!=0;}))dynamic.push_back({{"offset_bytes",offset}});
            model["gep"]=dynamic;
            model["operands"]=Json::array({original.at("operands").at(0)});
            if(dynamic.empty()){slot_copy(address_of(original.at("id")),operand_slot(original.at("operands").at(0),out),size_of(original.at("type")),out);return;}
        }
        const auto name=model.contains("callee")?model.at("callee").value("name",""):"";
        const bool context=op=="va_arg" || starts(name,"llvm.va_") || starts(name,"llvm.stackrestore");
        if(context && current) {
            Json args=Json::array();
            for(const auto& a:current->source->at("args"))args.push_back({{"type",a.at("type")},{"attrs",a.at("attrs")}});
            model["runtime_context"]=args;
        }
        std::vector<Json> operands;
        std::map<std::string,unsigned> indexes;
        std::function<void(Json&)> bind=[&](Json& node) {
            if(node.is_object()) {
                const auto kind=node.value("kind","");
                if(node.contains("type") && (kind=="ref" || kind=="bytes" || kind=="zero" || kind=="undef" || kind=="poison" || kind=="symbol" || kind=="aggregate" || kind=="constexpr" || kind=="blockaddress")) {
                    const auto key=node.dump();auto it=indexes.find(key);unsigned index;
                    if(it==indexes.end()){index=static_cast<unsigned>(operands.size());indexes[key]=index;operands.push_back(node);}else index=it->second;
                    node={{"kind","runtime_slot"},{"type",node.at("type")},{"address",arg("a"+std::to_string(index))}};return;
                }
                for(auto& item:node.items())if(item.key()!="callee")bind(item.value());
            }else if(node.is_array())for(auto& child:node)bind(child);
        };
        bind(model);
        const auto key=model.dump();auto found=runtime_operations.find(key);
        std::string procedure;
        if(found==runtime_operations.end()) {
            procedure="__scl_rt_op_"+std::to_string(runtime_operations.size());runtime_operations[key]=procedure;
            std::vector<std::string> parameters={"D"};
            if(context){parameters.push_back("frame");parameters.push_back("args");parameters.push_back("result");}
            for(unsigned n=0;n<operands.size();++n)parameters.push_back("a"+std::to_string(n));
            Script body;building_runtime=true;runtime_destination=arg("D");
            instruction(model,body);
            runtime_destination=nullptr;building_runtime=false;
            project.procedure(procedure,parameters,std::move(body));
        }else procedure=found->second;
        std::vector<Expr> arguments={size_of(original.at("type"))?address_of(original.at("id")):Expr(boot_result)};
        if(context){arguments.push_back(frame());arguments.push_back(arg("args"));arguments.push_back(arg("result"));}
        for(const auto& operand:operands)arguments.push_back(operand_slot(operand,out));
        out.push_back(call(procedure,arguments));
    }
    Expr small(const Bytes& b, Script& out, const std::string& what) {
        if(b.empty()) return 0;
        Expr result=0, factor=1;
        for(unsigned i=0;i<std::min<unsigned>(3,static_cast<unsigned>(b.size()));++i) {
            result=add(result,mul(b[i],factor)); factor=mul(factor,256);
        }
        if(b.size()>3) {
            Expr nonzero=eq(0,1);
            for(unsigned i=3;i<b.size();++i) nonzero=lor(nonzero,lnot(eq(b[i],0)));
            out.push_back(iff(nonzero,trap(what+" exceeds addressable memory")));
        }
        return save(result,out,"index");
    }
    Expr checked_address(const Bytes& b, unsigned n, Script& out) {
        Expr p=small(b,out,"pointer");
        if(n) out.push_back(iff(lor(lt(p,data_start),gt(add(p,n-1),options.memory_size)),trap("invalid memory access")));
        return p;
    }
    Expr vector_index(const Bytes& bytes, unsigned count, Script& out) {
        Expr low=0, valid=eq(0,0);double factor=1;
        for(unsigned i=0;i<bytes.size();++i) {
            if(i<3){low=add(low,mul(bytes[i],factor));factor*=256;}
            else valid=land(valid,eq(bytes[i],0));
        }
        auto name=temp("vector_index");out.push_back(set(name,count));
        out.push_back(iff(valid,{set(name,low)}));
        return var(name);
    }
    Bytes from_small(Expr n,unsigned count) {
        Bytes b; double factor=1;
        for(unsigned i=0;i<count;++i) { b.push_back(i<3 ? mod(floor_(scratch::div(n,factor)),256) : Expr(0)); factor*=256; }
        return b;
    }
    Bytes accept(NumericResult r, Script& out) { extend(out,r.code); return r.bytes; }
    Bytes canonical(Bytes bytes, const Json& type) {
        const auto kind = type.value("kind", "");
        if ((kind == "int" || kind == "vector") && !bytes.empty()) {
            const unsigned bits = bits_of(type);
            if (bits % 8) bytes.back() = mod(bytes.back(), 1u << (bits % 8));
        }
        return bytes;
    }
    unsigned aggregate_offset(const Json& type,const Json& indices,Json* final_type=nullptr) {
        Json t=type; unsigned offset=0;
        for(const auto& ix:indices) {
            unsigned i=ix.get<unsigned>(); const auto kind=t.value("kind","");
            if(kind=="struct") { offset+=t.at("offsets").at(i).get<unsigned>(); t=t.at("fields").at(i); }
            else if(kind=="array") { offset+=i*t.at("element").value("alloc_size",size_of(t.at("element"))); t=t.at("element"); }
            else error("invalid aggregate index");
        }
        if(final_type) *final_type=t; return offset;
    }
    Bytes element(const Bytes& bytes,unsigned index,unsigned bits) {
        if(bits%8==0) {
            const unsigned offset=index*(bits/8), count=bits/8;
            if(offset+count>bytes.size())error("vector value has insufficient storage");
            return Bytes(bytes.begin()+offset,bytes.begin()+offset+count);
        }
        Bytes r((bits+7)/8,Expr(0));
        for(unsigned bit=0;bit<bits;++bit) {
            unsigned src=index*bits+bit;
            if(src/8>=bytes.size()) error("vector value has insufficient storage");
            Expr v=mod(floor_(scratch::div(bytes[src/8],1u<<(src%8))),2);
            r[bit/8]=add(r[bit/8],mul(v,1u<<(bit%8)));
        }
        return r;
    }
    void pack(Bytes& target,const Bytes& value,unsigned bit_offset,unsigned bits) {
        if(bit_offset%8==0 && bits%8==0) {
            const unsigned offset=bit_offset/8,count=bits/8;
            if(offset+count>target.size() || count>value.size())error("packed value exceeds target storage");
            for(unsigned i=0;i<count;++i) {
                if(target[offset+i].is_number() && target[offset+i]==0)target[offset+i]=value[i];
                else target[offset+i]=add(target[offset+i],value[i]);
            }
            return;
        }
        for(unsigned bit=0;bit<bits;++bit) {
            unsigned dst=bit_offset+bit;
            if(dst/8>=target.size()) error("packed value exceeds target storage");
            target[dst/8]=add(target[dst/8],mul(mod(floor_(scratch::div(value[bit/8],1u<<(bit%8))),2),1u<<(dst%8)));
        }
    }
    std::vector<unsigned> constant_value(const Json& v,std::set<std::string>* resolving=nullptr) {
        std::set<std::string> own;auto& seen=resolving?*resolving:own;
        return evaluate_constant(v,[&](const Json& symbol) {
            const unsigned n=size_of(symbol.at("type"));std::vector<unsigned> bytes(n,0);
            if(symbol.at("kind")=="blockaddress") {
                const auto key=std::make_pair(symbol.at("function").get<std::string>(),symbol.at("block").get<std::string>());
                if(!block_addresses.count(key))error("unallocated block address");
                uint64_t address=block_addresses.at(key);
                for(unsigned i=0;i<std::min(n,8u);++i){bytes[i]=address&255;address>>=8;}
                return bytes;
            }
            const std::string name=symbol.at("name");
            if(symbols.count(name)) {
                uint64_t address=symbols.at(name);
                for(unsigned i=0;i<std::min(n,8u);++i){bytes[i]=address&255;address>>=8;}
            }else if(aliases.count(name)) {
                if(!seen.insert(name).second)error("cyclic alias '"+name+"'");
                bytes=constant_value(aliases.at(name),&seen);bytes.resize(n,0);seen.erase(name);
            }else if(name=="__scrpp_io_read" || name=="__scrpp_io_write") {
                error("standard I/O terminal adapter is missing; link console >= 0.2.0 with SDK ABI 2 (or provide "+name+")");
            }else error("unresolved symbol '"+name+"'");
            const auto offset=symbol.value("addend",int64_t(0));unsigned carry=0;
            for(unsigned i=0;i<n;++i) {
                unsigned delta=i<8?((static_cast<uint64_t>(offset)>>(i*8))&255u):(offset<0?255u:0u);
                unsigned sum=bytes[i]+delta+carry;bytes[i]=sum&255;carry=sum>>8;
            }
            return bytes;
        });
    }
    Bytes value(const Json& v,Script& out,std::set<std::string>* resolving=nullptr) {
        const auto kind=v.value("kind","");const unsigned n=size_of(v.at("type"));
        if(kind=="ref")return read(address_of(v.at("id")),n);
        if(kind=="runtime_slot")return snapshot(read(v.at("address"),n),out);
        if(kind=="runtime_bytes")return v.at("bytes").get<Bytes>();
        Bytes result;for(auto byte:constant_value(v,resolving))result.push_back(byte);return result;
    }
    Bytes operation(const Json& inst,Script& out) {
        const std::string op=inst.at("op"); const Json& t=inst.at("type");
        const auto& ops=inst.at("operands");
        if(op=="getelementptr" || op=="gep") {
            if(t.value("kind","")=="vector") {
                const auto& pointer_type=t.at("element");unsigned count=t.at("count"),width=bits_of(pointer_type);
                auto base=value(ops.at(0),out);Bytes result=zeros(size_of(t));
                for(unsigned lane=0;lane<count;++lane) {
                    Json scalar=inst;scalar["type"]=pointer_type;
                    if(ops.at(0).at("type").value("kind","")=="vector")
                        scalar["operands"][0]={{"kind","runtime_bytes"},{"type",pointer_type},{"bytes",element(base,lane,width)}};
                    for(unsigned step=0;step<scalar.at("gep").size();++step) {
                        if(!scalar["gep"][step].contains("index"))continue;
                        const auto index=inst.at("gep")[step].at("index");
                        if(index.at("type").value("kind","")=="vector") {
                            const auto& element_type=index.at("type").at("element");
                            scalar["gep"][step]["index"]={{"kind","runtime_bytes"},{"type",element_type},{"bytes",element(value(index,out),lane,bits_of(element_type))}};
                        }
                    }
                    auto pointer=operation(scalar,out);pack(result,pointer,lane*width,width);
                }
                return snapshot(result,out);
            }
            Bytes base=snapshot(value(ops.at(0),out),out); unsigned pbits=bits_of(t), ibits=t.value("index_bits",pbits);
            if(!ibits) ibits=pbits;
            Bytes sum=pbits==ibits?base:accept(numeric.cast("trunc",pbits,ibits,base),out);
            for(const auto& step:inst.at("gep")) {
                Bytes delta;
                if(step.contains("offset_bytes")){for(const auto& byte:step.at("offset_bytes"))delta.push_back(byte);delta.resize((ibits+7)/8,0);}
                else if(step.contains("offset")) delta=literal(step.at("offset").get<uint64_t>(),(ibits+7)/8);
                else {
                    auto x=value(step.at("index"),out);
                    const unsigned source_bits=bits_of(step.at("index").at("type"));
                    if(source_bits!=ibits)x=accept(numeric.cast(source_bits>ibits?"trunc":"sext",source_bits,ibits,x),out);
                    const uint64_t stride=step.at("stride");
                    if(stride==1)delta=x;
                    else if(stride && !(stride&(stride-1))) {
                        unsigned shift=0;for(uint64_t s=stride;s>1;s>>=1)++shift;
                        delta=accept(numeric.binary("shl",ibits,x,literal(shift,(ibits+7)/8)),out);
                    }else delta=accept(numeric.binary("mul",ibits,x,literal(stride,(ibits+7)/8)),out);
                }
                sum=accept(numeric.binary("add",ibits,sum,delta),out);
            }
            for(unsigned i=0;i<base.size();++i) {
                if(i*8>=ibits) continue;
                if((i+1)*8<=ibits) base[i]=sum.at(i);
                else base[i]=add(sum.at(i),mul(floor_(scratch::div(base[i],1u<<(ibits%8))),1u<<(ibits%8)));
            }
            return base;
        }
        if(op=="freeze") return snapshot(value(ops.at(0),out),out);
        if(op=="select") {
            Bytes c=value(ops.at(0),out), a=value(ops.at(1),out), b=value(ops.at(2),out);
            Bytes r; Script yes,no;
            if(ops.at(0).at("type").value("kind","")=="vector") {
                auto elem=t.at("element"); const unsigned width=bits_of(elem), count=t.at("count");
                Bytes packed=zeros(size_of(t));
                for(unsigned i=0;i<count;++i) {
                    auto aa=element(a,i,width),bb=element(b,i,width); Bytes lane;
                    Script y,n;
                    for(unsigned j=0;j<aa.size();++j) {auto v=temp("select"); lane.push_back(var(v));y.push_back(set(v,aa[j]));n.push_back(set(v,bb[j]));}
                    out.push_back(iff(element(c,i,1)[0],y,n)); pack(packed,lane,i*width,width);
                }
                return snapshot(packed,out);
            }
            for(unsigned i=0;i<size_of(t);++i) { auto n=temp("select"); r.push_back(var(n)); yes.push_back(set(n,a.at(i)));no.push_back(set(n,b.at(i))); }
            out.push_back(iff(c.at(0),yes,no)); return r;
        }
        if(op=="extractvalue") {
            auto b=value(ops.at(0),out); unsigned off=aggregate_offset(ops.at(0).at("type"),inst.at("indices"));
            return Bytes(b.begin()+off,b.begin()+off+size_of(t));
        }
        if(op=="insertvalue") {
            auto b=value(ops.at(0),out), v=value(ops.at(1),out); unsigned off=aggregate_offset(t,inst.at("indices"));
            std::copy(v.begin(),v.end(),b.begin()+off); return b;
        }
        if(op=="bitcast") { auto b=value(ops.at(0),out); if(b.size()!=size_of(t))error("invalid bitcast storage size"); return b; }
        static const std::set<std::string> binary={"add","sub","mul","udiv","sdiv","urem","srem","and","or","xor","shl","lshr","ashr"};
        static const std::set<std::string> casts={"trunc","zext","sext","ptrtoint","inttoptr"};
        if(binary.count(op) || op=="icmp" || casts.count(op)) {
            auto a=value(ops.at(0),out); auto source=ops.at(0).at("type");
            if(source.value("kind","")=="vector") {
                unsigned count=source.at("count"), width=bits_of(source.at("element"));
                unsigned dstwidth=op=="icmp"?1:bits_of(t.at("element"));
                Bytes b=ops.size()>1?value(ops.at(1),out):Bytes{}, r=zeros(size_of(t));
                for(unsigned i=0;i<count;++i) {
                    auto aa=element(a,i,width); Bytes lane;
                    if(op=="icmp") lane=accept(numeric.compare(inst.at("predicate"),width,aa,element(b,i,width)),out);
                    else if(casts.count(op)) lane=accept(numeric.cast(op,width,dstwidth,aa),out);
                    else lane=accept(numeric.binary(op,width,aa,element(b,i,width)),out);
                    pack(r,lane,i*dstwidth,dstwidth);
                }
                return snapshot(r,out);
            }
            if(casts.count(op)) return accept(numeric.cast(op,bits_of(source),bits_of(t),a),out);
            auto b=value(ops.at(1),out);
            if(op=="icmp") return accept(numeric.compare(inst.at("predicate"),bits_of(source),a,b),out);
            return accept(numeric.binary(op,bits_of(t),a,b),out);
        }
        if(op=="extractelement" || op=="insertelement") {
            auto vector=value(ops.at(0),out); const auto& vt=ops.at(0).at("type"); unsigned count=vt.at("count"), width=bits_of(vt.at("element"));
            unsigned idx=op=="extractelement"?1:2; auto index=vector_index(value(ops.at(idx),out),count,out);
            Bytes result; for(unsigned i=0;i<size_of(t);++i) result.push_back(save(0,out,"vector"));
            auto assign=[&](const Bytes& b) {Script s;for(unsigned i=0;i<b.size();++i)s.push_back(set(result[i].at("fields").at("VARIABLE").at(0),b[i]));return s;};
            for(unsigned i=0;i<count;++i) {
                Bytes b;
                if(op=="extractelement") b=element(vector,i,width);
                else {
                    b=zeros(size_of(vt)); auto replacement=value(ops.at(1),out);
                    for(unsigned j=0;j<count;++j)pack(b,j==i?replacement:element(vector,j,width),j*width,width);
                }
                out.push_back(iff(eq(index,i),assign(b)));
            }
            return result;
        }
        if(op=="shufflevector") {
            auto a=value(ops.at(0),out),b=value(ops.at(1),out); auto vt=ops.at(0).at("type");
            unsigned width=bits_of(vt.at("element")),count=vt.at("count"); Bytes r=zeros(size_of(t)); unsigned i=0;
            for(const auto& m:inst.at("mask")) {int ix=m.get<int>(); if(ix>=0)pack(r,element(ix<static_cast<int>(count)?a:b,ix%count,width),i*width,width);++i;}
            return snapshot(r,out);
        }
        error("unsupported value instruction '"+op+"'"); return {};
    }
    void store_result(const Json& i,const Bytes& b,Script& out) {
        if(!size_of(i.at("type")))return;
        if(b.size()!=size_of(i.at("type"))) error("result byte count mismatch for '"+i.at("op").get<std::string>()+"'");
        write(runtime_destination.is_null()?address_of(i.at("id")):runtime_destination,canonical(b,i.at("type")),out);
    }
    void edge(const std::string& target,Script& out) {
        if(!current->blocks.count(target))error("unknown branch target");
        struct Move { Expr destination,source;unsigned destination_offset,source_offset,bytes;bool local; };
        std::vector<Move> copies;
        for(const auto& i:current->blocks.at(target)->at("instructions")) {
            if(i.at("op")!="phi")break;
            bool found=false;
            for(const auto& incoming:i.at("incoming")) if(incoming.at("block")==current_block) {
                const auto& value=incoming.at("value");auto source=operand_slot(value,out),destination=address_of(i.at("id"));
                const bool local=value.value("kind","")=="ref";
                if(source!=destination)copies.push_back({destination,source,current->slots.at(i.at("id")).offset,
                    local?current->slots.at(value.at("id")).offset:0,size_of(i.at("type")),local});
                found=true;break;
            }
            if(!found)error("phi has no value on edge to '"+target+"'");
        }
        unsigned temporary_bytes=0;
        auto overlaps=[](unsigned a,unsigned an,unsigned b,unsigned bn){return a<b+bn && b<a+an;};
        while(!copies.empty()) {
            auto ready=copies.end();
            for(auto it=copies.begin();it!=copies.end();++it) {
                bool blocked=false;
                for(auto other=copies.begin();other!=copies.end();++other)
                    if(other!=it && other->local && overlaps(it->destination_offset,it->bytes,other->source_offset,other->bytes)){blocked=true;break;}
                if(!blocked){ready=it;break;}
            }
            if(ready!=copies.end()) {
                slot_copy(ready->destination,ready->source,ready->bytes,out);copies.erase(ready);
            }else {
                // Break only a real dependency cycle. Scratch runtime helpers
                // cannot call guest code, so the outgoing area is free here.
                auto& move=copies.front();unsigned offset=current->outgoing+temporary_bytes;
                slot_copy(add(frame(),offset),move.source,move.bytes,out);
                move.source=add(frame(),offset);move.source_offset=offset;move.local=true;
                temporary_bytes+=move.bytes;
                current->outgoing_size=std::max(current->outgoing_size,temporary_bytes);
                current->frame_size=align_up(current->outgoing+current->outgoing_size,16);
                if(current->frame_size>options.memory_size)error("phi temporary slots exceed memory capacity");
            }
        }
        out.push_back(slot_memory.encode(frame(),current->block_ids.at(target),4));
    }
    void copy_memory(Expr dst,Expr src,Expr count,Script& out,bool move) {
        (void)move; // memmove also satisfies every defined memcpy use.
        out.push_back(iff(gt(count,0),{slot_memory.check_access(dst,count),slot_memory.check_access(src,count),slot_memory.copy(dst,src,count)}));
    }
#include "varargs.inc"
    bool intrinsic(const Json& i,const std::string& name,Script& out) {
        const auto& ops=i.at("operands");
        if(varargs_intrinsic(i,name,out))return true;
        if(starts(name,"llvm.lifetime.") || starts(name,"llvm.dbg.") || starts(name,"llvm.assume"))return true;
        if(starts(name,"llvm.invariant.start.")){store_result(i,zeros(size_of(i.at("type"))),out);return true;}
        if(starts(name,"llvm.invariant.end."))return true;
        if(starts(name,"llvm.expect.")){store_result(i,value(ops.at(0),out),out);return true;}
        if(starts(name,"llvm.trap") || starts(name,"llvm.debugtrap")){extend(out,trap(name));return true;}
        if(starts(name,"llvm.stacksave")){store_result(i,from_small(var(SP),size_of(i.at("type"))),out);return true;}
        if(starts(name,"llvm.stackrestore")){
            auto p=small(value(ops.at(0),out),out,"saved stack pointer");
            out.push_back(iff(lor(lt(p,var(SP)),gt(p,frame())),trap("invalid stack restore")));
            out.push_back(set(SP,p));return true;
        }
        if(starts(name,"llvm.memcpy") || starts(name,"llvm.memmove")) {
            auto count=small(value(ops.at(2),out),out,"copy length");Script access;
            auto dst=small(value(ops.at(0),access),access,"destination"),src=small(value(ops.at(1),access),access,"source");
            copy_memory(dst,src,count,access,starts(name,"llvm.memmove"));out.push_back(iff(gt(count,0),access));return true;
        }
        if(starts(name,"llvm.memset")) {
            auto count=small(value(ops.at(2),out),out,"fill length");Script fill;
            auto dst=small(value(ops.at(0),fill),fill,"destination"),byte=save(value(ops.at(1),fill).at(0),fill);
            auto k=temp("fill");
            fill.push_back(iff(lor(lt(dst,data_start),gt(add(dst,sub(count,1)),options.memory_size)),trap("invalid memory fill")));
            fill.push_back(set(k,0));fill.push_back(repeat(count,{replace(MEMORY,add(dst,var(k)),byte),set(k,add(var(k),1))}));
            out.push_back(iff(gt(count,0),fill));return true;
        }
        if(starts(name,"llvm.masked.load.") || starts(name,"llvm.masked.gather.") || starts(name,"llvm.masked.store.") || starts(name,"llvm.masked.scatter.")) {
            const bool loading=starts(name,"llvm.masked.load.") || starts(name,"llvm.masked.gather.");
            const bool scattered=starts(name,"llvm.masked.gather.") || starts(name,"llvm.masked.scatter.");
            const Json& vector_type=loading?i.at("type"):ops.at(0).at("type");
            const auto& elem=vector_type.at("element");unsigned width=bits_of(elem),count=vector_type.at("count"),size=size_of(elem);
            if(width%8)error("masked memory operations currently require byte-aligned vector element widths");
            const unsigned pointer_index=loading?0:1,mask_index=static_cast<unsigned>(ops.size())-(loading?2:1);
            auto pointers=snapshot(value(ops.at(pointer_index),out),out),mask=snapshot(value(ops.at(mask_index),out),out);
            auto values=snapshot(value(ops.at(loading?ops.size()-1:0),out),out);
            Bytes result=loading?snapshot(values,out):Bytes{};
            for(unsigned lane=0;lane<count;++lane) {
                Script access;Bytes ptr;
                if(scattered)ptr=element(pointers,lane,pointer_bytes*8);
                else ptr=accept(numeric.binary("add",pointer_bytes*8,pointers,literal(static_cast<uint64_t>(lane)*size,pointer_bytes)),access);
                auto address=checked_address(ptr,size,access);
                if(loading) {
                    auto loaded=read(address,size);
                    for(unsigned byte=0;byte<size;++byte)access.push_back(set(result.at(lane*size+byte).at("fields").at("VARIABLE").at(0),loaded[byte]));
                } else write(address,element(values,lane,width),access);
                out.push_back(iff(element(mask,lane,1).at(0),std::move(access)));
            }
            if(loading)store_result(i,result,out);
            return true;
        }
        if(starts(name,"llvm.ucmp.") || starts(name,"llvm.scmp.")) {
            // LLVM 22 three-way compares have independently overloaded input
            // and result widths; -1/0/+1 are signed results even for ucmp.
            // This path lives inside the existing cached slot instruction
            // helper and reuses exact byte comparisons, never native numbers.
            if(ops.size()!=2)error("three-way compare intrinsic requires two operands");
            const auto& source=ops.at(0).at("type");
            const auto& other=ops.at(1).at("type");
            const auto& target=i.at("type");
            const bool vector=source.value("kind","")=="vector";
            const auto& input=vector?source.at("element"):source;
            if(input.value("kind","")!="int" || source!=other ||
               (vector?(target.value("kind","")!="vector" || target.value("count",0u)!=source.value("count",0u)):
                       target.value("kind","")!="int"))
                error("three-way compare intrinsic requires matching integer operands and scalar/vector result shape");
            const auto& output=vector?target.at("element"):target;
            if(output.value("kind","")!="int" || bits_of(output)<2)
                error("three-way compare intrinsic result must be an integer of at least 2 bits");
            const unsigned width=bits_of(input),result_width=bits_of(output),lanes=vector?source.at("count").get<unsigned>():1;
            auto left=snapshot(value(ops.at(0),out),out),right=snapshot(value(ops.at(1),out),out);
            Bytes packed=zeros(size_of(target));
            for(unsigned lane=0;lane<lanes;++lane) {
                auto a=vector?element(left,lane,width):left,b=vector?element(right,lane,width):right;
                const auto less=save(accept(numeric.compare(starts(name,"llvm.scmp.")?"slt":"ult",width,a,b),out).at(0),out);
                const auto greater=save(accept(numeric.compare(starts(name,"llvm.scmp.")?"sgt":"ugt",width,a,b),out).at(0),out);
                Bytes result((result_width+7)/8,mul(less,255));
                result[0]=add(result[0],greater);
                if(result_width%8)result.back()=mod(result.back(),1u<<(result_width%8));
                if(vector)pack(packed,result,lane*result_width,result_width);
                else packed=std::move(result);
            }
            store_result(i,packed,out);return true;
        }
        if(starts(name,"llvm.")) {
            std::vector<Bytes> args;for(const auto& a:ops)args.push_back(value(a,out));
            if(starts(name,"llvm.experimental.cttz.elts.")) {
                if(ops.size()!=2 || ops.at(0).at("type").value("kind","")!="vector" ||
                   i.at("type").value("kind","")!="int")error("unsupported cttz.elts signature");
                const auto& source=ops.at(0).at("type");
                unsigned count=source.at("count"),width=bits_of(source.at("element"));
                const auto index=save(Expr(count),out);
                for(unsigned lane=count;lane-- >0;) {
                    auto nonzero=accept(numeric.compare("ne",width,element(args[0],lane,width),zeros((width+7)/8)),out).at(0);
                    out.push_back(iff(nonzero,{set(index.at("fields").at("VARIABLE").at(0),lane)}));
                }
                out.push_back(iff(land(args[1].at(0),eq(index,count)),trap("cttz.elts zero poison")));
                store_result(i,from_small(index,size_of(i.at("type"))),out);return true;
            }
            if(starts(name,"llvm.vector.reduce.")) {
                if(ops.empty() || ops.at(0).at("type").value("kind","")!="vector")error("unsupported reduction signature");
                const auto& source=ops.at(0).at("type");unsigned count=source.at("count"),width=bits_of(source.at("element"));
                std::string op=name.substr(std::string("llvm.vector.reduce.").size());op=op.substr(0,op.find('.'));
                Bytes result=snapshot(element(args[0],0,width),out);
                for(unsigned lane=1;lane<count;++lane) {
                    auto next=element(args[0],lane,width);
                    if(op=="add" || op=="mul" || op=="and" || op=="or" || op=="xor")result=accept(numeric.binary(op,width,result,next),out);
                    else result=accept(numeric.intrinsic(op,width,{result,next}),out);
                }
                store_result(i,result,out);return true;
            }
            if(!ops.empty() && ops.at(0).at("type").value("kind","")=="vector") {
                const auto& source=ops.at(0).at("type");unsigned count=source.at("count"),width=bits_of(source.at("element"));
                const auto& result_type=i.at("type");bool tuple=result_type.value("kind","")=="struct";
                const auto& vector_type=tuple?result_type.at("fields").at(0):result_type;
                if(vector_type.value("kind","")!="vector")error("unsupported vector intrinsic result shape: "+name);
                unsigned result_width=bits_of(vector_type.at("element"));Bytes packed=zeros(size_of(vector_type));
                Bytes flags=tuple?zeros(size_of(result_type.at("fields").at(1))):Bytes{};
                for(unsigned lane=0;lane<count;++lane) {
                    std::vector<Bytes> lane_args;
                    for(unsigned a=0;a<args.size();++a) {
                        const auto& at=ops.at(a).at("type");
                        lane_args.push_back(at.value("kind","")=="vector"?element(args[a],lane,bits_of(at.at("element"))):args[a]);
                    }
                    auto r=accept(numeric.intrinsic(name,width,lane_args),out);
                    if(tuple) {if(r.size()!=(result_width+7)/8+1)error("unsupported vector intrinsic tuple layout");pack(flags,Bytes{r.back()},lane,1);r.pop_back();}
                    pack(packed,r,lane*result_width,result_width);
                }
                if(tuple) {
                    Bytes result=zeros(size_of(result_type));auto off0=result_type.at("offsets").at(0).get<unsigned>(),off1=result_type.at("offsets").at(1).get<unsigned>();
                    std::copy(packed.begin(),packed.end(),result.begin()+off0);std::copy(flags.begin(),flags.end(),result.begin()+off1);store_result(i,result,out);
                }else store_result(i,packed,out);
                return true;
            }
            unsigned bits=ops.empty()?bits_of(i.at("type")):bits_of(ops.at(0).at("type"));
            auto result=accept(numeric.intrinsic(name,bits,args),out);
            if(i.at("type").value("kind","")=="struct" && i.at("type").at("fields").size()==2 && result.size()==size_of(i.at("type").at("fields").at(0))+1) {
                Bytes padded=zeros(size_of(i.at("type")));auto n=size_of(i.at("type").at("fields").at(0));
                std::copy(result.begin(),result.begin()+n,padded.begin());padded[i.at("type").at("offsets").at(1).get<unsigned>()]=result.back();result=std::move(padded);
            }
            store_result(i,result,out);return true;
        }
        return false;
    }
    void invoke(const Json& i,Script& out) {
        if(i.contains("asm")) {if(building_runtime || i.at("asm").at("template").get<std::string>().find_first_not_of(" \t\r\n")==std::string::npos)assembly(i,out);else share_instruction(i,out);return;}
        const auto& callee=i.at("callee");const auto& args=i.at("operands");
        std::string name=callee.value("kind","")=="symbol"?callee.value("name",""):"";
        if(!building_runtime && starts(name,"llvm.")) {
            if(starts(name,"llvm.lifetime.") || starts(name,"llvm.dbg.") || starts(name,"llvm.assume"))return;
            if(starts(name,"llvm.invariant.start.")){store_result(i,zeros(size_of(i.at("type"))),out);return;}
            if(starts(name,"llvm.invariant.end."))return;
            if(starts(name,"llvm.trap") || starts(name,"llvm.debugtrap")){extend(out,trap(name));return;}
            if(starts(name,"llvm.memcpy") || starts(name,"llvm.memmove")) {
                auto d=operand_slot(args.at(0),out),s=operand_slot(args.at(1),out),n=operand_slot(args.at(2),out);
                out.push_back(starts(name,"llvm.memmove")?slot_memory.memmove_slot(d,s,n,size_of(args.at(2).at("type"))):slot_memory.memcpy_slot(d,s,n,size_of(args.at(2).at("type"))));return;
            }
            if(starts(name,"llvm.memset")) {
                auto p=operand_slot(args.at(0),out),byte=operand_slot(args.at(1),out),n=operand_slot(args.at(2),out);
                out.push_back(slot_memory.memset_slot(p,item(MEMORY,byte),n,size_of(args.at(2).at("type"))));return;
            }
            if(starts(name,"llvm.expect.")) {slot_copy(address_of(i.at("id")),operand_slot(args.at(0),out),size_of(i.at("type")),out);return;}
            if(starts(name,"llvm.va_end"))return;
            if(starts(name,"llvm.stackrestore")) {out.push_back(slot_memory.stack_restore(operand_slot(args.at(0),out),frame()));return;}
            if(starts(name,"llvm.stacksave")) {out.push_back(slot_memory.encode(address_of(i.at("id")),var(SP)));return;}
            static const std::vector<std::string> numeric_prefixes={"llvm.ctlz.","llvm.cttz.","llvm.ctpop.","llvm.bswap.","llvm.bitreverse.","llvm.fshl.","llvm.fshr.","llvm.abs.","llvm.smin.","llvm.smax.","llvm.umin.","llvm.umax.","llvm.uadd.","llvm.sadd.","llvm.usub.","llvm.ssub.","llvm.umul.","llvm.smul.","llvm.ushl.sat.","llvm.sshl.sat."};
            if(!args.empty() && args.at(0).at("type").value("kind","")!="vector" &&
               std::any_of(numeric_prefixes.begin(),numeric_prefixes.end(),[&](const auto& prefix){return starts(name,prefix);})) {
                std::vector<Expr> slots;std::vector<unsigned> lengths;
                for(const auto& a:args){slots.push_back(operand_slot(a,out));lengths.push_back(size_of(a.at("type")));}
                auto destination=address_of(i.at("id"));unsigned bits=bits_of(args.at(0).at("type"));
                out.push_back(slot_numeric.intrinsic(name,bits,destination,slots,lengths));
                if(i.at("type").value("kind","")=="struct") {
                    const unsigned bytes=(bits+7)/8,flag=i.at("type").at("offsets").at(1);
                    if(flag!=bytes){slot_copy(add(destination,flag),add(destination,bytes),1,out);out.push_back(slot_memory.fill(add(destination,bytes),0,flag-bytes));}
                    if(size_of(i.at("type"))>flag+1)out.push_back(slot_memory.fill(add(destination,flag+1),0,size_of(i.at("type"))-flag-1));
                }
                return;
            }
            share_instruction(i,out);return;
        }
        if(!name.empty() && intrinsic(i,name,out))return;
        if(i.value("vararg",false) && i.value("tail_kind","")=="musttail")error("musttail forwarding of variadic arguments is not implemented yet");
        if(i.value("tail_kind","")=="musttail") {
            auto target=slot_memory.decode(operand_slot(callee,out),out,"tail-call target");
            std::vector<Expr> sources;unsigned pack_size=0;
            for(const auto& a:args){sources.push_back(operand_slot(a,out));pack_size+=size_of(a.at("type"));}
            unsigned copy_offset=align_up(pack_size,16),arg_offset=0;
            for(unsigned index=0;index<args.size();++index) {
                const auto attrs=i.value("arg_attrs",Json::array());
                if(index<attrs.size() && attrs[index].contains("byval")) {
                    const auto& t=attrs[index].at("byval");unsigned n=t.value("alloc_size",size_of(t));
                    copy_offset=align_up(copy_offset,attrs[index].value("align",t.value("align",1u)));
                    auto src=slot_memory.decode(sources[index],out);out.push_back(slot_memory.check_access(src,n));
                    copy_memory(tail_args+copy_offset,src,n,out,false);
                    out.push_back(slot_memory.encode(tail_args+arg_offset,tail_args+copy_offset));copy_offset+=n;
                }else {
                    slot_copy(tail_args+arg_offset,sources[index],size_of(args[index].at("type")),out);
                }
                arg_offset+=size_of(args[index].at("type"));
            }
            out.push_back(set("__scl_tail_id",target));out.push_back(slot_memory.fill(frame(),0,4u));return;
        }
        const bool direct=!name.empty() && functions.count(name);
        if(direct) {
            const auto& fn=functions.at(name);
            if(args.size()<fn.source->at("args").size() || (!fn.source->value("vararg",false) && args.size()!=fn.source->at("args").size()))error("call signature argument count mismatch");
        }
        Json model=i;model.erase("id");model.erase("callee");model.erase("debug");model.erase("debug_origin");model["op"]="marshal_call";
        model["target"]=direct?Json{{"kind","direct"},{"name",name}}:Json{{"kind","indirect"}};
        std::vector<std::string> parameters={"out","D"};
        std::vector<Expr> actual={add(frame(),current->outgoing),size_of(i.at("type"))?address_of(i.at("id")):Expr(boot_result)};
        if(!direct){parameters.push_back("target");actual.push_back(operand_slot(callee,out));}
        for(unsigned n=0;n<args.size();++n) {
            auto parameter="A"+std::to_string(n);parameters.push_back(parameter);actual.push_back(operand_slot(args[n],out));
            model["operands"][n]={{"kind","runtime_slot"},{"type",args[n].at("type")},{"address",arg(parameter)}};
        }
        const auto key=model.dump();auto found=runtime_operations.find(key);std::string procedure;
        if(found==runtime_operations.end()) {
            procedure="__scl_rt_call_"+std::to_string(runtime_operations.size());runtime_operations[key]=procedure;
            Script body;Expr target=0;if(!direct)target=slot_memory.decode(arg("target"),body,"function pointer");
            if(i.value("vararg",false))shared_varargs_call(model,arg("out"),body);
            else {
                unsigned offset=0;
                for(unsigned n=0;n<args.size();++n){slot_copy(add(arg("out"),offset),arg("A"+std::to_string(n)),size_of(args[n].at("type")),body);offset+=size_of(args[n].at("type"));}
            }
            if(direct)body.push_back(call(functions.at(name).entry,{arg("out"),arg("D")}));
            else body.push_back(call("__scl_dispatch",{target,arg("out"),arg("D")}));
            // Nothing in this adapter is read after the guest call: nested or
            // recursive calls may reuse leaf-runtime scratch registers safely.
            project.procedure(procedure,parameters,std::move(body));
        }else procedure=found->second;
        out.push_back(call(procedure,actual));
    }
    void assembly(const Json& i,Script& out) {
        std::vector<Json> types;std::vector<Bytes> operands;
        for(const auto& operand:i.at("operands")){types.push_back(operand.at("type"));operands.push_back(value(operand,out));}
        auto result=emit_assembly(i.at("asm"),i.at("type"),types,operands,project);
        extend(out,result.code);store_result(i,result.bytes,out);
    }
    void instruction(const Json& i,Script& out) {
        const std::string op=i.at("op");const auto& ops=i.at("operands");
        if(op=="phi" || op=="fence")return;
        if(!building_runtime) {
            const auto& type=i.at("type");
            static const std::set<std::string> binary={"add","sub","mul","udiv","sdiv","urem","srem","and","or","xor","shl","lshr","ashr"};
            static const std::set<std::string> casts={"trunc","zext","sext","ptrtoint","inttoptr"};
            const bool scalar=!ops.empty() && ops.at(0).at("type").value("kind","")!="vector";
            static const std::set<std::string> foldable={"gep","getelementptr","add","sub","mul","udiv","sdiv","urem","srem","and","or","xor","shl","lshr","ashr","icmp","trunc","zext","sext","ptrtoint","inttoptr","bitcast","freeze","select","extractvalue","insertvalue","extractelement","insertelement","shufflevector"};
            if(foldable.count(op) && !contains_ssa_reference(i)) {
                Json constant=i;constant["kind"]="constexpr";
                slot_copy(address_of(i.at("id")),operand_slot(constant,out),size_of(type),out);return;
            }
            if(op=="load") {
                out.push_back(slot_memory.load(address_of(i.at("id")),operand_slot(ops.at(0),out),size_of(type),
                    type.value("kind","")=="int" || type.value("kind","")=="vector"?bits_of(type):0));return;
            }
            if(op=="store") {
                const auto& t=ops.at(0).at("type");
                if(ops.at(0).value("kind","")!="ref") {
                    const auto bytes=constant_value(ops.at(0));
                    if(!bytes.empty() && std::all_of(bytes.begin(),bytes.end(),[&](unsigned b){return b==bytes.front();})) {
                        out.push_back(slot_memory.memset(operand_slot(ops.at(1),out),bytes.front(),size_of(t)));return;
                    }
                }
                out.push_back(slot_memory.store(operand_slot(ops.at(1),out),operand_slot(ops.at(0),out),size_of(t),
                    t.value("kind","")=="int" || t.value("kind","")=="vector"?bits_of(t):0));return;
            }
            if(binary.count(op) && scalar) {
                auto equals_constant=[&](const Json& v,unsigned expected) {
                    if(v.value("kind","")=="ref")return false;
                    const auto bytes=constant_value(v);
                    return !bytes.empty() && bytes[0]==expected && std::all_of(bytes.begin()+1,bytes.end(),[](unsigned b){return b==0;});
                };
                const bool right_zero=equals_constant(ops[1],0),right_one=equals_constant(ops[1],1);
                if((right_zero && (op=="add" || op=="sub" || op=="or" || op=="xor" || op=="shl" || op=="lshr" || op=="ashr")) || (right_one && op=="mul")) {
                    slot_copy(address_of(i.at("id")),operand_slot(ops[0],out),size_of(type),out);return;
                }
                if((equals_constant(ops[0],0) && (op=="add" || op=="or" || op=="xor")) || (equals_constant(ops[0],1) && op=="mul")) {
                    slot_copy(address_of(i.at("id")),operand_slot(ops[1],out),size_of(type),out);return;
                }
                if(op=="mul" && (right_zero || equals_constant(ops[0],0))) {out.push_back(slot_memory.fill(address_of(i.at("id")),0,size_of(type)));return;}
                if((op=="shl" || op=="lshr" || op=="ashr") && ops[1].value("kind","")!="ref") {
                    const auto bytes=constant_value(ops[1]);unsigned amount=0,bits=bits_of(type);
                    for(auto it=bytes.rbegin();it!=bytes.rend();++it)amount=static_cast<unsigned>(std::min<uint64_t>(bits,uint64_t(amount)*256+*it));
                    out.push_back(slot_numeric.shift_constant(op,bits,amount,address_of(i.at("id")),operand_slot(ops[0],out)));return;
                }
                if(op=="mul")for(unsigned side=0;side<2;++side)if(ops[side].value("kind","")!="ref") {
                    const auto bytes=constant_value(ops[side]);unsigned shift=0,ones=0;
                    for(unsigned n=0;n<bytes.size();++n)for(unsigned bit=0;bit<8;++bit)if(bytes[n]&(1u<<bit)){++ones;shift=n*8+bit;}
                    if(ones==1){out.push_back(slot_numeric.shift_constant("shl",bits_of(type),shift,address_of(i.at("id")),operand_slot(ops[1-side],out)));return;}
                }
                out.push_back(slot_numeric.binary(op,bits_of(type),address_of(i.at("id")),operand_slot(ops.at(0),out),operand_slot(ops.at(1),out)));return;
            }
            if(op=="icmp" && scalar) {
                out.push_back(slot_numeric.compare(i.at("predicate"),bits_of(ops.at(0).at("type")),address_of(i.at("id")),operand_slot(ops.at(0),out),operand_slot(ops.at(1),out)));return;
            }
            if(casts.count(op) && scalar) {
                const auto from=bits_of(ops.at(0).at("type")),to=bits_of(type);
                if(from==to)slot_copy(address_of(i.at("id")),operand_slot(ops.at(0),out),size_of(type),out);
                else out.push_back(slot_numeric.cast(op,from,to,address_of(i.at("id")),operand_slot(ops.at(0),out)));
                return;
            }
            if(op=="bitcast" || op=="freeze") {
                slot_copy(address_of(i.at("id")),operand_slot(ops.at(0),out),size_of(type),out);return;
            }
            if(op=="extractvalue" && bits_of(type)%8==0) {
                auto source=operand_slot(ops.at(0),out);auto offset=aggregate_offset(ops.at(0).at("type"),i.at("indices"));
                slot_copy(address_of(i.at("id")),offset?add(source,offset):source,size_of(type),out);return;
            }
            if(op!="br" && op!="switch" && op!="indirectbr" && op!="call" && op!="ret" && op!="unreachable") {
                share_instruction(i,out);return;
            }
        }
        if(op=="va_arg"){varargs_argument(i,out);return;}
        if(op=="br") {
            const auto& targets=i.at("targets");
            if(targets.size()==1)edge(targets[0],out);
            else {auto c=save(value(ops.at(0),out).at(0),out);Script yes,no;edge(targets[0],yes);edge(targets[1],no);out.push_back(iff(c,yes,no));}
            return;
        }
        if(op=="indirectbr") {
            auto address=small(value(ops.at(0),out),out,"block address");Script chain=trap("invalid indirect branch target");
            const auto& targets=i.at("targets");
            for(auto target=targets.rbegin();target!=targets.rend();++target) {
                Script path;edge(target->get<std::string>(),path);
                const auto key=std::make_pair(current->name,target->get<std::string>());
                chain={iff(eq(address,block_addresses.at(key)),std::move(path),std::move(chain))};
            }
            extend(out,chain);return;
        }
        if(op=="switch") {
            auto c=operand_slot(ops.at(0),out);Script chain;edge(i.at("default"),chain);
            auto temporary=add(frame(),current->outgoing);
            const auto& cases=i.at("cases");
            for(auto it=cases.rbegin();it!=cases.rend();++it) {
                Script path,compare;auto b=operand_slot(it->at("value"),compare);
                compare.push_back(slot_numeric.compare("eq",bits_of(ops.at(0).at("type")),temporary,c,b));
                edge(it->at("target"),path);compare.push_back(iff(item(MEMORY,temporary),path,chain));chain=std::move(compare);
            }
            extend(out,chain);return;
        }
        if(op=="ret") { if(!ops.empty())slot_copy(arg("result"),operand_slot(ops.at(0),out),size_of(ops.at(0).at("type")),out);out.push_back(slot_memory.fill(frame(),0,4u));return; }
        if(op=="unreachable") {extend(out,trap("reached unreachable"));return;}
        if(op=="call") {invoke(i,out);return;}
        if(op=="alloca") {
            auto count=small(value(ops.at(0),out),out,"allocation count");unsigned size=i.at("allocated_type").value("alloc_size",size_of(i.at("allocated_type")));
            unsigned align=std::max(1u,i.value("align",i.at("allocated_type").value("align",1u)));
            auto next=save(mul(floor_(scratch::div(sub(var(SP),mul(count,size)),align)),align),out,"allocation");
            out.push_back(iff(lt(next,var(HP)),trap("stack exhausted")));out.push_back(set(SP,next));
            store_result(i,from_small(next,size_of(i.at("type"))),out);return;
        }
        if(op=="load") {auto p=checked_address(value(ops.at(0),out),size_of(i.at("type")),out);store_result(i,read(p,size_of(i.at("type"))),out);return;}
        if(op=="store") {auto b=snapshot(value(ops.at(0),out),out);auto p=checked_address(value(ops.at(1),out),static_cast<unsigned>(b.size()),out);write(p,b,out);return;}
        if(op=="atomicrmw" || op=="cmpxchg") {
            auto operand=snapshot(value(ops.at(1),out),out);auto p=checked_address(value(ops.at(0),out),static_cast<unsigned>(operand.size()),out);auto old=snapshot(read(p,static_cast<unsigned>(operand.size())),out);
            if(op=="cmpxchg") {
                auto replacement=snapshot(value(ops.at(2),out),out);auto same=accept(numeric.compare("eq",bits_of(ops.at(1).at("type")),old,operand),out);
                Script yes;write(p,replacement,yes);out.push_back(iff(same.at(0),yes));Bytes result=zeros(size_of(i.at("type")));
                std::copy(old.begin(),old.end(),result.begin());result[i.at("type").at("offsets").at(1).get<unsigned>()]=same.at(0);store_result(i,result,out);
            } else {
                unsigned operation=i.at("operation");Bytes result;
                const unsigned bits=bits_of(i.at("type"));
                if(operation==0)result=operand;
                else if(operation==1)result=accept(numeric.binary("add",bits,old,operand),out);
                else if(operation==2)result=accept(numeric.binary("sub",bits,old,operand),out);
                else if(operation==3 || operation==4) {result=accept(numeric.binary("and",bits,old,operand),out);if(operation==4)result=accept(numeric.binary("xor",bits,result,Bytes(result.size(),Expr(255))),out);}
                else if(operation==5 || operation==6)result=accept(numeric.binary(operation==5?"or":"xor",bits,old,operand),out);
                else if(operation>=7 && operation<=10)result=accept(numeric.intrinsic(operation==7?"smax":operation==8?"smin":operation==9?"umax":"umin",bits,{old,operand}),out);
                else error("unsupported atomicrmw operation");
                write(p,result,out);store_result(i,old,out);
            }
            return;
        }
        store_result(i,operation(i,out),out);
    }
    Script block_tree(const std::vector<Script>& bodies,unsigned first,unsigned last,Expr pc) {
        if(first==last)return bodies[first-1];
        unsigned mid=(first+last)/2;
        return {iff(lnot(gt(pc,mid)),block_tree(bodies,first,mid,pc),block_tree(bodies,mid+1,last,pc))};
    }
    Script dispatch_tree(const std::vector<Function*>& fns,unsigned first,unsigned last) {
        if(first==last) return {iff(eq(arg("id"),fns[first]->address),{call(fns[first]->entry,{arg("args"),arg("result")})},trap("invalid function pointer"))};
        unsigned mid=(first+last)/2;
        return {iff(lnot(gt(arg("id"),fns[mid]->address)),dispatch_tree(fns,first,mid),dispatch_tree(fns,mid+1,last))};
    }
    Script tail_tree(const std::vector<Function*>& fns,unsigned first,unsigned last) {
        if(first==last) {
            auto* f=fns[first];
            Script body={set("__scl_tail_id",0),slot_memory.frame_enter(f->frame_size),call(f->body,{var(SP),tail_args,arg("result")}),slot_memory.frame_leave(f->frame_size)};
            return {iff(eq(arg("id"),f->address),body,trap("invalid tail-call target"))};
        }
        unsigned mid=(first+last)/2;
        return {iff(lnot(gt(arg("id"),fns[mid]->address)),tail_tree(fns,first,mid),tail_tree(fns,mid+1,last))};
    }
    void layout() {
        unsigned code=16,index=0;
        for(const auto& f:module.at("functions")) {
            if(f.value("vararg",false))variadic_header(f);
            Function fn;fn.source=&f;fn.name=f.at("name");fn.entry="__scl_fn_"+std::to_string(index);fn.body="__scl_body_"+std::to_string(index++);
            code=align_up(code,std::max(16u,f.value("align",0u)));fn.address=code;symbols[fn.name]=code;code+=16;
            unsigned slot=4,argoff=0;
            for(const auto& a:f.at("args")) {
                const auto& t=a.at("type");slot=align_up(slot,t.value("align",1u));fn.slots[a.at("id")]={slot,t};slot+=size_of(t);fn.arg_offsets.push_back(argoff);argoff+=size_of(t);
                if(slot>options.memory_size || argoff>options.memory_size)throw Error("function parameters exceed memory capacity: "+fn.name);
                const auto& attrs=a.at("attrs");if(attrs.contains("inalloca") || attrs.contains("preallocated"))error("inalloca/preallocated are not yet lowered");
            }
            unsigned blockid=1;
            for(const auto& b:f.at("blocks")) {
                fn.block_ids[b.at("id")]=blockid++;fn.blocks[b.at("id")]=&b;
                for(const auto& i:b.at("instructions")) {
                    const auto& t=i.at("type");if(size_of(t)){slot=align_up(slot,t.value("align",1u));fn.slots[i.at("id")]={slot,t};slot+=size_of(t);}
                    if(slot>options.memory_size)throw Error("SSA frame exceeds memory capacity: "+fn.name);
                    if(i.at("op")=="call") {
                        unsigned n=0;for(const auto& a:i.at("operands")){n+=size_of(a.at("type"));if(n>options.memory_size)throw Error("call arguments exceed memory capacity");}
                        if(i.value("vararg",false) && !i.contains("asm"))n=varargs_layout(i).size;
                        fn.outgoing_size=std::max(fn.outgoing_size,n);
                        if(i.value("tail_kind","")=="musttail") {
                            unsigned total=align_up(n,16);
                            for(const auto& attrs:i.value("arg_attrs",Json::array())) if(attrs.contains("byval")) {
                                const auto& byval_type=attrs.at("byval");total=align_up(total,attrs.value("align",byval_type.value("align",1u)));total+=byval_type.value("alloc_size",size_of(byval_type));
                            }
                            tail_size=std::max(tail_size,total);
                        }
                    }
                }
            }
            std::map<std::string,std::pair<std::string,unsigned>> views;
            for(const auto& block:f.at("blocks"))for(const auto& inst:block.at("instructions")) {
                const auto& operands=inst.at("operands");if(operands.empty() || operands[0].value("kind","")!="ref")continue;
                const std::string op=inst.at("op");const auto& type=inst.at("type");const auto& source=operands[0].at("type");
                if((op=="bitcast" || op=="ptrtoint" || op=="inttoptr" || op=="freeze") && bits_of(type)==bits_of(source) && size_of(type)==size_of(source))
                    views[inst.at("id")]={operands[0].at("id"),0};
                else if(op=="extractvalue" && bits_of(type)%8==0)
                    views[inst.at("id")]={operands[0].at("id"),aggregate_offset(source,inst.at("indices"))};
                else if((op=="gep" || op=="getelementptr") && size_of(type)==size_of(source) &&
                    std::all_of(inst.at("gep").begin(),inst.at("gep").end(),[](const Json& step) {
                        if(step.contains("offset"))return step.at("offset")==0;
                        if(step.at("stride")==0)return true;
                        const auto& index=step.at("index");
                        return index.value("kind","")=="bytes" && std::all_of(index.at("bytes").begin(),index.at("bytes").end(),[](const Json& byte){return byte==0;});
                    }))views[inst.at("id")]={operands[0].at("id"),0};
            }
            std::set<std::string> resolving;
            for(const auto& block:f.at("blocks"))for(const auto& inst:block.at("instructions")) {
                if(inst.at("op")!="phi")continue;
                const auto& incoming=inst.at("incoming");if(incoming.empty())continue;
                const auto& first=incoming[0].at("value");if(first.value("kind","")!="ref")continue;
                const std::string source=first.at("id"),destination=inst.at("id");
                if(!std::all_of(incoming.begin(),incoming.end(),[&](const Json& edge){return edge.at("value").value("kind","")=="ref" && edge.at("value").at("id")==source;}))continue;
                auto cursor=source;std::set<std::string> visited;
                while(cursor!=destination && views.count(cursor) && visited.insert(cursor).second)cursor=views.at(cursor).first;
                if(cursor!=destination)views[destination]={source,0};
            }
            std::function<void(const std::string&)> resolve_view=[&](const std::string& id) {
                auto found=views.find(id);if(found==views.end())return;
                if(!resolving.insert(id).second)throw Error("cyclic SSA slot view");
                const auto source=found->second;resolve_view(source.first);
                fn.slots.at(id).offset=fn.slots.at(source.first).offset+source.second;
                views.erase(id);resolving.erase(id);
            };
            while(!views.empty())resolve_view(views.begin()->first);
            fn.outgoing=align_up(slot,16);fn.frame_size=align_up(fn.outgoing+std::max(16u,fn.outgoing_size),16);
            if(fn.frame_size>=options.memory_size)throw Error("function frame exceeds memory capacity: "+fn.name);
            functions.emplace(fn.name,std::move(fn));
        }
        auto reserve_block=[&](const std::string& function,const std::string& block) {
            const auto key=std::make_pair(function,block);
            if(block_addresses.count(key))return;
            if(!functions.count(function) || !functions.at(function).blocks.count(block))throw Error("unknown blockaddress target: "+function+":"+block);
            code=align_up(code,4);block_addresses[key]=code;code+=4;
        };
        std::function<void(const Json&)> collect_blocks=[&](const Json& node) {
            if(node.is_object()) {
                if(node.value("kind","")=="blockaddress")reserve_block(node.at("function"),node.at("block"));
                for(const auto& field:node.items())collect_blocks(field.value());
            } else if(node.is_array())for(const auto& entry:node)collect_blocks(entry);
        };
        collect_blocks(module);
        for(const auto& function:module.at("functions"))for(const auto& block:function.at("blocks"))for(const auto& instruction:block.at("instructions"))
            if(instruction.at("op")=="indirectbr")for(const auto& target:instruction.at("targets"))reserve_block(function.at("name"),target);
        for(const auto& d:module.at("declarations")) {declarations[d.at("name")]=&d;if(d.value("linkage",0u)==12)symbols[d.at("name")]=0;}
        for(const auto& a:module.at("aliases"))aliases[a.at("name")]=a.at("value");
        data_start=align_up(code,16);unsigned cursor=data_start;
        for(const auto& g:module.at("globals")) {
            const auto name=g.at("name").get<std::string>();
            if(g.at("initializer").is_null()) {if(g.value("linkage",0u)==12){symbols[name]=0;continue;}throw Error("unresolved global '"+name+"'");}
            if(starts(name,"llvm.global_"))continue;
            const uint64_t global_size=g.at("size").get<uint64_t>();
            if(global_size>options.memory_size)throw Error("global object exceeds memory capacity: "+name);
            cursor=align_up(cursor,g.value("align",1u));symbols[name]=cursor;
            if(uint64_t(cursor)+global_size>options.memory_size)throw Error("static data exceeds memory capacity");
            cursor+=static_cast<unsigned>(global_size);
        }
        const unsigned result_size=functions.count("main")?std::max(32u,size_of(functions.at("main").source->at("return_type"))):32u;
        boot_args=align_up(cursor,16);boot_result=boot_args+64;tail_args=align_up(boot_result+result_size,16);cursor=align_up(tail_args+tail_size,16);
        if(functions.count("main") && !functions.at("main").source->at("args").empty()) {
            const auto& args=functions.at("main").source->at("args");
            if(args.size()!=2 || args[0].at("type").value("kind","")!="int" || bits_of(args[0].at("type"))!=32 || args[1].at("type").value("kind","")!="pointer")
                throw Error("entry adapter supports main() or main(i32, ptr)");
            argv_address=align_up(cursor,args[1].at("type").value("align",pointer_bytes));
            const uint64_t table_size=(static_cast<uint64_t>(options.program_args.size())+2)*pointer_bytes;
            if(table_size>options.memory_size)throw Error("program argument table exceeds memory capacity");
            cursor=argv_address+static_cast<unsigned>(table_size);
            std::vector<std::string> strings={"program"};strings.insert(strings.end(),options.program_args.begin(),options.program_args.end());
            for(const auto& text:strings) {
                if(text.size()+1>options.memory_size || cursor+text.size()+1>options.memory_size)throw Error("program argument strings exceed memory capacity");
                argument_strings.push_back({cursor,text});cursor+=static_cast<unsigned>(text.size()+1);
            }
        }
        heap_start=align_up(cursor,16);
        if(heap_start+256>=options.memory_size)throw Error("static memory and code addresses exceed configured capacity");
    }
    void emit_function(Function& f) {
        current=&f;Script body;
        body.push_back(slot_memory.encode(frame(),1,4));
        unsigned index=0;
        for(const auto& a:f.source->at("args")) {
            const auto& attrs=a.at("attrs");auto source_slot=add(arg("args"),f.arg_offsets[index++]);
            const auto& type=a.at("type");auto destination=address_of(a.at("id"));
            if(attrs.contains("byval")) {
                const auto& t=attrs.at("byval");unsigned n=t.value("alloc_size",size_of(t)),al=attrs.value("align",t.value("align",1u));
                auto source=slot_memory.decode(source_slot,body);body.push_back(slot_memory.check_access(source,n));
                auto dst=save(mul(floor_(scratch::div(sub(var(SP),n),std::max(1u,al))),std::max(1u,al)),body,"byval");
                body.push_back(iff(lt(dst,var(HP)),trap("stack exhausted")));body.push_back(set(SP,dst));
                body.push_back(slot_memory.copy(dst,source,n));body.push_back(slot_memory.encode(destination,dst));
            }else if((type.value("kind","")=="int" || type.value("kind","")=="vector") && bits_of(type)%8) {
                body.push_back(slot_numeric.cast("trunc",size_of(type)*8,bits_of(type),destination,source_slot));
            }else {
                slot_copy(destination,source_slot,size_of(type),body);
            }
        }
        std::vector<Script> bodies;
        unsigned debug_ordinal=0;
        Json debug_instructions=Json::object();
        for(const auto& b:f.source->at("blocks")) {
            current_block=b.at("id");Script code;
            for(const auto& i:b.at("instructions")) {
                const auto before=code.size();
                try {instruction(i,code);}catch(const Error&){throw;}catch(const std::exception& e){error("instruction "+i.at("op").get<std::string>()+": "+e.what());}
                if(options.debug_info) {
                    Json point=i.value("debug",Json::object());
                    point["function"]=f.name;point["instruction"]=i.at("id");
                    point["block"]=current_block;point["ordinal"]=debug_ordinal++;
                    point["op"]=i.at("op");
                    debug_instructions[i.at("id").get<std::string>()]=point;
                    if(before<code.size())code[before]["debug_point"]=std::move(point);
                }
                if(i.at("op")=="call" && i.value("tail_kind","")=="musttail")break;
            }
            bodies.push_back(outline_script(std::move(code)));
        }
        if(bodies.empty())error("function has no blocks");
        unsigned pc_bytes=1;for(size_t count=bodies.size();count>255;count>>=8)++pc_bytes;
        auto pc=read(frame(),pc_bytes);Expr done=eq(pc[0],0),decoded=pc[0];double factor=256;
        for(unsigned n=1;n<pc_bytes;++n){done=land(done,eq(pc[n],0));decoded=add(decoded,mul(pc[n],factor));factor*=256;}
        auto selector=temp("pc");Script iteration={set(selector,decoded)};
        extend(iteration,block_tree(bodies,1,static_cast<unsigned>(bodies.size()),var(selector)));
        body.push_back(until(done,std::move(iteration)));
        body.push_back(set(SP,frame()));
        // A large CFG can also accumulate many small blocks in its dispatcher.
        // Bound those branches and the prologue, not only individual blocks.
        project.procedure(f.body,{"frame","args","result"},outline_script(std::move(body)));
        Script entry={set("__scl_tail_id",0),slot_memory.frame_enter(f.frame_size),call(f.body,{var(SP),arg("args"),arg("result")}),slot_memory.frame_leave(f.frame_size),iff(lnot(eq(var("__scl_tail_id"),0)),{call("__scl_tail_loop",{arg("result")})})};
        project.procedure(f.entry,{"args","result"},std::move(entry));
        if(options.debug_info) {
            Json slots=Json::object();
            for(const auto& slot:f.slots)slots[slot.first]={{"offset",slot.second.offset},{"type",slot.second.type}};
            project.debug_map["functions"][f.name]={{"entry",f.entry+" %s %s"},{"body",f.body+" %s %s %s"},
                {"frameSize",f.frame_size},{"address",f.address},{"slots",std::move(slots)},
                {"source",f.source->value("debug",Json::object())},
                {"variables",f.source->value("debug_variables",Json::array())},{"instructions",std::move(debug_instructions)}};
        }
        current=nullptr;
    }
public:
    Compiler(const Json& m,BackendOptions o):module(m),options(o),slot_memory(project,m.at("pointer_bytes")),slot_numeric(project),pointer_bytes(m.at("pointer_bytes")){}
    Project run() {
        if(!module.value("little_endian",false))throw Error("only little-endian memory is supported");
        if(options.memory_size<1024 || options.memory_size>200000)throw Error("--memory must be between 1024 and 200000 bytes for vanilla Scratch");
        if(pointer_bytes>16 || pointer_bytes<3)throw Error("unsupported pointer representation width");
        layout();slot_memory.set_bounds(data_start,options.memory_size);if(!functions.count("main"))throw Error("entry function 'main' is not defined");
        if(options.debug_info) {
            project.debug_map={{"schemaVersion",1},{"memoryList",MEMORY},{"pointerBytes",pointer_bytes},
                {"memorySize",options.memory_size},{"functions",Json::object()},{"globals",Json::object()}};
            for(const auto& global:module.at("globals")) {
                const std::string name=global.at("name");
                if(symbols.count(name))project.debug_map["globals"][name]={{"address",symbols.at(name)},
                    {"type",global.value("type",Json::object())},{"size",global.value("size",0u)}};
            }
        }
        for(auto& f:functions)emit_function(f.second);
        heap_start=align_up(heap_start,16);
        std::vector<Function*> ordered;for(auto& f:functions)ordered.push_back(&f.second);
        std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->address<b->address;});
        project.procedure("__scl_dispatch",{"id","args","result"},dispatch_tree(ordered,0,static_cast<unsigned>(ordered.size()-1)));
        project.procedure("__scl_tail_step",{"id","result"},tail_tree(ordered,0,static_cast<unsigned>(ordered.size()-1)));
        project.procedure("__scl_tail_loop",{"result"},{until(eq(var("__scl_tail_id"),0),{call("__scl_tail_step",{var("__scl_tail_id"),arg("result")})})});
        Script boot={set(STATUS,"running"),set("exit_code",0),clear(MEMORY),clear("return_bytes"),repeat(options.memory_size,{append(MEMORY,0)}),set(HP,heap_start),set(SP,(options.memory_size+1)/16*16)};
        std::vector<std::pair<unsigned,unsigned>> initial_data;
        for(const auto& g:module.at("globals")) {
            const std::string name=g.at("name");if(g.at("initializer").is_null() || starts(name,"llvm.global_"))continue;
            // The green flag already zeroes the whole byte memory. Large
            // runtime arenas and callback tables need no per-byte zero blocks.
            const auto bytes=value(g.at("initializer"),boot);
            for(unsigned index=0;index<bytes.size();++index) {
                if(bytes[index].is_number() && bytes[index].get<double>()==0)continue;
                initial_data.push_back({static_cast<unsigned>(symbols.at(name))+index,bytes[index].get<unsigned>()});
            }
        }
        for(const auto& entry:constant_initializers) {
            auto bytes=value(entry.second,boot); // Constant evaluator already normalized partial bytes.
            for(unsigned n=0;n<bytes.size();++n)if(!bytes[n].is_number() || bytes[n]!=0)
                initial_data.push_back({entry.first+n,bytes[n].get<unsigned>()});
        }
        auto initialize_integer=[&](unsigned address,uint64_t value,unsigned bytes) {
            for(unsigned n=0;n<bytes;++n){unsigned byte=n<8?((value>>(n*8))&255u):0;if(byte)initial_data.push_back({address+n,byte});}
        };
        if(argv_address) {
            initialize_integer(boot_args,argument_strings.size(),4);initialize_integer(boot_args+4,argv_address,pointer_bytes);
            for(unsigned n=0;n<argument_strings.size();++n) {
                const auto& entry=argument_strings[n];initialize_integer(argv_address+n*pointer_bytes,entry.first,pointer_bytes);
                for(unsigned j=0;j<entry.second.size();++j)if(static_cast<unsigned char>(entry.second[j]))initial_data.push_back({entry.first+j,static_cast<unsigned char>(entry.second[j])});
            }
        }
        if(initial_data.size()<=64) {
            for(const auto& entry:initial_data)boot.push_back(replace(MEMORY,entry.first,entry.second));
        }else {
            // Constant data are numeric tables, not thousands of sequential
            // blocks. Each table remains within the original Scratch list cap.
            project.lists["__scl_initial_addresses"]=Json::array();project.lists["__scl_initial_bytes"]=Json::array();
            for(const auto& entry:initial_data){project.lists["__scl_initial_addresses"].push_back(entry.first);project.lists["__scl_initial_bytes"].push_back(entry.second);}
            const auto cursor="__scl_initial_cursor";
            project.procedure("__scl_rt_initialize",{}, {set(cursor,1),repeat(initial_data.size(),{
                replace(MEMORY,item("__scl_initial_addresses",var(cursor)),item("__scl_initial_bytes",var(cursor))),set(cursor,add(var(cursor),1))})});
            boot.push_back(call("__scl_rt_initialize",{}));
        }
        auto call_initializers=[&](const std::string& name,bool reverse) {
            for(const auto& g:module.at("globals"))if(g.at("name")==name && g.at("initializer").value("kind","")=="aggregate") {
                std::vector<std::pair<unsigned,Json>> entries;
                for(const auto& e:g.at("initializer").at("elements")) {
                    const auto& fields=e.at("elements");unsigned priority=0,shift=0;
                    for(const auto& b:fields.at(0).at("bytes")){if(shift<32)priority|=b.get<unsigned>()<<shift;shift+=8;}
                    entries.push_back({priority,fields.at(1)});
                }
                std::stable_sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b){return reverse?a.first>b.first:a.first<b.first;});
                for(const auto& e:entries) {
                    const auto bytes=constant_value(e.second);uint64_t address=0;bool high=false;
                    for(unsigned n=0;n<bytes.size();++n){if(n<8)address|=uint64_t(bytes[n])<<(n*8);else high=high || bytes[n]!=0;}
                    if(!address && !high)continue;
                    auto found=std::find_if(functions.begin(),functions.end(),[&](const auto& f){return !high && f.second.address==address;});
                    if(found!=functions.end())boot.push_back(call(found->second.entry,{boot_args,boot_result}));
                    else extend(boot,trap("invalid initializer function pointer"));
                }
            }
        };
        call_initializers("llvm.global_ctors",false);
        const auto& main=functions.at("main");
        boot.push_back(call(main.entry,{boot_args,boot_result}));
        unsigned n=size_of(main.source->at("return_type"));for(unsigned i=0;i<n;++i)boot.push_back(append("return_bytes",item(MEMORY,boot_result+i)));
        if(n<=4) {
            Expr code=0;double factor=1;for(unsigned i=0;i<n;++i){code=add(code,mul(item(MEMORY,boot_result+i),factor));factor*=256;}
            boot.push_back(set("exit_code",code));
        }
        call_initializers("llvm.global_dtors",true);boot.push_back(set(STATUS,"done"));
        project.procedure("__scl_start",{},std::move(boot));project.green_flag({call("__scl_start",{})});
        numeric.install(project);slot_numeric.install(project);project.variables[STATUS]="ready";project.variables["exit_code"]=0;
        return std::move(project);
    }
};
}
Project compile(const Json& module,const BackendOptions& options){return Compiler(module,options).run();}
}

