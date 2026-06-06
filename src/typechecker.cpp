#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "territory_ast.h"
#include "type.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>

using namespace std;

TypeThing* RefinementEnv::lookup(const AccessPath& d)
{
    auto it = map.find(d);
    if (it != map.end())
    {
        return it->second;
    }
    return nullptr;
}

bool Region::outlives(const Region* o) const
{
    const Region* current = o;
    while (current)
    {
        if (current == this)
        {
            return true;
        }
        current = current->parent;
    }
    return false;
}

static void error(const string& message)
{
    cerr << "Type Error:\n    " << message << '\n';
    cout.flush();
    cerr.flush();
    exit(1);
}

static std::optional<AccessPath> getAccessPath(Expr* expr)
{
    if (const auto* var = dynamic_cast<VariableExpr*>(expr))
    {
        return AccessPath{var->decl, {}};
    }

    if (const auto* att = dynamic_cast<AttribExpr*>(expr))
    {
        auto base_path = getAccessPath(att->foo);
        if (base_path.has_value())
        {
            base_path->fields.push_back(att->bar.value);
            return base_path;
        }
    }

    return std::nullopt;
}

static RefinementEnv mergeEnvs(RefinementEnv& a, RefinementEnv& b)
{
    RefinementEnv result;
    unordered_set<AccessPath, AccessPathHash> keys;
    
    for (auto& [k, _] : a.map)
    {
        keys.insert(k);
    }
    for (auto& [k, _] : b.map)
    {
        keys.insert(k);
    }

    for (const AccessPath& key : keys)
    {
        TypeThing* ta = a.lookup(key);
        TypeThing* tb = b.lookup(key);
        if (!ta)
        {
            result.map[key] = tb;
            continue;
        }
        if (!tb)
        {
            result.map[key] = ta;
            continue;
        }
        if (ta->kind == TypeKind::I_NULL && tb->kind == TypeKind::I_NULL)
        {
            result.map[key] = type_inull;
        }
        else if (ta->kind != TypeKind::I_NULL && tb->kind != TypeKind::I_NULL)
        {
            if (ta == tb)
            {
                result.map[key] = interner->getNullable(ta);
            }
        }
        else if (ta->kind == TypeKind::I_NULL && tb->kind != TypeKind::I_NULL)
        {
            result.map[key] = interner->getNullable(tb);
        }
        else if (ta->kind != TypeKind::I_NULL && tb->kind == TypeKind::I_NULL)
        {
            result.map[key] = interner->getNullable(ta);
        }
    }

    return result;
}

static RefinementEnv refineTrue(Expr* condition, RefinementEnv& env)
{
    if (auto* bin = dynamic_cast<BinaryExpr*>(condition))
    {
        if (bin->op.value == "&&")
        {
            auto env1 = refineTrue(bin->left, env);
            return refineTrue(bin->right, env1);
        }
        else if (bin->op.value == "||")
        {
            auto a = refineTrue(bin->left, env);
            auto b = refineTrue(bin->right, env);
            return mergeEnvs(a, b);
        }
    }
    if (condition->t->kind == TypeKind::NULLABLE)
    {
        if (auto path = getAccessPath(condition))
        {
            RefinementEnv e = {};
            e.map[path.value()] = std::get<NullableType>(condition->t->data).base;
            return mergeEnvs(env, e);
        }
    }
    return env;
}

static RefinementEnv refineFalse(Expr* condition, RefinementEnv& env)
{
    if (auto* bin = dynamic_cast<BinaryExpr*>(condition))
    {
        if (bin->op.value == "&&")
        {
            auto a = refineFalse(bin->left, env);
            auto o = refineTrue(bin->left, env);
            auto b = refineFalse(bin->right, o);
            return mergeEnvs(a, b);
        }
        else if (bin->op.value == "||")
        {
            auto env1 = refineFalse(bin->left, env);
            return refineFalse(bin->right, env1);
        }
    }

    if (condition->t->kind == TypeKind::NULLABLE)
    {
        if (auto path = getAccessPath(condition))
        {
            RefinementEnv e = {};
            e.map[path.value()] = type_inull;
            return mergeEnvs(env, e);
        }
    }
    return env;
}

RefinementEnv TypeChecker::checkBlock(BlockStmt* block)
{
    RefinementEnv saved = current_env;

    for (auto& stmt : block->statements)
    {
        walk(stmt);
    }

    RefinementEnv result = current_env;
    current_env = saved;

    return result;
}

RefinementEnv TypeChecker::checkIf(IfStmt* node)
{
    RefinementEnv old_env = current_env;

    walk(node->condition);

    auto true_env = refineTrue(node->condition, current_env);
    auto false_env = refineFalse(node->condition, current_env);

    current_env = true_env;
    RefinementEnv after_true = checkBlock(&node->body);

    RefinementEnv after_false;

    if (node->elseIf)
    {
        current_env = false_env;
        after_false = checkIf(node->elseIf);
    }
    else if (node->elseStmt)
    {
        current_env = false_env;
        after_false = checkBlock(&node->elseStmt->body);
    }
    else
    {
        after_false = false_env;
    }

    current_env = mergeEnvs(after_true, after_false);

    return current_env;
}

void TypeChecker::open(ASTNode* node)
{
    if (auto* func = dynamic_cast<FuncStmt*>(node))
    {
        current_function = func;
    }
}

static int numOrder(const TypeThing* t)
{
    switch (t->kind)
    {
    case TypeKind::F64:
        return 100;
    case TypeKind::F32:
        return 99;
    case TypeKind::I64:
    case TypeKind::U64:
        return 98;
    case TypeKind::I32:
    case TypeKind::U32:
        return 97;
    case TypeKind::I16:
    case TypeKind::U16:
        return 96;
    case TypeKind::I8:
    case TypeKind::U8:
        return 95;
    case TypeKind::UNTYPED_INT:
    case TypeKind::UNTYPED_FLOAT:
        return 90;
    default:
        return 0;
    }
}

void TypeChecker::close(ASTNode* node)
{
    if (auto* lit = dynamic_cast<LiteralExpr*>(node))
    {
        switch (lit->type)
        {
        case LiteralExpr::Integer:
            lit->setType(type_unint);
            break;
        case LiteralExpr::Decimal:
            lit->setType(type_unfloat);
            break;
        case LiteralExpr::Boolean:
            lit->setType(type_bool);
            break;
        case LiteralExpr::Char:
            lit->setType(type_u8);
            break;
        case LiteralExpr::MetaType:
            lit->setType(interner->getMeta(lit->typeValue));
            break;
        case LiteralExpr::Null:
            lit->setType(type_inull);
            break;
        case LiteralExpr::String:
            lit->setType(interner->getPointer(type_u8));
            break;
        }
    }
    else if (auto* var = dynamic_cast<VariableExpr*>(node))
    {
        var->setType(var->decl->toType());
    }
    else if (auto* grp = dynamic_cast<GroupingExpr*>(node))
    {
        grp->setType(grp->expression->t);
    }
    else if (auto* bin = dynamic_cast<BinaryExpr*>(node))
    {
        string op = bin->op.value;

        vector<pair<TypeThing*, TypeThing*>> combinations = {
            {bin->left->t, bin->right->t}, {bin->right->t, bin->left->t}
        };

        for (auto& [lhs, rhs] : combinations)
        {
            if (op == "+" || op == "- " || op == "*" || op == "/")
            {
                if (lhs->kind == TypeKind::UNTYPED_INT && rhs->kind == TypeKind::UNTYPED_INT)
                {
                    bin->left->setType(type_i32);
                    bin->right->setType(type_i32);
                    bin->setType(type_i32);
                    break;
                }
                if (lhs->kind == TypeKind::UNTYPED_FLOAT && (rhs->kind == TypeKind::UNTYPED_FLOAT || rhs->kind ==
                    TypeKind::UNTYPED_INT))
                {
                    bin->left->setType(type_f64);
                    bin->right->setType(type_f64);
                    bin->setType(type_f64);
                    break;
                }
                if (!isNumeric(lhs) || !isNumeric(rhs))
                {
                    error("operands of arithmetic operation must be numeric");
                }
                // if (isUnsigned(lhs) != isUnsigned(rhs))
                // {
                //     error(
                //         "arithmetic operation between unsigned and signed type is "
                //         "invalid.");
                // }
                if (lhs == rhs)
                {
                    bin->setType(lhs);
                    break;
                }
                if (numOrder(lhs) >= numOrder(rhs))
                {
                    bin->left->setType(lhs);
                    bin->right->setType(lhs);
                    bin->setType(lhs);
                    break;
                }
            }
            else if (op == "&" || op == "|" || op == "^")
            {
                if (!isInt(lhs) || !isInt(rhs))
                {
                    error("operands of bitwise operation must be integer");
                }
                if (lhs->kind == TypeKind::UNTYPED_INT && rhs->kind == TypeKind::UNTYPED_INT)
                {
                    bin->setType(type_i32);
                    break;
                }
                if (lhs == rhs)
                {
                    bin->setType(lhs);
                    break;
                }
                if (numOrder(lhs) >= numOrder(rhs))
                {
                    bin->setType(lhs);
                    break;
                }
            }
            else if (op == "&&" || op == "||")
            {
                if (lhs->kind == TypeKind::NULLABLE)
                {
                    lhs = type_bool;
                }
                if (rhs->kind == TypeKind::NULLABLE)
                {
                    rhs = type_bool;
                }
                if (lhs != type_bool || rhs != type_bool)
                {
                    error("operands of logical operation must be bool");
                }
                bin->setType(type_bool);
                break;
            }
            else if (op == ">" || op == "<" || op == ">=" || op == "<=")
            {
                if (!isNumeric(lhs) || !isNumeric(rhs))
                {
                    error("both sides of comparison must be numeric");
                }
                bin->setType(type_bool);
                break;
            }
            else if (op == "==")
            {
                // TODO should be better
                if (isNumeric(lhs) != isNumeric(rhs))
                {
                    error("both sides of equality operation must be comparable");
                }
                bin->setType(type_bool);
                break;
            }
        }
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(node))
    {
        TypeThing* lhs = assign->left->t;
        TypeThing* rhs = assign->right->t;

        assert(lhs != nullptr);
        assert(rhs != nullptr);

        if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull)
        {
            goto end;
        }

        if (isNumeric(lhs) && isNumeric(rhs))
        {
            if (numOrder(lhs) >= numOrder(rhs))
            {
                assign->right->setType(lhs);
                rhs = lhs;
            }
            else
            {
                error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
            }
        }

        if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE)
        {
            rhs = interner->getNullable(rhs);
        }

        if (lhs == rhs)
        {
            goto end;
        }

        assign->setType(rhs);
    }
    else if (auto* ret = dynamic_cast<ReturnStmt*>(node))
    {
        TypeThing* typ = ret->value ? ret->value->t : type_void;

        if (typ->kind == TypeKind::UNTYPED_INT)
        {
            if (isInt(current_function->returnType) || isFloat(current_function->returnType))
            {
                ret->value->setType(current_function->returnType);
                goto end;
            }
            else
            {
                error(
                    "invalid return type: " + current_function->returnType->toString() + " function cannot return int");
            }
        }
        if (typ->kind == TypeKind::UNTYPED_FLOAT)
        {
            if (isFloat(current_function->returnType))
            {
                ret->value->setType(current_function->returnType);
                goto end;
            }
            else
            {
                error(
                    "invalid return type: " + current_function->returnType->toString() +
                    " function cannot return float");
            }
        }

        if (!(isNumeric(typ) && isNumeric(current_function->returnType) && numOrder(current_function->returnType) >=
            numOrder(typ)) && (typ != current_function->returnType))
        {
            error("wrong return type");
        }
        else if (ret->value)
        {
            ret->value->setType(current_function->returnType);
            goto end;
        }

        if (typ != current_function->returnType)
        {
            error("wrong return type");
        }
    }
    else if (auto* str = dynamic_cast<StructStmt*>(node))
    {
        for (size_t i = 0; i < str->types.size(); ++i)
        {
            TypeThing* lhs = str->types[i];
            TypeThing* rhs = str->definitions[i]->t;

            assert(lhs != nullptr);
            assert(rhs != nullptr);

            if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull)
            {
                goto defer_strct;
            }

            if (isNumeric(lhs) && isNumeric(rhs))
            {
                if (numOrder(lhs) >= numOrder(rhs))
                {
                    rhs = lhs;
                }
                else
                {
                    error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
                }
            }

            if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE)
            {
                rhs = interner->getNullable(rhs);
            }

            if (lhs == rhs)
            {
                goto defer_strct;
            }

            error("Struct default value type mismatch " + str->types[i]->toString() + " != " + str->definitions[i]->t->
                toString());

        defer_strct:;
        }
    }
    else if (auto* str_init = dynamic_cast<StructInitExpr*>(node))
    {
        StructType& str_ty = std::get<StructType>(str_init->struct_type->data);

        auto& p = std::get<StructDecl>(str_ty.decl->data);
        ssize_t last_index = -1;
        for (size_t i = 0; i < str_init->names.size(); ++i)
        {
            Lexer::Token& t = str_init->names[i];
            auto it = std::ranges::find_if(p.fieldNames, [t](const Lexer::Token& field)
            {
                return field.value == t.value;
            });
            if (it != p.fieldNames.end())
            {
                ssize_t idx = std::distance(p.fieldNames.begin(), it);
                if (idx > last_index)
                {
                    last_index = idx;
                    TypeThing* lhs = p.fieldTypes[static_cast<size_t>(idx)];
                    TypeThing* rhs = str_init->values[i]->t;

                    assert(lhs != nullptr);
                    assert(rhs != nullptr);

                    if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull)
                    {
                        goto defer;
                    }

                    if (isNumeric(lhs) && isNumeric(rhs))
                    {
                        if (numOrder(lhs) >= numOrder(rhs))
                        {
                            rhs = lhs;
                            str_init->values[i]->setType(lhs);
                        }
                        else
                        {
                            error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
                        }
                    }

                    if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE)
                    {
                        rhs = interner->getNullable(rhs);
                        str_init->values[i]->setType(rhs);
                    }

                    if (lhs == rhs)
                    {
                        goto defer;
                    }

                    // TODO check territory against stack struct init maybe

                    error("Struct assignment type mismatch: " + p.fieldTypes[static_cast<size_t>(idx)]->toString() +
                        " != " + str_init->values[i]->t->toString());
                defer:;
                }
                else
                {
                    error("Struct initializer fields must appear in the order they were "
                        "defined.");
                }
            }
            else
            {
                error("Unkown struct field: " + t.value);
            }
        }
        str_init->setType(interner->getStruct(str_ty.name));
    }
    else if (auto* assign_stmt = dynamic_cast<AssignStmt*>(node))
    {
        TypeThing* lhs = assign_stmt->type;
        TypeThing* rhs = assign_stmt->value->t;

        assert(lhs != nullptr);
        assert(rhs != nullptr);

        if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull)
        {
            goto end;
        }

        if (isNumeric(lhs) && isNumeric(rhs))
        {
            if (numOrder(lhs) >= numOrder(rhs))
            {
                assign_stmt->value->setType(lhs);
                rhs = lhs;
            }
            else
            {
                error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
            }
        }

        if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE)
        {
            rhs = interner->getNullable(rhs);
        }

        if (lhs == rhs)
        {
            goto end;
        }
        error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
    }
    else if (auto unary = dynamic_cast<UnaryExpr*>(node))
    {
        unary->setType(unary->right->t);
        if (unary->op.value == "*")
        {
            if (unary->right->t->kind != TypeKind::POINTER)
            {
                error("Cannot dereference a " + unary->right->t->toString());
            }
            unary->setType(std::get<PtrType>(unary->right->t->data).pointee);
        }
    }
    else if (auto cal = dynamic_cast<CallExpr*>(node))
    {
        bool struct_member_call = false;

        if (auto* att = dynamic_cast<AttribExpr*>(cal->func))
        {
            if (att->foo->t->kind == TypeKind::REGION)
            {
                TypeThing* region_type = att->foo->t;

                if (att->bar.value != "alloc")
                {
                    error("region can only be used for allocation: [region].alloc<>()");
                }

                if (cal->typeArgs.size() != 1)
                {
                    error("alloc expects one type argument `foo.alloc<T>([N])`");
                }

                if (cal->args.size() > 1)
                {
                    error("alloc expects zero or one argument `foo.alloc<T>([N])`");
                }

                TypeThing* targ = cal->typeArgs[0]->t;

                if (targ->kind != TypeKind::META)
                {
                    error("alloc expects a type");
                }

                if (cal->args.size() == 1)
                {
                    if (!isInt(cal->args[0]->t))
                    {
                        error("alloc size argument must be an unsigned integer");
                    }
                }

                TypeThing* allocated = std::get<MetaType>(targ->data).type;

                cal->setType(interner->getPointer(allocated));
                cal->region = std::get<RegionType>(region_type->data).region->region;

                goto end;
            }
            else if (att->foo->t->kind == TypeKind::STRUCT)
            {
                struct_member_call = true;
            }
        }

        if (cal->func->t->kind == TypeKind::FUNCTION)
        {
            FuncType f = std::get<FuncType>(cal->func->t->data);
            size_t off = struct_member_call ? 1 : 0;
            if (f.params.size() != (cal->args.size() + off))
            {
                error("Function call argument size mismatch: Expected " + to_string(f.params.size()) + " got " +
                    to_string(cal->args.size()) + " instead.");
            }
            for (size_t i = 0; i < f.params.size() - off; ++i)
            {
                TypeThing* lhs = f.params[i + off];
                TypeThing* rhs = cal->args[i]->t;

                if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull)
                {
                    continue;
                }

                if (isNumeric(lhs) && isNumeric(rhs))
                {
                    if (numOrder(lhs) >= numOrder(rhs))
                    {
                        cal->args[i]->setType(lhs);
                        rhs = lhs;
                    }
                    else
                    {
                        error("Function argument type mismatch " + lhs->toString() + " = " + rhs->toString());
                    }
                }

                if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE)
                {
                    rhs = interner->getNullable(rhs);
                }

                if (lhs == rhs)
                {
                    continue;
                }
            }
            cal->setType(f.return_type);
            goto end;
        }
        else if (cal->func->t->kind == TypeKind::GENERIC_FUNC)
        {
            GenericFuncType g = std::get<GenericFuncType>(cal->func->t->data);
            if (g.params.size() != cal->args.size())
            {
                error("Function call argument size mismatch: Expected " + to_string(g.params.size()) + " got " +
                    to_string(cal->args.size()) + " instead.");
            }
            if (g.type_params.size() != cal->typeArgs.size())
            {
                error("Function call generic arguments size mismatch: Expected " + to_string(g.type_params.size()) +
                    " got " + to_string(cal->typeArgs.size()) + " instead.");
            }
            std::unordered_map<TypeKey, TypeThing*, TypeKeyHash, TypeKeyEq> subst;
            for (size_t i = 0; i < g.type_params.size(); ++i)
            {
                if (cal->typeArgs[i]->t->kind != TypeKind::META)
                {
                    error("Function call generic arguments must be types.");
                }
                TypeKey k = {};
                k.kind = TypeKind::TYPE_VAR;
                k.name = std::get<VarType>(g.type_params[i]->data).name;

                subst[k] = dynamic_cast<LiteralExpr*>(cal->typeArgs[i])->typeValue;
            }
            std::vector<TypeThing*> params = {};
            params.reserve(g.params.size());
            for (TypeThing* p : g.params)
            {
                params.push_back(interner->substitute(p, subst));
            }
            FuncType f = {
                .params = params,
                .return_type = interner->substitute(g.return_type, subst)
            };
            for (size_t i = 0; i < f.params.size(); ++i)
            {
                if (f.params[i] != cal->args[i]->t)
                {
                    error("Function argument type mismatch: Got " + cal->args[i]->t->toString() + " instead of " + f.
                        params[i]->toString() + ".");
                }
            }
            cal->setType(f.return_type);
        }
        else
        {
            error("Cannot call non-function type");
        }
    }
    else if (auto* att = dynamic_cast<AttribExpr*>(node))
    {
        if (att->foo->t->kind == TypeKind::STRUCT)
        {
            auto* decl = std::get<StructType>(att->foo->t->data).decl;
            auto& ddata = std::get<StructDecl>(decl->data);
            for (unsigned int i = 0; i < ddata.fieldNames.size(); i++)
            {
                if (ddata.fieldNames[i].value == att->bar.value)
                {
                    att->setType(ddata.fieldTypes[i]);
                    goto end;
                }
            }
            for (unsigned int i = 0; i < ddata.methods.size(); i++)
            {
                auto& met = std::get<FuncDecl>(ddata.methods[i]->data);
                if (ddata.methods[i]->name.value == att->bar.value)
                {
                    if (met.genericParams.empty())
                    {
                        std::vector<TypeThing*> paramTypes;
                        paramTypes.reserve(met.params.size());
                        for (auto* p : met.params)
                        {
                            paramTypes.push_back(std::get<VarDecl>(p->data).type);
                        }
                        att->setType(interner->getFunction(paramTypes, met.returnType));
                    }
                    else
                    {
                        std::vector<TypeThing*> paramTypes;
                        paramTypes.reserve(met.params.size());
                        for (auto* p : met.params)
                        {
                            paramTypes.push_back(std::get<VarDecl>(p->data).type);
                        }
                        att->setType(interner->getGenericFunction(met.genericParams, paramTypes, met.returnType));
                    }
                    goto end;
                }
            }
            error("Struct " + decl->name.value + " has no attribute " + att->bar.value);
        }
        else if (att->foo->t->kind == TypeKind::REGION)
        {
            // Ignore, handled earlier
        }
        else
        {
            error("Unexpected attribute, only region and struct can be attributed");
        }
    }
    else if (auto* idx = dynamic_cast<IndexExpr*>(node))
    {
        if (idx->arr->t->kind == TypeKind::ARRAY)
        {
            idx->setType(std::get<ArrType>(idx->arr->t->data).element);
        }
        else if (idx->arr->t->kind == TypeKind::POINTER)
        {
            idx->setType(std::get<PtrType>(idx->arr->t->data).pointee);
        }
        else
        {
            error("Cannot index unexpected type " + idx->arr->t->toString());
        }
    }
    else if (auto* arr = dynamic_cast<ArrayExpr*>(node))
    {
        TypeThing* t = std::get<ArrType>(arr->arr_type->data).element;
        for (auto* e : arr->elements)
        {
            if (e->t != t)
            {
                error("Array type inconsistency " + t->toString() + " != " + e->t->toString());
            }
        }
    }
    end:
    if (auto* expr = dynamic_cast<Expr*>(node))
    {
        if (const auto path = getAccessPath(expr))
        {
            if (auto* ref_type = current_env.lookup(path.value()))
            {
                expr->t = ref_type;
            }
        }
    }
}

void TypeChecker::walk(ASTNode* node)
{
    if (auto* ifs = dynamic_cast<IfStmt*>(node))
    {
        checkIf(ifs);
        return;
    }

    open(node);

    for (auto sub : node->walk())
    {
        if (!sub)
        {
            continue;
        }
        walk(sub);
    }

    close(node);
}
