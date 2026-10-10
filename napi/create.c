
// a more conventional addon

#include <stdio.h>
#include <node_api.h>

#define NODE_API_CALL(env, call) \
	do { \
		napi_status status = (call); \
		if(status != napi_ok) \
		{ \
			const napi_extended_error_info * error_info = NULL; \
			napi_get_last_error_info((env), &error_info); \
			const char * error_message = error_info->error_message; \
			bool is_pending; \
			napi_is_exception_pending((env), &is_pending); \
			if(!is_pending) \
			{ \
				const char * message = error_message ? error_message : "error"; \
				napi_throw_error((env), NULL, message); \
			} \
			return NULL; \
		} \
	} while(0)

static napi_value CreateInt32(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_int32(env, 123, &result));
	return result;
}

static napi_value CreateUint32(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_uint32(env, 123, &result));
	return result;
}

static napi_value CreateInt64(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_int64(env, 9876543210ll, &result));
	return result;
}

/*static napi_value CreateUint64(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_uint64(env, 9876543210ll, &result));
	return result;
}*/

static napi_value CreateDouble(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_double(env, 123.456, &result));
	return result;
}

static napi_value CreateBigInt64(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_bigint_int64(env, -9876543210ll, &result));
	return result;
}

static napi_value CreateBigUint64(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_bigint_uint64(env, 9876543210ll, &result));
	return result;
}

static napi_value CreateBigIntWords(napi_env env, napi_callback_info info)
{
	napi_value result;
	static const uint64_t words[] = { 0, 1 };
	NODE_API_CALL(env, napi_create_bigint_words(env, /* sign_bit = */ 1, /* word_count = */ 2, words, &result));
	return result;
}

static napi_value CreateStringLatin1(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_string_latin1(env, "[\xC1]", NAPI_AUTO_LENGTH, &result));
	return result;
}

static napi_value CreateStringUTF16(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_string_utf16(env, u"[\u00C2]", NAPI_AUTO_LENGTH, &result));
	return result;
}

static napi_value CreateStringUTF8(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_string_utf8(env, "[\xC3\x83]", NAPI_AUTO_LENGTH, &result));
	return result;
}

static napi_value CreateArray(napi_env env, napi_callback_info info)
{
	napi_value result;
	napi_value element;
	NODE_API_CALL(env, napi_create_array(env, &result));
	NODE_API_CALL(env, napi_create_double(env, 123.456, &element));
	NODE_API_CALL(env, napi_set_named_property(env, result, "0", element));
	NODE_API_CALL(env, napi_get_boolean(env, true, &element));
	NODE_API_CALL(env, napi_set_named_property(env, result, "1", element));
	NODE_API_CALL(env, napi_create_string_utf16(env, u"[\u00C2]", NAPI_AUTO_LENGTH, &element));
	NODE_API_CALL(env, napi_set_named_property(env, result, "2", element));
	return result;
}

static void finalize_external(napi_env env, void * finalize_data, void * finalize_hint)
{
	printf("Finalizing %p\n", finalize_data);
}

static napi_value CreateExternal(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_create_external(env, (void *)0x1234, finalize_external, NULL, &result));
	return result;
}

static napi_value CreateSymbol(napi_env env, napi_callback_info info)
{
	napi_value description;
	NODE_API_CALL(env, napi_create_string_utf16(env, u"DefinedSymbol", NAPI_AUTO_LENGTH, &description));
	napi_value result;
	NODE_API_CALL(env, napi_create_symbol(env, description, &result));
	return result;
}

static napi_value GetBoolean(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_get_boolean(env, true, &result));
	return result;
}

static napi_value GetNull(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_get_null(env, &result));
	return result;
}

static napi_value GetUndefined(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_get_undefined(env, &result));
	return result;
}

static napi_value GetGlobal(napi_env env, napi_callback_info info)
{
	napi_value result;
	NODE_API_CALL(env, napi_get_global(env, &result));
	return result;
}

#define ADD_METHOD(env, object, name, function) \
	do { \
		napi_value result; \
		NODE_API_CALL((env), napi_create_function( \
			(env), \
			(name), \
			NAPI_AUTO_LENGTH, \
			(function), \
			NULL, \
			&result)); \
		NODE_API_CALL((env), napi_set_named_property( \
			(env), \
			(object), \
			(name), \
			result)); \
	} while(0)

NAPI_MODULE_INIT(/*napi_env env, napi_value exports*/)
{
	ADD_METHOD(env, exports, "createInt32", CreateInt32);
	ADD_METHOD(env, exports, "createUint32", CreateUint32);
	ADD_METHOD(env, exports, "createInt64", CreateInt64);
	//ADD_METHOD(env, exports, "createUint64", CreateUint64);
	ADD_METHOD(env, exports, "createDouble", CreateDouble);
	ADD_METHOD(env, exports, "createBigInt64", CreateBigInt64);
	ADD_METHOD(env, exports, "createBigUint64", CreateBigUint64);
	ADD_METHOD(env, exports, "createBigIntWords", CreateBigIntWords);
	ADD_METHOD(env, exports, "createStringLatin1", CreateStringLatin1);
	ADD_METHOD(env, exports, "createStringUTF16", CreateStringUTF16);
	ADD_METHOD(env, exports, "createStringUTF8", CreateStringUTF8);
	ADD_METHOD(env, exports, "createArray", CreateArray);
	ADD_METHOD(env, exports, "createExternal", CreateExternal);
	ADD_METHOD(env, exports, "createSymbol", CreateSymbol);
	ADD_METHOD(env, exports, "getBoolean", GetBoolean);
	ADD_METHOD(env, exports, "getNull", GetNull);
	ADD_METHOD(env, exports, "getUndefined", GetUndefined);
	ADD_METHOD(env, exports, "getGlobal", GetGlobal);
	return exports;
}

