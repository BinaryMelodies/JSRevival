
#include <node.h>
#include <node_api.h>
#include "macros.hh"
#include "execute.hh"
#include "environment.hh"
#include "napi.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

JSValue JS::Napi::Environment::JS_false = false;
JSValue JS::Napi::Environment::JS_true = true;

static inline napi_env environment_to_napi_env(JS::Napi::Environment * env)
{
	return reinterpret_cast<napi_env>(env);
}

static inline JS::Napi::Environment * napi_env_to_environment(napi_env env)
{
	return reinterpret_cast<JS::Napi::Environment *>(env);
}

static inline napi_value value_to_napi_value(JS::Napi::Environment * env, JSValue value)
{
	auto pointer = JSValue::UnwrapValue(value);
	if(env->current_scope != nullptr)
	{
		if(auto u = dynamic_cast<JS::Class::Internal::PrimitiveValue *>(pointer))
		{
			env->current_scope->used_primitives.insert(u->shared_from_this());
		}
		else if(auto o = dynamic_cast<JS::Class::Object *>(pointer))
		{
			env->current_scope->used_objects.insert(gcn::gc_ptr<JS::Class::Object>(o));
		}
	}
	return reinterpret_cast<napi_value>(static_cast<JS::Class::Internal::Value *>(pointer));
}

static inline JSValue napi_value_to_value([[maybe_unused]] JS::Napi::Environment * env, napi_value value)
{
	if(value)
	{
		return JSValue::WrapValue(reinterpret_cast<JS::Class::Internal::Value *>(value));
	}
	else
	{
		return JS_undefined; // TODO
	}
}

JS::Napi::Scope::~Scope()
{
	for(auto primitive_pointer : used_primitives)
	{
		// release from preallocation
		JSValue::ReleaseValue(primitive_pointer.get());
	}
}

JSValue JS::Napi::Environment::LoadLibrary(std::string path)
{
	library = JS::LibraryLoader::Host.Open(path);
	if(library == nullptr)
	{
		JS::Throw(JS::New<JS::Class::Error> __LOC("cannot load module"));
	}

	auto register_module = library->Load<napi_value(napi_env, napi_value)>("napi_register_module_v1");
	if(register_module == nullptr)
	{
		library->Close();
		delete library;
		JS::Throw(JS::New<JS::Class::Error> __LOC("cannot load module"));
	}

	auto current_scope = OpenScope();

	auto result =
		napi_value_to_value(
			this,
			register_module(
				environment_to_napi_env(this),
				value_to_napi_value(this, JS::New<JS::Class::OrdinaryObject>())
			));

	CloseScope(current_scope);

#if 0
	for(auto primitive : used_primitives)
	{
		// release from preallocation
		JSValue::WrapValue(primitive.get());
	}
#endif

	return result;
}

std::shared_ptr<JS::Napi::Scope> JS::Napi::Environment::OpenScope()
{
	return current_scope = std::make_shared<Scope>(current_scope);
}

void JS::Napi::Environment::CloseScope(std::shared_ptr<Scope> up_to_scope)
{
	while(current_scope != nullptr)
	{
		bool closing_final_scope = current_scope == up_to_scope;
		current_scope = current_scope->outer_scope;
		if(closing_final_scope)
		{
			break;
		}
	}
}

//// CallbackFunction

JS::Napi::CallbackFunction::CallbackFunction(Environment * environment, JSString name, void * callback, void * data)
	: BuiltinFunction(GetCurrentRealm(), name, 0, true), environment(environment), callback(callback), data(data)
{
}

JSValue JS::Napi::CallbackFunction::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO: phase should be Run
	std::unique_ptr<CallbackInformation> cbinfo = std::make_unique<CallbackInformation>();
	cbinfo->this_argument = this_argument;
	cbinfo->arguments = &arguments;
	cbinfo->data = data;

	auto current_scope = environment->OpenScope();
	auto result = napi_value_to_value(
		environment,
		reinterpret_cast<napi_callback>(callback)(
			environment_to_napi_env(environment),
			reinterpret_cast<napi_callback_info>(cbinfo.get())));
	environment->CloseScope(current_scope);
	return result;
}

JSValue JS::Napi::CallbackFunction::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	// TODO: phase should be Run
	std::unique_ptr<CallbackInformation> cbinfo = std::make_unique<CallbackInformation>();

	JSValue proto;
	if(CurrentVersion < Version::ES6)
	{
		proto = JS::Get(this, u"prototype");
	}
	else
	{
		proto = JS::Get(new_target, u"prototype");
	}

	if(IsPrimitive(proto))
	{
		proto = GetFunctionRealm()->Intrinsics.Object_prototype;
		// TODO: the language of ES1 seems to claim this is Object.prototype, whatever its current state is, however Object.prototype is supposed to be read-only
	}

	cbinfo->this_argument = JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto);
	cbinfo->arguments = &arguments;
	cbinfo->new_target = new_target;
	cbinfo->data = data;

	auto current_scope = environment->OpenScope();
	auto result = napi_value_to_value(
		environment,
		reinterpret_cast<napi_callback>(callback)(
			environment_to_napi_env(environment),
			reinterpret_cast<napi_callback_info>(cbinfo.get())));
	environment->CloseScope(current_scope);
	return result;
}

JSValue JS::Napi::NativeModuleRecord::Perform()
{
	JS::Runtime::ExecutionContext * scriptContext = new JS::Runtime::ExecutionContext();
	scriptContext->Function = nullptr;
	scriptContext->Realm = Realm;
	scriptContext->ScriptOrModule = this;
	scriptContext->VariableEnvironment = Realm->GlobalEnv;
	scriptContext->LexicalEnvironment = Realm->GlobalEnv;
	scriptContext->PrivateEnvironment = nullptr;
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
	scriptContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(scriptContext);

	environment = new (GC) Environment();

	auto exports = environment->LoadLibrary(sourcefilename.value());
	JS::Set(CommonJSModule, u"exports", exports);

	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	return exports;
}

//// values

extern "C" napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype * result)
{
	auto v = napi_value_to_value(napi_env_to_environment(env), value);
	if(JS::IsUndefined(v))
	{
		*result = napi_undefined;
	}
	else if(JS::IsNull(v))
	{
		*result = napi_null;
	}
	else if(JS::GetBoolean(v))
	{
		*result = napi_boolean;
	}
	else if(JS::GetNumber(v))
	{
		*result = napi_number;
	}
	else if(JS::GetString(v))
	{
		*result = napi_string;
	}
	else if(JS::GetSymbol(v))
	{
		*result = napi_symbol;
	}
	else if(JS::GetBigInt(v))
	{
		*result = napi_bigint;
	}
	else if(JS::IsCallable(v))
	{
		*result = napi_function;
	}
	else if(v.cast_object<JS::Class::External>())
	{
		*result = napi_external;
	}
	else if(JS::GetNonNullObject(v))
	{
		*result = napi_object;
	}
	else
	{
		return napi_invalid_arg; // TODO: how should we represent these values?
	}
	return napi_ok;
}

extern "C" napi_status napi_get_undefined(napi_env env, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JS_undefined);
	return napi_ok;
}

extern "C" napi_status napi_get_null(napi_env env, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JS_null);
	return napi_ok;
}

//// boolean

extern "C" napi_status napi_get_boolean(napi_env env, bool value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), value ? JS::Napi::Environment::JS_true : JS::Napi::Environment::JS_false);
	return napi_ok;
}

extern "C" napi_status napi_get_value_bool(napi_env env, napi_value value, bool * result)
{
	auto v = napi_value_to_value(napi_env_to_environment(env), value);
	if(auto b = JS::GetBoolean(v))
	{
		*result = b.value();
		return napi_ok;
	}
	else
	{
		return napi_boolean_expected;
	}
}

//// numbers

extern "C" napi_status napi_create_int32(napi_env env, int32_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSNumber(value));
	return napi_ok;
}

extern "C" napi_status napi_create_uint32(napi_env env, uint32_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSNumber(value));
	return napi_ok;
}

extern "C" napi_status napi_create_int64(napi_env env, int64_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSNumber(value));
	return napi_ok;
}

// not part of Node API, but included for symmetry
extern "C" napi_status napi_create_uint64(napi_env env, uint64_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSNumber(value));
	return napi_ok;
}

extern "C" napi_status napi_create_double(napi_env env, double value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), value);
	return napi_ok;
}

extern "C" napi_status napi_get_value_double(napi_env env, napi_value value, double * result)
{
	auto v = napi_value_to_value(napi_env_to_environment(env), value);
	if(auto n = JS::GetNumber(v))
	{
		*result = n.value();
		return napi_ok;
	}
	else
	{
		return napi_number_expected;
	}
}

//// bigints

extern "C" napi_status napi_create_bigint_int64(napi_env env, int64_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSBigInt(value));
	return napi_ok;
}

extern "C" napi_status napi_create_bigint_uint64(napi_env env, uint64_t value, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JSBigInt(value));
	return napi_ok;
}

extern "C" napi_status napi_create_bigint_words(napi_env env, int sign_bit, size_t word_count, const uint64_t * words, napi_value * result)
{
	JSBigInt value;
	mpz_import(value.get_mpz_t(), word_count, -1, sizeof(uint64_t), 0, 0, reinterpret_cast<const void *>(words));
	*result = value_to_napi_value(napi_env_to_environment(env), sign_bit ? -value : value);
	return napi_ok;
}

extern "C" napi_status napi_get_value_bigint_words(napi_env env, napi_value value, int * sign_bit, size_t * word_count, uint64_t * words)
{
	auto v = napi_value_to_value(napi_env_to_environment(env), value);
	if(auto b = JS::GetBigInt(v))
	{
		size_t allocated_word_count = (mpz_sizeinbase(b.value().get_mpz_t(), 2) + 8 * sizeof(uint64_t) - 1) / (8 * sizeof(uint64_t));
		if(words != NULL)
		{
			uint64_t * actual_words = words;
			if(word_count != NULL && *word_count < allocated_word_count)
			{
				actual_words = new uint64_t[allocated_word_count];
			}
			mpz_export(reinterpret_cast<void *>(actual_words), NULL, -1, sizeof(uint64_t), 0, 0, b.value().get_mpz_t());
			if(word_count != NULL && *word_count < allocated_word_count)
			{
				memcpy(words, actual_words, sizeof(uint64_t) * *word_count);
				delete actual_words;
			}
		}
		if(word_count != NULL)
		{
			*word_count = allocated_word_count;
		}
		if(sign_bit != NULL)
		{
			*sign_bit = sgn(b.value()) < 0;
		}
		return napi_ok;
	}
	else
	{
		return napi_bigint_expected;
	}
}

//// strings

extern "C" napi_status napi_create_string_latin1(napi_env env, const char * str, size_t length, napi_value * result)
{
	JSString s;
	for(size_t index = 0; length == NAPI_AUTO_LENGTH ? str[index] != '\0' : index < length; index++)
	{
		s += char16_t(uint8_t(str[index]));
	}
	*result = value_to_napi_value(napi_env_to_environment(env), s);
	return napi_ok;
}

extern "C" napi_status napi_create_string_utf16(napi_env env, const char16_t * str, size_t length, napi_value * result)
{
	std::u16string s = length == NAPI_AUTO_LENGTH ? std::u16string(str) : std::u16string(str, length);
	*result = value_to_napi_value(napi_env_to_environment(env), s);
	return napi_ok;
}

extern "C" napi_status napi_create_string_utf8(napi_env env, const char * str, size_t length, napi_value * result)
{
	std::string s = length == NAPI_AUTO_LENGTH ? std::string(str) : std::string(str, length);
	*result = value_to_napi_value(napi_env_to_environment(env), JS::UTF8toUTF16(s));
	return napi_ok;
}

extern "C" napi_status napi_get_value_string_utf8(napi_env env, napi_value value, char * buf, size_t bufsize, size_t * result)
{
	auto v = napi_value_to_value(napi_env_to_environment(env), value);
	if(auto s = JS::GetString(v))
	{
		std::ostringstream out;
		JS::PrintString(out, s.value());
		std::string str = out.str();
		if(buf == NULL)
		{
			*result = str.size();
		}
		else
		{
			if(bufsize < str.size() + 1)
			{
				memcpy(buf, str.c_str(), bufsize - 1);
				buf[bufsize - 1] = '\0';
				*result = bufsize - 1;
			}
			else
			{
				memcpy(buf, str.c_str(), str.size() + 1);
				*result = str.size();
			}
		}
		return napi_ok;
	}
	else
	{
		return napi_string_expected;
	}
}

//// symbols

extern "C" napi_status napi_create_symbol(napi_env env, napi_value description, napi_value * result)
{
	auto environment = napi_env_to_environment(env);
	JSSymbol symbol;
	if(auto description_option = JS::GetString(napi_value_to_value(environment, description)))
	{
		symbol = new JS::Runtime::SymbolInstance(description_option.value());
	}
	else
	{
		symbol = new JS::Runtime::SymbolInstance(); // TODO: any error?
	}
	*result = value_to_napi_value(environment, symbol);
	return napi_ok;
}

//// objects

extern "C" napi_status napi_create_object(napi_env env, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JS::New<JS::Class::OrdinaryObject>());
	return napi_ok;
}

extern "C" napi_status napi_set_named_property(napi_env env, napi_value object, const char * utf8name, napi_value value)
{
	auto environment = napi_env_to_environment(env);
#if 0
	if(auto object_option = JS::GetNonNullObject(napi_value_to_value(environment, object)))
	{
		JS::Set(object_option.value(), JS::UTF8toUTF16(utf8name), napi_value_to_value(environment, value));
	}
#endif
	JS::Set(JS::ToObject(napi_value_to_value(environment, object)), JS::UTF8toUTF16(utf8name), napi_value_to_value(environment, value));
	return napi_ok;
}

extern "C" napi_status napi_get_named_property(napi_env env, napi_value object, const char * utf8name, napi_value * result)
{
	auto environment = napi_env_to_environment(env);
#if 0
	if(auto object_option = JS::GetNonNullObject(napi_value_to_value(environment, object)))
	{
		*result = value_to_napi_value(environment, JS::Get(object_option.value(), JS::UTF8toUTF16(utf8name)));
		return napi_ok;
	}
	else
	{
		return napi_object_expected;
	}
#endif
	*result = value_to_napi_value(environment, JS::Get(JS::ToObject(napi_value_to_value(environment, object)), JS::UTF8toUTF16(utf8name)));
	return napi_ok;
}

extern "C" napi_status napi_get_global(napi_env env, napi_value * result)
{
	auto realm = JS::GetCurrentRealm();
	*result = value_to_napi_value(napi_env_to_environment(env), realm ? realm->GlobalObject : JS_null);
	return napi_ok;
}

//// arrays

extern "C" napi_status napi_create_array(napi_env env, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), JS::New<JS::Class::Array>());
	return napi_ok;
}

//// externals

extern "C" napi_status napi_create_external(napi_env env, void * data, node_api_nogc_finalize finalize_cb, void * finalize_hint, napi_value * result)
{
	*result = value_to_napi_value(napi_env_to_environment(env), gcn::make_gc<JS::Class::External>(data, reinterpret_cast<void *>(finalize_cb), finalize_hint));
	return napi_ok;
}


extern "C" napi_status napi_get_value_external(napi_env env, napi_value value, void ** result)
{
	auto environment = napi_env_to_environment(env);
	if(auto external_option = napi_value_to_value(environment, value).cast_object<JS::Class::External>())
	{
		*result = external_option->data;
		return napi_ok;
	}
	else
	{
		return napi_invalid_arg;
	}
}

//// functions

extern "C" napi_status napi_create_function(napi_env env, const char * utf8name, size_t length, napi_callback cb, void * data, napi_value * result)
{
	auto environment = napi_env_to_environment(env);
	std::string s = length == NAPI_AUTO_LENGTH ? std::string(utf8name) : std::string(utf8name, length);
	auto callback = JS::New<JS::Napi::CallbackFunction>(environment, JS::UTF8toUTF16(utf8name), reinterpret_cast<void *>(cb), data);
	*result = value_to_napi_value(environment, callback);
	return napi_ok;
}

extern "C" napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo, size_t * argc, napi_value * argv, napi_value * thisArg, void ** data)
{
	JS::Napi::CallbackFunction::CallbackInformation * info = reinterpret_cast<JS::Napi::CallbackFunction::CallbackInformation *>(cbinfo);
	auto environment = napi_env_to_environment(env);

	if(argv != NULL)
	{
		for(size_t index = 0; index < *argc; index ++)
		{
			JSValue argument = JS_undefined;
			if(index < info->arguments->size())
			{
				argument = (*info->arguments)[index];
			}
			argv[index] = value_to_napi_value(environment, argument);
		}
	}

	if(argc != NULL)
	{
		*argc = info->arguments->size();
	}

	if(thisArg != NULL)
	{
		*thisArg = value_to_napi_value(environment, info->this_argument);
	}

	if(data != NULL)
	{
		*data = info->data;
	}

	return napi_ok;
}

extern "C" napi_status napi_get_new_target(napi_env env, napi_callback_info cbinfo, napi_value * result)
{
	JS::Napi::CallbackFunction::CallbackInformation * info = reinterpret_cast<JS::Napi::CallbackFunction::CallbackInformation *>(cbinfo);
	auto environment = napi_env_to_environment(env);
	if(info->new_target.has_value())
	{
		*result = value_to_napi_value(environment, info->new_target.value());
	}
	else
	{
		*result = nullptr;
	}
	return napi_ok;
}

extern "C" napi_status napi_call_function(napi_env env, napi_value recv, napi_value func, size_t argc, const napi_value * argv, napi_value * result)
{
	auto environment = napi_env_to_environment(env);
	if(auto callable = JS::GetFunction(napi_value_to_value(environment, func)))
	{
		std::vector<JSValue> arguments;

		for(size_t index = 0; index < argc; index++)
		{
			arguments.push_back(napi_value_to_value(environment, argv[index]));
		}

		*result = value_to_napi_value(
			environment,
			JS::Call(
				callable.value(),
				napi_value_to_value(environment, recv),
				arguments
				RUNTIME_PHASE));

		return napi_ok;
	}
	else
	{
		return napi_function_expected;
	}
}

extern "C" napi_status napi_new_instance(napi_env env, napi_value cons, size_t argc, const napi_value * argv, napi_value * result)
{
	auto environment = napi_env_to_environment(env);
	if(auto callable = JS::GetFunction(napi_value_to_value(environment, cons)))
	{
		std::vector<JSValue> arguments;

		for(size_t index = 0; index < argc; index++)
		{
			arguments.push_back(napi_value_to_value(environment, argv[index]));
		}

		*result = value_to_napi_value(
			environment,
			JS::Construct(
				callable.value(),
				arguments,
				callable.value()
				RUNTIME_PHASE));

		return napi_ok;
	}
	else
	{
		return napi_function_expected;
	}
}

//// errors

extern "C" napi_status napi_get_last_error_info(node_api_nogc_env env, const napi_extended_error_info ** result)
{
	auto environment = napi_env_to_environment(const_cast<napi_env>(env));

	static napi_extended_error_info error_info;
	if(environment->pending_exception.has_value())
	{
		error_info.error_message = nullptr; // TODO
		error_info.error_code = napi_ok; // TODO
	}
	else
	{
		error_info.error_message = nullptr;
		error_info.error_code = napi_ok;
	}
	*result = &error_info;
	return napi_ok;
}

extern "C" napi_status napi_is_exception_pending(napi_env env, bool * result)
{
	auto environment = napi_env_to_environment(env);
	*result = environment->pending_exception.has_value();
	return napi_ok;
}

extern "C" napi_status napi_throw(napi_env env, napi_value error)
{
	auto environment = napi_env_to_environment(env);
	environment->pending_exception = napi_value_to_value(environment, error);
	return napi_ok;
}

extern "C" napi_status napi_throw_error([[maybe_unused]] napi_env env, [[maybe_unused]] const char * code, [[maybe_unused]] const char * msg)
{
	// TODO
	return napi_ok;
}

