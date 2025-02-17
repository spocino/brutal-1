#include <bid/gen.h>
#include <brutal/debug.h>
#include <cc/builder.h>
#include <cc/gen.h>

static CType bidgen_c_type(BidType const *type, Alloc *alloc);

void bidgen_c_unit_add_typedef(CUnit *unit, Str s, BidType type, Alloc *alloc)
{
    CType t = bidgen_c_type(&type, alloc);
    CDecl type_def = cdecl_type(s, t, alloc);
    cunit_member(unit, cunit_decl(type_def));
}

static CType bidgen_c_primitive(BidPrimitive const *type, Alloc *alloc)
{
    Str res;

    if (type->args.len > 0)
    {
        Str arguments = str_fmt(alloc, "");
        for (int i = 0; i < type->args.len; i++)
        {
            if (i != 0)
            {
                arguments = str_fmt(alloc, "{},", arguments);
            }

            arguments = str_fmt(alloc, "{} {}", arguments, type->args.data[i]);
        }

        res = str_fmt(alloc, "{}({})", type->name, arguments);
    }
    else
    {
        res = type->name;
    }

    return ctype_name(res, alloc);
}

static CType bidgen_c_enum(BidEnum const *type, Alloc *alloc)
{
    CType enum_type = ctype_enum(alloc);

    int value = 0;
    vec_foreach(member, &type->members)
    {
        ctype_constant(&enum_type, member, cval_unsigned(value), alloc);
    }
    return enum_type;
}

static CType bidgen_c_struct(BidStruct const *type, Alloc *alloc)
{
    CType cstruct = ctype_struct(alloc);
    vec_foreach(member, &type->members)
    {
        ctype_member(&cstruct,
                     member.name,
                     bidgen_c_type(&member.type, alloc),
                     alloc);
    }

    return cstruct;
}

static CType bidgen_c_type(BidType const *type, Alloc *alloc)
{
    switch (type->type)
    {
    case BID_TYPE_PRIMITIVE:
        return bidgen_c_primitive(&type->primitive_, alloc);

    case BID_TYPE_ENUM:
        return bidgen_c_enum(&type->enum_, alloc);

    case BID_TYPE_STRUCT:
        return bidgen_c_struct(&type->struct_, alloc);

    default:
        panic$("Unknow type {}", type->type);
    }
}

static CExpr bidgen_c_method_flags(BidType request_type, Alloc *alloc)
{
    switch (request_type.type)
    {
    case BID_TYPE_PRIMITIVE:
    {
        if (bid_type_is_handle(&request_type))
        {
            CExpr call = cexpr_call(alloc, cexpr_ident(str$("BR_MSG_HND"), alloc));
            cexpr_member(&call, cexpr_constant(cval_signed(0)));
            return (call);
        }
        else
        {
            return cexpr_constant(cval_signed(0));
        }
        break;
    }

    case BID_TYPE_ENUM:
    {
        return cexpr_constant(cval_signed(0));
    }

    case BID_TYPE_STRUCT:
    {
        CExpr v = cexpr_constant(cval_signed(0));

        int index = 0;
        vec_foreach(member, &request_type.struct_.members)
        {
            if (bid_type_is_handle(&member.type))
            {
                // | BR_MSG_HND(index)
                CExpr call = cexpr_call(alloc, cexpr_ident(str$("BR_MSG_HND"), alloc));
                cexpr_member(&call, cexpr_constant(cval_signed(index)));
                v = cexpr_bit_or(v, call, alloc);
            }
            index++;
        }
        return v;
    }

    default:
        return cexpr_constant(cval_signed(0));
    }
}

static CExpr bidgen_c_msg_args(BidType request_type, Alloc *alloc)
{
    CExpr arr_init = cexpr_initializer(alloc);
    switch (request_type.type)
    {
    case BID_TYPE_PRIMITIVE:
    case BID_TYPE_ENUM:
    {
        // (BrArg)req
        CExpr targ = cexpr_cast(
            cexpr_ident(str$("req"), alloc),
            ctype_name(str$("BrArg"), alloc),
            alloc);

        cexpr_member(&arr_init, targ);
        break;
    }

    case BID_TYPE_STRUCT:
    {
        vec_foreach(member, &request_type.struct_.members)
        {
            // (BrArg)req->{member.name}
            CExpr targ = cexpr_cast(
                cexpr_ptr_access(
                    cexpr_ident(str$("req"), alloc),
                    cexpr_ident(member.name, alloc),
                    alloc),
                ctype_name(str$("BrArg"), alloc),
                alloc);

            cexpr_member(&arr_init, targ);
        }
        break;
    }

    default:
        return cexpr_empty();
        break;
    }

    return arr_init;
}

static CStmt bidgen_c_msg_create(BidIface const *iface, BidMethod const *method, Alloc *alloc)
{
    CExpr msg_init = cexpr_initializer(alloc);

    Str protocol_id = str_fmt(alloc, "{case:upper}_PROTOCOL_ID", iface->name);
    cc_push_initializer_member(&msg_init, str$("prot"), cexpr_ident(protocol_id, alloc), alloc);

    Str request = str_fmt(alloc, "{case:upper}_REQUEST", method->name);
    cc_push_initializer_member(&msg_init, str$("type"), cexpr_ident(request, alloc), alloc);

    if (method->request.type != BID_TYPE_NONE)
    {
        // .arg = {}
        CExpr args = bidgen_c_msg_args(method->request, alloc);
        cc_push_initializer_member(&msg_init, str$("arg"), args, alloc);

        // .flags = {}
        CExpr flags = bidgen_c_method_flags(method->request, alloc);
        cc_push_initializer_member(&msg_init, str$("flags"), flags, alloc);
    }
    CStmt res = cc_var_decl_str(str$("BrMsg"), str$("req_msg"), msg_init, alloc);
    return res;
}

static CDecl bidgen_c_method_decl(BidIface const *iface, BidMethod const *method, CStmt func_stmt, Alloc *alloc)
{
    Str err_type = str_fmt(alloc, "{case:pascal}Error", iface->name);

    CType ret_type = ctype_name(err_type, alloc);
    CType func_type = ctype_named(ctype_func(ret_type, alloc), method->name, alloc);

    ctype_member(&func_type, str$("task"), ctype_name(str$("BrId"), alloc), alloc);

    if (method->request.type != BID_TYPE_NONE)
    {
        CType req_type = ctype_ptr(ctype_attr(ctype_name(method->request.primitive_.name, alloc), CTYPE_CONST), alloc);
        ctype_member(&func_type, str$("req"), req_type, alloc);
    }

    if (method->response.type != BID_TYPE_NONE)
    {
        CType res_type = ctype_ptr(ctype_name(method->request.primitive_.name, alloc), alloc);
        ctype_member(&func_type, str$("resp"), res_type, alloc);
    }

    CDecl func = cdecl_func(method->name, func_type, func_stmt, alloc);

    return cdecl_attrib(func, CDECL_INLINE | CDECL_STATIC);
}

static CExpr bidgen_c_br_ev_eq_call(Alloc *alloc)
{
    CExpr call = cc_func_call(str$("br_ev_req"), alloc);

    cexpr_member(&call, cexpr_ident(str$("task"), alloc));
    cexpr_member(&call, cexpr_ref(cexpr_ident(str$("req_msg"), alloc), alloc));
    cexpr_member(&call, cexpr_ref(cexpr_ident(str$("resp_msg"), alloc), alloc));

    return call;
}

static void bidgen_c_msg_handle(CStmt *targ, BidIface const *iface, BidMethod const *method, Alloc *alloc)
{
    /*
    if (result != BR_SUCCESS)
    {
        return {}_BAD_COMMUNICATION
    }
    */

    Str bad_communication_str = str_fmt(alloc, "{case:upper}_BAD_COMMUNICATION", iface->name);

    CStmt stmt = cstmt_if(
        cexpr_str_op(not_eq, str$("result"), str$("BR_SUCCESS"), alloc),
        cstmt_return(cexpr_ident(bad_communication_str, alloc)), // return bad communication str
        alloc);

    cstmt_block_add(targ, stmt);

    /*
    "if (resp_msg.prot != protocol_id || "
                   "(resp_msg.type != response_id && resp_msg.type != error_id))\n"
                   "{{\n"
                   "return INTERFACE_UNEXPECTED_MESSAGE;\n"
                   "}}\n",
    */

    Str protocol_id_str = str_fmt(alloc, "{case:upper}_PROTOCOL_ID", iface->name);
    Str response_id_str = str_fmt(alloc, "{case:upper}_RESPONSE", method->name);
    Str error_id_str = str_fmt(alloc, "{case:upper}_ERROR", iface->name);
    Str unexpected_str = str_fmt(alloc, "{case:upper}_UNEXPECTED_MESSAGE", iface->name);

    CExpr cexpr_resp_access_type = cc_access_str(str$("resp_msg"), str$("type"), alloc); // resp_msg.type

    CExpr resp_or_error_cond =
        cexpr_or( // ||
            cexpr_not_eq(
                cc_access_str(str$("resp_msg"), str$("prot"), alloc), // resp_msg.prot
                cexpr_ident(protocol_id_str, alloc),
                alloc),
            cexpr_and(
                // resp_msg.type != response_id
                cexpr_not_eq(
                    cexpr_resp_access_type,
                    cexpr_ident(response_id_str, alloc), alloc),
                // resp_msg.type != error_id
                cexpr_not_eq(
                    cexpr_resp_access_type,
                    cexpr_ident(error_id_str, alloc), alloc),
                alloc),
            alloc);

    stmt = cstmt_if(resp_or_error_cond,
                    cstmt_return(cexpr_ident(unexpected_str, alloc)), // return bad communication str
                    alloc);

    cstmt_block_add(targ, stmt);

    /*
    if (resp_msg.type == error)
    {
        return  (error_type)resp_msg.arg[0];
    }
    */

    Str err_type = str_fmt(alloc, " {case:pascal}Error", iface->name);

    CExpr ret_res = cexpr_cast(
        cc_index_constant(cc_access_str(str$("resp_msg"), str$("arg"), alloc), 0, alloc), // resp_msg.arg[0]
        ctype_name(err_type, alloc),
        alloc);

    stmt = cstmt_if(cexpr_eq(
                        cc_access_str(str$("resp_msg"), str$("type"), alloc),
                        cexpr_ident(error_id_str, alloc),
                        alloc),
                    cstmt_return(ret_res), // return bad communication str
                    alloc);

    cstmt_block_add(targ, stmt);
}

static void bidgen_c_msg_resp_init(CStmt *targ, BidMethod const *method, Alloc *alloc)
{

    BidType response = method->response;

    Str request_type = str_fmt(alloc, "{case:pascal}Request", method->name);
    switch (response.type)
    {
    case BID_TYPE_PRIMITIVE:
    case BID_TYPE_ENUM:
    {
        CExpr set_expr = cexpr_assign(
            cexpr_deref(cexpr_ident(str$("resp"), alloc), alloc),
            cexpr_cast(cc_access_str(str$("resp_msg"), str$("arg"), alloc), ctype_name(request_type, alloc), alloc), alloc);

        cstmt_block_add(targ, cstmt_expr(set_expr));
        break;
    }
    case BID_TYPE_STRUCT:
    {
        int index = 0;
        vec_foreach(member, &response.struct_.members)
        {
            // resp->{} = (typeof(resp->{}))resp_msg.arg[{}]
            CExpr set_expr = cexpr_assign(
                cc_ptr_access_str(str$("resp"), member.name, alloc),
                cexpr_cast(cc_index_constant(
                               cc_access_str(str$("resp_msg"), str$("arg"), alloc),
                               0, alloc),
                           bidgen_c_type(&member.type, alloc),
                           alloc),
                alloc);

            cstmt_block_add(targ, cstmt_expr(set_expr));
            index++;
        }
        break;
    }

    default:
        break;
    }
}

static CDecl bidgen_c_method(BidIface const *iface, BidMethod const *method, Alloc *alloc)
{
    CStmt statement = cstmt_block(alloc);

    cstmt_block_add(&statement, bidgen_c_msg_create(iface, method, alloc));
    // BrMsg resp_msg = {};
    cstmt_block_add(&statement, cc_var_decl_str(str$("BrMsg"), str$("resp_msg"), cexpr_initializer(alloc), alloc));

    // BrResult result = br_ev_req(task, &req_msg, &resp_msg);
    cstmt_block_add(&statement, cc_var_decl_str(str$("BrResult"), str$("result"), bidgen_c_br_ev_eq_call(alloc), alloc));

    bidgen_c_msg_handle(&statement, iface, method, alloc);

    bidgen_c_msg_resp_init(&statement, method, alloc);

    Str success_str = str_fmt(alloc, "{case:upper}_SUCCESS",
                              iface->name);

    cstmt_block_add(&statement, cstmt_return(cexpr_ident(success_str, alloc))); // return {case:upper}_SUCCESS;

    CDecl func_decl = bidgen_c_method_decl(iface, method, statement, alloc);
    return func_decl;
}

static CDecl bidgen_c_callback(BidIface const *iface, BidMethod const *method, Alloc *alloc)
{
    Str err_type = str_fmt(alloc, "{case:pascal}Error", iface->name);
    Str request_type = str_fmt(alloc, "{case:pascal}Request", method->name);
    Str response_type = str_fmt(alloc, "{case:pascal}Response", method->name);
    Str func_name = str_fmt(alloc, "{case:pascal}Fn", method->name);

    CType ctype_fn = ctype_func(ctype_name(err_type, alloc), alloc);

    ctype_member(&ctype_fn, str$("from"), ctype_name(str$("BrId"), alloc), alloc); // BrId from

    if (method->request.type != BID_TYPE_NONE)
    {
        ctype_member(&ctype_fn, str$("req"),
                     ctype_ptr(ctype_attr(ctype_name(request_type, alloc), CTYPE_CONST), alloc), // request_type const *
                     alloc);                                                                     // BrId from
    }

    if (method->request.type != BID_TYPE_NONE)
    {
        ctype_member(&ctype_fn, str$("res"),
                     ctype_ptr(ctype_name(response_type, alloc), alloc), // request_type const *
                     alloc);                                             // BrId from
    }

    ctype_member(&ctype_fn, str$("ctx"),
                 ctype_ptr(ctype_void(), alloc), // request_type const *
                 alloc);

    return cdecl_type(str$(func_name), ctype_fn, alloc);
}

/* --- Types ---------------------------------------------------------------- */

void bidgen_c_typedef(CUnit *unit, const BidIface *iface, Alloc *alloc)
{
    vec_foreach(alias, &iface->aliases)
    {
        CDecl type_def = cdecl_type(alias.name, bidgen_c_type(&alias.type, alloc), alloc);
        cunit_member(unit, cunit_decl(type_def));
    }
}

/* --- Messages ------------------------------------------------------------- */

CType bidgen_c_method_message_type(BidIface const *iface, Alloc *alloc)
{

    CType message_type_decl = ctype_enum(alloc);

    int index = 0;

#define bidgen_c_const_msg_type(name)      \
    ctype_constant(&message_type_decl,     \
                   name,                   \
                   cval_unsigned(index++), \
                   alloc);

    bidgen_c_const_msg_type(str_fmt(alloc, "{case:upper}_INVALID", iface->name));
    bidgen_c_const_msg_type(str_fmt(alloc, "{case:upper}_ERROR", iface->name));

    vec_foreach(method, &iface->methods)
    {
        bidgen_c_const_msg_type(str_fmt(alloc, "{case:upper}_RESPONSE", method.name));
        bidgen_c_const_msg_type(str_fmt(alloc, "{case:upper}_REQUEST", method.name));
    }

    return message_type_decl;
}

CStmt bidgen_c_dispatcher_case_if_success(BidIface const *iface, BidMethod method, Alloc *alloc)
{
    Str response_constant_name = str_fmt(alloc, "{case:upper}_RESPONSE", method.name);
    Str protocol_constant_name = str_fmt(alloc, "{case:upper}_PROTOCOL_ID", iface->name);
    BidType response = method.response;
    CStmt if_true_block = cstmt_block(alloc);

    // resp_msg.prot = {case:upper}_PROTOCOL_ID;
    CExpr set_prot_expr = cexpr_assign(cc_access_str(str$("resp_msg"), str$("prot"), alloc), cexpr_ident(protocol_constant_name, alloc), alloc);
    cstmt_block_add(&if_true_block, cstmt_expr(set_prot_expr));

    // resp_msg.type = {case:upper}_{case:upper}_RESPONSE;
    CExpr set_type_expr = cexpr_assign(cc_access_str(str$("resp_msg"), str$("type"), alloc), cexpr_ident(response_constant_name, alloc), alloc);
    cstmt_block_add(&if_true_block, cstmt_expr(set_type_expr));

    switch (response.type)
    {
    case BID_TYPE_PRIMITIVE:
    case BID_TYPE_ENUM:
    {

        CExpr left_resp = cc_index_constant(cc_access_str(str$("resp_msg"), str$("arg"), alloc), 0, alloc);
        CExpr casted_right = cexpr_cast(cc_access_str(str$("resp"), str$("handle"), alloc), ctype_name(str$("BrId"), alloc), alloc);

        CExpr set_expr = cexpr_assign(left_resp, casted_right, alloc);
        cstmt_block_add(&if_true_block, cstmt_expr(set_expr));
        break;
    }

    case BID_TYPE_STRUCT:
    {
        int index = 0;
        vec_foreach(member, &response.struct_.members)
        {
            // resp_msg.{} = (type)resp.arg[{}];
            CExpr res_msg_expr = cc_access_str(str$("resp"), member.name, alloc);
            CExpr left = cc_index_constant(cc_access_str(str$("resp_msg"), str$("arg"), alloc), index, alloc);
            CExpr res_msg_casted = cexpr_cast(res_msg_expr, ctype_name(str$("BrId"), alloc), alloc);
            CExpr set_expr = cexpr_assign(left, res_msg_casted, alloc);

            cstmt_block_add(&if_true_block, cstmt_expr(set_expr));
            index++;
        }
        break;
    }

    default:
        break;
    }

    return if_true_block;
}

CStmt bidgen_c_dispatcher_case_if_error(BidIface const *iface, Alloc *alloc)
{
    Str error_constant_name = str_fmt(alloc, "{case:upper}_ERROR", iface->name);

    Str protocol_constant_name = str_fmt(alloc, "{case:upper}_PROTOCOL_ID", iface->name);
    CStmt if_false_block = cstmt_block(alloc);
    // resp_msg.prot = {case:upper}_PROTOCOL_ID;
    CExpr set_prot_expr = cexpr_assign(cc_access_str(str$("resp_msg"), str$("prot"), alloc), cexpr_ident(protocol_constant_name, alloc), alloc);
    cstmt_block_add(&if_false_block, cstmt_expr(set_prot_expr));

    // resp_msg.type = {case:upper}_{case:upper}_ERROR;
    CExpr set_type_expr = cexpr_assign(cc_access_str(str$("resp_msg"), str$("type"), alloc), cexpr_ident(error_constant_name, alloc), alloc);
    cstmt_block_add(&if_false_block, cstmt_expr(set_type_expr));

    // resp_msg.arg[0] = error;
    CExpr set_error_info = cexpr_assign((cc_index_constant(cc_access_str(str$("resp_msg"), str$("arg"), alloc), 0, alloc)), cexpr_ident(str$("error"), alloc), alloc);
    cstmt_block_add(&if_false_block, cstmt_expr(set_error_info));

    return if_false_block;
}

void bidgen_c_dispatcher_case(BidIface const *iface, CStmt *switch_block, BidMethod method, Alloc *alloc)
{
    Str success_constant_name = str_fmt(alloc, "{case:upper}_SUCCESS", iface->name);
    Str request_constant_name = str_fmt(alloc, "{case:upper}_REQUEST", method.name);
    Str request_type_name = str_fmt(alloc, "{case:pascal}Request", method.name);
    Str response_type_name = str_fmt(alloc, "{case:pascal}Response", method.name);
    Str error_type_name = str_fmt(alloc, "{case:pascal}Error", iface->name);
    Str server_handle_name = str_fmt(alloc, "handle_{case:lower}", method.name);
    BidType request = method.request;

    // case {case:upper}_{case:upper}_REQUEST:
    CStmt case_stmt = cstmt_case(cexpr_ident(str$(request_constant_name), alloc));
    CStmt case_block = cstmt_block(alloc);

    // {} req = {}
    cstmt_block_add(&case_block, cc_var_decl_str(request_type_name, str$("req"), cexpr_initializer(alloc), alloc));

    // req = ... req_msg->{}

    switch (request.type)
    {
    case BID_TYPE_PRIMITIVE:
    case BID_TYPE_ENUM:
    {
        // req = (req_type)req_msg->arg[0];
        CExpr req_msg_expr = cc_index_constant(cc_ptr_access_str(str$("req_msg"), str$("arg"), alloc), 0, alloc);

        CExpr req_msg_casted = cexpr_cast(req_msg_expr, ctype_name(request_type_name, alloc), alloc);
        CExpr set_expr = cexpr_assign(cexpr_ident(str$("req"), alloc), req_msg_casted, alloc);
        cstmt_block_add(&case_block, cstmt_expr(set_expr));
        break;
    }

    case BID_TYPE_STRUCT:
    {
        int index = 0;
        vec_foreach(member, &request.struct_.members)
        {
            // req.{} = (type)req_msg->arg[{}];
            CExpr left = cc_access_str(str$("req"), member.name, alloc);
            CExpr req_msg_expr = cc_index_constant(cc_ptr_access_str(str$("req_msg"), str$("arg"), alloc), index, alloc);
            CExpr req_msg_casted = cexpr_cast(req_msg_expr, bidgen_c_type(&member.type, alloc), alloc);
            CExpr set_expr = cexpr_assign(left, req_msg_casted, alloc);

            cstmt_block_add(&case_block, cstmt_expr(set_expr));
            index++;
        }
    }
    break;

    default:
        break;
    }

    // {} resp = {}
    cstmt_block_add(&case_block, cc_var_decl_str(response_type_name, str$("resp"), cexpr_initializer(alloc), alloc));

    // {} error = server->handle_{case:lower}(req_msg->from, &req, &resp, server->ctx)

    CExpr handle_get = cc_ptr_access_str(str$("server"), str$(server_handle_name), alloc);
    CExpr call = cexpr_call(alloc, handle_get);

    cexpr_member(&call, cc_ptr_access_str(str$("req_msg"), str$("from"), alloc));
    cexpr_member(&call, cexpr_ref(cexpr_ident(str$("req"), alloc), alloc));
    cexpr_member(&call, cexpr_ref(cexpr_ident(str$("resp"), alloc), alloc));
    cexpr_member(&call, cc_ptr_access_str(str$("server"), str$("ctx"), alloc));

    cstmt_block_add(&case_block, cc_var_decl_str(error_type_name, str$("error"), call, alloc));

    // if (error == {case:upper}_SUCCESS)

    // error == {}_SUCCESS
    CExpr check_expr = cexpr_eq(cexpr_ident(str$("error"), alloc), cexpr_ident(success_constant_name, alloc), alloc);

    // if TRUE

    CStmt if_true_block = bidgen_c_dispatcher_case_if_success(iface, method, alloc);
    CStmt if_false_block = bidgen_c_dispatcher_case_if_error(iface, alloc);

    CStmt if_statement = cstmt_if_else(check_expr, if_true_block, if_false_block, alloc);

    cstmt_block_add(&case_block, if_statement);

    // break
    cstmt_block_add(&case_block, cstmt_break());
    cstmt_block_add(switch_block, case_stmt);
    cstmt_block_add(switch_block, case_block);
}

CDecl bidgen_c_dispatcher(BidIface const *iface, Alloc *alloc)
{
    Str server_handler_name = str_fmt(alloc, "{case:pascal}Server", iface->name);
    Str dispatcher_name = str_fmt(alloc, "{case:lower}_server_dispatch", iface->name);

    // static inline void {case:lower}_server_dispatch({case:pascal}Server* server, BrMsg const* req_msg)
    CType func_type = ctype_func(ctype_void(), alloc);
    ctype_member(&func_type, str$("server"), ctype_ptr(ctype_name(server_handler_name, alloc), alloc), alloc);
    ctype_member(&func_type, str$("req_msg"), ctype_ptr(ctype_attr(ctype_name(str$("BrMsg"), alloc), CTYPE_CONST), alloc), alloc);
    CStmt func_statement = cstmt_block(alloc);

    // BrMsg resp_msg = {}
    cstmt_block_add(&func_statement, cc_var_decl_str(str$("BrMsg"), str$("resp_msg"), cexpr_initializer(alloc), alloc));

    /*
        switch(req_msg->type) {
            case X:
            {
            }
            ...
        }
        */

    // req_msg->type
    CExpr case_expr = cc_ptr_access_str(str$("req_msg"), str$("type"), alloc);
    CStmt switch_block = cstmt_block(alloc);

    vec_foreach(method, &iface->methods)
    {
        bidgen_c_dispatcher_case(iface, &switch_block, method, alloc);
    }

    CStmt switch_statement = cstmt_switch(case_expr, switch_block, alloc);
    cstmt_block_add(&func_statement, switch_statement);

    // BrResult result = br_ev_resp(req_msg, &resp_msg)
    CExpr br_ev_resp_call = cexpr_call(alloc, cexpr_ident(str$("br_ev_resp"), alloc));

    cexpr_member(&br_ev_resp_call, cexpr_ident(str$("req_msg"), alloc));
    cexpr_member(&br_ev_resp_call, cexpr_ref(cexpr_ident(str$("resp_msg"), alloc), alloc));

    CStmt var_result = cc_var_decl_str(str$("BrResult"), str$("result"), br_ev_resp_call, alloc);
    cstmt_block_add(&func_statement, var_result);

    // if (result != BR_SUCCESS) {{ server->handle_error(req_msg->from, result, server->ctx); }}

    CExpr if_cond = cexpr_str_op(not_eq, str$("result"), str$("BR_SUCCESS"), alloc);
    CExpr server_handle = cexpr_ptr_access(cexpr_ident(str$("server"), alloc), cexpr_ident(str$("handle_error"), alloc), alloc);

    CExpr error_call = cexpr_call(alloc, server_handle);

    cexpr_member(&error_call, cc_ptr_access_str(str$("req_msg"), str$("from"), alloc));
    cexpr_member(&error_call, cexpr_ident(str$("result"), alloc));
    cexpr_member(&error_call, cc_ptr_access_str(str$("server"), str$("ctx"), alloc));

    CStmt if_stmt = cstmt_if(if_cond, cstmt_expr(error_call), alloc);

    cstmt_block_add(&func_statement, if_stmt);

    CDecl decl = cdecl_func(dispatcher_name, func_type, func_statement, alloc);
    decl = cdecl_attrib(decl, CDECL_STATIC | CDECL_INLINE);
    // -------------------------------------------------

    // "if (result != BR_SUCCESS) {{ server->handle_error(req_msg->from, result, server->ctx); }}\n");

    return decl;
}

CDecl bidgen_c_server_structure_declaration(BidIface const *iface, Alloc *alloc)
{
    /*
        typedef struct
        {
            void *ctx;
            {}{}Fn *handle_{};
            {}ErrorFn *handle_error;
        } {}Server;
    */

    Str error_fn_name = str_fmt(alloc, "{case:pascal}ErrorFn", iface->name);
    Str server_handler_name = str_fmt(alloc, "{case:pascal}Server", iface->name);

    CType server_handle = ctype_struct(alloc);

    ctype_member(&server_handle, str$("ctx"), ctype_ptr(ctype_void(), alloc), alloc);

    vec_foreach(method, &iface->methods)
    {
        Str func_type_name = str_fmt(alloc, "{case:pascal}Fn", method.name);
        Str member_name = str_fmt(alloc, "handle_{}", method.name);
        CType func_type = ctype_ptr(ctype_name(func_type_name, alloc), alloc);
        ctype_member(&server_handle, member_name, func_type, alloc);
    }

    ctype_member(&server_handle, str$("handle_error"), ctype_ptr(ctype_name(error_fn_name, alloc), alloc), alloc);

    return cdecl_type(server_handler_name, server_handle, alloc);
}

void bidgen_c_methods(CUnit *unit, BidIface const *iface, Alloc *alloc)
{

    Str m_type_str = str_fmt(alloc, "{case:pascal}MessageType", iface->name);

    CDecl m_type_def = cdecl_type(m_type_str, bidgen_c_method_message_type(iface, alloc), alloc);
    cunit_member(unit, cunit_decl(m_type_def));

    vec_foreach(method, &iface->methods)
    {
        cunit_member(unit, cunit_decl(bidgen_c_method(iface, &method, alloc)));
    }

    vec_foreach(method, &iface->methods)
    {
        cunit_member(unit, cunit_decl(bidgen_c_callback(iface, &method, alloc)));
    }

    Str error_fn_name = str_fmt(alloc, "{case:pascal}ErrorFn", iface->name);

    {
        // typedef void {case:pascal}ErrorFn(BrId from, BrResult error, void* ctx);

        CType t = ctype_func(ctype_void(), alloc);
        ctype_member(&t, str$("from"), ctype_name(str$("BrId"), alloc), alloc);
        ctype_member(&t, str$("error"), ctype_name(str$("BrResult"), alloc), alloc);
        ctype_member(&t, str$("ctx"), ctype_ptr(ctype_void(), alloc), alloc);
        CDecl typedef_decl = cdecl_type(error_fn_name, t, alloc);
        cunit_member(unit, cunit_decl(typedef_decl));
    }

    CDecl typedef_decl = bidgen_c_server_structure_declaration(iface, alloc);
    cunit_member(unit, cunit_decl(typedef_decl));

    CDecl dispatcher_decl = bidgen_c_dispatcher(iface, alloc);
    cunit_member(unit, cunit_decl(dispatcher_decl));
}

CUnit bidgen_c(BidIface const *iface, Alloc *alloc)
{
    CUnit unit = cunit(alloc);

    cunit_member(&unit, cunit_pragma_once(alloc));
    cunit_member(&unit, cunit_include(true, str$("brutal/types.h"), alloc));
    cunit_member(&unit, cunit_include(true, str$("brutal/ds/vec.h"), alloc));
    cunit_member(&unit, cunit_include(true, str$("bal/types.h"), alloc));
    cunit_member(&unit, cunit_include(true, str$("bal/ev.h"), alloc));

    Str protocol_id = str_fmt(alloc, "{case:upper}_PROTOCOL_ID", iface->name);
    cunit_member(&unit, cunit_define(protocol_id, cexpr_cast(cexpr_constant(cval_unsigned(iface->id)), ctype_name(str$("uint32_t"), alloc), alloc), alloc));

    bidgen_c_typedef(&unit, iface, alloc);

    bidgen_c_methods(&unit, iface, alloc);

    return unit;
}
