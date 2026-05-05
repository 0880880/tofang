#pragma once
#include "decl.h"
#include <deque>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <unordered_map>

class BlockStmt;

struct IRContext {
    llvm::LLVMContext& ctx;
    llvm::IRBuilder<>& builder;
    llvm::Module& mod;
    llvm::IRBuilder<>* fn_entry = nullptr;
    llvm::Function* current_function = nullptr;
    std::unordered_map<Decl*, std::vector<llvm::Value*>> allocations = {};
    std::deque<BlockStmt*> blocks = {};
    bool unpack_stored = false;
    llvm::Value* attrib_struct = nullptr;

    IRContext(llvm::LLVMContext& c, llvm::IRBuilder<>& b, llvm::Module& m)
        : ctx(c)
        , builder(b)
        , mod(m)
    {
    }
};

class IRCOptions {
private:
    IRContext& ir;
    bool prev_unpack_stored;

public:
    IRCOptions(IRContext& ir)
        : ir(ir)
        , prev_unpack_stored(ir.unpack_stored)
    {
    }
    IRCOptions& unpackStored()
    {
        ir.unpack_stored = true;
        return *this;
    }
    IRCOptions& packStored()
    {
        ir.unpack_stored = false;
        return *this;
    }
    ~IRCOptions()
    {
        ir.unpack_stored = prev_unpack_stored;
    }
};