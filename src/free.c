// Boring boilerplate code to free up data structures used in compilation.

#include "jou_compiler.h"
#include <assert.h>
#include <stdlib.h>

void free_tokens(Token *tokenlist)
{
    for (Token *t = tokenlist; t->type != TOKEN_END_OF_FILE; t++)
        if (t->type == TOKEN_STRING)
            free(t->data.string_value);
    free(tokenlist);
}

void free_constant(const Constant *c)
{
    if (c->kind == CONSTANT_STRING)
        free(c->data.str);
}

static void free_expression(const AstExpression *expr);

static void free_call(const AstCall *call)
{
    for (int i = 0; i < call->nargs; i++)
        free_expression(&call->args[i]);
    free(call->argnames);
    free(call->args);
}

static void free_expression(const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
    case AST_EXPR_BRACE_INIT:
        free_call(&expr->data.call);
        break;
    case AST_EXPR_GET_FIELD:
    case AST_EXPR_DEREF_AND_GET_FIELD:
        free_expression(expr->data.field.obj);
        free(expr->data.field.obj);
        break;
    case AST_EXPR_INDEXING:
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
    case AST_EXPR_AND:
    case AST_EXPR_OR:
        free_expression(&expr->data.operands[0]);
        free_expression(&expr->data.operands[1]);
        free(expr->data.operands);
        break;
    case AST_EXPR_NOT:
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_DEREFERENCE:
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        free_expression(&expr->data.operands[0]);
        free(expr->data.operands);
        break;
    case AST_EXPR_AS:
        free_expression(expr->data.as.obj);
        free(expr->data.as.obj);
        break;
    case AST_EXPR_CONSTANT:
        free_constant(&expr->data.constant);
        break;
    case AST_EXPR_GET_VARIABLE:
        break;
    }
}

static void free_body(const AstBody *body);

static void free_statement(const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
            free_expression(&stmt->data.ifstatement.if_and_elifs[i].condition);
            free_body(&stmt->data.ifstatement.if_and_elifs[i].body);
        }
        free(stmt->data.ifstatement.if_and_elifs);
        free_body(&stmt->data.ifstatement.elsebody);
        break;
    case AST_STMT_WHILE:
        free_expression(&stmt->data.whileloop.condition);
        free_body(&stmt->data.whileloop.body);
        break;
    case AST_STMT_FOR:
        free_statement(stmt->data.forloop.init);
        free_expression(&stmt->data.forloop.cond);
        free_statement(stmt->data.forloop.incr);
        free(stmt->data.forloop.init);
        free(stmt->data.forloop.incr);
        free_body(&stmt->data.forloop.body);
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
    case AST_STMT_RETURN_VALUE:
        free_expression(&stmt->data.expression);
        break;
    case AST_STMT_DECLARE_LOCAL_VAR:
        if (stmt->data.vardecl.initial_value) {
            free_expression(stmt->data.vardecl.initial_value);
            free(stmt->data.vardecl.initial_value);
        }
        break;
    case AST_STMT_ASSIGN:
        free_expression(&stmt->data.assignment.target);
        free_expression(&stmt->data.assignment.value);
        break;
    case AST_STMT_RETURN_WITHOUT_VALUE:
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
        break;
    }
}

static void free_body(const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        free_statement(&body->statements[i]);
    free(body->statements);
}

static void free_signature(const Signature *sig)
{
    free(sig->argnames);
    free(sig->argtypes);
}

static void free_ast_signature(const AstSignature *sig)
{
    free(sig->argnames);
    free(sig->argtypes);
}

void free_ast(AstToplevelNode *topnodelist)
{
    for (AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        switch(t->kind) {
        case AST_TOPLEVEL_DECLARE_FUNCTION:
            free_ast_signature(&t->data.decl_signature);
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            free_ast_signature(&t->data.funcdef.signature);
            free_body(&t->data.funcdef.body);
            break;
        case AST_TOPLEVEL_DEFINE_STRUCT:
            free(t->data.structdef.fieldnames);
            free(t->data.structdef.fieldtypes);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}


void free_control_flow_graph_block(const CfGraph *cfg, CfBlock *b)
{
    for (const CfInstruction *ins = b->instructions.ptr; ins < End(b->instructions); ins++) {
        if (ins->kind == CF_CONSTANT)
            free_constant(&ins->data.constant);
        free(ins->operands);
    }
    free(b->instructions.ptr);
    if (b != &cfg->start_block && b != &cfg->end_block)
        free(b);
}

static void free_cfg(CfGraph *cfg)
{
    for (CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++)
        free_control_flow_graph_block(cfg, *b);

    for (Variable **v = cfg->variables.ptr; v < End(cfg->variables); v++)
        free(*v);

    free(cfg->all_blocks.ptr);
    free(cfg->variables.ptr);
    free(cfg);
}

void free_control_flow_graphs(const CfGraphFile *cfgfile)
{
    destroy_type_context(&cfgfile->typectx);

    for (int i = 0; i < cfgfile->nfuncs; i++) {
        free_signature(&cfgfile->signatures[i]);
        if (cfgfile->graphs[i])
            free_cfg(cfgfile->graphs[i]);
    }

    free(cfgfile->signatures);
    free(cfgfile->graphs);
}
