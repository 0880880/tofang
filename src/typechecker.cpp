#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "territory_ast.h"
#include "type.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

using namespace std;

TypeThing* RefinementEnv::lookup(Decl* d)
{
    auto it = map.find(d);
    if (it != map.end()) {
        return it->second;
    }
    return nullptr;
}

bool Region::outlives(const Region* o) const
{
    const Region* current = o;
    while (current) {
        if (current == this) {
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

static RefinementEnv mergeEnvs(RefinementEnv& a, RefinementEnv& b)
{
    RefinementEnv result;
    unordered_set<Decl*> keys;

    for (auto& [k, _] : a.map) {
        keys.insert(k);
    }
    for (auto& [k, _] : b.map) {
        keys.insert(k);
    }

    for (Decl* key : keys) {
        TypeThing* ta = a.lookup(key);
        TypeThing* tb = b.lookup(key);
        if (!ta) {
            result.map[key] = tb;
            continue;
        }
        if (!tb) {
            result.map[key] = ta;
            continue;
        }
        if (ta->kind == TypeKind::I_NULL && tb->kind == TypeKind::I_NULL) {
            result.map[key] = type_inull;
        } else if (ta->kind != TypeKind::I_NULL && tb->kind != TypeKind::I_NULL) {
            if (ta == tb) {
                result.map[key] = interner->getNullable(ta);
            }
        } else if (ta->kind == TypeKind::I_NULL && tb->kind != TypeKind::I_NULL) {
            result.map[key] = interner->getNullable(tb);
        } else if (ta->kind != TypeKind::I_NULL && tb->kind == TypeKind::I_NULL) {
            result.map[key] = interner->getNullable(ta);
        }
    }

    return result;
}

static RefinementEnv refineTrue(Expr* condition, RefinementEnv& env)
{
    if (auto* bin = dynamic_cast<BinaryExpr*>(condition)) {
        if (bin->op.value == "&&") {
            auto env1 = refineTrue(bin->left, env);
            return refineTrue(bin->right, env1);
        } else if (bin->op.value == "||") {
            auto a = refineTrue(bin->left, env);
            auto b = refineTrue(bin->right, env);
            return mergeEnvs(a, b);
        }
    }
    if (condition->t->kind == TypeKind::NULLABLE) {
        if (auto var = dynamic_cast<VariableExpr*>(condition)) {
            RefinementEnv e = {};
            e.map[var->decl] = std::get<NullableType>(condition->t->data).base;
            return mergeEnvs(env, e);
        }
    }
    return env;
}

static RefinementEnv refineFalse(Expr* condition, RefinementEnv& env)
{
    if (auto* bin = dynamic_cast<BinaryExpr*>(condition)) {
        if (bin->op.value == "&&") {
            auto a = refineFalse(bin->left, env);
            auto o = refineTrue(bin->left, env);
            auto b = refineFalse(bin->right, o);
            return mergeEnvs(a, b);
        } else if (bin->op.value == "||") {
            auto env1 = refineFalse(bin->left, env);
            return refineFalse(bin->right, env1);
        }
    }
    if (condition->t->kind == TypeKind::NULLABLE) {
        if (auto var = dynamic_cast<VariableExpr*>(condition)) {
            RefinementEnv e = {};
            e.map[var->decl] = type_inull;
            return mergeEnvs(env, e);
        }
    }
    return env;
}

RefinementEnv TypeChecker::checkBlock(BlockStmt* block)
{
    RefinementEnv saved = current_env;

    for (auto& stmt : block->statements) {
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

    if (node->elseIf) {
        current_env = false_env;
        after_false = checkIf(node->elseIf);
    } else if (node->elseStmt) {
        current_env = false_env;
        after_false = checkBlock(&node->elseStmt->body);
    } else {
        after_false = false_env;
    }

    current_env = mergeEnvs(after_true, after_false);

    return current_env;
}

void TypeChecker::open(ASTNode* node)
{
    if (auto* func = dynamic_cast<FuncStmt*>(node)) {
        current_function = func;
    }
}
static int numOrder(const TypeThing* t)
{
    switch (t->kind) {
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

    if (auto* lit = dynamic_cast<LiteralExpr*>(node)) {
        switch (lit->type) {
        case LiteralExpr::Integer:
            lit->t = type_unint;
            break;
        case LiteralExpr::Decimal:
            lit->t = type_unfloat;
            break;
        case LiteralExpr::Boolean:
            lit->t = type_bool;
            break;
        case LiteralExpr::Char:
            lit->t = type_u8;
            break;
        case LiteralExpr::MetaType:
            lit->t = interner->getMeta(lit->typeValue);
            break;
        case LiteralExpr::Null:
            lit->t = type_inull;
            break;
        case LiteralExpr::String:
            throw runtime_error("Unhandeled String literal");
        }
    } else if (auto* var = dynamic_cast<VariableExpr*>(node)) {
        TypeThing* refined = current_env.lookup(var->decl);

        if (refined) {
            var->t = refined;
        } else {
            var->t = var->decl->toType();
        }

    } else if (auto* grp = dynamic_cast<GroupingExpr*>(node)) {
        grp->t = grp->expression->t;
    } else if (auto* bin = dynamic_cast<BinaryExpr*>(node)) {

        string op = bin->op.value;

        vector<pair<TypeThing*, TypeThing*>> combinations = {
            { bin->left->t, bin->right->t }, { bin->right->t, bin->left->t }
        };

        for (auto& [lhs, rhs] : combinations) {
            if (op == "+" || op == "-" || op == "*" || op == "/") {
                if (!isNumeric(lhs) || !isNumeric(rhs)) {
                    error("operands of aritmhetic operation must be numeric");
                }
                if (isUnsigned(lhs) != isUnsigned(rhs)) {
                    error(
                        "arithmetic operation between unsigned and non-unsigned type is "
                        "invalid.");
                }
                if (lhs->kind == TypeKind::UNTYPED_INT && rhs->kind == TypeKind::UNTYPED_INT) {
                    bin->left->t = type_i32;
                    bin->right->t = type_i32;
                    bin->t = type_i32;
                    break;
                }
                if (lhs->kind == TypeKind::UNTYPED_FLOAT && (rhs->kind == TypeKind::UNTYPED_FLOAT || rhs->kind == TypeKind::UNTYPED_INT)) {
                    bin->left->t = type_f64;
                    bin->right->t = type_f64;
                    bin->t = type_f64;
                    break;
                }
                if (lhs == rhs) {
                    bin->t = lhs;
                    break;
                }
                if (numOrder(lhs) >= numOrder(rhs)) {
                    bin->left->t = lhs;
                    bin->right->t = lhs;
                    bin->t = lhs;
                    break;
                }
            } else if (op == "&" || op == "|" || op == "^") {
                if (!isInt(lhs) || !isInt(rhs)) {
                    error("operands of bitwise operation must be integer");
                }
                if (lhs->kind == TypeKind::UNTYPED_INT && rhs->kind == TypeKind::UNTYPED_INT) {
                    bin->t = type_i32;
                    break;
                }
                if (lhs == rhs) {
                    bin->t = lhs;
                    break;
                }
                if (numOrder(lhs) >= numOrder(rhs)) {
                    bin->t = lhs;
                    break;
                }
            } else if (op == "&&" || op == "||") {
                if (lhs != type_bool || rhs != type_bool) {
                    error("operands of logical operation must be bool");
                }
                bin->t = type_bool;
                break;
            } else if (op == ">" || op == "<" || op == ">=" || op == "<=") {
                if (!isNumeric(lhs) || !isNumeric(rhs)) {
                    error("both sides of comparison must be numeric");
                }
                bin->t = type_bool;
                break;
            } else if (op == "==") {
                // TODO should be better
                if (isNumeric(lhs) != isNumeric(rhs)) {
                    error("both sides of equality operation must be comparable");
                }
                bin->t = type_bool;
                break;
            }
        }
    } else if (auto* assign = dynamic_cast<AssignExpr*>(node)) {

        TypeThing* lhs = assign->left->t;
        TypeThing* rhs = assign->right->t;

        assert(lhs != nullptr);
        assert(rhs != nullptr);

        if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
            return;
        }

        if (isNumeric(lhs) && isNumeric(rhs)) {
            if (numOrder(lhs) >= numOrder(rhs)) {
                assign->right->t = lhs;
                rhs = lhs;
            } else {
                error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
            }
        }

        if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
            rhs = interner->getNullable(rhs);
        }

        if (lhs == rhs) {
            return;
        }

        assign->t = rhs;

    } else if (auto* ret = dynamic_cast<ReturnStmt*>(node)) {

        TypeThing* typ = ret->value ? ret->value->t : type_void;

        if (typ->kind == TypeKind::UNTYPED_INT) {
            if (isInt(current_function->returnType) || isFloat(current_function->returnType)) {
                ret->value->t = current_function->returnType;
                return;
            } else {
                error(
                    "invalid return type: " + current_function->returnType->toString() + " function cannot return int");
            }
        }
        if (typ->kind == TypeKind::UNTYPED_FLOAT) {
            if (isFloat(current_function->returnType)) {
                ret->value->t = current_function->returnType;
                return;
            } else {
                error(
                    "invalid return type: " + current_function->returnType->toString() + " function cannot return float");
            }
        }

        if (!(isNumeric(typ) && isNumeric(current_function->returnType) && numOrder(current_function->returnType) >= numOrder(typ)) && (typ != current_function->returnType)) {
            error("wrong return type");
        } else if (ret->value) {
            ret->value->t = current_function->returnType;
            return;
        }

        if (typ != current_function->returnType) {
            error("wrong return type");
        }
    } else if (auto* str = dynamic_cast<StructStmt*>(node)) {
        for (size_t i = 0; i < str->types.size(); ++i) {
            TypeThing* lhs = str->types[i];
            TypeThing* rhs = str->definitions[i]->t;

            assert(lhs != nullptr);
            assert(rhs != nullptr);

            if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
                goto defer_strct;
            }

            if (isNumeric(lhs) && isNumeric(rhs)) {
                if (numOrder(lhs) >= numOrder(rhs)) {
                    rhs = lhs;
                } else {
                    error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
                }
            }

            if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
                rhs = interner->getNullable(rhs);
            }

            if (lhs == rhs) {
                goto defer_strct;
            }

            error("Struct default value type mismatch " + str->types[i]->toString() + " != " + str->definitions[i]->t->toString());

        defer_strct:;
        }
    } else if (auto* str_init = dynamic_cast<StructInitExpr*>(node)) {
        Decl* d = std::get<StructType>(str_init->struct_type->data).str;
        auto& p = std::get<StructDecl>(d->data);
        ssize_t last_index = -1;
        for (size_t i = 0; i < str_init->names.size(); ++i) {
            Lexer::Token& t = str_init->names[i];
            auto it = std::ranges::find_if(p.fieldNames, [t](const Lexer::Token& field) {
                return field.value == t.value;
            });
            if (it != p.fieldNames.end()) {
                ssize_t idx = std::distance(p.fieldNames.begin(), it);
                if (idx > last_index) {
                    last_index = idx;
                    TypeThing* lhs = p.fieldTypes[static_cast<size_t>(idx)];
                    TypeThing* rhs = str_init->values[i]->t;

                    assert(lhs != nullptr);
                    assert(rhs != nullptr);

                    if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
                        goto defer;
                    }

                    if (isNumeric(lhs) && isNumeric(rhs)) {
                        if (numOrder(lhs) >= numOrder(rhs)) {
                            rhs = lhs;
                        } else {
                            error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
                        }
                    }

                    if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
                        rhs = interner->getNullable(rhs);
                    }

                    if (lhs == rhs) {
                        goto defer;
                    }

                    // TODO check territory against stack struct init maybe

                    error("Struct assignment type mismatch: " + p.fieldTypes[static_cast<size_t>(idx)]->toString() + " != " + str_init->values[i]->t->toString());
                defer:;
                } else {
                    error("Struct initializer fields must appear in the order they were "
                          "defined.");
                }
            } else {
                error("Unkown struct field: " + t.value);
            }
        }
        str_init->t = interner->getStruct(d);
    } else if (auto* assign_stmt = dynamic_cast<AssignStmt*>(node)) {

        TypeThing* lhs = assign_stmt->type;
        TypeThing* rhs = assign_stmt->value->t;

        assert(lhs != nullptr);
        assert(rhs != nullptr);

        if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
            return;
        }

        if (isNumeric(lhs) && isNumeric(rhs)) {
            if (numOrder(lhs) >= numOrder(rhs)) {
                assign_stmt->value->t = lhs;
                rhs = lhs;
            } else {
                error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
            }
        }

        if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
            rhs = interner->getNullable(rhs);
        }

        if (lhs == rhs) {
            return;
        }
        error("assignment type mismatch " + lhs->toString() + " = " + rhs->toString());
    } else if (auto unary = dynamic_cast<UnaryExpr*>(node)) {
        unary->t = unary->right->t;
        if (unary->op.value == "*") {
            if (unary->right->t->kind != TypeKind::POINTER) {
                error("Cannot dereference a " + unary->right->t->toString());
            }
            unary->t = std::get<PtrType>(unary->right->t->data).pointee;
        }
    } else if (auto cal = dynamic_cast<CallExpr*>(node)) {

        if (auto* att = dynamic_cast<AttribExpr*>(cal->func)) {

            if (att->foo->t->kind == TypeKind::REGION) {

                TypeThing* region_type = att->foo->t;

                if (att->bar.value != "alloc") {
                    error("region can only be used for allocation: [region].alloc<>()");
                }

                if (cal->typeArgs.size() != 1) {
                    error("alloc expects one type argument `foo.alloc<T>([N])`");
                }

                if (cal->args.size() > 1) {
                    error("alloc expects zero or one argument `foo.alloc<T>([N])`");
                }

                TypeThing* targ = cal->typeArgs[0]->t;

                if (targ->kind != TypeKind::META) {
                    error("alloc expects a type");
                }

                if (cal->args.size() == 1) {
                    if (!isInt(cal->args[0]->t)) {
                        error("alloc size argument must be an unsigned integer");
                    }
                }

                TypeThing* allocated = std::get<MetaType>(targ->data).type;

                cal->t = interner->getPointer(allocated);
                cal->region = std::get<RegionType>(region_type->data).region->region;

                return;
            }
        }

        if (cal->func->t->kind == TypeKind::FUNCTION) {
            FuncType f = std::get<FuncType>(cal->func->t->data);
            if (f.params.size() != cal->args.size()) {
                error("Function call argument size mismatch: Expected " + to_string(f.params.size()) + " got " + to_string(cal->args.size()) + " instead.");
            }
            for (size_t i = 0; i < f.params.size(); ++i) {

                TypeThing* lhs = f.params[i];
                TypeThing* rhs = cal->args[i]->t;

                if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
                    continue;
                }

                if (isNumeric(lhs) && isNumeric(rhs)) {
                    if (numOrder(lhs) >= numOrder(rhs)) {
                        cal->args[i]->t = lhs;
                        rhs = lhs;
                    } else {
                        error("Function argument type mismatch " + lhs->toString() + " = " + rhs->toString());
                    }
                }

                if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
                    rhs = interner->getNullable(rhs);
                }

                if (lhs == rhs) {
                    continue;
                }
            }
            cal->t = f.return_type;
            return;
        } else if (cal->func->t->kind == TypeKind::GENERIC_FUNC) {
            GenericFuncType g = std::get<GenericFuncType>(cal->func->t->data);
            if (g.params.size() != cal->args.size()) {
                error("Function call argument size mismatch: Expected " + to_string(g.params.size()) + " got " + to_string(cal->args.size()) + " instead.");
            }
            if (g.type_params.size() != cal->typeArgs.size()) {
                error("Function call generic arguments size mismatch: Expected " + to_string(g.type_params.size()) + " got " + to_string(cal->typeArgs.size()) + " instead.");
            }
            std::unordered_map<TypeKey, TypeThing*, TypeKeyHash, TypeKeyEq> subst;
            for (size_t i = 0; i < g.type_params.size(); ++i) {
                if (cal->typeArgs[i]->t->kind != TypeKind::META) {
                    error("Function call generic arguments must be types.");
                }
                TypeKey k = {};
                k.kind = TypeKind::TYPE_VAR;
                k.name = std::get<VarType>(g.type_params[i]->data).name;

                subst[k] = dynamic_cast<LiteralExpr*>(cal->typeArgs[i])->typeValue;
            }
            std::vector<TypeThing*> params = {};
            params.reserve(g.params.size());
            for (TypeThing* p : g.params) {
                params.push_back(interner->substitute(p, subst));
            }
            FuncType f = { .params = params,
                .return_type = interner->substitute(g.return_type, subst) };
            for (size_t i = 0; i < f.params.size(); ++i) {
                if (f.params[i] != cal->args[i]->t) {
                    error("Function argument type mismatch: Got " + cal->args[i]->t->toString() + " instead of " + f.params[i]->toString() + ".");
                }
            }
            cal->t = f.return_type;
        } else {
            error("Cannot call non-function type");
        }
    } else if (auto* att = dynamic_cast<AttribExpr*>(node)) {
        if (att->foo->t->kind == TypeKind::STRUCT) {
            auto* decl = std::get<StructType>(att->foo->t->data).str;
            auto& ddata = std::get<StructDecl>(decl->data);
            for (unsigned int i = 0; i < ddata.fieldNames.size(); i++) {
                if (ddata.fieldNames[i].value == att->bar.value) {
                    att->t = ddata.fieldTypes[i];
                    return;
                }
            }
            for (unsigned int i = 0; i < ddata.methods.size(); i++) {
                auto* met = ddata.methods[i];
                if (ddata.methods[i]->name.value == att->bar.value) {
                    if (met->genericParams.empty()) {
                        att->t = interner->getFunction(met->paramTypes, met->returnType);
                    } else {
                        att->t = interner->getGenericFunction(met->genericParams, met->paramTypes, met->returnType);
                    }
                    return;
                }
            }
            error("Struct " + decl->name.value + " has no attribute " + att->bar.value);
        } else if (att->foo->t->kind == TypeKind::REGION) {
            // Ignore, handled earlier
        } else {
            std::cout << att->foo->t->toString() << "\n";
            error("Unexpected attribute, only region and struct can be attributed");
        }
    } else if (auto* idx = dynamic_cast<IndexExpr*>(node)) {
        if (idx->arr->t->kind == TypeKind::ARRAY) {
            idx->t = std::get<ArrType>(idx->arr->t->data).element;
        } else if (idx->arr->t->kind == TypeKind::POINTER) {
            idx->t = std::get<PtrType>(idx->arr->t->data).pointee;
        } else {
            error("Cannot index unexpected type " + idx->arr->t->toString());
        }
    }
}

void TypeChecker::walk(ASTNode* node)
{

    if (auto* ifs = dynamic_cast<IfStmt*>(node)) {
        checkIf(ifs);
        return;
    }

    open(node);

    for (auto sub : node->walk()) {
        if (!sub) {
            continue;
        }
        walk(sub);
    }

    close(node);
}