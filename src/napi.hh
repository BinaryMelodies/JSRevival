#ifndef _NAPI_HH
#define _NAPI_HH

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "modules.hh"
#include "library.hh"

namespace JS
{
	namespace Napi
	{
		class Scope
		{
		public:
			std::set<gcn::gc_ptr<JS::Class::Object>> used_objects;
			std::set<std::shared_ptr<JS::Class::Internal::PrimitiveValue>> used_primitives;

			std::shared_ptr<Scope> outer_scope;

			Scope(std::shared_ptr<Scope> outer_scope = nullptr)
				: outer_scope(outer_scope)
			{
			}

			~Scope();
		};

		class Environment
		{
		public:
			static JSValue JS_false;
			static JSValue JS_true;

			JS::Library * library = nullptr;
			std::shared_ptr<Scope> current_scope;
			std::optional<JSValue> pending_exception;

			Environment() { }

			JSValue LoadLibrary(std::string path);
			std::shared_ptr<Scope> OpenScope();
			void CloseScope(std::shared_ptr<Scope> up_to_scope);
		};

		class CallbackFunction : public JS::Runtime::Builtin::BuiltinFunction
		{
		public:
			Environment * environment;
			/* napi_callback */ void * callback;
			void * data;

			CallbackFunction(Environment * environment, JSString name, void * callback, void * data);

			struct CallbackInformation
			{
				JSValue this_argument;
				std::vector<JSValue> * arguments;
				std::optional<JSFunction> new_target;
				void * data;
			};

			JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			JSValue Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase)) override;
		};

		class NativeModuleRecord : public JS::Runtime::ScriptRecord
		{
		public:
			Environment * environment;

			NativeModuleRecord(std::optional<std::filesystem::path> filename, Runtime::Realm * Realm)
				: ScriptRecord(filename, Realm, nullptr)
			{
			}

			JSValue Perform() override;
		};
	}
}

#endif // _NAPI_HH
