    // System V AMD64 va_list is observable ordinary memory:
    // { i32 gp_offset, i32 fp_offset, ptr overflow_arg_area, ptr reg_save_area }.
    // Keep packed named arguments at offset zero for the normal function ABI;
    // append the descriptor, register save area, and stack argument area.
    struct VarargsLayout {
        unsigned named = 0, named_bytes = 0, header = 0, registers = 0, overflow = 0;
        unsigned named_gp = 0, named_fp = 0, named_overflow = 0, size = 0;
        std::vector<unsigned> offsets;
    };
    void require_sysv_varargs(unsigned convention = 0) {
        const auto target = module.value("target", "");
        const bool architecture = starts(target,"x86_64-") || starts(target,"amd64-");
        const bool unix_abi = target.find("linux")!=std::string::npos || target.find("freebsd")!=std::string::npos ||
            target.find("netbsd")!=std::string::npos || target.find("openbsd")!=std::string::npos ||
            target.find("dragonfly")!=std::string::npos || target.find("darwin")!=std::string::npos ||
            target.find("apple")!=std::string::npos || target.find("solaris")!=std::string::npos;
        // An explicit sysvcc is accepted only on a supported target; Win64 and
        // targetless inputs must never silently receive a SysV va_list.
        if(!architecture || !unix_abi || pointer_bytes!=8 || (convention!=0 && convention!=78))
            error("variadic ABI requires a supported x86_64 System V target with 64-bit pointers and ccc/sysvcc; target '"+target+"' is unsupported");
    }
    bool varargs_fp(const Json& type, const Json& attrs = Json::object()) {
        for(const auto* name:{"byval","byref","inalloca","preallocated","inreg","nest","swiftself","swifterror"})
            if(attrs.contains(name)) error(std::string("variadic ABI does not yet support parameter attribute '")+name+"'");
        const auto kind=type.value("kind","");const auto bits=bits_of(type);
        if(type.value("align",1u)>8)error("variadic ABI does not yet support scalar alignment greater than 8");
        if((kind=="int" && bits>=1 && bits<=64) || (kind=="pointer" && size_of(type)==8))return false;
        if(kind=="float" && (bits==32 || bits==64))return true;
        error("variadic ABI currently supports only scalar integers through i64, pointers, float and double; aggregate/vector/wide arguments require ABI classification");
        return false;
    }
    unsigned variadic_header(const Json& function) {
        require_sysv_varargs(function.value("calling_convention",0u));
        unsigned bytes=0;
        for(const auto& a:function.at("args")) {varargs_fp(a.at("type"),a.value("attrs",Json::object()));bytes+=size_of(a.at("type"));}
        return align_up(bytes,16);
    }
    VarargsLayout varargs_layout(const Json& instruction) {
        require_sysv_varargs(instruction.value("calling_convention",0u));
        if(!instruction.contains("named_arg_count"))error("variadic call is missing its fixed-parameter signature");
        if(instruction.value("tail_kind","")=="musttail")error("musttail forwarding of variadic arguments is not implemented yet");
        VarargsLayout layout;layout.named=instruction.at("named_arg_count");
        const auto& operands=instruction.at("operands");
        const auto attrs=instruction.value("arg_attrs",Json::array());
        if(layout.named>operands.size())error("variadic call has fewer arguments than fixed parameters");
        for(unsigned n=0;n<layout.named;++n)layout.named_bytes+=size_of(operands[n].at("type"));
        layout.header=align_up(layout.named_bytes,16);layout.registers=layout.header+32;
        layout.overflow=layout.registers+176;
        unsigned gp=0,fp=0,overflow=0;
        for(unsigned n=0;n<operands.size();++n) {
            const bool floating=varargs_fp(operands[n].at("type"),n<attrs.size()?attrs[n]:Json::object());
            if(floating && fp<8)layout.offsets.push_back(layout.registers+48+16*fp++);
            else if(!floating && gp<6)layout.offsets.push_back(layout.registers+8*gp++);
            else {layout.offsets.push_back(layout.overflow+overflow);overflow+=8;}
            if(n+1==layout.named) {layout.named_gp=gp;layout.named_fp=fp;layout.named_overflow=overflow;}
        }
        layout.size=align_up(layout.overflow+overflow,16);
        return layout;
    }
    void write_varargs_call(const Json& instruction,Expr base,Script& out) {
        const auto layout=varargs_layout(instruction);const auto& operands=instruction.at("operands");
        unsigned packed=0;
        // Unused register bytes have unspecified contents; concretize them to
        // zero. This also defines padding for narrow scalar arguments.
        write(add(base,layout.registers),zeros(layout.size-layout.registers),out);
        for(unsigned n=0;n<operands.size();++n) {
            auto source=operand_slot(operands[n],out);unsigned bytes=size_of(operands[n].at("type"));
            if(n<layout.named) {slot_copy(add(base,packed),source,bytes,out);packed+=bytes;}
            slot_copy(add(base,layout.offsets[n]),source,bytes,out);
        }
        out.push_back(slot_memory.encode(add(base,layout.header),layout.named_gp*8,4));
        out.push_back(slot_memory.encode(add(base,layout.header+4),48+layout.named_fp*16,4));
        out.push_back(slot_memory.encode(add(base,layout.header+8),add(base,layout.overflow+layout.named_overflow),8));
        out.push_back(slot_memory.encode(add(base,layout.header+16),add(base,layout.registers),8));
    }
    void shared_varargs_call(const Json& instruction,Expr destination,Script& out) {
        Json model=instruction;model.erase("id");model.erase("callee");model["op"]="pack_varargs";
        std::vector<Expr> arguments={destination};std::vector<std::string> parameters={"D"};
        for(unsigned n=0;n<model.at("operands").size();++n) {
            arguments.push_back(operand_slot(instruction.at("operands")[n],out));
            auto name="A"+std::to_string(n);parameters.push_back(name);
            model["operands"][n]={{"kind","runtime_slot"},{"type",instruction.at("operands")[n].at("type")},{"address",arg(name)}};
        }
        const auto key=model.dump();auto found=runtime_operations.find(key);std::string name;
        if(found==runtime_operations.end()) {
            name="__scl_rt_va_pack_"+std::to_string(runtime_operations.size());runtime_operations[key]=name;
            Script body;write_varargs_call(model,arg("D"),body);project.procedure(name,parameters,std::move(body));
        }else name=found->second;
        out.push_back(call(name,arguments));
    }
    bool varargs_intrinsic(const Json& instruction,const std::string& name,Script& out) {
        if(!starts(name,"llvm.va_start") && !starts(name,"llvm.va_copy") && !starts(name,"llvm.va_end"))return false;
        require_sysv_varargs();
        const auto& operands=instruction.at("operands");
        if(starts(name,"llvm.va_end"))return true;
        auto destination=checked_address(value(operands.at(0),out),24,out);
        if(starts(name,"llvm.va_start")) {
            if(!current->source->value("vararg",false))error("va_start requires a variadic function");
            const unsigned header=variadic_header(*current->source);
            copy_memory(destination,add(arg("args"),header),24,out,false);
        } else {
            auto source=checked_address(value(operands.at(1),out),24,out);
            copy_memory(destination,source,24,out,true);
        }
        return true;
    }
    void varargs_argument(const Json& instruction,Script& out) {
        require_sysv_varargs();
        const auto& type=instruction.at("type");const bool floating=varargs_fp(type);
        auto descriptor=checked_address(value(instruction.at("operands").at(0),out),24,out);
        auto field=save(add(descriptor,floating?4:0),out,"va_field");
        auto offset=small(read(field,4),out,"va_list register offset");
        Script registers,overflow;
        auto save_area=small(read(add(descriptor,16),8),registers,"va_list register save area");
        auto reg_address=checked_address(from_small(add(save_area,offset),8),size_of(type),registers);
        auto register_value=snapshot(read(reg_address,size_of(type)),registers);
        write(field,from_small(add(offset,floating?16:8),4),registers);
        store_result(instruction,canonical(register_value,type),registers);
        auto stack_address=checked_address(read(add(descriptor,8),8),size_of(type),overflow);
        auto stack_value=snapshot(read(stack_address,size_of(type)),overflow);
        write(add(descriptor,8),from_small(add(stack_address,8),8),overflow);
        store_result(instruction,canonical(stack_value,type),overflow);
        out.push_back(iff(lnot(gt(offset,floating?160:40)),registers,overflow));
    }
