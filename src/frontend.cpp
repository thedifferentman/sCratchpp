#include "scratch/frontend.hpp"

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Error.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

namespace scratch {
namespace {

std::string take_message(char* message) {
    if (!message) return {};
    std::string result(message);
    LLVMDisposeMessage(message);
    return result;
}

std::string value_text(LLVMValueRef value) {
    return take_message(LLVMPrintValueToString(value));
}

std::string value_name(LLVMValueRef value) {
    size_t length = 0;
    const char* name = LLVMGetValueName2(value, &length);
    return std::string(name, length);
}

[[noreturn]] void unsupported(const std::string& reason, LLVMValueRef value = nullptr) {
    throw Error("LLVM frontend: " + reason + (value ? "\n  " + value_text(value) : ""));
}

std::string linkage_name(LLVMLinkage linkage) {
    switch (linkage) {
        case LLVMExternalLinkage: return "external";
        case LLVMAvailableExternallyLinkage: return "available_externally";
        case LLVMLinkOnceAnyLinkage: return "linkonce";
        case LLVMLinkOnceODRLinkage: return "linkonce_odr";
        case LLVMWeakAnyLinkage: return "weak";
        case LLVMWeakODRLinkage: return "weak_odr";
        case LLVMAppendingLinkage: return "appending";
        case LLVMInternalLinkage: return "internal";
        case LLVMPrivateLinkage: return "private";
        case LLVMExternalWeakLinkage: return "extern_weak";
        case LLVMCommonLinkage: return "common";
        default: return "linkage_" + std::to_string(static_cast<unsigned>(linkage));
    }
}

struct ContextOwner {
    std::string diagnostics;
    LLVMContextRef value = LLVMContextCreate();
    ContextOwner() {
        LLVMContextSetDiagnosticHandler(value, [](LLVMDiagnosticInfoRef info, void* opaque) {
            auto& self = *static_cast<ContextOwner*>(opaque);
            std::string message = take_message(LLVMGetDiagInfoDescription(info));
            if (!self.diagnostics.empty()) self.diagnostics += "\n";
            self.diagnostics += message;
        }, this);
    }
    ~ContextOwner() { LLVMContextDispose(value); }
};
struct ModuleOwner {
    LLVMModuleRef value = nullptr;
    ~ModuleOwner() { if (value) LLVMDisposeModule(value); }
};
struct LayoutOwner {
    LLVMTargetDataRef value = nullptr;
    ~LayoutOwner() { if (value) LLVMDisposeTargetData(value); }
};

unsigned pointer_index_bits(LLVMTargetDataRef layout, const std::string& specification) {
    unsigned result = LLVMPointerSizeForAS(layout, 0) * 8;
    std::istringstream specifications(specification);
    std::string part;
    while (std::getline(specifications, part, '-')) {
        if (part.rfind("p:", 0) != 0 && part.rfind("p0:", 0) != 0) continue;
        std::istringstream components(part);
        std::string component;
        std::vector<std::string> fields;
        while (std::getline(components, component, ':')) fields.push_back(component);
        if (fields.size() >= 5) result = static_cast<unsigned>(std::stoul(fields[4]));
    }
    return result;
}

// Only compiler-owned portable integer runtimes may have their layout string
// normalized. User modules must still match exactly. Compare every reachable
// LLVM type under both layouts, including types hidden behind opaque-pointer
// alloca/GEP operations and typed ABI attributes, before making the change.
void verify_runtime_layout(LLVMModuleRef runtime, const std::string& desired, const std::string& path) {
    LayoutOwner source, target;
    const std::string original = LLVMGetDataLayoutStr(runtime);
    source.value = LLVMCreateTargetData(original.c_str());
    target.value = LLVMCreateTargetData(desired.c_str());
    auto fail = [&](const std::string& reason) {
        throw Error("runtime module '" + path + "' is not layout-compatible: " + reason);
    };
    if (LLVMByteOrder(source.value) != LLVMLittleEndian || LLVMByteOrder(target.value) != LLVMLittleEndian)
        fail("only little-endian runtime adaptation is supported");
    if (LLVMPointerSizeForAS(source.value, 0) != LLVMPointerSizeForAS(target.value, 0) ||
        pointer_index_bits(source.value, original) != pointer_index_bits(target.value, desired))
        fail("pointer representation or GEP index width differs");

    std::set<LLVMTypeRef> checked_types;
    std::function<void(LLVMTypeRef)> check_type = [&](LLVMTypeRef type) {
        if (!checked_types.insert(type).second) return;
        const auto kind = LLVMGetTypeKind(type);
        if (kind == LLVMFunctionTypeKind) {
            if (LLVMIsFunctionVarArg(type)) fail("variadic functions cannot be layout-normalized");
            check_type(LLVMGetReturnType(type));
            std::vector<LLVMTypeRef> args(LLVMCountParamTypes(type));
            LLVMGetParamTypes(type, args.data());
            for (auto arg : args) check_type(arg);
            return;
        }
        if (kind == LLVMVoidTypeKind || kind == LLVMLabelTypeKind || kind == LLVMMetadataTypeKind) return;
        if (kind == LLVMPointerTypeKind && LLVMGetPointerAddressSpace(type))
            fail("nonzero pointer address spaces require an explicit ABI adapter");
        if (kind == LLVMScalableVectorTypeKind || !LLVMTypeIsSized(type))
            fail("unsized or scalable runtime types are not supported");
        if (LLVMSizeOfTypeInBits(source.value, type) != LLVMSizeOfTypeInBits(target.value, type) ||
            LLVMStoreSizeOfType(source.value, type) != LLVMStoreSizeOfType(target.value, type) ||
            LLVMABISizeOfType(source.value, type) != LLVMABISizeOfType(target.value, type) ||
            LLVMABIAlignmentOfType(source.value, type) != LLVMABIAlignmentOfType(target.value, type))
            fail("size or alignment differs for " + take_message(LLVMPrintTypeToString(type)));
        if (kind == LLVMArrayTypeKind || kind == LLVMVectorTypeKind) check_type(LLVMGetElementType(type));
        if (kind == LLVMStructTypeKind) {
            for (unsigned i = 0; i < LLVMCountStructElementTypes(type); ++i) {
                if (LLVMOffsetOfElement(source.value, type, i) != LLVMOffsetOfElement(target.value, type, i))
                    fail("structure field offsets differ");
                check_type(LLVMStructGetTypeAtIndex(type, i));
            }
        }
    };
    std::set<LLVMValueRef> checked_values;
    std::function<void(LLVMValueRef)> check_value = [&](LLVMValueRef value) {
        if (!checked_values.insert(value).second) return;
        check_type(LLVMTypeOf(value));
        if (LLVMIsAInlineAsm(value)) fail("inline assembly requires its original target environment");
        if (LLVMIsAFunction(value)) { check_type(LLVMGlobalGetValueType(value)); return; }
        if (LLVMIsAGlobalVariable(value)) { check_type(LLVMGlobalGetValueType(value)); return; }
        if (LLVMIsAArgument(value) || LLVMIsABasicBlock(value)) return;
        if (LLVMIsAInstruction(value)) {
            const auto op = LLVMGetInstructionOpcode(value);
            if (op == LLVMAlloca) check_type(LLVMGetAllocatedType(value));
            if (op == LLVMGetElementPtr) check_type(LLVMGetGEPSourceElementType(value));
            if (op == LLVMCall || op == LLVMInvoke || op == LLVMCallBr) check_type(LLVMGetCalledFunctionType(value));
        } else if (LLVMIsAConstantExpr(value) && LLVMGetConstOpcode(value) == LLVMGetElementPtr) {
            check_type(LLVMGetGEPSourceElementType(value));
        }
        if (LLVMIsAInstruction(value) || LLVMIsAConstant(value))
            for (int i = 0; i < LLVMGetNumOperands(value); ++i) check_value(LLVMGetOperand(value, i));
    };
    auto check_attributes = [&](LLVMValueRef value, bool call, unsigned parameters) {
        for (unsigned n = 0; n <= parameters + 1; ++n) {
            LLVMAttributeIndex index = n == parameters + 1 ? static_cast<LLVMAttributeIndex>(LLVMAttributeFunctionIndex) : n;
            unsigned count = call ? LLVMGetCallSiteAttributeCount(value, index) : LLVMGetAttributeCountAtIndex(value, index);
            std::vector<LLVMAttributeRef> attrs(count);
            if (call) LLVMGetCallSiteAttributes(value, index, attrs.data());
            else LLVMGetAttributesAtIndex(value, index, attrs.data());
            for (auto attr : attrs) if (LLVMIsTypeAttribute(attr)) check_type(LLVMGetTypeAttributeValue(attr));
        }
    };
    size_t asm_length = 0;
    LLVMGetModuleInlineAsm(runtime, &asm_length);
    if (asm_length) fail("module assembly cannot be layout-normalized");
    for (auto global = LLVMGetFirstGlobal(runtime); global; global = LLVMGetNextGlobal(global)) {
        check_value(global);
        if (auto initializer = LLVMGetInitializer(global)) check_value(initializer);
    }
    for (auto alias = LLVMGetFirstGlobalAlias(runtime); alias; alias = LLVMGetNextGlobalAlias(alias))
        check_value(LLVMAliasGetAliasee(alias));
    for (auto function = LLVMGetFirstFunction(runtime); function; function = LLVMGetNextFunction(function)) {
        check_value(function);
        check_attributes(function, false, LLVMCountParams(function));
        for (auto block = LLVMGetFirstBasicBlock(function); block; block = LLVMGetNextBasicBlock(block)) {
            for (auto instruction = LLVMGetFirstInstruction(block); instruction; instruction = LLVMGetNextInstruction(instruction)) {
                check_value(instruction);
                if (LLVMGetInstructionOpcode(instruction) == LLVMCall)
                    check_attributes(instruction, true, LLVMGetNumArgOperands(instruction));
            }
        }
    }
}

void verify(LLVMModuleRef module, const std::string& stage) {
    char* message = nullptr;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &message))
        throw Error("LLVM verification failed " + stage + ":\n" + take_message(message));
    LLVMDisposeMessage(message);
}

bool is_symbol(LLVMValueRef value) {
    return LLVMIsAFunction(value) || LLVMIsAGlobalVariable(value) || LLVMIsAGlobalAlias(value) || LLVMIsAGlobalIFunc(value);
}

void constant_references(LLVMValueRef value, std::set<LLVMValueRef>& references) {
    if (is_symbol(value)) { references.insert(value); return; }
    if (LLVMIsABlockAddress(value)) { references.insert(LLVMGetBlockAddressFunction(value)); return; }
    // Instructions and arguments are SSA edges rather than symbolic edges.
    // Their defining instructions are inspected independently in each block.
    if (!LLVMIsAConstant(value)) return;
    for (int i = 0; i < LLVMGetNumOperands(value); ++i)
        constant_references(LLVMGetOperand(value, static_cast<unsigned>(i)), references);
}

std::set<LLVMValueRef> reachable_values(LLVMModuleRef module, const std::vector<std::string>& roots,
                                       std::vector<std::string>* retained_addresses = nullptr) {
    struct Node { std::set<LLVMValueRef> references; bool indirect = false; };
    std::map<LLVMValueRef, Node> graph;
    std::map<std::string, LLVMValueRef> names;
    std::set<LLVMValueRef> address_taken;
    auto add_references = [&](Node& node, LLVMValueRef operand, bool visible_address) {
        std::set<LLVMValueRef> references;
        constant_references(operand, references);
        node.references.insert(references.begin(), references.end());
        if (visible_address) {
            for (auto reference : references)
                if (LLVMIsAFunction(reference) || LLVMIsAGlobalAlias(reference) || LLVMIsAGlobalIFunc(reference))
                    address_taken.insert(reference);
        }
    };
    for (auto function = LLVMGetFirstFunction(module); function; function = LLVMGetNextFunction(function)) {
        names[value_name(function)] = function;
        auto& node = graph[function];
        if (LLVMHasPersonalityFn(function)) add_references(node, LLVMGetPersonalityFn(function), false);
        if (LLVMHasPrefixData(function)) add_references(node, LLVMGetPrefixData(function), true);
        if (LLVMHasPrologueData(function)) add_references(node, LLVMGetPrologueData(function), true);
        for (auto block = LLVMGetFirstBasicBlock(function); block; block = LLVMGetNextBasicBlock(block)) {
            for (auto inst = LLVMGetFirstInstruction(block); inst; inst = LLVMGetNextInstruction(inst)) {
                const auto op = LLVMGetInstructionOpcode(inst);
                const bool call = op == LLVMCall || op == LLVMInvoke || op == LLVMCallBr;
                LLVMValueRef callee = call ? LLVMGetCalledValue(inst) : nullptr;
                if (call && !LLVMIsAInlineAsm(callee) && !is_symbol(callee)) node.indirect = true;
                for (int n = 0; n < LLVMGetNumOperands(inst); ++n) {
                    LLVMValueRef operand = LLVMGetOperand(inst, static_cast<unsigned>(n));
                    // The same function constant may occur both as an actual
                    // argument and as the callee (e.g. register_callback(@f)
                    // implemented by @f). Arguments still expose its address.
                    const bool direct_callee = call && n >= static_cast<int>(LLVMGetNumArgOperands(inst)) &&
                        operand == callee && is_symbol(callee);
                    add_references(node, operand, !direct_callee);
                }
            }
        }
    }
    for (auto global = LLVMGetFirstGlobal(module); global; global = LLVMGetNextGlobal(global)) {
        names[value_name(global)] = global;
        auto& node = graph[global];
        if (auto initializer = LLVMGetInitializer(global)) add_references(node, initializer, true);
    }
    for (auto alias = LLVMGetFirstGlobalAlias(module); alias; alias = LLVMGetNextGlobalAlias(alias)) {
        names[value_name(alias)] = alias;
        add_references(graph[alias], LLVMAliasGetAliasee(alias), true);
    }
    for (auto ifunc = LLVMGetFirstGlobalIFunc(module); ifunc; ifunc = LLVMGetNextGlobalIFunc(ifunc)) {
        names[value_name(ifunc)] = ifunc;
        add_references(graph[ifunc], LLVMGetGlobalIFuncResolver(ifunc), true);
    }
    std::set<LLVMValueRef> live;
    std::deque<LLVMValueRef> pending;
    auto mark = [&](LLVMValueRef value) { if (live.insert(value).second) pending.push_back(value); };
    for (const auto& root : roots) {
        auto found = names.find(root);
        if (found == names.end()) throw Error("LLVM reachability: entry point '" + root + "' is absent from linked module");
        mark(found->second);
    }
    for (const char* root : {"llvm.global_ctors", "llvm.global_dtors", "llvm.used", "llvm.compiler.used"}) {
        auto found = names.find(root);
        if (found != names.end()) mark(found->second);
    }
    bool indirect = false;
    while (!pending.empty()) {
        LLVMValueRef value = pending.front(); pending.pop_front();
        const auto found = graph.find(value);
        if (found == graph.end()) continue;
        for (auto reference : found->second.references) mark(reference);
        if (!indirect && found->second.indirect) {
            indirect = true;
            for (auto function : address_taken) mark(function);
        }
    }
    if (indirect && retained_addresses) {
        for (auto value : address_taken) if (live.count(value)) retained_addresses->push_back(value_name(value));
        std::sort(retained_addresses->begin(), retained_addresses->end());
    }
    return live;
}

// LLVM's printed integer constants are exact decimal APInts. Convert digit by
// digit, modulo 2^bits, without ever passing through a floating-point number.
std::vector<unsigned> integer_bytes(LLVMValueRef value, unsigned bits) {
    const size_t count = (static_cast<size_t>(bits) + 7) / 8;
    std::vector<unsigned> result(count, 0);
    std::string literal = value_text(value);
    const auto separator = literal.find(' ');
    if (separator == std::string::npos) unsupported("cannot read integer constant", value);
    literal = literal.substr(separator + 1);
    if (literal == "true") literal = "1";
    if (literal == "false") literal = "0";
    bool negative = !literal.empty() && literal[0] == '-';
    const size_t start = negative ? 1 : 0;
    if (start == literal.size()) unsupported("empty integer constant", value);
    for (size_t digit = start; digit < literal.size(); ++digit) {
        if (literal[digit] < '0' || literal[digit] > '9')
            unsupported("unsupported integer constant spelling", value);
        unsigned carry = static_cast<unsigned>(literal[digit] - '0');
        for (auto& byte : result) {
            const unsigned full = byte * 10 + carry;
            byte = full & 255;
            carry = full >> 8;
        }
    }
    if (negative) {
        unsigned carry = 1;
        for (auto& byte : result) {
            const unsigned full = (byte ^ 255) + carry;
            byte = full & 255;
            carry = full >> 8;
        }
    }
    if ((bits & 7) && !result.empty()) result.back() &= (1u << (bits & 7)) - 1;
    return result;
}

std::string opcode_name(LLVMOpcode opcode) {
    switch (opcode) {
#define OP(enum_name, text) case enum_name: return text
        OP(LLVMRet,"ret"); OP(LLVMBr,"br"); OP(LLVMSwitch,"switch");
        OP(LLVMIndirectBr,"indirectbr"); OP(LLVMInvoke,"invoke");
        OP(LLVMUnreachable,"unreachable"); OP(LLVMCallBr,"callbr");
        OP(LLVMAdd,"add"); OP(LLVMFAdd,"fadd"); OP(LLVMSub,"sub");
        OP(LLVMFSub,"fsub"); OP(LLVMMul,"mul"); OP(LLVMFMul,"fmul");
        OP(LLVMUDiv,"udiv"); OP(LLVMSDiv,"sdiv"); OP(LLVMFDiv,"fdiv");
        OP(LLVMURem,"urem"); OP(LLVMSRem,"srem"); OP(LLVMFRem,"frem");
        OP(LLVMShl,"shl"); OP(LLVMLShr,"lshr"); OP(LLVMAShr,"ashr");
        OP(LLVMAnd,"and"); OP(LLVMOr,"or"); OP(LLVMXor,"xor");
        OP(LLVMAlloca,"alloca"); OP(LLVMLoad,"load"); OP(LLVMStore,"store");
        OP(LLVMGetElementPtr,"gep"); OP(LLVMTrunc,"trunc");
        OP(LLVMZExt,"zext"); OP(LLVMSExt,"sext"); OP(LLVMFPToUI,"fptoui");
        OP(LLVMFPToSI,"fptosi"); OP(LLVMUIToFP,"uitofp"); OP(LLVMSIToFP,"sitofp");
        OP(LLVMFPTrunc,"fptrunc"); OP(LLVMFPExt,"fpext");
        OP(LLVMPtrToInt,"ptrtoint"); OP(LLVMIntToPtr,"inttoptr");
        OP(LLVMBitCast,"bitcast"); OP(LLVMAddrSpaceCast,"addrspacecast");
        OP(LLVMICmp,"icmp"); OP(LLVMFCmp,"fcmp"); OP(LLVMPHI,"phi");
        OP(LLVMCall,"call"); OP(LLVMSelect,"select"); OP(LLVMVAArg,"va_arg");
        OP(LLVMExtractElement,"extractelement"); OP(LLVMInsertElement,"insertelement");
        OP(LLVMShuffleVector,"shufflevector"); OP(LLVMExtractValue,"extractvalue");
        OP(LLVMInsertValue,"insertvalue"); OP(LLVMFence,"fence");
        OP(LLVMAtomicCmpXchg,"cmpxchg"); OP(LLVMAtomicRMW,"atomicrmw");
        OP(LLVMResume,"resume"); OP(LLVMLandingPad,"landingpad");
        OP(LLVMCleanupRet,"cleanupret"); OP(LLVMCatchRet,"catchret");
        OP(LLVMCatchPad,"catchpad"); OP(LLVMCleanupPad,"cleanuppad");
        OP(LLVMCatchSwitch,"catchswitch"); OP(LLVMFNeg,"fneg");
        OP(LLVMFreeze,"freeze"); OP(LLVMPtrToAddr,"ptrtoaddr");
#undef OP
        default: break;
    }
    return "unknown_" + std::to_string(static_cast<unsigned>(opcode));
}

std::string integer_predicate(LLVMIntPredicate predicate) {
    switch (predicate) {
        case LLVMIntEQ: return "eq"; case LLVMIntNE: return "ne";
        case LLVMIntUGT: return "ugt"; case LLVMIntUGE: return "uge";
        case LLVMIntULT: return "ult"; case LLVMIntULE: return "ule";
        case LLVMIntSGT: return "sgt"; case LLVMIntSGE: return "sge";
        case LLVMIntSLT: return "slt"; case LLVMIntSLE: return "sle";
    }
    unsupported("unknown integer comparison predicate");
}

std::string real_predicate(LLVMRealPredicate predicate) {
    static const char* names[] = {"false","oeq","ogt","oge","olt","ole","one","ord",
                                 "uno","ueq","ugt","uge","ult","ule","une","true"};
    const unsigned index = static_cast<unsigned>(predicate);
    if (index >= sizeof(names) / sizeof(*names)) unsupported("unknown floating comparison predicate");
    return names[index];
}

class Serializer {
    LLVMContextRef context_;
    LLVMModuleRef module_;
    LLVMTargetDataRef layout_;
    const std::set<LLVMValueRef>* live_ = nullptr;
    bool debug_info_ = false;
    std::map<LLVMMetadataRef, std::string> debug_scope_ids_;
    unsigned index_bits_ = 0;
    std::unordered_map<LLVMTypeRef, Json> types_;
    std::unordered_map<LLVMValueRef, std::string> ids_;
    std::unordered_map<LLVMBasicBlockRef, std::string> blocks_;
    std::unordered_map<LLVMBasicBlockRef, std::pair<std::string, std::string>> all_blocks_;
    std::map<unsigned, std::string> attribute_names_;

    std::string debug_scope(LLVMMetadataRef metadata) {
        if (!metadata) return {};
        auto found = debug_scope_ids_.find(metadata);
        if (found != debug_scope_ids_.end()) return found->second;
        auto key = "scope" + std::to_string(debug_scope_ids_.size());
        debug_scope_ids_[metadata] = key;
        return key;
    }

    std::string metadata_text(LLVMMetadataRef metadata) {
        return metadata ? value_text(LLVMMetadataAsValue(context_, metadata)) : "";
    }

    // llvm-c exposes generic metadata operands, but not DIVariable::getName /
    // getType. These operand indices are the LLVM 22 DIVariable representation.
    LLVMValueRef metadata_operand(LLVMMetadataRef metadata, unsigned index) {
        if (!metadata) return nullptr;
        auto value = LLVMMetadataAsValue(context_, metadata);
        const unsigned count = LLVMGetMDNodeNumOperands(value);
        if (index >= count) return nullptr;
        std::vector<LLVMValueRef> operands(count);
        LLVMGetMDNodeOperands(value, operands.data());
        return operands[index];
    }

    std::string metadata_string_operand(LLVMMetadataRef metadata, unsigned index) {
        auto value = metadata_operand(metadata, index);
        if (!value || !LLVMIsAMDString(value)) return {};
        unsigned length = 0;
        const char* data = LLVMGetMDString(value, &length);
        return data ? std::string(data, length) : "";
    }

    std::string debug_file(LLVMMetadataRef file) {
        if (!file) return {};
        unsigned length = 0;
        const char* data = LLVMDIFileGetFilename(file, &length);
        std::string name(data ? data : "", length);
        data = LLVMDIFileGetDirectory(file, &length);
        std::string directory(data ? data : "", length);
        if (name.empty()) return {};
        // Normalize separators without destroying Windows drive/UNC paths when
        // inspecting Windows-produced IR on a Unix host, or the reverse.
        std::replace(name.begin(), name.end(), '\\', '/');
        std::replace(directory.begin(), directory.end(), '\\', '/');
        auto absolute = [](const std::string& path) {
            return !path.empty() && (path[0] == '/' ||
                (path.size() >= 3 && path[1] == ':' && path[2] == '/'));
        };
        std::string combined = absolute(name) || directory.empty() ? name : directory + "/" + name;
        if (!absolute(combined)) combined = std::filesystem::current_path().u8string() + "/" + combined;
        return std::filesystem::u8path(combined).lexically_normal().generic_u8string();
    }

    Json debug_location(LLVMMetadataRef location, unsigned depth = 0) {
        if (!location || depth > 32) return Json::object();
        auto scope = LLVMDILocationGetScope(location);
        Json result = {{"file", debug_file(scope ? LLVMDIScopeGetFile(scope) : nullptr)},
            {"line", LLVMDILocationGetLine(location)}, {"column", LLVMDILocationGetColumn(location)},
            {"scope", debug_scope(scope)}};
        if (auto parent = LLVMDILocationGetInlinedAt(location))
            result["inlined_at"] = debug_location(parent, depth + 1);
        return result;
    }

    Json debug_type(LLVMMetadataRef metadata, unsigned depth = 0) {
        Json result = {{"kind", "unknown"}, {"name", ""}, {"bits", 0}, {"bytes", 0}};
        if (!metadata || depth > 12) return result;
        const auto kind = LLVMGetMetadataKind(metadata);
        if (kind != LLVMDIBasicTypeMetadataKind && kind != LLVMDIDerivedTypeMetadataKind &&
            kind != LLVMDICompositeTypeMetadataKind) return result;
        size_t length = 0;
        const char* name = LLVMDITypeGetName(metadata, &length);
        result["name"] = std::string(name ? name : "", length);
        const auto bits = LLVMDITypeGetSizeInBits(metadata);
        result["bits"] = bits;
        result["bytes"] = (bits + 7) / 8;
        const auto spelling = metadata_text(metadata);
        if (kind == LLVMDIBasicTypeMetadataKind) {
            if (spelling.find("DW_ATE_float") != std::string::npos) result["kind"] = "float";
            else if (spelling.find("DW_ATE_boolean") != std::string::npos) result["kind"] = "bool";
            else if (spelling.find("DW_ATE_signed") != std::string::npos) {
                result["kind"] = "int"; result["signed"] = true;
            } else if (spelling.find("DW_ATE_unsigned") != std::string::npos) {
                result["kind"] = "int"; result["signed"] = false;
            }
        } else if (kind == LLVMDIDerivedTypeMetadataKind) {
            if (spelling.find("DW_TAG_pointer_type") != std::string::npos ||
                spelling.find("DW_TAG_reference_type") != std::string::npos ||
                spelling.find("DW_TAG_rvalue_reference_type") != std::string::npos) {
                result["kind"] = "pointer";
                if (!bits) { result["bits"] = LLVMPointerSizeForAS(layout_, 0) * 8; result["bytes"] = LLVMPointerSizeForAS(layout_, 0); }
            }
            // LLVM 22 DIType operands: file, scope, name, size, offset;
            // DIDerivedType then adds its base type.
            auto base = metadata_operand(metadata, 5);
            if (base) {
                auto underlying = debug_type(LLVMValueAsMetadata(base), depth + 1);
                if (result["kind"] == "pointer") result["element"] = underlying;
                else if (spelling.find("DW_TAG_typedef") != std::string::npos ||
                    spelling.find("DW_TAG_const_type") != std::string::npos ||
                    spelling.find("DW_TAG_volatile_type") != std::string::npos ||
                    spelling.find("DW_TAG_restrict_type") != std::string::npos) {
                    const auto alias = result["name"].get<std::string>();
                    result = std::move(underlying);
                    if (!alias.empty()) result["name"] = alias;
                }
            }
        } else {
            result["kind"] = spelling.find("DW_TAG_array_type") != std::string::npos ? "array" : "aggregate";
        }
        return result;
    }

    void debug_records(LLVMValueRef instruction, LLVMBasicBlockRef block, Json& variables,
                       std::map<LLVMMetadataRef, size_t>& indices) {
        for (auto record = LLVMGetFirstDbgRecord(instruction); record; record = LLVMGetNextDbgRecord(record)) {
            const auto kind = LLVMDbgRecordGetKind(record);
            if (kind == LLVMDbgRecordLabel) continue;
            auto variable = LLVMDbgVariableRecordGetVariable(record);
            if (!variable) continue;
            auto found = indices.find(variable);
            if (found == indices.end()) {
                auto type_value = metadata_operand(variable, 3);
                Json info = {{"id", "local" + std::to_string(indices.size())},
                    {"name", metadata_string_operand(variable, 1)},
                    {"file", debug_file(LLVMDIVariableGetFile(variable))},
                    {"line", LLVMDIVariableGetLine(variable)},
                    {"scope", debug_scope(LLVMDIVariableGetScope(variable))}, {"parameter", 0},
                    {"type", debug_type(type_value ? LLVMValueAsMetadata(type_value) : nullptr)},
                    {"locations", Json::array()}};
                const auto spelling = metadata_text(variable);
                auto arg = spelling.find("arg: ");
                if (arg != std::string::npos) info["parameter"] = std::stoul(spelling.substr(arg + 5));
                found = indices.emplace(variable, variables.size()).first;
                variables.push_back(std::move(info));
            }
            Json location = {{"block", block_id(block)}, {"before", id(instruction)},
                {"kind", "unavailable"}, {"debug", debug_location(LLVMDbgRecordGetDebugLoc(record))}};
            const auto expression = metadata_text(LLVMDbgVariableRecordGetExpression(record));
            if (kind == LLVMDbgRecordAssign) {
                location["reason"] = "assignment tracking is not supported by this debugger";
            } else if (expression.find("!DIExpression()") == std::string::npos) {
                location["expression"] = expression;
                location["reason"] = "complex debug location expression";
            } else {
                auto value = LLVMDbgVariableRecordGetValue(record, 0);
                if (!value || LLVMIsUndef(value) || LLVMIsPoison(value)) {
                    location["reason"] = "optimized out";
                } else {
                    try {
                        location["operand"] = operand(value);
                        location["kind"] = kind == LLVMDbgRecordDeclare ? "address" : "value";
                    } catch (const Error&) {
                        location["reason"] = "unsupported debug value";
                    }
                }
            }
            variables[found->second]["locations"].push_back(std::move(location));
        }
    }

    void init_attribute_names() {
        const char* names[] = {
            "align", "alignstack", "allocalign", "allocptr", "allocsize", "alwaysinline", "argmemonly",
            "builtin", "byref", "byval", "cold", "convergent", "dereferenceable", "dereferenceable_or_null",
            "disable_sanitizer_instrumentation", "elementtype", "fn_ret_thunk_extern", "hot", "immarg",
            "inalloca", "inaccessiblememonly", "inaccessiblemem_or_argmemonly", "inlinehint", "inreg",
            "jumptable", "memory", "minsize", "mustprogress", "naked", "nest", "noalias", "nobuiltin",
            "nocallback", "nocapture", "nocf_check", "noduplicate", "nofree", "noimplicitfloat",
            "noinline", "nomerge", "nonlazybind", "nonnull", "noprofile", "norecurse", "noredzone",
            "noreturn", "nosanitize_bounds", "nosanitize_coverage", "nosync", "nounwind", "null_pointer_is_valid",
            "optforfuzzing", "optnone", "optsize", "preallocated", "readnone", "readonly", "returned",
            "returns_twice", "safestack", "sanitize_address", "sanitize_hwaddress", "sanitize_memtag",
            "sanitize_memory", "sanitize_thread", "shadowcallstack", "signext", "speculatable",
            "speculative_load_hardening", "sret", "ssp", "sspreq", "sspstrong", "strictfp", "swiftasync",
            "swifterror", "swiftself", "uwtable", "vscale_range", "willreturn", "writeonly", "zeroext",
            "noundef", "nocreateundeforpoison", "presplitcoroutine", "nocoro", "nooutline", "nofpclass",
            "range", "captures", "dead_on_unwind", "dead_on_return", "initializes", "writable"
        };
        for (const char* name : names) {
            const unsigned kind = LLVMGetEnumAttributeKindForName(name, std::strlen(name));
            if (kind) attribute_names_[kind] = name;
        }
    }

    Json attributes(LLVMValueRef value, LLVMAttributeIndex index, bool call = false) {
        unsigned count = call ? LLVMGetCallSiteAttributeCount(value, index) : LLVMGetAttributeCountAtIndex(value, index);
        std::vector<LLVMAttributeRef> attrs(count);
        if (count) {
            if (call) LLVMGetCallSiteAttributes(value, index, attrs.data());
            else LLVMGetAttributesAtIndex(value, index, attrs.data());
        }
        Json result = Json::object();
        for (auto attr : attrs) {
            if (LLVMIsStringAttribute(attr)) {
                unsigned key_length = 0, value_length = 0;
                const char* key = LLVMGetStringAttributeKind(attr, &key_length);
                const char* data = LLVMGetStringAttributeValue(attr, &value_length);
                result[std::string(key, key_length)] = std::string(data, value_length);
            } else {
                if (!LLVMIsTypeAttribute(attr) && !LLVMIsEnumAttribute(attr)) {
                    // LLVM 22 exposes construction but no read accessor for
                    // ConstantRange(List) attributes. These two attributes are
                    // optimization promises, not instructions or ABI rules.
                    // Identify them by their uniqued attribute identity and
                    // conservatively decline to exploit the promise. Do not
                    // call LLVMGetEnumAttributeValue on their representation.
                    bool safely_ignored = false;
                    for (const char* name : {"range", "initializes"}) {
                        const unsigned kind = LLVMGetEnumAttributeKindForName(name, std::strlen(name));
                        if (!kind) continue;
                        LLVMAttributeRef candidate = call ? LLVMGetCallSiteEnumAttribute(value, index, kind) :
                            LLVMGetEnumAttributeAtIndex(value, index, kind);
                        if (candidate == attr) {
                            result[name] = {{"ignored_optimization_promise", true},
                                            {"reason", "LLVM 22 C API has no range read accessor"}};
                            safely_ignored = true;
                            break;
                        }
                    }
                    if (safely_ignored) continue;
                    unsupported("unsupported non-enum LLVM attribute", value);
                }
                const unsigned kind = LLVMGetEnumAttributeKind(attr);
                auto found = attribute_names_.find(kind);
                const std::string name = found == attribute_names_.end() ? "kind_" + std::to_string(kind) : found->second;
                if (LLVMIsTypeAttribute(attr)) result[name] = type(LLVMGetTypeAttributeValue(attr));
                else {
                    const uint64_t number = LLVMGetEnumAttributeValue(attr);
                    const bool numeric = name == "align" || name == "alignstack" || name == "allocsize" ||
                        name == "dereferenceable" || name == "dereferenceable_or_null" || name == "memory" ||
                        name == "nofpclass" || name == "uwtable" || name == "vscale_range" || name == "captures";
                    result[name] = (number || numeric) ? Json(number) : Json(true);
                }
            }
        }
        return result;
    }

    std::string id(LLVMValueRef value) const {
        auto found = ids_.find(value);
        if (found == ids_.end()) unsupported("value is outside the current function", value);
        return found->second;
    }
    std::string block_id(LLVMBasicBlockRef block) const {
        auto found = blocks_.find(block);
        if (found == blocks_.end()) unsupported("basic block is outside the current function");
        return found->second;
    }

    Json gep(LLVMValueRef value) {
        Json result = Json::array();
        LLVMTypeRef current = LLVMGetGEPSourceElementType(value);
        unsigned operands = LLVMGetNumOperands(value);
        if (operands < 2) return result;
        result.push_back({{"index", operand(LLVMGetOperand(value, 1))},
                          {"stride", LLVMABISizeOfType(layout_, current)}});
        for (unsigned i = 2; i < operands; ++i) {
            LLVMValueRef index = LLVMGetOperand(value, i);
            if (LLVMGetTypeKind(current) == LLVMStructTypeKind) {
                if (!LLVMIsAConstantInt(index)) unsupported("nonconstant structure GEP index", value);
                uint64_t field = LLVMConstIntGetZExtValue(index);
                unsigned field_count = LLVMCountStructElementTypes(current);
                if (field >= field_count) unsupported("structure GEP index outside its type", value);
                result.push_back({{"offset", LLVMOffsetOfElement(layout_, current, static_cast<unsigned>(field))}});
                current = LLVMStructGetTypeAtIndex(current, static_cast<unsigned>(field));
            } else {
                const auto kind = LLVMGetTypeKind(current);
                if (kind != LLVMArrayTypeKind && kind != LLVMVectorTypeKind)
                    unsupported("unsupported GEP indexed type", value);
                current = LLVMGetElementType(current);
                result.push_back({{"index", operand(index)}, {"stride", LLVMABISizeOfType(layout_, current)}});
            }
        }
        return result;
    }

public:
    Serializer(LLVMContextRef context, LLVMModuleRef module, LLVMTargetDataRef layout,
               const std::set<LLVMValueRef>* live = nullptr, bool debug_info = false)
        : context_(context), module_(module), layout_(layout), live_(live), debug_info_(debug_info) {
        init_attribute_names();
        index_bits_ = pointer_index_bits(layout_, LLVMGetDataLayoutStr(module_));
        for (auto function = LLVMGetFirstFunction(module_); function; function = LLVMGetNextFunction(function)) {
            unsigned index = 0;
            for (auto block = LLVMGetFirstBasicBlock(function); block; block = LLVMGetNextBasicBlock(block))
                all_blocks_[block] = {value_name(function), "b" + std::to_string(index++)};
        }
    }

    Json type(LLVMTypeRef value) {
        auto cached = types_.find(value);
        if (cached != types_.end()) return cached->second;
        Json result = Json::object();
        LLVMTypeKind kind = LLVMGetTypeKind(value);
        switch (kind) {
            case LLVMVoidTypeKind: result["kind"] = "void"; result["bits"] = 0; break;
            case LLVMIntegerTypeKind: result["kind"] = "int"; result["bits"] = LLVMGetIntTypeWidth(value); break;
            case LLVMHalfTypeKind: result["kind"] = "float"; result["bits"] = 16; result["format"] = "half"; break;
            case LLVMBFloatTypeKind: result["kind"] = "float"; result["bits"] = 16; result["format"] = "bfloat"; break;
            case LLVMFloatTypeKind: result["kind"] = "float"; result["bits"] = 32; result["format"] = "float"; break;
            case LLVMDoubleTypeKind: result["kind"] = "float"; result["bits"] = 64; result["format"] = "double"; break;
            case LLVMX86_FP80TypeKind: result["kind"] = "float"; result["bits"] = 80; result["format"] = "x86_fp80"; break;
            case LLVMFP128TypeKind: result["kind"] = "float"; result["bits"] = 128; result["format"] = "fp128"; break;
            case LLVMPPC_FP128TypeKind: result["kind"] = "float"; result["bits"] = 128; result["format"] = "ppc_fp128"; break;
            case LLVMPointerTypeKind:
                if (LLVMGetPointerAddressSpace(value) != 0) unsupported("nonzero pointer address spaces are not supported");
                result["kind"] = "pointer";
                result["bits"] = LLVMPointerSizeForAS(layout_, 0) * 8;
                result["index_bits"] = index_bits_;
                result["address_space"] = 0;
                break;
            case LLVMArrayTypeKind:
                result["kind"] = "array"; result["count"] = LLVMGetArrayLength2(value);
                result["element"] = type(LLVMGetElementType(value)); break;
            case LLVMVectorTypeKind:
                result["kind"] = "vector"; result["count"] = LLVMGetVectorSize(value);
                result["element"] = type(LLVMGetElementType(value)); break;
            case LLVMStructTypeKind: {
                if (LLVMIsOpaqueStruct(value)) unsupported("opaque structure used as a sized value");
                result["kind"] = "struct";
                result["packed"] = LLVMIsPackedStruct(value) != 0;
                result["fields"] = Json::array(); result["offsets"] = Json::array();
                unsigned count = LLVMCountStructElementTypes(value);
                for (unsigned i = 0; i < count; ++i) {
                    result["fields"].push_back(type(LLVMStructGetTypeAtIndex(value, i)));
                    result["offsets"].push_back(LLVMOffsetOfElement(layout_, value, i));
                }
                break;
            }
            case LLVMScalableVectorTypeKind: unsupported("scalable vector types are not supported");
            case LLVMTokenTypeKind: unsupported("token types require an unsupported control/ABI extension");
            case LLVMMetadataTypeKind: unsupported("metadata operand in executable instruction");
            default: unsupported("unsupported LLVM type: " + take_message(LLVMPrintTypeToString(value)));
        }
        if (kind == LLVMVoidTypeKind) {
            result["size"] = 0; result["alloc_size"] = 0; result["align"] = 1;
        } else {
            result["size"] = LLVMStoreSizeOfType(layout_, value);
            result["alloc_size"] = LLVMABISizeOfType(layout_, value);
            result["align"] = LLVMABIAlignmentOfType(layout_, value);
            if (!result.contains("bits")) result["bits"] = LLVMSizeOfTypeInBits(layout_, value);
        }
        types_.emplace(value, result);
        return result;
    }

    Json operand(LLVMValueRef value) {
        Json result = {{"type", type(LLVMTypeOf(value))}};
        if (LLVMIsAInstruction(value) || LLVMIsAArgument(value)) {
            result["kind"] = "ref"; result["id"] = id(value); return result;
        }
        if (LLVMIsPoison(value)) { result["kind"] = "poison"; return result; }
        if (LLVMIsUndef(value)) { result["kind"] = "undef"; return result; }
        if (LLVMIsAConstantInt(value)) {
            if (result["type"]["size"].get<uint64_t>() > 200000)
                unsupported("integer constant exceeds the Scratch memory capacity", value);
            result["kind"] = "bytes";
            result["bytes"] = integer_bytes(value, LLVMGetIntTypeWidth(LLVMTypeOf(value)));
            return result;
        }
        if (LLVMIsAConstantFP(value)) {
            unsigned bits = result["type"]["bits"].get<unsigned>();
            LLVMValueRef bits_value = LLVMConstBitCast(value, LLVMIntTypeInContext(context_, bits));
            if (!LLVMIsAConstantInt(bits_value)) unsupported("cannot obtain exact floating constant bit pattern", value);
            result["kind"] = "bytes"; result["bytes"] = integer_bytes(bits_value, bits); return result;
        }
        if (LLVMIsAFunction(value) || LLVMIsAGlobalVariable(value) || LLVMIsAGlobalAlias(value)) {
            result["kind"] = "symbol"; result["name"] = value_name(value); result["addend"] = 0;
            result["symbol_kind"] = LLVMIsAFunction(value) ? "function" : LLVMIsAGlobalVariable(value) ? "global" : "alias";
            return result;
        }
        if (LLVMIsAConstantPointerNull(value) || LLVMIsAConstantAggregateZero(value)) {
            if (result["type"]["size"].get<uint64_t>() > 200000) {
                // Preserve compact zero initialization so the backend can
                // reject an oversized allocation before allocating a huge JSON array.
                result["kind"] = "zero"; return result;
            }
            result["kind"] = "bytes";
            result["bytes"] = std::vector<unsigned>(result["type"]["size"].get<size_t>(), 0); return result;
        }
        if (LLVMIsAConstantExpr(value)) {
            LLVMOpcode op = LLVMGetConstOpcode(value);
            result["kind"] = "constexpr"; result["op"] = opcode_name(op);
            result["operands"] = Json::array();
            for (unsigned i = 0; i < static_cast<unsigned>(LLVMGetNumOperands(value)); ++i)
                result["operands"].push_back(operand(LLVMGetOperand(value, i)));
            if (op == LLVMGetElementPtr) {
                result["gep"] = gep(value);
                result["index_bits"] = index_bits_;
            }
            if (op == LLVMICmp) result["predicate"] = integer_predicate(LLVMGetICmpPredicate(value));
            if (op == LLVMFCmp) result["predicate"] = real_predicate(LLVMGetFCmpPredicate(value));
            if (result["op"].get<std::string>().rfind("unknown_", 0) == 0)
                unsupported("unsupported constant expression", value);
            return result;
        }
        auto kind = LLVMGetTypeKind(LLVMTypeOf(value));
        if (LLVMIsAConstant(value) && (kind == LLVMArrayTypeKind || kind == LLVMStructTypeKind || kind == LLVMVectorTypeKind)) {
            result["kind"] = "aggregate"; result["elements"] = Json::array();
            unsigned count = kind == LLVMStructTypeKind ? LLVMCountStructElementTypes(LLVMTypeOf(value)) :
                             kind == LLVMVectorTypeKind ? LLVMGetVectorSize(LLVMTypeOf(value)) :
                             static_cast<unsigned>(LLVMGetArrayLength2(LLVMTypeOf(value)));
            for (unsigned i = 0; i < count; ++i) {
                LLVMValueRef element = LLVMGetAggregateElement(value, i);
                if (!element) unsupported("cannot read constant aggregate element", value);
                result["elements"].push_back(operand(element));
            }
            return result;
        }
        if (LLVMIsABlockAddress(value)) {
            auto block = LLVMGetBlockAddressBasicBlock(value);
            auto found = all_blocks_.find(block);
            if (found == all_blocks_.end()) unsupported("blockaddress refers outside the linked module", value);
            result["kind"] = "blockaddress";
            result["function"] = found->second.first;
            result["block"] = found->second.second;
            return result;
        }
        unsupported("unsupported LLVM operand", value);
    }

    Json instruction(LLVMValueRef value) {
        const LLVMOpcode op = LLVMGetInstructionOpcode(value);
        Json result = {{"id", id(value)}, {"op", opcode_name(op)}, {"type", type(LLVMTypeOf(value))}, {"operands", Json::array()}};
        if (debug_info_) {
            if (auto location = LLVMInstructionGetDebugLoc(value)) result["debug"] = debug_location(location);
        }
        auto add_operands = [&] {
            for (unsigned i = 0; i < static_cast<unsigned>(LLVMGetNumOperands(value)); ++i)
                result["operands"].push_back(operand(LLVMGetOperand(value, i)));
        };
        switch (op) {
            case LLVMRet: case LLVMUnreachable: add_operands(); break;
            case LLVMBr:
                result["targets"] = Json::array();
                if (LLVMIsConditional(value)) result["operands"].push_back(operand(LLVMGetCondition(value)));
                for (unsigned i = 0; i < LLVMGetNumSuccessors(value); ++i)
                    result["targets"].push_back(block_id(LLVMGetSuccessor(value, i)));
                break;
            case LLVMIndirectBr:
                result["operands"].push_back(operand(LLVMGetOperand(value, 0)));
                result["targets"] = Json::array();
                for (unsigned i = 0; i < LLVMGetNumSuccessors(value); ++i)
                    result["targets"].push_back(block_id(LLVMGetSuccessor(value, i)));
                break;
            case LLVMSwitch:
                result["operands"].push_back(operand(LLVMGetOperand(value, 0)));
                result["default"] = block_id(LLVMGetSwitchDefaultDest(value));
                result["cases"] = Json::array();
                for (unsigned i = 1; i < LLVMGetNumSuccessors(value); ++i)
                    result["cases"].push_back({{"value", operand(LLVMGetSwitchCaseValue(value, i))},
                        {"target", block_id(LLVMGetSuccessor(value, i))}});
                break;
            case LLVMPHI:
                result["incoming"] = Json::array();
                for (unsigned i = 0; i < LLVMCountIncoming(value); ++i)
                    result["incoming"].push_back({{"value", operand(LLVMGetIncomingValue(value, i))},
                                                  {"block", block_id(LLVMGetIncomingBlock(value, i))}});
                break;
            case LLVMAlloca:
                result["allocated_type"] = type(LLVMGetAllocatedType(value));
                result["align"] = LLVMGetAlignment(value); add_operands(); break;
            case LLVMLoad: case LLVMStore:
                result["align"] = LLVMGetAlignment(value);
                result["volatile"] = LLVMGetVolatile(value) != 0;
                result["ordering"] = static_cast<unsigned>(LLVMGetOrdering(value));
                add_operands(); break;
            case LLVMGetElementPtr:
                result["operands"].push_back(operand(LLVMGetOperand(value, 0)));
                result["gep"] = gep(value); result["source_type"] = type(LLVMGetGEPSourceElementType(value));
                result["index_bits"] = index_bits_;
                result["inbounds"] = LLVMIsInBounds(value) != 0; break;
            case LLVMICmp: result["predicate"] = integer_predicate(LLVMGetICmpPredicate(value)); add_operands(); break;
            case LLVMFCmp: result["predicate"] = real_predicate(LLVMGetFCmpPredicate(value)); add_operands(); break;
            case LLVMExtractValue: case LLVMInsertValue: {
                result["indices"] = Json::array();
                const unsigned* indices = LLVMGetIndices(value);
                for (unsigned i = 0; i < LLVMGetNumIndices(value); ++i) result["indices"].push_back(indices[i]);
                add_operands(); break;
            }
            case LLVMCall: {
                LLVMValueRef callee = LLVMGetCalledValue(value);
                const std::string name = value_name(callee);
                result["calling_convention"] = LLVMGetInstructionCallConv(value);
                result["vararg"] = LLVMIsFunctionVarArg(LLVMGetCalledFunctionType(value)) != 0;
                result["named_arg_count"] = LLVMCountParamTypes(LLVMGetCalledFunctionType(value));
                result["tail"] = LLVMIsTailCall(value) != 0;
                const auto tail_kind = LLVMGetTailCallKind(value);
                result["tail_kind"] = tail_kind == LLVMTailCallKindMustTail ? "musttail" :
                    tail_kind == LLVMTailCallKindTail ? "tail" : tail_kind == LLVMTailCallKindNoTail ? "notail" : "none";
                result["attrs"] = attributes(value, static_cast<LLVMAttributeIndex>(LLVMAttributeFunctionIndex), true);
                result["return_attrs"] = attributes(value, LLVMAttributeReturnIndex, true);
                result["arg_attrs"] = Json::array();
                unsigned args = LLVMGetNumArgOperands(value);
                for (unsigned i = 0; i < args; ++i) {
                    result["operands"].push_back(operand(LLVMGetOperand(value, i)));
                    result["arg_attrs"].push_back(attributes(value, i + 1, true));
                }
                if (LLVMIsAInlineAsm(callee)) {
                    size_t text_length = 0, constraint_length = 0;
                    const char* assembly = LLVMGetInlineAsmAsmString(callee, &text_length);
                    const char* constraints = LLVMGetInlineAsmConstraintString(callee, &constraint_length);
                    result["asm"] = {{"template", std::string(assembly, text_length)},
                                     {"constraints", std::string(constraints, constraint_length)},
                                     {"side_effects", LLVMGetInlineAsmHasSideEffects(callee) != 0}};
                    if (LLVMGetInlineAsmCanUnwind(callee)) unsupported("unwinding inline assembly is not supported", value);
                } else result["callee"] = operand(callee);
                for(unsigned index=0;index<LLVMGetNumOperandBundles(value);++index) {
                    const auto bundle=LLVMGetOperandBundleAtIndex(value,index);
                    size_t length=0;const char* tag=LLVMGetOperandBundleTag(bundle,&length);
                    const std::string name(tag,length);
                    LLVMDisposeOperandBundle(bundle);
                    // Verified assume alignment bundles are optimization facts,
                    // not actions. Clang emits them for aligned operator new.
                    // Keep rejecting stateful bundles (deopt/funclet/etc.).
                    if(!LLVMIsAFunction(callee) || value_name(callee)!="llvm.assume" || name!="align")
                        unsupported("call operand bundle '"+name+"' is not supported yet",value);
                }
                break;
            }
            case LLVMFence:
                result["ordering"] = static_cast<unsigned>(LLVMGetOrdering(value)); break;
            case LLVMAtomicCmpXchg:
                result["volatile"] = LLVMGetVolatile(value) != 0;
                result["weak"] = LLVMGetWeak(value) != 0;
                result["success_ordering"] = static_cast<unsigned>(LLVMGetCmpXchgSuccessOrdering(value));
                result["failure_ordering"] = static_cast<unsigned>(LLVMGetCmpXchgFailureOrdering(value));
                add_operands(); break;
            case LLVMAtomicRMW:
                result["volatile"] = LLVMGetVolatile(value) != 0;
                result["ordering"] = static_cast<unsigned>(LLVMGetOrdering(value));
                result["operation"] = static_cast<unsigned>(LLVMGetAtomicRMWBinOp(value));
                add_operands(); break;
            case LLVMShuffleVector:
                result["mask"] = Json::array();
                for (unsigned i = 0; i < LLVMGetNumMaskElements(value); ++i)
                    result["mask"].push_back(LLVMGetMaskValue(value, i));
                add_operands(); break;
            case LLVMAdd: case LLVMSub: case LLVMMul: case LLVMUDiv: case LLVMSDiv:
            case LLVMURem: case LLVMSRem: case LLVMShl: case LLVMLShr: case LLVMAShr:
            case LLVMAnd: case LLVMOr: case LLVMXor: case LLVMTrunc: case LLVMZExt:
            case LLVMSExt: case LLVMFPToUI: case LLVMFPToSI: case LLVMUIToFP:
            case LLVMSIToFP: case LLVMFPTrunc: case LLVMFPExt: case LLVMPtrToInt:
            case LLVMIntToPtr: case LLVMBitCast: case LLVMSelect: case LLVMExtractElement:
            case LLVMInsertElement: case LLVMFAdd: case LLVMFSub: case LLVMFMul:
            case LLVMFDiv: case LLVMFRem: case LLVMFNeg: case LLVMFreeze: case LLVMVAArg:
                add_operands(); break;
            default: unsupported("unsupported instruction '" + opcode_name(op) + "'", value);
        }
        return result;
    }

    Json function(LLVMValueRef value) {
        ids_.clear(); blocks_.clear();
        unsigned next_id = 0, next_block = 0;
        for (LLVMValueRef arg = LLVMGetFirstParam(value); arg; arg = LLVMGetNextParam(arg))
            ids_[arg] = "v" + std::to_string(next_id++);
        for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(value); block; block = LLVMGetNextBasicBlock(block)) {
            blocks_[block] = "b" + std::to_string(next_block++);
            for (LLVMValueRef inst = LLVMGetFirstInstruction(block); inst; inst = LLVMGetNextInstruction(inst))
                ids_[inst] = "v" + std::to_string(next_id++);
        }
        LLVMTypeRef function_type = LLVMGlobalGetValueType(value);
        Json result = {{"name", value_name(value)}, {"align", LLVMGetAlignment(value)},
                       {"return_type", type(LLVMGetReturnType(function_type))},
                       {"calling_convention", LLVMGetFunctionCallConv(value)},
                       {"vararg", LLVMIsFunctionVarArg(function_type) != 0},
                       {"linkage", static_cast<unsigned>(LLVMGetLinkage(value))},
                       {"linkage_name", linkage_name(LLVMGetLinkage(value))},
                       {"external_weak", LLVMGetLinkage(value) == LLVMExternalWeakLinkage},
                       {"attrs", attributes(value, static_cast<LLVMAttributeIndex>(LLVMAttributeFunctionIndex))},
                       {"return_attrs", attributes(value, LLVMAttributeReturnIndex)},
                       {"args", Json::array()}, {"blocks", Json::array()}};
        std::map<LLVMMetadataRef, size_t> debug_variable_indices;
        if (debug_info_) {
            result["debug_variables"] = Json::array();
            if (auto subprogram = LLVMGetSubprogram(value)) {
                result["debug"] = {{"name", metadata_string_operand(subprogram, 2)},
                    {"file", debug_file(LLVMDIScopeGetFile(subprogram))},
                    {"line", LLVMDISubprogramGetLine(subprogram)}, {"scope", debug_scope(subprogram)}};
            }
        }
        if (result["attrs"].contains("null_pointer_is_valid"))
            unsupported("null_pointer_is_valid is incompatible with address zero reservation", value);
        if (LLVMHasPersonalityFn(value)) unsupported("exception personality functions are not supported yet", value);
        const char* gc = LLVMGetGC(value);
        if (gc && *gc) unsupported("garbage-collection strategies are not supported yet", value);
        if (LLVMHasPrefixData(value) || LLVMHasPrologueData(value))
            unsupported("function prefix/prologue data is incompatible with the code-address representation", value);
        unsigned arg_index = 1;
        for (LLVMValueRef arg = LLVMGetFirstParam(value); arg; arg = LLVMGetNextParam(arg), ++arg_index)
            result["args"].push_back({{"id", id(arg)}, {"source_name", value_name(arg)},
                                      {"type", type(LLVMTypeOf(arg))}, {"attrs", attributes(value, arg_index)}});
        for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(value); block; block = LLVMGetNextBasicBlock(block)) {
            Json encoded = {{"id", block_id(block)}, {"instructions", Json::array()}};
            for (LLVMValueRef inst = LLVMGetFirstInstruction(block); inst; inst = LLVMGetNextInstruction(inst)) {
                if (debug_info_) debug_records(inst, block, result["debug_variables"], debug_variable_indices);
                if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
                    std::string callee_name = value_name(LLVMGetCalledValue(inst));
                    if (callee_name.rfind("llvm.dbg.", 0) == 0) continue;
                    if (callee_name == "llvm.experimental.noalias.scope.decl") {
                        if(LLVMGetNumOperandBundles(inst))
                            unsupported("operand bundles on noalias scope declarations are not supported",inst);
                        continue;
                    }
                }
                try {
                    encoded["instructions"].push_back(instruction(inst));
                } catch (const Error& error) {
                    throw Error(std::string(error.what()) + "\nwhile lowering @" + value_name(value) + ":\n" + value_text(inst));
                }
            }
            result["blocks"].push_back(std::move(encoded));
        }
        return result;
    }

    Json run() {
        Json result = {{"layout", LLVMGetDataLayoutStr(module_)}, {"target", LLVMGetTarget(module_)},
                       {"little_endian", true}, {"pointer_bytes", LLVMPointerSizeForAS(layout_, 0)},
                       {"pointer_index_bits", index_bits_},
                       {"globals", Json::array()}, {"functions", Json::array()},
                       {"declarations", Json::array()}, {"aliases", Json::array()}, {"used_symbols", Json::array()}};
        size_t asm_length = 0;
        LLVMGetModuleInlineAsm(module_, &asm_length);
        if (asm_length) unsupported("module-level assembly is not supported; use Scratch inline assembly calls");
        for (LLVMValueRef global = LLVMGetFirstGlobal(module_); global; global = LLVMGetNextGlobal(global)) {
            if (live_ && !live_->count(global)) continue;
            if (LLVMGetPointerAddressSpace(LLVMTypeOf(global)) != 0)
                unsupported("global variables in nonzero address spaces are not supported", global);
            const std::string name = value_name(global);
            if (name == "llvm.used" || name == "llvm.compiler.used") {
                LLVMValueRef initializer = LLVMGetInitializer(global);
                if (initializer) result["used_symbols"].push_back(operand(initializer));
                continue;
            }
            LLVMTypeRef global_type = LLVMGlobalGetValueType(global);
            Json encoded = {{"name", name}, {"type", type(global_type)},
                            {"size", LLVMABISizeOfType(layout_, global_type)},
                            {"align", LLVMGetAlignment(global) ? LLVMGetAlignment(global) : LLVMABIAlignmentOfType(layout_, global_type)},
                            {"constant", LLVMIsGlobalConstant(global) != 0},
                            {"linkage", static_cast<unsigned>(LLVMGetLinkage(global))},
                            {"linkage_name", linkage_name(LLVMGetLinkage(global))},
                            {"external_weak", LLVMGetLinkage(global) == LLVMExternalWeakLinkage},
                            {"externally_initialized", LLVMIsExternallyInitialized(global) != 0},
                            {"thread_local", LLVMIsThreadLocal(global) != 0}};
            LLVMValueRef initializer = LLVMGetInitializer(global);
            encoded["initializer"] = initializer ? operand(initializer) : Json(nullptr);
            result["globals"].push_back(std::move(encoded));
        }
        for (LLVMValueRef alias = LLVMGetFirstGlobalAlias(module_); alias; alias = LLVMGetNextGlobalAlias(alias)) {
            if (live_ && !live_->count(alias)) continue;
            result["aliases"].push_back({{"name", value_name(alias)}, {"value", operand(LLVMAliasGetAliasee(alias))},
                                          {"linkage", static_cast<unsigned>(LLVMGetLinkage(alias))},
                                          {"linkage_name", linkage_name(LLVMGetLinkage(alias))},
                                          {"external_weak", LLVMGetLinkage(alias) == LLVMExternalWeakLinkage}});
        }
        for (auto ifunc = LLVMGetFirstGlobalIFunc(module_); ifunc; ifunc = LLVMGetNextGlobalIFunc(ifunc))
            if (!live_ || live_->count(ifunc)) unsupported("global indirect functions are not supported", ifunc);
        for (LLVMValueRef fn = LLVMGetFirstFunction(module_); fn; fn = LLVMGetNextFunction(fn)) {
            if (live_ && !live_->count(fn)) continue;
            const std::string name = value_name(fn);
            if (name.rfind("llvm.dbg.", 0) == 0 || name == "llvm.experimental.noalias.scope.decl") continue;
            result[LLVMIsDeclaration(fn) ? "declarations" : "functions"].push_back(function(fn));
        }
        return result;
    }
};

} // namespace

Json read_modules(const std::vector<std::string>& paths, const FrontendOptions& options) {
    if (paths.empty()) throw Error("no LLVM input modules were provided");
    ContextOwner context;
    ModuleOwner linked;
    std::string expected_layout, expected_target;
    std::vector<std::pair<std::string, bool>> inputs;
    for (const auto& path : paths) inputs.emplace_back(path, false);
    for (const auto& path : options.runtime_paths) inputs.emplace_back(path, true);
    Json runtime_adaptations = Json::array();
    for (const auto& input : inputs) {
        const auto& path = input.first;
        const bool is_runtime = input.second;
        context.diagnostics.clear();
        LLVMMemoryBufferRef buffer = nullptr;
        char* message = nullptr;
        if (LLVMCreateMemoryBufferWithContentsOfFile(path.c_str(), &buffer, &message))
            throw Error("cannot read LLVM module '" + path + "': " + take_message(message));
        ModuleOwner current;
        const char* data = LLVMGetBufferStart(buffer);
        size_t size = LLVMGetBufferSize(buffer);
        const bool bitcode = size >= 4 && ((static_cast<unsigned char>(data[0]) == 'B' && data[1] == 'C' &&
            static_cast<unsigned char>(data[2]) == 0xc0 && static_cast<unsigned char>(data[3]) == 0xde) ||
            (static_cast<unsigned char>(data[0]) == 0xde && static_cast<unsigned char>(data[1]) == 0xc0 &&
             static_cast<unsigned char>(data[2]) == 0x17 && static_cast<unsigned char>(data[3]) == 0x0b));
        if (bitcode) {
            LLVMBool failed = LLVMParseBitcodeInContext2(context.value, buffer, &current.value);
            LLVMDisposeMemoryBuffer(buffer);
            if (failed) throw Error("cannot parse LLVM bitcode module '" + path + "':\n" + context.diagnostics);
        } else {
            // LLVMParseIRInContext takes ownership of this buffer.
            if (LLVMParseIRInContext(context.value, buffer, &current.value, &message))
                throw Error("cannot parse LLVM IR module '" + path + "':\n" + take_message(message));
        }
        if (options.asm_validator) {
            for (auto function = LLVMGetFirstFunction(current.value); function; function = LLVMGetNextFunction(function)) {
                for (auto block = LLVMGetFirstBasicBlock(function); block; block = LLVMGetNextBasicBlock(block)) {
                    for (auto instruction = LLVMGetFirstInstruction(block); instruction; instruction = LLVMGetNextInstruction(instruction)) {
                        const auto op = LLVMGetInstructionOpcode(instruction);
                        if (op != LLVMCall && op != LLVMInvoke && op != LLVMCallBr) continue;
                        LLVMValueRef callee = LLVMGetCalledValue(instruction);
                        if (!LLVMIsAInlineAsm(callee)) continue;
                        size_t length = 0;
                        const char* text = LLVMGetInlineAsmAsmString(callee, &length);
                        if (length) {
                            try { options.asm_validator(std::string(text, length)); }
                            catch (const std::exception& error) {
                                throw Error("inline assembly in '" + path + "', @" + value_name(function) + ": " + error.what());
                            }
                        }
                    }
                }
            }
        }
        std::string layout = LLVMGetDataLayoutStr(current.value);
        if (layout.empty()) {
            if (options.default_layout.empty())
                throw Error("module '" + path + "' has no DataLayout; supply an explicit --data-layout");
            layout = options.default_layout;
            LLVMSetDataLayout(current.value, layout.c_str());
        }
        std::string target = LLVMGetTarget(current.value);
        if (is_runtime) {
            verify_runtime_layout(current.value, expected_layout, path);
            runtime_adaptations.push_back({{"path", path}, {"original_layout", layout},
                {"layout", expected_layout}, {"original_target", target}, {"target", expected_target}});
            layout = expected_layout; target = expected_target;
            LLVMSetDataLayout(current.value, layout.c_str());
            LLVMSetTarget(current.value, target.c_str());
        }
        if (!linked.value) { expected_layout = layout; expected_target = target; }
        else {
            if (layout != expected_layout)
                throw Error("incompatible DataLayout in module '" + path + "'");
            if (!expected_target.empty() && !target.empty() && expected_target != target)
                throw Error("incompatible target triples in module '" + path + "': '" + expected_target + "' and '" + target + "'");
            if (expected_target.empty()) expected_target = target;
        }
        verify(current.value, "in '" + path + "'");
        if (!linked.value) { linked.value = current.value; current.value = nullptr; }
        else {
            LLVMModuleRef consumed = current.value; current.value = nullptr;
            if (LLVMLinkModules2(linked.value, consumed))
                throw Error("LLVM linking failed for module '" + path + "':\n" + context.diagnostics);
        }
    }
    verify(linked.value, "after linking");
    std::string pipeline=options.passes;
    if(options.whole_program) {
        if(options.entry_points.empty())throw Error("whole-program pruning requires explicit entry points");
        std::string internalize="internalize<";
        bool first=true;
        for(const auto& root:options.entry_points) {
            if(root.empty() || root.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$")!=std::string::npos)
                throw Error("entry name is not supported by the whole-program pass syntax: "+root);
            auto entry=LLVMGetNamedFunction(linked.value,root.c_str());
            if(!entry)throw Error("whole-program entry function is absent: "+root);
            // Explicit entries may start with internal/private linkage. They
            // must also survive the user's O2 pipeline before internalization.
            if(LLVMGetLinkage(entry)==LLVMInternalLinkage || LLVMGetLinkage(entry)==LLVMPrivateLinkage)
                LLVMSetLinkage(entry,LLVMExternalLinkage);
            if(!first)internalize+=';';
            internalize+="preserve-gv="+root;first=false;
        }
        internalize+=">,globaldce";
        // Prune before optimizing large bitcode libraries as well as afterwards.
        // Optimizing unreachable locale/charconv code first can be prohibitively slow.
        pipeline = pipeline.empty() ? internalize : internalize + ',' + pipeline + ',' + internalize;
    }
    if (!pipeline.empty()) {
        LLVMPassBuilderOptionsRef pass_options = LLVMCreatePassBuilderOptions();
        LLVMPassBuilderOptionsSetVerifyEach(pass_options, true);
        LLVMErrorRef error = LLVMRunPasses(linked.value, pipeline.c_str(), nullptr, pass_options);
        LLVMDisposePassBuilderOptions(pass_options);
        if (error) {
            char* text = LLVMGetErrorMessage(error);
            std::string description(text); LLVMDisposeErrorMessage(text);
            throw Error("LLVM optimization pipeline failed: " + description);
        }
        verify(linked.value, "after optimization");
    }
    // These intrinsics are compile-time queries and must disappear even in an
    // -O0/optnone build. Reuse LLVM's manifest-constant and object-size rules;
    // treating every LLVM Constant (notably global addresses) as manifest would
    // be incorrect. This is final lowering, not a general optimization pipeline.
    bool lower_constant_queries = false;
    std::vector<std::pair<LLVMValueRef, LLVMAttributeRef>> restore_optnone;
    const unsigned optnone_kind = LLVMGetEnumAttributeKindForName("optnone", 7);
    for (auto function = LLVMGetFirstFunction(linked.value); function; function = LLVMGetNextFunction(function)) {
        bool needs_lowering = false;
        for (auto block = LLVMGetFirstBasicBlock(function); block && !needs_lowering; block = LLVMGetNextBasicBlock(block)) {
            for (auto inst = LLVMGetFirstInstruction(block); inst; inst = LLVMGetNextInstruction(inst)) {
                if (LLVMGetInstructionOpcode(inst) != LLVMCall) continue;
                const auto name = value_name(LLVMGetCalledValue(inst));
                if (name.rfind("llvm.is.constant.", 0) == 0 || name.rfind("llvm.objectsize.", 0) == 0) {
                    needs_lowering = true;
                    break;
                }
            }
        }
        if (!needs_lowering) continue;
        lower_constant_queries = true;
        const auto index = static_cast<LLVMAttributeIndex>(LLVMAttributeFunctionIndex);
        if (auto attribute = LLVMGetEnumAttributeAtIndex(function, index, optnone_kind)) {
            restore_optnone.emplace_back(function, attribute);
            LLVMRemoveEnumAttributeAtIndex(function, index, optnone_kind);
        }
    }
    if (lower_constant_queries) {
        auto pass_options = LLVMCreatePassBuilderOptions();
        LLVMPassBuilderOptionsSetVerifyEach(pass_options, true);
        auto error = LLVMRunPasses(linked.value, "function(lower-constant-intrinsics)", nullptr, pass_options);
        LLVMDisposePassBuilderOptions(pass_options);
        for (const auto& saved : restore_optnone)
            LLVMAddAttributeAtIndex(saved.first, static_cast<LLVMAttributeIndex>(LLVMAttributeFunctionIndex), saved.second);
        if (error) {
            char* text = LLVMGetErrorMessage(error);
            std::string description(text); LLVMDisposeErrorMessage(text);
            throw Error("LLVM constant intrinsic lowering failed: " + description);
        }
        if (options.whole_program) {
            pass_options = LLVMCreatePassBuilderOptions();
            error = LLVMRunPasses(linked.value, "globaldce", nullptr, pass_options);
            LLVMDisposePassBuilderOptions(pass_options);
            if (error) {
                char* text = LLVMGetErrorMessage(error);
                std::string description(text); LLVMDisposeErrorMessage(text);
                throw Error("LLVM post-lowering global DCE failed: " + description);
            }
        }
        verify(linked.value, "after mandatory constant intrinsic lowering");
    }
    LayoutOwner layout;
    layout.value = LLVMCreateTargetData(LLVMGetDataLayoutStr(linked.value));
    if (LLVMByteOrder(layout.value) != LLVMLittleEndian)
        throw Error("the current Scratch target supports only little-endian DataLayouts");
    std::set<LLVMValueRef> live;
    std::vector<std::string> retained_addresses;
    if (!options.entry_points.empty()) live = reachable_values(linked.value, options.entry_points, &retained_addresses);
    if (options.debug_info && !LLVMIsNewDbgInfoFormat(linked.value))
        LLVMSetIsNewDbgInfoFormat(linked.value, true);
    Json result = Serializer(context.value, linked.value, layout.value,
        options.entry_points.empty() ? nullptr : &live, options.debug_info).run();
    if (options.debug_info) result["debug_info"] = {{"version", 1}, {"format", "llvm-source-locations"}};
    result["entry_points"] = options.entry_points;
    result["whole_program"] = options.whole_program;
    result["llvm_reachability"] = {{"enabled", !options.entry_points.empty()}, {"live_symbols", live.size()},
                                     {"address_taken_retained", retained_addresses}};
    result["runtime_layout_adaptations"] = std::move(runtime_adaptations);
    return result;
}

} // namespace scratch
