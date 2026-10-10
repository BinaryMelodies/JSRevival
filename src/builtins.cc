
#include <cassert>
#include <cmath>
#include <cstring>
#include <gmpxx.h>
#include <optional>
#include <sstream>
#include <string>
#include <unicode/uchar.h>
#include <variant>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "core.hh"
#include "iterator.hh"
#include "promise.hh"
#include "execute.hh"
#include "environment.hh"
#include "builtins.hh"
#include "debugger.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

const JSSymbol JS::Symbols::hasInstance = new JS::Runtime::SymbolInstance(u"Symbol.hasInstance");
const JSSymbol JS::Symbols::isConcatSpreadable = new JS::Runtime::SymbolInstance(u"Symbol.isConcatSpreadable");
const JSSymbol JS::Symbols::iterator = new JS::Runtime::SymbolInstance(u"Symbol.iterator");
const JSSymbol JS::Symbols::match = new JS::Runtime::SymbolInstance(u"Symbol.match");
const JSSymbol JS::Symbols::replace = new JS::Runtime::SymbolInstance(u"Symbol.replace");
const JSSymbol JS::Symbols::search = new JS::Runtime::SymbolInstance(u"Symbol.search");
const JSSymbol JS::Symbols::species = new JS::Runtime::SymbolInstance(u"Symbol.species");
const JSSymbol JS::Symbols::split = new JS::Runtime::SymbolInstance(u"Symbol.split");
const JSSymbol JS::Symbols::toPrimitive = new JS::Runtime::SymbolInstance(u"Symbol.toPrimitive");
const JSSymbol JS::Symbols::toStringTag = new JS::Runtime::SymbolInstance(u"Symbol.toStringTag");
const JSSymbol JS::Symbols::unscopables = new JS::Runtime::SymbolInstance(u"Symbol.unscopables");

// ESNext
const JSSymbol JS::Symbols::asyncIterator = new JS::Runtime::SymbolInstance(u"Symbol.asyncIterator");
// ESNext
const JSSymbol JS::Symbols::matchAll = new JS::Runtime::SymbolInstance(u"Symbol.matchAll");

thread_local JS::Runtime::Agent * JS::Runtime::Agent::CurrentAgent;

//// Builtin::BuiltinFunction

// TODO: move
JSObject JS::Runtime::Intrinsics::* JS::Runtime::Builtin::BuiltinFunction::IntrinsicPrototype = &JS::Runtime::Intrinsics::Function_prototype;
#if ENABLE_ES4
JSClass JS::Runtime::Intrinsics::* JS::Runtime::Builtin::BuiltinFunction::IntrinsicClass = &JS::Runtime::Intrinsics::Function_class;
#endif

JS::Runtime::Builtin::BuiltinFunction::BuiltinFunction(JS::Runtime::Realm * Realm, JSString name, uint32_t length, bool has_constructor_method)
	: InitialName(name), has_constructor_method(has_constructor_method), Realm(Realm)
{
	// TODO: duplicate for Class::Function
#if ENABLE_ES4
	if(IsVersionES4())
	{
		//add_slot(JS::Intrinsics()->Function_class, u"name" PUBLIC_NAMESPACES, name);
		add_slot(JS::Intrinsics()->Function_class, u"length" PUBLIC_NAMESPACES, JSNumber(length));
	}
	else
#endif
	{
		if(Version::ES6 <= CurrentVersion)
		{
			Define(u"name",
				JSProperty::MakeData(name, /* Writable = */ false, false, true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);
		}

		Define(
			u"length",
			JSProperty::MakeData(JSNumber(length), /* Writable = */ false, false, true)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}
}

#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::Runtime::Builtin::BuiltinFunction::GetSignature()
{
	return {}; // TODO
}
#endif

JS::Runtime::Realm * JS::Runtime::Builtin::BuiltinFunction::GetFunctionRealm()
{
	return Realm;
}

JSString JS::Runtime::Builtin::BuiltinFunction::GetClassString()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC()); // ES4 employs a different mechanism to obtain the class string
	}
#endif
	return u"Function";
}

/*JS::Class::Function::ConstructorKindType JS::Runtime::Builtin::BuiltinFunction::GetConstructorKind()
{
	return has_constructor_method ? JS::Class::Function::ConstructorKindType::Base : JS::Class::Function::ConstructorKindType::NonConstructor; // TODO: do we need more than this?
}*/

JS::Invocability JS::Runtime::Builtin::BuiltinFunction::GetInvocability()
{
	return has_constructor_method ? Invocability::Constructor : Invocability::NonConstructor;
	//return IsConstructor() ? Invocability::Constructor : Invocability::NonConstructor;
//	return GetConstructorKind() != JS::Class::Function::ConstructorKindType::NonConstructor ? Invocability::Constructor : Invocability::NonConstructor;
}

bool JS::Runtime::Builtin::BuiltinFunction::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	GetCurrentExecutionContext()->Action_Push<JSValue>(Call(this_argument, arguments RUNTIME_PHASE));
	return false; // call not deferred
}

//// Builtin::NativeFunction

JSValue JS::Runtime::Builtin::NativeFunction::Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(!callback)
	{
		if(!compile)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		Begin_Call(this_argument, arguments);
		return JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
	}

	JS::Runtime::CallContextCapture context_capture;

	// PrepareForOrdinaryCall
	auto callee_context = new JS::Runtime::ExecutionContext();
	callee_context->Function = JSFunction::MakeFunction(this);
	callee_context->Realm = Realm;
//	callee_context->ScriptOrModule = ScriptOrModule;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

	return callback(this_argument, arguments, nullptr WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::NativeFunction::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(!callback)
	{
		if(!compile)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		Begin_Construct(arguments, new_target);
		return JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>();
	}

	JS::Runtime::CallContextCapture context_capture;

	// PrepareForOrdinaryCall
	auto callee_context = new JS::Runtime::ExecutionContext();
	callee_context->Function = JSFunction::MakeFunction(this);
	callee_context->Realm = Realm;
//	callee_context->ScriptOrModule = ScriptOrModule;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

	return callback(std::optional<JSValue>{}, arguments, /*JSAbstractFunction*/(new_target) WITH_PHASE(phase));
}

bool JS::Runtime::Builtin::NativeFunction::Begin_Call(JSValue this_argument, std::vector<JSValue> arguments)
{
	if(compile)
	{
		// PrepareForOrdinaryCall
		auto callee_context = new JS::Runtime::ExecutionContext();
		callee_context->Function = JSFunction::MakeFunction(this);
		callee_context->Realm = Realm;
//		callee_context->ScriptOrModule = ScriptOrModule;
		JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

		JS::Interpreter::Code * code = new JS::Interpreter::Code();
		JS::Interpreter::State<> state = code->Begin();
		JS::Interpreter::FunctionRange<> function_range = JS::Interpreter::Code::BeginFunctionRange(state, false, /* is_generator = */ false, /* is_async = */ false); // TODO

		JS::Interpreter::State<std::optional<JSValue>> state1 = compile(state, this_argument, arguments, nullptr).Return<JSValue>();
		[[maybe_unused]] JS::Interpreter::State<JSValue> state2 = function_range.EndFunctionRange(state1);

		JS::Runtime::ExecutionContext::GetContextStack().back()->code = code;
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		JS::Runtime::ExecutionContext::GetContextStack()[JS::Runtime::ExecutionContext::GetContextStack().size() - 2]->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;

		// execution will now continue in function body
		return true; // execution deferred
	}
	else
	{
		GetCurrentExecutionContext()->Action_Push<JSValue>(Call(this_argument, arguments RUNTIME_PHASE));
		return false; // execution not deferred
	}
}

void JS::Runtime::Builtin::NativeFunction::Begin_Construct(std::vector<JSValue> arguments, JSFunction new_target)
{
	if(compile)
	{
		//JS::Class::OrdinaryObject * this_argument = nullptr;

		// PrepareForOrdinaryCall
		auto callee_context = new JS::Runtime::ExecutionContext();
		callee_context->Function = JSFunction::MakeFunction(this);
		callee_context->Realm = Realm;
//		callee_context->ScriptOrModule = ScriptOrModule;
		JS::Runtime::ExecutionContext::GetContextStack().push_back(callee_context);

		JS::Interpreter::Code * code = new JS::Interpreter::Code();
		JS::Interpreter::State<> state = code->Begin();
		JS::Interpreter::FunctionRange<> function_range = JS::Interpreter::Code::BeginFunctionRange(state, false, /* is_generator = */ false, /* is_async = */ false); // TODO

		//auto constructor_env = callee_context->LexicalEnvironment;

		JS::Interpreter::State<std::optional<JSValue>> state1 = compile(state, std::optional<JSValue>{}, arguments, /*JSAbstractFunction*/(new_target)).Return<JSValue>();
		[[maybe_unused]] JS::Interpreter::State<JSValue> state2 = function_range.EndFunctionRange(state1);

		JS::Runtime::ExecutionContext::GetContextStack().back()->code = code;
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		JS::Runtime::ExecutionContext::GetContextStack()[JS::Runtime::ExecutionContext::GetContextStack().size() - 2]->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;

		// execution will now continue in function body
	}
	else
	{
		GetCurrentExecutionContext()->Action_Push<JSValue>(Construct(arguments, new_target RUNTIME_PHASE));
	}
}

//// builtin TypedArray constructors

JSFunction JS::Runtime::Intrinsics::* JS::Runtime::Builtin::TypedArray::IntrinsicPrototype = &JS::Runtime::Intrinsics::TypedArray;

JSValue JS::Runtime::Builtin::TypedArray::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"typed array constructor called as function"));
}

JSValue JS::Runtime::Builtin::TypedArray::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	auto proto = GetPrototypeFromConstructor(new_target, prototype);
	if(arguments.size() == 0)
	{
		return JS::NewWithPrototype<JS::Class::TypedArray>(proto, element_type);
	}
	else if(auto first_argument = arguments[0].get_if<JSObject>())
	{
		auto typed_array = JS::NewWithPrototype<JS::Class::TypedArray>(proto, element_type, nullptr);
		// TODO
		return typed_array;
	}
	else
	{
		return JS::NewWithPrototype<JS::Class::TypedArray>(proto, element_type, JS::ToIndex(arguments[0]));
	}
}

////

//// builtin ArgumentError

JSValue JS::Runtime::Builtin::AggregateError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArgumentError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Array

JSValue JS::Runtime::Builtin::Array_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target == nullptr)
	{
		new_target = GetCurrentExecutionContext()->Function;
	}
	auto proto = GetPrototypeFromConstructor(new_target, &Intrinsics::Array_prototype);
	if(arguments.size() == 1)
	{
		if(auto len = GetNumber(arguments[0]))
		{
			auto int_len = ToUint32(*len RUNTIME_PHASE);
			if(arguments[0] != JSNumber(int_len))
			{
				JS::Throw(JS::New<RangeError> __LOC(u"invalid value for new array length"));
			}
			return JS::NewWithPrototype<JS::Class::Array>(proto, int_len);
		}
	}
	return JS::NewWithPrototype<JS::Class::Array>(proto, arguments);
}

//// builtin Array class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ArrayClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::ArrayClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC("array class constructor at compile time"));
	}

	return ConstructInstanceOf<JS::Class::Array>(arguments);
}

void JS::Runtime::Builtin::ArrayClass::Init(JSValue this_argument, std::vector<JSValue> arguments)
{
	if(arguments.size() == 1)
	{
		if(auto len = GetNumber(arguments[0]))
		{
			auto int_len = ToInteger(*len RUNTIME_PHASE);
			if(arguments[0] != int_len)
			{
				JS::Throw(JS::New<RangeError> __LOC(u"invalid value for new array length"));
			}
			std::dynamic_pointer_cast<JS::Class::Array>(this_argument.get<JSObject>())->set_length(JSPropertyDescriptor().withValue(int_len));
			return;
		}
	}
	std::dynamic_pointer_cast<JS::Class::Array>(this_argument.get<JSObject>())->init(arguments);
}
#endif

JSValue JS::Runtime::Builtin::ArrayBuffer_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC(u"calling ArrayBuffer constructor without 'new' not allowed"));
	}
	uintptr_t byte_length = JS::ToIndex(arguments.size() > 0 ? arguments[0] : JS_undefined);
	if(arguments.size() > 1)
	{
		if(auto options = arguments[1].get_if<JSObject>())
		{
			auto max_byte_length = Get(options.value(), u"maxByteLength");
			if(!IsUndefined(max_byte_length))
			{
				return JS::New<JS::Class::GrowableArrayBuffer>(byte_length, JS::ToIndex(max_byte_length));
			}
		}
	}
	return JS::New<JS::Class::FixedArrayBuffer>(byte_length);
}

JSValue JS::Runtime::Builtin::ArrayBuffer_isView_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_byteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_detached_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_maxByteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_resizable_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_resize_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_slice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_transfer_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_prototype_transferToFixedLength_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayBuffer_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ArrayIteratorPrototype_next_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		return new TypeError __LOC("not a constructor");
	}

	if(auto this_object = GetNonNullObject(this_argument.value()))
	{
		if(auto array_iterator = std::dynamic_pointer_cast<JS::Class::ArrayIterator>(this_object.value()))
		{
			if(!array_iterator->IteratedArrayLike.has_value())
			{
				return JS::CreateIteratorResultObject(JS_undefined, true);
			}
			JSObject array = array_iterator->IteratedArrayLike.value();

			uintptr_t len;
			if(auto typed_array = std::dynamic_pointer_cast<JS::Class::TypedArray>(array))
			{
				(void) typed_array;
				// TODO
			}
			else
			{
				len = JS::ToLength(JS::Get(array, u"length"));
			}

			uintptr_t index = array_iterator->ArrayLikeNextIndex;
			if(index >= len)
			{
				array_iterator->ArrayLikeNextIndex = {};
				return JS::CreateIteratorResultObject(JS_undefined, true);
			}
			array_iterator->ArrayLikeNextIndex ++;

			JSNumber index_number = index;
			JSValue result;
			if(array_iterator->ArrayLikeIterationKind == JS::Class::ArrayIterator::Kind::Key)
			{
				result = index_number;
			}
			else
			{
				JSValue element_value = JS::Get(array, JS::ToString(index_number RUNTIME_PHASE));
				switch(array_iterator->ArrayLikeIterationKind)
				{
				case JS::Class::ArrayIterator::Kind::Key:
					assert(false);
				case JS::Class::ArrayIterator::Kind::Value:
					result = element_value;
					break;
				case JS::Class::ArrayIterator::Kind::KeyValue:
					//result = JS::Class::Array::CreateArrayFromList({index_number, element_value});
					result = JS::New<JS::Class::Array>(std::vector<JSValue>{index_number, element_value});
					break;
				}
			}
			return JS::CreateIteratorResultObject(result, false);
		}
	}

	JS::Throw(JS::New<TypeError> __LOC());
}

JSValue JS::Runtime::Builtin::Array_from_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_isArray_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_of_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Array.prototype

JSValue JS::Runtime::Builtin::Array_prototype_at_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_concat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_copyWithin_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_entries_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_every_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_fill_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_filter_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_find_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_findIndex_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_findLast_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_findLastIndex_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_flat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_flatMap_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_forEach_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_includes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_indexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_join_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_keys_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_lastIndexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_map_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_pop_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_push_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_reduce_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_reduceRight_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_reverse_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_shift_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_slice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_some_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_sort_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_splice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_toLocaleString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_toReversed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_toSorted_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_toSpliced_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_unshift_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_prototype_values_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		return new TypeError __LOC("not a constructor");
	}
	return JS::New<JS::Class::ArrayIterator>(ToObject(this_argument.value()), JS::Class::ArrayIterator::Kind::Value);
}

JSValue JS::Runtime::Builtin::Array_prototype_with_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Array_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncFromSyncIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncFromSyncIteratorPrototype_return_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncFromSyncIteratorPrototype_throw_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncFunction_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncGeneratorFunction_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncGeneratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncGeneratorPrototype_return_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncGeneratorPrototype_throw_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AsyncIteratorPrototype_asyncIterator_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_add_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_and_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_compareExchage_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_exchange_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_isLockFree_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_load_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_notify_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return Atomics_wake_Call(this_argument, arguments, new_target WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::Atomics_or_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_store_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_sub_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_wait_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_waitAsync_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_wake_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Atomics_xor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Attribute class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::AttributeClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::AttributeClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JS::Runtime::TypeHint JS::Runtime::Builtin::AttributeClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

//// builtin AttributeError

JSValue JS::Runtime::Builtin::AttributeError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::BigInt_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target != nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	JSValue argument = arguments.size() >= 1 ? arguments[0] : JS_undefined;
	auto p = ToPrimitive(argument WITH_PHASE(phase), JS::Runtime::TypeHint::Number);
	if(auto n = p.get_if<JSNumber>())
	{
		int64_t i = *n;
		if(JSNumber(i) != *n)
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}
		else
		{
			return JSBigInt(i);
		}
	}
	return ToBigInt(p);
}

JSValue JS::Runtime::Builtin::BigInt_asIntN_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::BigInt_asUintN_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::BigInt_prototype_toLocaleString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::BigInt_prototype_toString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS::ToString(this_argument.value().cast_object<JS::Class::BigInt>()->BigIntData WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::BigInt_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Boolean

JSValue JS::Runtime::Builtin::Boolean_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	JSValue argument = arguments.size() >= 1 ? arguments[0] : JS_undefined;
	bool b = ToBoolean(argument);
	if(new_target == nullptr)
	{
		// called as function
		return b;
	}
	else
	{
		// called as constructor
		return JS::New<JS::Class::Boolean>(b);
	}
}

//// builtin Boolean class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::BooleanClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSFunction::MakeFunction(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::BooleanClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(false);
	}
	else
	{
		return JS::ToBoolean(arguments[0]);
	}
}

std::optional<JSValue> JS::Runtime::Builtin::BooleanClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(GetBoolean(object))
	{
		return object;
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::BooleanClass::GetTypeofString()
{
	return u"boolean";
}

std::optional<JSValue> JS::Runtime::Builtin::BooleanClass::GetDefaultValue()
{
	return false;
}
#endif

//// builtin Boolean.prototype

JSValue JS::Runtime::Builtin::Boolean_prototype_toString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS::ToString(this_argument.value().cast_object<JS::Class::Boolean>()->BooleanData WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::Boolean_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin char

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::char_fromCharCode_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::char_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::char_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

//// builtin Class class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ClassClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ClassClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSString JS::Runtime::Builtin::ClassClass::GetTypeofString()
{
	return u"function";
}

JS::Runtime::TypeHint JS::Runtime::Builtin::ClassClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}

JSValue JS::Runtime::Builtin::Class_prototype_get(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return std::dynamic_pointer_cast<JS::Class::Class>(this_argument.value().get<JSObject>())->ClassPrototype.value_or(JS_undefined);
}

JSValue JS::Runtime::Builtin::String_length_get(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JSNumber(this_argument.value().get<JSString>().size());
}

JSValue JS::Runtime::Builtin::Array_length_get(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JSNumber(std::dynamic_pointer_cast<JS::Class::Array>(this_argument.value().get<JSObject>())->elements.size());
}

JSValue JS::Runtime::Builtin::Array_length_set(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	std::dynamic_pointer_cast<JS::Class::Array>(this_argument.value().get<JSObject>())->set_length(JSPropertyDescriptor().withValue(arguments[0]));
	return JS_undefined;
}
#endif

//// builtin Class.prototype

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::Class_prototype_toString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(auto object_pointer = this_argument.value_or(JS_undefined).get_if<JSObject>())
	{
		if(auto class_object = std::dynamic_pointer_cast<JS::Class::Class>(object_pointer.value()))
		{
			return u"[class " + class_object->Name + u"]";
		}
	}
	JS::Throw(JS::New<TypeError> __LOC(u"coercion failed: expected class"));
}
#endif

JSValue JS::Runtime::Builtin::console_log_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_DEBUG
	if(Enable_Debug)
		return JS_undefined;
#endif
	bool started = false;
	for(auto argument : arguments)
	{
		if(started)
		{
			std::cout << " ";
		}
		else
		{
			started = true;
		}
		std::map<JSObject, std::string> visited;
		::inspect<JSValue>::inspect_value(std::cout, argument, 5, visited, "self");
	}
	std::cout << std::endl;
	return JS_undefined;
}

//// builtin ConstantError

JSValue JS::Runtime::Builtin::ConstantError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_buffer_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_byteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_byteOffset_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getBigInt64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getBigUint64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getFloat16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getFloat32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getFloat64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getInt16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getInt32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getInt8_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getUint16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getUint32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_getUint8_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setBigInt64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setBigUint64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setFloat16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setFloat32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setFloat64_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setInt16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setInt32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setInt8_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setUint16_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setUint32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DataView_prototype_setUint8_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Date

JSValue JS::Runtime::Builtin::Date_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Date class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::DateClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DateClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JS::Runtime::TypeHint JS::Runtime::Builtin::DateClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

JSValue JS::Runtime::Builtin::Date_now_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_parse_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Date.prototype

JSValue JS::Runtime::Builtin::Date_prototype_getDate_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getDay_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getFullYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getHours_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getMilliseconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getMinutes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getMonth_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getSeconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getTime_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getTimezoneOffset_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCDate_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCDay_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCFullYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCHours_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCMilliseconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCMinutes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCMonth_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getUTCSeconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_getYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setDate_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setFullYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setHours_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setMilliseconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setMinutes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setMonth_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setSeconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setTime_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCDate_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCFullYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCHours_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCMilliseconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCMinutes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCMonth_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setUTCSeconds_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_setYear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toDateString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toGMTString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toISOString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toJSON_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toLocaleDateString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toLocaleString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toLocaleTimeString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toPrimitive_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toTimeString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_toUTCString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Date_UTC_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::decodeURI_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::decodeURIComponent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin DefinitionError

JSValue JS::Runtime::Builtin::DefinitionError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::encodeURI_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::encodeURIComponent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Error_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	std::optional<JSString> message;
	std::optional<JSValue> cause;

	if(arguments.size() > 0 && !IsUndefined(arguments[0]))
	{
		message = JS::ToString(arguments[0] WITH_PHASE(phase));
	}

	if(arguments.size() > 1)
	{
		if(auto options_object = arguments[1].get<JSObject>())
		{
			if(options_object->HasProperty(u"cause" PUBLIC_NAMESPACES READ_ACCESS WITH_PHASE(phase)))
			{
				cause = options_object->Get(u"cause", options_object, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(options_object->GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
			}
		}
	}

	// TODO: select prototype for specific error class
	if(Version::ES6 <= CurrentVersion && new_target != nullptr)
	{
		return JS::NewWithPrototype<JS::Class::Error>(GetPrototypeFromConstructor(new_target, &Intrinsics::Error_prototype), message, cause);
	}
	else
	{
		return JS::NewWithPrototype<JS::Class::Error>(JS::Intrinsics()->Error_prototype, message, cause);
	}
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ErrorClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return Construct(arguments, JSClass(this) WITH_PHASE(phase));
	}
	else if(IsNull(arguments[0]) || Is(arguments[0]))
	{
		return arguments[0];
	}
	else
	{
		return Construct(arguments, JSClass(this) WITH_PHASE(phase));
	}
}

void JS::Runtime::Builtin::ErrorClass::Init([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments)
{
	// TODO
}
#endif

JSValue JS::Runtime::Builtin::Error_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::escape_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::eval_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::EvalError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::FinalizationRegistry_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::FinalizationRegistry_prototype_register_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::FinalizationRegistry_prototype_unregister_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Float class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::FloatClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::FloatClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSFloat(0.0f);
	}
	else
	{
		auto p = JS::ToPrimitive(arguments[0] WITH_PHASE(phase), JS::Runtime::TypeHint::Number);
		if(IsNull(p))
		{
			return JSValue(0.0f);
		}
		else if(auto b = GetBoolean(p))
		{
			return JSValue(*b ? 1.0f : 0.f);
		}
		else if(IsUndefined(p))
		{
			return JSValue(JSFloat(NAN));
		}
		else if(auto l = p.get_if<JSLong>())
		{
			return JSValue(JSFloat(*l));
		}
		else if(auto u = p.get_if<JSUlong>())
		{
			return JSValue(JSFloat(*u));
		}
		else if(auto f = p.get_if<JSFloat>())
		{
			return JSValue(*f);
		}
		else if(auto d = p.get_if<JSNumber>())
		{
			return JSValue(JSFloat(*d));
		}
		else
		{
			JSString s;
			if(auto c = p.get_if<JSChar>())
			{
				s = std::u16string(1, *c);
			}
			else
			{
				s = GetString(p).value();
			}
			// TODO
			std::string tmp;
			for(char16_t c : s)
			{
				if(c < 0x7F)
					tmp += char(c);
				else
					return NAN;
			}
			double v;
			size_t pos;
			try
			{
				v = std::stod(tmp, &pos); // TODO
			}
			catch(const std::invalid_argument&)
			{
				return JSValue(JSFloat(NAN));
			}
			if(pos != tmp.size())
			{
				return JSValue(JSFloat(NAN));
			}
			return JSValue(JSFloat(v));
		}
	}
}

std::optional<JSValue> JS::Runtime::Builtin::FloatClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(auto l = object.get_if<JSLong>())
	{
		return JSFloat(*l);
	}
	else if(auto u = object.get_if<JSUlong>())
	{
		return JSFloat(*u);
	}
	else if(auto f = object.get_if<JSFloat>())
	{
		return *f;
	}
	else if(auto n = object.get_if<JSNumber>())
	{
		return JSFloat(*n);
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::FloatClass::GetTypeofString()
{
	return u"float";
}

std::optional<JSValue> JS::Runtime::Builtin::FloatClass::GetDefaultValue()
{
	return JSValue(0.0f);
}
#endif

//// builtin float.prototype

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::float_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::float_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

//// builtin Function

JSValue JS::Runtime::Builtin::Function_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Function class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::FunctionClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::FunctionClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSString JS::Runtime::Builtin::FunctionClass::GetTypeofString()
{
	return u"function";
}

JS::Runtime::TypeHint JS::Runtime::Builtin::FunctionClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

//// builtin Function.prototype

JSValue JS::Runtime::Builtin::Function_prototype_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Function_prototype_apply_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Function_prototype_bind_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Function_prototype_call_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(auto function = GetFunction(this_argument.value_or(JS_undefined)))
	{
		JSValue this_arg = JS_undefined;
		if(arguments.size() > 0)
		{
			this_arg = arguments[0];
			arguments.erase(arguments.begin());
		}
		return JS::Call(function.value(), this_arg, arguments WITH_PHASE(phase));
	}
	JS::Throw(JS::New<TypeError> __LOC(u"argument not callable"));
}

JSValue JS::Runtime::Builtin::Function_prototype_hasInstance_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Function_prototype_toString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value() || new_target != nullptr)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	auto this_value = this_argument.value();
	if(auto function = this_value.cast_object<JS::Class::Function>())
	{
		return function->SourceText;
	}
	else if(JS::IsCallable(this_value))
	{
		JSString result = u"function ";
		// TODO: get/set?
		if(auto function = this_value.cast_object<JS::Runtime::Builtin::BuiltinFunction>())
		{
			result += function->InitialName;
		}
		// ESNext expects this body
		return result + u"() { [ native code ] }";
	}

	JS::Throw(JS::New<TypeError> __LOC(u"not a function object"));
}

//// builtin GeneralNumber class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::GeneralNumberClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::GeneralNumberClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSNumber(0.0);
	}
	else
	{
		return JS::ToNumeric(arguments[0] WITH_PHASE(phase));
	}
}

std::optional<JSValue> JS::Runtime::Builtin::GeneralNumberClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(object.get_if<JSLong>() || object.get_if<JSUlong>() || object.get_if<JSFloat>() || object.get_if<JSNumber>())
	{
		return object;
	}
	else
	{
		return {};
	}
}

std::optional<JSValue> JS::Runtime::Builtin::GeneralNumberClass::GetDefaultValue()
{
	return NAN;
}
#endif

//// builtin GeneralNumber.prototype

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::GeneralNumber_prototype_toExponential_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::GeneralNumber_prototype_toFixed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::GeneralNumber_prototype_toPrecision_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::GeneralNumber_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::GeneralNumber_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::GeneratorFunction_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::GeneratorPrototype_next_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // not a constructor
	}

	if(auto generator = std::dynamic_pointer_cast<JS::Class::Generator>(this_argument.value().get<JSObject>()))
	{
		JSValue value = arguments.size() > 0 ? arguments[0] : JS_undefined;
		return generator->Resume(JS::Runtime::CompletionRecord::Normal(value));
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

JSValue JS::Runtime::Builtin::GeneratorPrototype_return_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // not a constructor
	}

	if(auto generator = std::dynamic_pointer_cast<JS::Class::Generator>(this_argument.value().get<JSObject>()))
	{
		JSValue value = arguments.size() > 0 ? arguments[0] : JS_undefined;
		return generator->Resume(JS::Runtime::CompletionRecord::Return(value));
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

JSValue JS::Runtime::Builtin::GeneratorPrototype_throw_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // not a constructor
	}

	if(auto generator = std::dynamic_pointer_cast<JS::Class::Generator>(this_argument.value().get<JSObject>()))
	{
		JSValue value = arguments.size() > 0 ? arguments[0] : JS_undefined;
		return generator->Resume(JS::Runtime::CompletionRecord::Throw(value));
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

JSValue JS::Runtime::Builtin::isFinite_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::isNaN_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::isXMLName_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Iterator_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorHelperPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorHelperPrototype_return_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_constructor_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_constructor_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_drop_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_every_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_filter_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_find_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_flatMap_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_forEach_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_iterator_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(!this_argument.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC()); // not a constructor
	}

	return this_argument.value();
}

JSValue JS::Runtime::Builtin::IteratorPrototype_map_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_reduce_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_some_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_take_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_toArray_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_toStringTag_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::IteratorPrototype_toStringTag_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Iterator_from_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::JSON_parse_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::JSON_stringify_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Long class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::LongClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::LongClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(0l);
	}
	else
	{
		auto p = JS::ToPrimitive(arguments[0] WITH_PHASE(phase), JS::Runtime::TypeHint::Number);
		if(IsNull(p))
		{
			return JSValue(JSLong(0));
		}
		else if(auto b = GetBoolean(p))
		{
			return JSValue(JSLong(*b ? 1 : 0));
		}
		else if(IsUndefined(p))
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}
		else if(auto l = p.get_if<JSLong>())
		{
			return JSValue(*l);
		}
		else if(auto u = p.get_if<JSUlong>())
		{
			if(JSUlong(INT64_MAX) < *u)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(*u);
			}
		}
		else if(auto f = p.get_if<JSFloat>())
		{
			if(std::fpclassify(*f) == FP_NAN || std::fpclassify(*f) == FP_INFINITE || *f < INT64_MIN || INT64_MAX < *f)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			JSLong l = JSLong(*f);
			if(JSFloat(l) != *f)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(l);
			}
		}
		else if(auto d = p.get_if<JSNumber>())
		{
			if(std::fpclassify(*d) == FP_NAN || std::fpclassify(*d) == FP_INFINITE || *d < INT64_MIN || INT64_MAX < *d)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			JSLong l = JSLong(*d);
			if(JSNumber(l) != *d)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(l);
			}
		}
		else
		{
			JSString s;
			if(auto c = p.get_if<JSChar>())
			{
				s = std::u16string(1, *c);
			}
			else
			{
				s = GetString(p).value();
			}
			// TODO
			std::string tmp;
			for(char16_t c : s)
			{
				if(c < 0x7F)
					tmp += char(c);
				else
					return NAN;
			}
			long long v;
			size_t pos;
			try
			{
				v = std::stoll(tmp, &pos, 10); // TODO
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<TypeError> __LOC()); // TODO: or possibly RangeError
			}
			if(pos != tmp.size())
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			if(v < INT64_MIN || INT64_MAX < v)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			return JSValue(JSLong(v));
		}
	}
}

std::optional<JSValue> JS::Runtime::Builtin::LongClass::Coerce(JSValue object, bool strict)
{
	if(auto l = object.get_if<JSLong>())
	{
		return JSValue(*l);
	}
	else if(auto u = object.get_if<JSUlong>())
	{
		if(JSUlong(INT64_MAX) < *u)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(JSUlong(*u));
		}
	}
	else if(auto f = object.get_if<JSFloat>())
	{
		if(std::fpclassify(*f) == FP_NAN || std::fpclassify(*f) == FP_INFINITE || *f < INT64_MIN || INT64_MAX < *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		JSLong l = JSLong(*f);
		if(JSFloat(l) != *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(l);
		}
	}
	else if(auto d = object.get_if<JSNumber>())
	{
		if(std::fpclassify(*d) == FP_NAN || std::fpclassify(*d) == FP_INFINITE || *d < INT64_MIN || INT64_MAX < *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		JSLong l = JSLong(*d);
		if(JSNumber(l) != *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(l);
		}
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::LongClass::GetTypeofString()
{
	return u"long";
}

std::optional<JSValue> JS::Runtime::Builtin::LongClass::GetDefaultValue()
{
	return JSValue(JSLong(0));
}
#endif

//// builtin long.prototype

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::long_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::long_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

//// builtin Map

JSValue JS::Runtime::Builtin::Map_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::MapIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_groupBy_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_clear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_delete_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_entries_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_forEach_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_has_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_keys_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_size_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_prototype_values_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Map_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_abs_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_acos_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_acosh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_asin_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_asinh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_atan_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_atan2_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_atanh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_cbrt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_ceil_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_clz32_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_cos_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_cosh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_exp_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_expm1_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_f16round_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_floor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_fround_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_hypot_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_imul_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_log_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_log10_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_log1p_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_log2_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_max_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_min_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_pow_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_random_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_round_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_sign_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_sin_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_sinh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_sqrt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_tan_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_tanh_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Math_trunc_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Namespace_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target != nullptr)
	{
		// constructor
		if(arguments.size() == 1)
		{
			if(auto namespace_ = std::dynamic_pointer_cast<JS::Class::Namespace>(GetNonNullObject(arguments[0]).value_or(nullptr)))
			{
				return namespace_;
			}
		}
	}

	if(arguments.size() == 0)
	{
		return JS::New<JS::Class::Namespace>(u"", u"");
	}
	else if(arguments.size() == 1)
	{
		if(auto namespace_ = std::dynamic_pointer_cast<JS::Class::Namespace>(GetNonNullObject(arguments[0]).value_or(nullptr)))
		{
			return JS::New<JS::Class::Namespace>(namespace_->prefix, namespace_->uri);
		}
		else
		{
#if ENABLE_ES4
			if(IsVersionES4() && (IsUndefined(arguments[0]) || IsNull(arguments[0])))
			{
				// Note: E4X and ActionScript would return -:"undefined" or -:"null"
				// Note: The Netscape proposal would return -:"anonymous"
				return JS::New<JS::Class::Namespace>(u"", u"");
			}
			else
			// Note: The Netscape proposal only allows strings and char to be passed, and two namespaces constructed with the same name are equal, we do not do this
#endif
			if(auto qname = std::dynamic_pointer_cast<JS::Class::QName>(GetNonNullObject(arguments[0]).value_or(nullptr)))
			{
				if(qname->uri.has_value())
				{
					return JS::New<JS::Class::Namespace>(qname->Prefix, qname->uri.value());
				}
			}

			auto uri = JS::ToString(arguments[0] WITH_PHASE(phase));
			if(uri == u"")
			{
				return JS::New<JS::Class::Namespace>(u"", u"");
			}
			else
			{
				return JS::New<JS::Class::Namespace>(uri);
			}
		}
	}
	else
	{
		JSString uri;
		if(auto qname = std::dynamic_pointer_cast<JS::Class::QName>(GetNonNullObject(arguments[1]).value_or(nullptr)))
		{
			if(qname->uri.has_value())
			{
				uri = qname->uri.value();
			}
			else
			{
				uri = JS::ToString(arguments[1] WITH_PHASE(phase));
			}
		}
		else
		{
			uri = JS::ToString(arguments[1] WITH_PHASE(phase));
		}

		JSString prefix = JS::ToString(arguments[0] WITH_PHASE(phase));

		if(uri == u"")
		{
			if(IsUndefined(arguments[0]) || prefix == u"")
			{
				return JS::New<JS::Class::Namespace>(u"", u"");
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}
		else if(IsUndefined(arguments[0]) || !JS::Type::QName::IsXMLName(arguments[0]))
		{
			return JS::New<JS::Class::Namespace>(uri);
		}
		else
		{
			return JS::New<JS::Class::Namespace>(prefix, uri);
		}
	}
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::NamespaceClass::Construct(std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(Supports_E4X())
	{
		return Namespace_Call(std::optional<JSValue>{}, arguments, new_target RUNTIME_PHASE);
	}
	else
	{
		if(arguments.size() > 1)
		{
			JS::Throw(JS::New<ArgumentError> __LOC("Namespace constructor takes at most 1 argument"));
		}
		else if(arguments.size() == 0 || IsNull(arguments[0]) || IsUndefined(arguments[0]))
		{
			return JS::New<JS::Class::Namespace>(u"anonymous");
		}
		else if(arguments[0].get_if<JSChar>() || arguments[0].get_if<JSString>())
		{
			JSString name = JS::ToString(arguments[0] RUNTIME_PHASE); // TODO: phase
			if(name == u"")
			{
				return JS_public;
			}
			auto named_namespace_it = JS::Class::Namespace::named_namespaces.find(name);
			if(named_namespace_it != JS::Class::Namespace::named_namespaces.end())
			{
				return named_namespace_it->second;
			}
			else
			{
				JSNamespace namespace_ = JS::New<JS::Class::Namespace>(name);
				JS::Class::Namespace::named_namespaces[name] = namespace_;
				return namespace_;
			}
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC(u"non-string argument supplied to Namespace constructor"));
		}
	}
}

JSString JS::Runtime::Builtin::NamespaceClass::GetTypeofString()
{
	return u"namespace";
}

JS::Runtime::TypeHint JS::Runtime::Builtin::NamespaceClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

JSValue JS::Runtime::Builtin::Namespace_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Never class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::NeverClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::NeverClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

std::optional<JSValue> JS::Runtime::Builtin::NeverClass::Coerce([[maybe_unused]] JSValue object, [[maybe_unused]] bool strict)
{
	return {};
}

JSString JS::Runtime::Builtin::NeverClass::GetTypeofString()
{
	return u"";
}

std::optional<JSValue> JS::Runtime::Builtin::NeverClass::GetDefaultValue()
{
	return {};
}
#endif

//// builtin Null class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::NullClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		return JS_null;
	}
}

JSValue JS::Runtime::Builtin::NullClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 0)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		return JS_null;
	}
}

std::optional<JSValue> JS::Runtime::Builtin::NullClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(IsNull(object))
	{
		return JS_null;
	}
	else
	{
		return {};
	}
}
#endif

//// builtin Number

JSValue JS::Runtime::Builtin::Number_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	JSNumber n;
	if(arguments.size() == 0)
	{
		n = 0.0;
	}
	else
	{
		JSValue p = ToPrimitive(arguments[0] WITH_PHASE(phase), JS::Runtime::TypeHint::Number);
		if(auto b = GetBigInt(p))
		{
			n = b->get_d();
		}
		else
		{
			n = JS::ToNumber(p WITH_PHASE(phase));
		}
	}

	if(new_target == nullptr)
	{
		// called as function
		return n;
	}
	else
	{
		// called as constructor
		return JS::New<JS::Class::Number>(n);
	}
}

//// builtin Number class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::NumberClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::NumberClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSNumber(0.0);
	}
	else
	{
		return JS::ToNumber(arguments[0] RUNTIME_PHASE);
	}
}

std::optional<JSValue> JS::Runtime::Builtin::NumberClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(auto l = object.get_if<JSLong>())
	{
		return JSNumber(*l);
	}
	else if(auto u = object.get_if<JSUlong>())
	{
		return JSNumber(*u);
	}
	else if(auto f = object.get_if<JSFloat>())
	{
		return JSNumber(*f);
	}
	else if(auto n = object.get_if<JSNumber>())
	{
		return *n;
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::NumberClass::GetTypeofString()
{
	return u"number";
}

std::optional<JSValue> JS::Runtime::Builtin::NumberClass::GetDefaultValue()
{
	return 0.0;
}
#endif

JSValue JS::Runtime::Builtin::Number_isFinite_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_isInteger_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_isNaN_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_isSafeInteger_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_parseFloat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_parseInt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_prototype_toExponential_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_prototype_toFixed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_prototype_toLocaleString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Number_prototype_toPrecision_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Number.prototype

JSValue JS::Runtime::Builtin::Number_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO: radix
	if(auto number_option = GetNumber(this_argument.value())) // TODO: only ES5 and later
	{
		return ToString(number_option.value() WITH_PHASE(phase));
	}
	else if(auto number_object = this_argument.value().cast_object<JS::Class::Number>())
	{
		return ToString(number_object->NumberData WITH_PHASE(phase));
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

JSValue JS::Runtime::Builtin::Number_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Object

JSValue JS::Runtime::Builtin::Object_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	/* [new] Object([value]) */

	auto value = arguments_get_index(arguments, 0);

	if(Version::ES6 <= CurrentVersion && new_target != nullptr && new_target != JS::Intrinsics()->Object)
	{
		// ES6 19.1.1.1: step 1.a

		// Super call
		auto proto = JS::GetPrototypeFromConstructor(new_target, &Intrinsics::Object_prototype);
		return JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto);
	}
	else if(IsNull(value) || IsUndefined(value))
	{
		// ES1 15.2.1.1: step 1; 15.2.1.2; 15.2.2.1: steps 1, 7; 15.2.2.2
		// ES2 15.2.1.1: step 1; 15.2.2.1: steps 1, 8
		// ES5 15.2.1.1: step 1; 15.2.2.1: steps 2-8
		// ES6 19.1.1.1: step 2

		// Call or Construct
		return JS::New<JS::Class::OrdinaryObject>();
	}
	else if(CurrentVersion < Version::ES5 && new_target == nullptr && GetNonNullObject(value))
	{
		// ES1 15.2.2.1: step 3; 15.2.2.2
		// ES2 15.2.2.1: step 4
		// ES5 15.2.2.1: step 1.a.ii

		// host objects may react differently to new Object(host-object)
		return (*GetNonNullObject(value))->NewObject();
	}
	else
	{
		// ES1 15.2.1.1: step 2; 15.2.2.1: steps 1, 2, 4-6
		// ES2 15.2.1.1: step 2; 15.2.2.1: steps 2, 3, 5-7
		// ES5 15.2.1.1: step 2; 15.2.2.1: step 1.a.i, 1.b-1.d
		// ES6 19.1.1.1: step 3

		return ToObject(value);
	}
}

//// builtin Object class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ObjectClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object called with more than 1 argument"));
	}
	else if(arguments.size() == 1)
	{
		return arguments[0];
	}
	else
	{
		return JS_undefined;
	}
}

JSValue JS::Runtime::Builtin::ObjectClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object constructor called with more than 1 argument"));
	}
	else if(arguments.size() == 0)
	{
		return JS::New<JS::Class::OrdinaryObject>();
	}
	else if(IsNull(arguments[0]) || IsUndefined(arguments[0]))
	{
		return JS::New<JS::Class::OrdinaryObject>();
	}
	else
	{
		return arguments[0];
	}
}

std::optional<JSValue> JS::Runtime::Builtin::ObjectClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	return object;
}

std::optional<JSValue> JS::Runtime::Builtin::ObjectClass::GetDefaultValue()
{
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::Object_assign_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.assign(target, ...sources) */
	auto target = arguments_get_index(arguments, 0);
	auto to = JS::ToObject(target);

	if(arguments.size() > 1)
	{
		for(auto sources_iterator = arguments.begin() + 1; sources_iterator != arguments.end(); sources_iterator ++)
		{
			auto nextSource = *sources_iterator;
			if(!IsUndefined(nextSource) && !IsNull(nextSource))
			{
				auto from = JS::ToObject(nextSource);
				for(auto nextKey : from->OwnPropertyKeys(VIA_CLASS(nullptr))) // ES4 has no Object.assign
				{
					auto desc = from->GetOwnProperty(nextKey PUBLIC_NAMESPACES READ_WRITE_ACCESS);
					if(desc.has_value() && desc->Enumerable)
					{
						auto propValue = JS::GetByKey(from, nextKey);
						if(!JS::SetByKey(to, nextKey, propValue))
						{
							JS::Throw(JS::New<TypeError> __LOC("unable to assign property"));
						}
					}
				}
			}
		}
	}

	return to;
}

void JS::Runtime::Builtin::DefineProperties(JSObject O, JSValue Properties)
{
	auto props = JS::ToObject(Properties);
	std::vector<std::tuple<JSPropertyKey, JSPropertyDescriptor>> descriptors;
	for(auto nextKey : props->OwnPropertyKeys(VIA_CLASS(props->GetClass())))
	{
		auto propDesc = props->GetOwnProperty(nextKey PUBLIC_NAMESPACES READ_ACCESS);
		if(propDesc.has_value() && propDesc.value().Enumerable)
		{
			auto descObj = JS::GetByKey(props, nextKey);
			auto desc = JS::ToPropertyDescriptor(descObj);
			descriptors.push_back(std::make_tuple(nextKey, desc));
		}
	}
	for(auto property : descriptors)
	{
		if(!O->DefineOwnProperty(std::get<0>(property), std::get<1>(property) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}
}

JSValue JS::Runtime::Builtin::Object_create_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.create(O, Properties) */

	auto O = arguments_get_index(arguments, 0);
	auto Properties = arguments_get_index(arguments, 1);

	if(auto object = GetObjectOrNull(O))
	{
		auto proto = object.value();
		auto result = JS::NewWithPrototype<JS::Class::OrdinaryObject>(proto == nullptr ? std::optional<JSValue>{} : std::optional<JSValue>{proto});

		if(!IsUndefined(Properties))
		{
			DefineProperties(result, Properties);
		}
		return result;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("non-object/null passed to Object.create"));
	}
}

JSValue JS::Runtime::Builtin::Object_defineProperties_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.defineProperties(O, Properties) */

	auto O = arguments_get_index(arguments, 0);
	auto Properties = arguments_get_index(arguments, 1);

	if(auto object_option = GetNonNullObject(O))
	{
		auto object = object_option.value();
		DefineProperties(object, Properties);
		return object;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("non-object to Object.defineProperties"));
	}
}

JSValue JS::Runtime::Builtin::Object_defineProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.defineProperty(O, P, Attributes) */

	auto O = arguments_get_index(arguments, 0);
	auto P = arguments_get_index(arguments, 1);
	auto Attributes = arguments_get_index(arguments, 2);

	auto object_option = GetNonNullObject(O);
	if(!object_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("defineProperty received non-object first argument"));
	}
	auto object = object_option.value();
	JSPropertyKey key;
	if(CurrentVersion <= Version::ES5)
	{
		key = JS::ToString(P RUNTIME_PHASE);
	}
	else
	{
		key = JS::ToPropertyKey(P RUNTIME_PHASE);
	}
	auto desc = JS::ToPropertyDescriptor(Attributes);
	if(!object->DefineOwnProperty(key, desc PUBLIC_NAMESPACES READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	return object;
}

std::vector<JSValue> JS::Runtime::Builtin::EnumerableOwnProperties(JSObject O, JS::Runtime::EnumerationKind kind)
{
	auto ownKeys = O->OwnPropertyKeys(VIA_CLASS(nullptr)); // not used by ES4
	std::vector<JSValue> results;
	for(auto key_value : ownKeys)
	{
		if(auto key_option = JS::GetString(key_value))
		{
			auto key = key_option.value();
			auto desc = O->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
			if(desc.has_value() && desc->Enumerable)
			{
				if(kind == JS::Runtime::EnumerationKind::Key)
				{
					results.push_back(key);
				}
				else
				{
					auto value = JS::GetByKey(O, key);
					if(kind == JS::Runtime::EnumerationKind::Value)
					{
						results.push_back(value);
					}
					else
					{
						auto entry = JS::New<JS::Class::Array>(std::vector<JSValue>{key, value});
						results.push_back(entry);
					}
				}
			}
		}
	}
	return results;
}

JSValue JS::Runtime::Builtin::Object_entries_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.entries(O) */

	auto O = arguments_get_index(arguments, 0);

	auto obj = JS::ToObject(O);
	auto entryList = EnumerableOwnProperties(obj, JS::Runtime::EnumerationKind::KeyValue);
	return JS::New<JS::Class::Array>(entryList);
}

bool JS::Runtime::Builtin::SetIntegrityLevel(JSValue O, bool isFrozen __ES4_ONLY(, std::optional<JSValue> propertyKey))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Throw(JS::New<InternalError> __LOC("ES4 does not support freezing objects"));
	}

	if(propertyKey.has_value())
	{
		// corresponds to ES4 sealLocalProperty()
		if(!O.HasProperty(propertyKey.value() PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
		{
			JS::Throw(JS::New<ReferenceError> __LOC("property not found"));
		}
		auto propertyName = propertyKey.value().ToString(VIA_RUNTIME_PHASE);
		auto c = O.GetClass();
		if(!c->GetBaseInstanceProperty(propertyName PUBLIC_NAMESPACES READ_ACCESS)
		&& !c->GetBaseInstanceProperty(propertyName PUBLIC_NAMESPACES WRITE_ACCESS))
		{
			if(auto object_option = GetNonNullObject(O))
			{
				auto object = object_option.value();

				// only look for readable to make sure we do not get an ambiguity with a setter
				auto m = object->GetOwnProperty(propertyName PUBLIC_NAMESPACES READ_ACCESS);
				if(m.has_value() && m.value().PropertyType == JSProperty::Type::Dynamic)
				{
					object->DefineOwnProperty(
						propertyName,
						JSPropertyDescriptor().withConfigurable(false)
						PUBLIC_NAMESPACES
						READ_ACCESS);
				}
			}
		}
		return true;
	}
#endif

	if(auto object_option = GetNonNullObject(O))
	{
		auto object = object_option.value();

		// corresponds to ES4 sealObject()
		if(!object->PreventExtensions())
		{
			return false;
		}

		// corresponds to ES4 sealAllLocalProperties()
		for(auto k_desc : object->ExtractAllProperties())
		{
			switch(k_desc.second.PropertyType)
			{
			case JSProperty::Type::Data:
				if(isFrozen)
				{
					if(!object->DefineOwnProperty(
						k_desc.first __ES4_ONLY(.PropertyKey),
						JSPropertyDescriptor().withConfigurable(false).withWritable(false)
							WITH_NAMESPACES(k_desc.first.Namespaces)
							WITH_ACCESS_SET(k_desc.first.Accesses)))
					{
						JS::Throw(JS::New<TypeError> __LOC("unable to freeze property"));
					}
				}
				else
				{
					if(!object->DefineOwnProperty(
						k_desc.first __ES4_ONLY(.PropertyKey),
						JSPropertyDescriptor().withConfigurable(false)
							WITH_NAMESPACES(k_desc.first.Namespaces)
							WITH_ACCESS_SET(k_desc.first.Accesses)))
					{
						JS::Throw(JS::New<TypeError> __LOC("unable to seal property"));
					}
				}
				break;
			case JSProperty::Type::Accessor:
				if(!object->DefineOwnProperty(
					k_desc.first __ES4_ONLY(.PropertyKey),
					JSPropertyDescriptor().withConfigurable(false)
						WITH_NAMESPACES(k_desc.first.Namespaces)
						WITH_ACCESS_SET(k_desc.first.Accesses)))
				{
					JS::Throw(JS::New<TypeError> __LOC("unable to seal property"));
				}
				break;
#if ENABLE_ES4
			default:
				// nothing to change
				break;
#endif
			}
		}
	}
	else
#if ENABLE_ES4
	if(!IsVersionES4())
#endif
	{
		JS::Throw(JS::New<InternalError> __LOC("attempting to freeze/seal primitive value"));
	}

	return true;
}

bool JS::Runtime::Builtin::TestIntegrityLevel(JSObject O, bool isFrozen)
{
	if(O->IsExtensible())
	{
		return false;
	}
	for(auto k : O->OwnPropertyKeys(VIA_CLASS(nullptr)))
	{
		auto currentDesc = O->GetOwnProperty(k PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		if(currentDesc.has_value())
		{
			if(currentDesc.value().Configurable)
			{
				return false;
			}
			if(isFrozen && currentDesc.value().PropertyType == JSProperty::Type::Data && currentDesc.value().IsWritable())
			{
				return false;
			}
		}
	}
	return true;
}

JSValue JS::Runtime::Builtin::Object_freeze_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.freeze(O) */

	auto O = arguments_get_index(arguments, 0);
	if(auto obj = GetNonNullObject(O))
	{
		if(!SetIntegrityLevel(O, /* isFrozen = */ true))
		{
			JS::Throw(JS::New<TypeError> __LOC("unable to freeze object"));
		}
	}
	return O;
}

void JS::Runtime::Builtin::AddEntriesFromIterable([[maybe_unused]] JSObject target, JSValue iterable, std::function<void(JSValue, JSValue)> adder)
{
	JS::Runtime::IteratorRecord iteratorRecord = JS::Runtime::IteratorRecord::GetIterator(iterable, /* async = */ false);
	while(true)
	{
		auto next = iteratorRecord.StepValue();
		if(!next.has_value())
		{
			return;
		}
		auto k = JS::Get(next.value(), u"0");
		auto v = JS::Get(next.value(), u"1");
		adder(k, v);
	}
}

JSValue JS::Runtime::Builtin::Object_fromEntries_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.fromEntries(iterable) */
	auto iterable = arguments_get_index(arguments, 0);
	RequireObjectCoercible(iterable);

	auto obj = JS::New<JS::Class::OrdinaryObject>();

	AddEntriesFromIterable(obj, iterable,
		[obj](JSValue key, JSValue value) -> void
	{
		CreateDataPropertyOrThrow(obj, JS::ToPropertyKey(key RUNTIME_PHASE), value);
	});

	return obj;
}

JSValue JS::Runtime::Builtin::Object_getOwnPropertyDescriptor_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.getOwnPropertyDescriptor(O, P) */
	auto O = arguments_get_index(arguments, 0);
	auto P = arguments_get_index(arguments, 1);

	if(CurrentVersion <= Version::ES5 && !GetNonNullObject(O))
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.getOwnPropertyDescriptor called with primitive value"));
	}

	auto obj = JS::ToObject(O);
	JSPropertyKey key;
	if(Version::ES6 <= CurrentVersion)
	{
		key = JS::ToPropertyKey(P RUNTIME_PHASE);
	}
	else
	{
		key = JS::ToString(P RUNTIME_PHASE);
	}

	auto desc = obj->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);

	if(desc.has_value())
	{
		return JS::FromPropertyDescriptor(JSPropertyDescriptor(desc.value()));
	}
	else
	{
		return JS_undefined;
	}
}

JSValue JS::Runtime::Builtin::Object_getOwnPropertyDescriptors_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.getOwnPropertyDescriptors(O) */
	auto O = arguments_get_index(arguments, 0);

	auto obj = ToObject(O);
	auto ownKeys = obj->OwnPropertyKeys(VIA_CLASS(nullptr));
	auto descriptors = JS::New<JS::Class::OrdinaryObject>();
	for(auto key : ownKeys)
	{
		auto desc = obj->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		if(desc.has_value())
		{
			auto descriptor =  JS::FromPropertyDescriptor(JSPropertyDescriptor(desc.value()));
			CreateDataPropertyOrThrow(descriptors, key, descriptor);
		}
	}

	return descriptors;
}

JSValue JS::Runtime::Builtin::Object_getOwnPropertyNames_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.getOwnPropertyNames(O) */
	auto O = arguments_get_index(arguments, 0);
	if(CurrentVersion <= Version::ES5 && !GetNonNullObject(O))
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.getOwnPropertyNames called with primitive value"));
	}

	auto obj = ToObject(O);
	auto keys = obj->OwnPropertyKeys(VIA_CLASS(nullptr));
	std::vector<JSValue> nameList;
	for(auto nextKey : keys)
	{
		if(auto nextKey_string = GetString(nextKey))
		{
			nameList.push_back(nextKey_string.value());
		}
	}

	return JS::New<JS::Class::Array>(nameList);
}

JSValue JS::Runtime::Builtin::Object_getOwnPropertySymbols_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.getOwnPropertySymbols(O) */
	auto O = arguments_get_index(arguments, 0);
	if(CurrentVersion <= Version::ES5 && !GetNonNullObject(O))
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.getOwnPropertySymbols called with primitive value"));
	}

	auto obj = ToObject(O);
	auto keys = obj->OwnPropertyKeys(VIA_CLASS(nullptr));
	std::vector<JSValue> nameList;
	for(auto nextKey : keys)
	{
		if(auto nextKey_symbol = GetSymbol(nextKey))
		{
			nameList.push_back(nextKey_symbol.value());
		}
	}

	return JS::New<JS::Class::Array>(nameList);
}

JSValue JS::Runtime::Builtin::Object_getPrototypeOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.getPrototypeOf(O) */
	auto O = arguments_get_index(arguments, 0);
	if(CurrentVersion <= Version::ES5 && !GetNonNullObject(O))
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.getPrototypeOf called with primitive value"));
	}

	return ToObject(O)->GetPrototypeOf().value_or(JS_null);
}

JSValue JS::Runtime::Builtin::Object_groupBy_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_hasOwn_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.hasOwn(O, P) */
	auto O = arguments_get_index(arguments, 0);
	auto P = arguments_get_index(arguments, 1);

	auto obj = O.ToObject();
	auto key = ToPropertyKey(P RUNTIME_PHASE);
	return obj->HasOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
}

JSValue JS::Runtime::Builtin::Object_is_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.is(value1, value2) */
	auto value1 = arguments_get_index(arguments, 0);
	auto value2 = arguments_get_index(arguments, 1);
	return JS::SameValue(value1, value2);
}

JSValue JS::Runtime::Builtin::Object_isExtensible_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.isExtensible(O) */
	auto O = arguments_get_index(arguments, 0);

	if(auto object = GetNonNullObject(O))
	{
		return object.value()->IsExtensible();
	}
	else
	{
		return false;
	}
}

JSValue JS::Runtime::Builtin::Object_isFrozen_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.isFrozen(O) */
	auto O = arguments_get_index(arguments, 0);
	
	if(auto object = GetNonNullObject(O))
	{
		return TestIntegrityLevel(object.value(), /* isFrozen = */ true);
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.isFrozen called with primitive value"));
	}
}

JSValue JS::Runtime::Builtin::Object_isSealed_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.isSealed(O) */
	auto O = arguments_get_index(arguments, 0);
	
	if(auto object = GetNonNullObject(O))
	{
		return TestIntegrityLevel(object.value(), /* isFrozen = */ false);
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.isSealed called with primitive value"));
	}
}

JSValue JS::Runtime::Builtin::Object_keys_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.keys(O) */
	auto O = arguments_get_index(arguments, 0);

	if(CurrentVersion <= Version::ES5 && !GetNonNullObject(O))
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.keys called with primitive value"));
	}

	auto obj = JS::ToObject(O);
	auto entryList = EnumerableOwnProperties(obj, JS::Runtime::EnumerationKind::Key);
	return JS::New<JS::Class::Array>(entryList);
}

JSValue JS::Runtime::Builtin::Object_preventExtensions_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.preventExtensions(O) */
	auto O = arguments_get_index(arguments, 0);

	if(auto obj = GetNonNullObject(O))
	{
		if(!obj.value()->PreventExtensions()
		&& Version::ES6 <= CurrentVersion)
		{
			JS::Throw(JS::New<TypeError> __LOC("unable to prevent extensions on Object"));
		}
	}
	else if(CurrentVersion <= Version::ES5)
	{
		JS::Throw(JS::New<TypeError> __LOC("Object.preventExtensions called with primitive value"));
	}

	return O;
}

JSValue JS::Runtime::Builtin::Object_seal_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.seal(O) */

	auto O = arguments_get_index(arguments, 0);
	if(auto obj = GetNonNullObject(O))
	{
		if(!SetIntegrityLevel(O, /* isFrozen = */ false))
		{
			JS::Throw(JS::New<TypeError> __LOC("unable to seal object"));
		}
	}
	return O;
}

JSValue JS::Runtime::Builtin::Object_setPrototypeOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.setPrototypeOf(O, proto) */

	auto O = arguments_get_index(arguments, 0);
	auto proto = arguments_get_index(arguments, 1);

	RequireObjectCoercible(O);

	if(!GetObjectOrNull(proto))
	{
		JS::Throw(JS::New<TypeError> __LOC("prototype can only be null or object"));
	}

	if(auto object = GetNonNullObject(O))
	{
		if(!object.value()->SetPrototypeOf(proto))
		{
			JS::Throw(JS::New<TypeError> __LOC("could not change prototype of object"));
		}
	}
	return O;
}

JSValue JS::Runtime::Builtin::Object_values_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.values(O) */
	auto O = arguments_get_index(arguments, 0);

	auto obj = JS::ToObject(O);
	auto entryList = EnumerableOwnProperties(obj, JS::Runtime::EnumerationKind::Value);
	return JS::New<JS::Class::Array>(entryList);
}

//// builtin Object.prototype

JSValue JS::Runtime::Builtin::Object_prototype_hasOwnProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_ES4
	if(phase != JS::Runtime::ExecutionPhase::Run)
	{
		JS::Throw(JS::New<ConstantError> __LOC("Object.prototype.hasOwnProperty cannot be called from a constant expression"));
	}

	if(IsVersionES4() && arguments.size() != 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object.prototype.hasOwnProperty expected 1 argument"));
	}
#endif

	/* Object.prototype.hasOwnProperty(V) */
	auto V = arguments_get_index(arguments, 0);

	JSPropertyKey P;
	if(Version::ES6 <= CurrentVersion)
	{
		P = ToPropertyKey(V RUNTIME_PHASE);
	}
	else
	{
		P = ToString(V RUNTIME_PHASE);
	}

	auto O = ToObject(this_argument.value());
	return O->HasOwnProperty(P PUBLIC_NAMESPACES READ_WRITE_ACCESS);
}

JSValue JS::Runtime::Builtin::Object_prototype_isPrototypeOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_ES4
	if(phase != JS::Runtime::ExecutionPhase::Run)
	{
		JS::Throw(JS::New<ConstantError> __LOC("Object.prototype.hasOwnProperty cannot be called from a constant expression"));
	}

	if(IsVersionES4() && arguments.size() != 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object.prototype.hasOwnProperty expected 1 argument"));
	}
#endif

	/* Object.prototype.isPrototypeOf(V) */
	auto V = arguments_get_index(arguments, 0);

	if(std::optional<JSValue> current_option = GetNonNullObject(V))
	{
		auto O = ToObject(this_argument.value());
		for(; current_option.has_value(); current_option = current_option.value().GetPrototypeOf())
		{
			if(JS::SameValue(O, current_option.value()))
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}

JSValue JS::Runtime::Builtin::Object_prototype_propertyIsEnumerable_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_ES4
	if(phase != JS::Runtime::ExecutionPhase::Run)
	{
		JS::Throw(JS::New<ConstantError> __LOC("Object.prototype.hasOwnProperty cannot be called from a constant expression"));
	}

	if(IsVersionES4() && arguments.size() != 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object.prototype.hasOwnProperty expected 1 argument"));
	}
#endif

	/* Object.prototype.propertyIsEnumerable(V) */
	auto V = arguments_get_index(arguments, 0);

	JSPropertyKey P;
	if(Version::ES6 <= CurrentVersion)
	{
		P = ToPropertyKey(V RUNTIME_PHASE);
	}
	else
	{
		P = ToString(V RUNTIME_PHASE);
	}

	auto O = ToObject(this_argument.value());

#if ENABLE_ES4
	if(IsVersionES4())
	{
		auto limit = O->GetClass();

		for(auto access : Accesses(JS::AccessSet::ReadWrite))
		{
			auto property_option = limit->GetBaseInstanceProperty(P PUBLIC_NAMESPACES, AsAccessSet(access));
			if(property_option.has_value())
			{
				auto property = limit->GetDerivedInstanceProperty(P PUBLIC_NAMESPACES, AsAccessSet(access)).value();
				if(property->Enumerable)
				{
					return true;
				}
			}
		}
	}
#endif

	auto desc = O->GetOwnProperty(P PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	return desc.has_value() && desc.value().Enumerable;
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::Object_prototype_sealProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(phase != JS::Runtime::ExecutionPhase::Run)
	{
		JS::Throw(JS::New<ConstantError> __LOC("Object.prototype.sealProperty cannot be called from a constant expression"));
	}

	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC("Object.prototype.sealProperty received more than 1 argument"));
	}

	/* Object.prototype.sealProperty(arg) */
	auto arg = arguments_get_index(arguments, 0, true);

	if(arg == true)
	{
		SetIntegrityLevel(this_argument.value(), /* isFrozen = */ false);
	}
	else if(arg == false)
	{
		if(auto object = GetNonNullObject(arg))
		{
			object.value()->PreventExtensions();
		}
	}
	else if(arg.get_if<JSChar>() || arg.get_if<JSString>())
	{
		SetIntegrityLevel(this_argument.value(), /* isFrozen = */ false, arg);
	}

	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::Object_prototype_toLocaleString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_ES4
	if(phase != JS::Runtime::ExecutionPhase::Run)
	{
		JS::Throw(JS::New<ConstantError> __LOC("Object.prototype.toLocaleString cannot be called from a constant expression"));
	}
#endif

	/* Object.prototype.toLocaleString([reserved1 [, reserved2]]) */
	auto this_value = this_argument.value();
	if(CurrentVersion == Version::ES5)
	{
		this_value = ToObject(this_value);
	}

	if(auto func = GetFunction(
		this_value.Get(u"toString", this_value, Intent::Call PUBLIC_NAMESPACES WITH_CLASS(this_value.GetClass()) WITH_ENVIRONMENT(nullptr) WITH_PHASE(phase))))
	{
		return
			JS::Call(
				func.value(),
				this_value,
#if ENABLE_ES4
				IsVersionES4() ? arguments :
#endif
					std::vector<JSValue>{}
				WITH_PHASE(phase));
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("toString not callable"));
	}
}

JSValue JS::Runtime::Builtin::Object_prototype_toString_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	/* Object.prototype.toString() */
	if(!this_argument.has_value())
	{
		// binding is not lexical, so this_argument must resolve to something
		JS::Throw(JS::New<InternalError> __LOC());
	}

	return JS::ObjectToString(this_argument.value() WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::Object_prototype_valueOf_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	auto this_value = this_argument.value();
	if(Version::ES5 <= CurrentVersion)
	{
		this_value = ToObject(this_value);
	}
	if(CurrentVersion <= Version::ES5)
	{
		if(auto this_object_option = GetNonNullObject(this_value))
		{
			this_value = this_object_option.value()->ValueOf();
		}
	}
	return this_value;
}

JSValue JS::Runtime::Builtin::Object_prototype___defineGetter___Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_prototype___defineSetter___Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_prototype___lookupGetter___Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_prototype___lookupSetter___Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_prototype___proto___get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Object_prototype___proto___set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin override

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::override_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() == 0)
	{
		return JSValue(JSCompoundAttribute().WithOverride(JS::Runtime::OverrideModifier::True));
	}
	else if(arguments.size() == 1)
	{
		if(IsUndefined(arguments[0]))
		{
			return JSValue(JSCompoundAttribute().WithOverride(JS::Runtime::OverrideModifier::Undefined));
		}
		else if(auto boolean = GetBoolean(arguments[0]))
		{
			return JSValue(JSCompoundAttribute().WithOverride(*boolean ? JS::Runtime::OverrideModifier::True : JS::Runtime::OverrideModifier::False));
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}
	else
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
}
#endif

//// builtin Package class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::PackageClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::PackageClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JS::Runtime::TypeHint JS::Runtime::Builtin::PackageClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

JSValue JS::Runtime::Builtin::parseFloat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::parseInt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::parseLong_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::print_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
#if ENABLE_DEBUG
	if(Enable_Debug)
		return JS_undefined;
#endif
	bool started = false;
	for(auto argument : arguments)
	{
		if(started)
		{
			std::cout << " ";
		}
		else
		{
			started = true;
		}
		PrintString(std::cout, JS::ToString(argument WITH_PHASE(phase)));
	}
	std::cout << std::endl;
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(new_target == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC("Promise called as function"));
	}

	auto executor_option = GetFunction(arguments.size() > 0 ? arguments[0] : JS_undefined);
	if(!executor_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("argument not callable"));
	}

	gcn::gc_ptr<JS::Class::Promise> promise = JS::New<JS::Class::Promise>();

	gcn::gc_ptr<PromiseResolveFunction> resolve;
	gcn::gc_ptr<PromiseRejectFunction>  reject;

	CreateResolvingFunctions(promise, resolve, reject);

	try
	{
		JS::Call(executor_option.value(), JS_undefined, {resolve, reject} RUNTIME_PHASE);
	}
	catch(JS::Class::Internal::Value * thrown_value)
	{
		JS::Call(std::static_pointer_cast<JS::Runtime::Builtin::BuiltinFunction>(reject), JS_undefined, {JSValue::WrapValue(thrown_value)} RUNTIME_PHASE);
	}

	return promise;
}

JSValue JS::Runtime::Builtin::Promise_all_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_allSettled_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_any_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_prototype_catch_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_prototype_finally_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_prototype_then_Call(std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(auto promise = this_argument.value_or(JS_undefined).cast_object<JS::Class::Promise>())
	{
		auto constructor = JS::GetSpeciesConstructor(promise, JS::Intrinsics()->Promise);
		PromiseCapability<> resultCapability { constructor };

		// PerformPromiseThen

		auto onFulfilled = arguments.size() > 0 ? arguments[0] : JS_undefined;
		auto onRejected = arguments.size() > 1 ? arguments[1] : JS_undefined;

		return promise->PerformPromiseThen(onFulfilled, onRejected, resultCapability);
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("Promise.prototype.then called on non-promise object"));
	}
}

JSValue JS::Runtime::Builtin::Promise_race_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_reject_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_resolve_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_try_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Promise_withResolvers_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin PrototypeFunction class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::PrototypeFunctionClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::PrototypeFunctionClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSString JS::Runtime::Builtin::PrototypeFunctionClass::GetTypeofString()
{
	return u"function";
}

JS::Runtime::TypeHint JS::Runtime::Builtin::PrototypeFunctionClass::GetDefaultHint()
{
	return JS::Runtime::TypeHint::String;
}
#endif

//// builtin Proxy

JSValue JS::Runtime::Builtin::Proxy_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC("Proxy constructor called without new"));
	}
	auto target = arguments_get_index(arguments, 0);
	auto handler = arguments_get_index(arguments, 1);
	auto target_object_option = GetNonNullObject(target);
	if(!target_object_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("Proxy constructor provided non-object as target"));
	}
	auto handler_object_option = GetNonNullObject(handler);
	if(!handler_object_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("Proxy constructor provided non-object as handler"));
	}
	// Proxy is an exotic object with no [[Prototype]] or [[Extensible]] internal slots, so we cannot call JS::New
	return new JS::Class::Proxy(target_object_option.value(), handler_object_option.value());
}

JSValue JS::Runtime::Builtin::Proxy_revocable_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::QName_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::QNameClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::QNameClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(phase == JS::Runtime::ExecutionPhase::Compile)
	{
		JS::Throw(JS::New<ConstantError> __LOC("error class constructor at compile time"));
	}

	// TODO
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::QName_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RangeError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ReferenceError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_apply_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_construct_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_defineProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_deleteProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_getOwnPropertyDescriptor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_getPrototypeOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_has_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_isExtensible_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_ownKeys_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_preventExtensions_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Reflect_setPrototypeOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin RegExp

JSValue JS::Runtime::Builtin::RegExp_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin RegExp class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::RegExpClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExpClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::RegExpStringIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_escape_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_compile_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_dotAll_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_exec_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_flags_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_global_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_hasIndices_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_ignoreCase_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_match_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_matchAll_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_multiline_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_replace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_search_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_source_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_split_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_sticky_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_test_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_unicodeSets_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_prototype_unicode_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::RegExp_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Set

JSValue JS::Runtime::Builtin::Set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SetIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_add_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_clear_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_delete_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_difference_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_entries_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_forEach_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_has_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_intersection_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_isDisjointFrom_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_isSubsetOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_isSupersetOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_keys_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_size_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_symmetricDifference_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_union_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_prototype_values_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Set_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_prototype_byteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_prototype_grow_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_prototype_growable_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_prototype_maxByteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_prototype_slice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SharedArrayBuffer_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin String

JSValue JS::Runtime::Builtin::String_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	JSString s;
	if(arguments.size() == 0)
	{
		s = u"";
	}
	else
	{
		if(new_target == nullptr)
		{
			if(auto symbol = GetSymbol(arguments[0]))
			{
				return u"Symbol(" + (*symbol)->Description.value_or(u"") + u")";
			}
		}

		s = JS::ToString(arguments[0] WITH_PHASE(phase));
	}

	if(new_target == nullptr)
	{
		// called as function
		return s;
	}
	else
	{
		// called as constructor
		return JS::New<JS::Class::String>(s);
	}
}

//// builtin String class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::StringClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::StringClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(u"");
	}
	else
	{
		return JS::ToString(arguments[0] RUNTIME_PHASE);
	}
}

std::optional<JSValue> JS::Runtime::Builtin::StringClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(IsNull(object) || GetString(object))
	{
		return object;
	}
	else if(auto c = object.get_if<JSChar>())
	{
		return std::u16string(1, *c);
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::StringClass::GetTypeofString()
{
	return u"string";
}

std::optional<JSValue> JS::Runtime::Builtin::StringClass::GetDefaultValue()
{
	return u"";
}
#endif

JSValue JS::Runtime::Builtin::StringIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_fromCharCode_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_fromCodePoint_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_anchor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_at_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_big_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_blink_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_bold_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_charAt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_charCodeAt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_codePointAt_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_concat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_endsWith_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_fixed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_fontcolor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_fontsize_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_includes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_indexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_isWellFormed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_italics_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_iterator_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_lastIndexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_link_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_localeCompare_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_match_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_matchAll_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_normalize_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_padEnd_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_padStart_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_repeat_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_replace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_replaceAll_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_search_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_slice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_small_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_split_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_startsWith_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_strike_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_sub_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_substr_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_substring_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_sup_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_toLocaleLowerCase_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_toLocaleUpperCase_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_toLowerCase_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS::ToString(this_argument.value().cast_object<JS::Class::String>()->StringData WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::String_prototype_toUpperCase_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_toWellFormed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_trim_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_trimEnd_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_trimLeft_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::String_raw_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target != nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	if(arguments.size() == 0 || IsUndefined(arguments[0]))
	{
		return new JS::Runtime::SymbolInstance();
	}
	else
	{
		return new JS::Runtime::SymbolInstance(JS::ToString(arguments[0] WITH_PHASE(phase)));
	}
}

JSValue JS::Runtime::Builtin::Symbol_for_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_keyFor_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_prototype_descriptor_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_prototype_toPrimitive_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::Symbol_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::SyntaxError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	JS::Throw(JS::New<TypeError> __LOC(u"calling TypedArray object is not allowed"));
}

JSValue JS::Runtime::Builtin::TypedArray_from_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_of_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_at_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_buffer_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_byteLength_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_byteOffset_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_copyWithin_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_entries_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_every_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_fill_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_filter_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_find_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_findIndex_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_findLast_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_findLastIndex_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_forEach_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_includes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_indexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_join_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_keys_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_lastIndexOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_length_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_map_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_reduce_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_reduceRight_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_reverse_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_slice_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_some_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_sort_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_subarray_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_toLocaleString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_toReversed_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_toSorted_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_toStringTag_get_Call(std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(this_argument.has_value())
	{
		if(auto this_object = this_argument.value().get_if<JSObject>())
		{
			if(auto typed_array = std::dynamic_pointer_cast<JS::Class::TypedArray>(this_object.value()))
			{
				return JS::Class::TypedArray::ArrayName(typed_array->TypedArrayName);
			}
		}
	}
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_values_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_prototype_with_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypedArray_species_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::TypeError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin ulong class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::UlongClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::UlongClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(0ul);
	}
	else
	{
		auto p = JS::ToPrimitive(arguments[0] WITH_PHASE(phase), JS::Runtime::TypeHint::Number);
		if(IsNull(p))
		{
			return JSValue(JSUlong(0));
		}
		else if(auto b = GetBoolean(p))
		{
			return JSValue(JSUlong(*b ? 1 : 0));
		}
		else if(IsUndefined(p))
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}
		else if(auto l = p.get_if<JSLong>())
		{
			if(*l < 0)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(JSLong(*l));
			}
		}
		else if(auto u = p.get_if<JSUlong>())
		{
			return JSValue(*l);
		}
		else if(auto f = p.get_if<JSFloat>())
		{
			if(std::fpclassify(*f) == FP_NAN || std::fpclassify(*f) == FP_INFINITE || *f < 0 || UINT64_MAX < *f)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			JSUlong u = JSUlong(*f);
			if(JSFloat(u) != *f)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(u);
			}
		}
		else if(auto d = p.get_if<JSNumber>())
		{
			if(std::fpclassify(*d) == FP_NAN || std::fpclassify(*d) == FP_INFINITE || *d < 0 || UINT64_MAX < *d)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			JSUlong u = JSUlong(*d);
			if(JSNumber(u) != *d)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(u);
			}
		}
		else
		{
			JSString s;
			if(auto c = p.get_if<JSChar>())
			{
				s = std::u16string(1, *c);
			}
			else
			{
				s = GetString(p).value();
			}
			// TODO
			std::string tmp;
			for(char16_t c : s)
			{
				if(c < 0x7F)
					tmp += char(c);
				else
					return NAN;
			}
			long long v;
			size_t pos;
			try
			{
				v = std::stoll(tmp, &pos, 10); // TODO
			}
			catch(const std::invalid_argument&)
			{
				JS::Throw(JS::New<TypeError> __LOC()); // TODO: or possibly RangeError
			}
			if(pos != tmp.size())
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			if(v < 0 || static_cast<long long>(UINT64_MAX) < v)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			return JSValue(JSUlong(v));
		}
	}
}

std::optional<JSValue> JS::Runtime::Builtin::UlongClass::Coerce(JSValue object, bool strict)
{
	if(auto l = object.get_if<JSLong>())
	{
		if(*l < 0)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(JSLong(*l));
		}
	}
	else if(auto u = object.get_if<JSUlong>())
	{
		return JSValue(*l);
	}
	else if(auto f = object.get_if<JSFloat>())
	{
		if(std::fpclassify(*f) == FP_NAN || std::fpclassify(*f) == FP_INFINITE || *f < 0 || UINT64_MAX < *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		JSUlong u = JSUlong(*f);
		if(JSFloat(u) != *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(u);
		}
	}
	else if(auto d = object.get_if<JSNumber>())
	{
		if(std::fpclassify(*d) == FP_NAN || std::fpclassify(*d) == FP_INFINITE || *d < 0 || UINT64_MAX < *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		JSUlong u = JSUlong(*d);
		if(JSNumber(u) != *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSValue(u);
		}
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::UlongClass::GetTypeofString()
{
	return u"ulong";
}

std::optional<JSValue> JS::Runtime::Builtin::UlongClass::GetDefaultValue()
{
	return JSValue(JSUlong(0));
}
#endif

//// builtin ulong.prototype

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ulong_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::ulong_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

//// builtin unescape

JSValue JS::Runtime::Builtin::unescape_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::UninitializedError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::URIError_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

//// builtin Void class

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::VoidClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		return JS_undefined;
	}
}

JSValue JS::Runtime::Builtin::VoidClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 0)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else
	{
		return JS_undefined;
	}
}

std::optional<JSValue> JS::Runtime::Builtin::VoidClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(IsNull(object) || IsUndefined(object))
	{
		return JS_undefined;
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::VoidClass::GetTypeofString()
{
	return u"undefined";
}

std::optional<JSValue> JS::Runtime::Builtin::VoidClass::GetDefaultValue()
{
	return JS_undefined;
}
#endif

JSValue JS::Runtime::Builtin::WeakMap_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakMap_prototype_delete_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakMap_prototype_get_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakMap_prototype_has_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakMap_prototype_set_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakRef_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakRef_prototype_deref_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakSet_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakSet_prototype_add_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakSet_prototype_delete_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WeakSet_prototype_has_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WrapForValidIteratorPrototype_next_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::WrapForValidIteratorPrototype_return_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::XMLClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSString JS::Runtime::Builtin::XMLClass::GetTypeofString()
{
	return u"xml";
}

std::optional<JSValue> JS::Runtime::Builtin::XMLClass::GetDefaultValue()
{
	return {}; // TODO
}
#endif

JSValue JS::Runtime::Builtin::XMLList_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::XMLListClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLListClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSString JS::Runtime::Builtin::XMLListClass::GetTypeofString()
{
	return u"xml";
}

std::optional<JSValue> JS::Runtime::Builtin::XMLListClass::GetDefaultValue()
{
	return {}; // TODO
}
#endif

JSValue JS::Runtime::Builtin::XMLList_prototype_attribute_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_attributes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_child_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_children_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_comments_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_contains_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_copy_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_descendants_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_domNode_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_domNodeList_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_elements_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_hasComplexContent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_hasOwnProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_hasSimpleContent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_normalize_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_parent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_processingInstructions_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_propertyIsEnumerable_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_text_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JS::ToString(this_argument.value_or(JS_undefined) RUNTIME_PHASE);
}

JSValue JS::Runtime::Builtin::XMLList_prototype_toXMLString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XMLList_prototype_xpath_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_defaultSettings_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_addNamespace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_appendChild_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_attribute_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_attributes_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_child_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_childIndex_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_children_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_comments_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_contains_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_copy_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_descendants_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_domNode_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_domNodeList_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_elements_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_hasComplexContent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_hasOwnProperty_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_hasSimpleContent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_inScopeNamespaces_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_insertChildAfter_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_insertChildBefore_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_length_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_localName_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_name_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_namespace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_namespaceDeclarations_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_nodeKind_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_normalize_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_parent_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_prependChild_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_processingInstructions_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_propertyIsEnumerable_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_removeNamespace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_replace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_setChildren_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_setLocalName_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_setName_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_setNamespace_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_text_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_toString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JS::ToString(this_argument.value_or(JS_undefined) RUNTIME_PHASE);
}

JSValue JS::Runtime::Builtin::XML_prototype_toXMLString_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_valueOf_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_prototype_xpath_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_setSettings_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::XML_settings_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

#if ENABLE_DEBUG
JSValue JS::Runtime::Builtin::_context_stack_size_Call([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	return JSNumber(JS::Runtime::ExecutionContext::GetContextStack().size());
}

JSValue JS::Runtime::Builtin::_log_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(Enable_Debug)
	{
		for(auto argument : arguments)
		{
			JS::Runtime::Debugger::Current->GotoElement(JS::Runtime::Output_Element);
			argument.serialize(JS::Runtime::Debugger::Current);
		}
	}
	else
	{
		std::cerr << "Debugging not enabled" << std::endl;
	}
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::_relog_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(Enable_Debug)
	{
		for(auto argument : arguments)
		{
			JS::Runtime::Debugger::Current->GotoElement(JS::Runtime::Output_Element);
			argument.serialize(JS::Runtime::Debugger::Current, /* force_output = */ true);
		}
	}
	else
	{
		std::cerr << "Debugging not enabled" << std::endl;
	}
	return JS_undefined;
}
#endif

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::ErrorSubClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}
#endif

//// builtin Integer classes

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::IntegerClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::IntegerClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(0l);
	}
	else
	{
		JSNumber n = JS::ToNumber(arguments[0] WITH_PHASE(phase));
		if(std::fpclassify(n) == FP_NAN || std::fpclassify(n) == FP_INFINITE || n < lowest || highest < n)
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}
		int64_t i = int64_t(n);
		if(JSNumber(i) != n)
		{
			JS::Throw(JS::New<RangeError> __LOC());
		}
		else
		{
			return JSNumber(i);
		}
	}
}

JSBoolean JS::Runtime::Builtin::IntegerClass::Is(JSValue object)
{
	if(auto p = JS::GetNumber(object))
	{
		auto n = *p;
		if(std::fpclassify(n) == FP_NAN || std::fpclassify(n) == FP_INFINITE || n < lowest || highest < n)
		{
			return false;
		}

		int64_t i = int64_t(n);
		return JSNumber(i) == n;
	}
	else
	{
		return false;
	}
}

std::optional<JSValue> JS::Runtime::Builtin::IntegerClass::Coerce(JSValue object, bool strict)
{
	if(auto l = object.get_if<JSLong>())
	{
		if(*l < lowest || highest < *l)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSNumber(*l);
		}
	}
	else if(auto u = object.get_if<JSUlong>())
	{
		if(uint64_t(highest) < *u)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSNumber(*u);
		}
	}
	else if(auto f = object.get_if<JSFloat>())
	{
		if(std::fpclassify(*f) == FP_NAN || std::fpclassify(*f) == FP_INFINITE || *f < lowest || highest < *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		int64_t i = int64_t(*f);
		if(JSFloat(i) != *f)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSNumber(i);
		}
	}
	else if(auto d = object.get_if<JSNumber>())
	{
		if(std::fpclassify(*d) == FP_NAN || std::fpclassify(*d) == FP_INFINITE || *d < lowest || highest < *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		int64_t i = int64_t(*d);
		if(JSNumber(i) != *d)
		{
			if(strict)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return {};
			}
		}
		else
		{
			return JSNumber(i);
		}
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::IntegerClass::GetTypeofString()
{
	return u"number";
}

std::optional<JSValue> JS::Runtime::Builtin::IntegerClass::GetDefaultValue()
{
	return 0.0;
}
#endif

//// builtin Decimal

#if ENABLE_DEC128
JSValue JS::Runtime::Builtin::DecimalClass::Call([[maybe_unused]] JSValue this_argument, [[maybe_unused]] std::vector<JSValue> arguments AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

JSValue JS::Runtime::Builtin::DecimalClass::Construct([[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	// TODO
	return JS_undefined;
}

std::optional<JSValue> JS::Runtime::Builtin::DecimalClass::Coerce([[maybe_unused]] JSValue object, [[maybe_unused]] bool strict)
{
	// TODO
	return {};
}

JSString JS::Runtime::Builtin::DecimalClass::GetTypeofString()
{
	return u"decimal";
}

std::optional<JSValue> JS::Runtime::Builtin::DecimalClass::GetDefaultValue()
{
	return JSValue(JSDecimal(0));
}
#endif

//// builtin Char

#if ENABLE_ES4
JSValue JS::Runtime::Builtin::CharClass::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return Construct(arguments, JSClass(this) WITH_PHASE(phase));
}

JSValue JS::Runtime::Builtin::CharClass::Construct(std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	if(arguments.size() > 1)
	{
		JS::Throw(JS::New<ArgumentError> __LOC());
	}
	else if(arguments.size() == 0)
	{
		return JSValue(JSChar(u'\0'));
	}
	else
	{
		auto argument = arguments[0];
		if(IsUndefined(argument))
		{
			return JSValue(JSChar(u'\0'));
		}
		else if(argument.get_if<JSChar>())
		{
			return argument;
		}
		else
		{
			auto string = JS::ToString(argument WITH_PHASE(phase));
			if(string.size() != 1)
			{
				JS::Throw(JS::New<RangeError> __LOC());
			}
			else
			{
				return JSValue(string[0]);
			}
		}
	}
}

std::optional<JSValue> JS::Runtime::Builtin::CharClass::Coerce(JSValue object, [[maybe_unused]] bool strict)
{
	if(object.get_if<JSChar>())
	{
		return object;
	}
	else
	{
		return {};
	}
}

JSString JS::Runtime::Builtin::CharClass::GetTypeofString()
{
	return u"char";
}

std::optional<JSValue> JS::Runtime::Builtin::CharClass::GetDefaultValue()
{
	return JSValue(JSChar(u'\0'));
}
#endif

//// builtins

// CommonJS
JSValue JS::Runtime::Builtin::require_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(new_target != nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC("require called as constructor"));
	}

	// TODO: AMD permits more arguments

	if(auto path = GetString(arguments_get_index(arguments, 0)))
	{
		return JS::Host->LoadRequiredModule(path.value());
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("require argument must be string"));
	}
}

// AMD
JSValue JS::Runtime::Builtin::define_Call([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase))
{
	auto id = arguments_get_index(arguments, 0);
	auto dependencies = arguments_get_index(arguments, 1);
	auto factory = arguments_get_index(arguments, 2);
	if(!GetString(id))
	{
		factory = dependencies;
		dependencies = id;
		id = JS_null;
	}
	if(dependencies.ToStringTag() != u"Array")
	{
		factory = dependencies;
		dependencies = JS_null;
	}
	JS::Host->DeclareDefine(JS::Runtime::AsynchronousModuleDefinition::Create(id, dependencies, factory));
	return JS_undefined;
}

//// Intrinsics

#if 0
#if ENABLE_ES4
JSFunction JS::Runtime::Intrinsics::CreateErrorConstructor(JSString name, JS::Class::OrdinaryObject *& prototype, JSClass& class_, Realm& realm)
#else
JSFunction JS::Runtime::Intrinsics::CreateErrorConstructor(JSString name, JS::Class::OrdinaryObject *& prototype, Realm& realm)
#endif
{
	// requires the class
	prototype = new JS::Class::OrdinaryObject();
	prototype->Extensible = true;
	prototype->Prototype = Error_prototype;

#if ENABLE_DEBUG
	std::ostringstream oss;
	PrintString(oss, name);
	std::string name_string = oss.str();

	JS::Runtime::Debugger::SetObjectLabel(prototype, name_string + ".prototype");
#endif

	JSFunction function;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		class_ = JS::New<JS::Runtime::Builtin::ErrorSubClass>(name, prototype, &realm);
		JS::Class::Class::AssignType(prototype, class_);

		function = class_;
	}
	else
#endif
	{
		function = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Error_Call, 1, name, prototype, &realm);
	}

#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(function, name_string);
#endif
	return function;
}
#endif

#if 0
void JS::Runtime::Intrinsics::Initialize(Realm& realm)
{
	//// global Object

#if ENABLE_ES4
	if(IsVersionES4())
	{
		// needs Class_class and Class_prototype, but these are only created later
		Object_class = JS::New<JS::Runtime::Builtin::ObjectClass>(&realm);
	}
#endif

	Object_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(std::optional<JSValue>{});
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Object_prototype, "Object.prototype");
#endif
#if ENABLE_ES4
	if(IsVersionES4())
	{
		Object_class->ClassPrototype = Object_prototype;
	}
#endif

	//// global Class

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Class_prototype = JS::New<JS::Class::OrdinaryObject>();
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Class_prototype, "Class.prototype");
#endif
		Object_class->Prototype = Class_prototype; // delayed until now

		// needs Class_class
		Class_class = JS::New<JS::Runtime::Builtin::ClassClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Class_class, "Class");
#endif
		JS::Class::Class::AssignType(Class_class, Class_class); // delayed until now
		JS::Class::Class::AssignType(Object_class, Class_class); // delayed until now
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Object = Object_class;
	}
	else
#endif
	{
		Object = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Object_Call, 1, u"Object", Object_prototype, &realm);
	}

#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Object, "Object");
#endif

	//// global Function

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Function_prototype = JS::New<JS::Class::OrdinaryObject>();
	}
	else
#endif
	{
		Function_prototype = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Function_prototype_Call, 0, u"", Object_prototype, &realm);
		Object.cast_object<JS::Class::OrdinaryObject *>()->Prototype = Function_prototype;
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Function_prototype, "Function.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		// needs Function_class
		Function_class = JS::New<JS::Runtime::Builtin::FunctionClass>(&realm);
		JS::Class::Class::AssignType(Function_prototype, Function_class);

		Function = Function_class;

		PrototypeFunction_class = JS::New<JS::Runtime::Builtin::PrototypeFunctionClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(PrototypeFunction_class, "PrototypeFunction");
#endif
	}
	else
#endif
	{
		Function = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Function_Call, 1, u"Function", Function_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Function, "Function");
#endif

	//// global GeneratorFunction

	if(Version::ES6 <= CurrentVersion)
	{
		IteratorPrototype = JS::New<JS::Class::OrdinaryObject>();
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(IteratorPrototype, "%IteratorPrototype%");
#endif

		ArrayIteratorPrototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(IteratorPrototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(ArrayIteratorPrototype, "%ArrayIteratorPrototype%");
#endif

		GeneratorPrototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(IteratorPrototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneratorPrototype, "%GeneratorPrototype%");
#endif
		GeneratorFunction_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(Function_prototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneratorFunction_prototype, "%GeneratorFunction%.prototype");
#endif
		GeneratorFunction = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::GeneratorFunction_Call, 1, u"GeneratorFunction", GeneratorFunction_prototype, &realm);
		Define(GeneratorFunction_prototype, u"constructor", GeneratorFunction);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneratorFunction, "%GeneratorFunction%");
#endif
	}

	if(Version::ESNEXT <= CurrentVersion)
	{
		AsyncIteratorPrototype = JS::New<JS::Class::OrdinaryObject>();
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(AsyncIteratorPrototype, "%AsyncIteratorPrototype%");
#endif
		AsyncFromSyncIteratorPrototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(AsyncIteratorPrototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(AsyncFromSyncIteratorPrototype, "%AsyncFromSyncIteratorPrototype%");
#endif

		AsyncFunction_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(Function_prototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneratorFunction, "%AsyncFunction%.prototype");
#endif
		AsyncFunction = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::AsyncFunction_Call, 1, u"AsyncFunction", AsyncFunction_prototype, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneratorFunction, "%AsyncFunction%");
#endif

		AsyncGeneratorPrototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(IteratorPrototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(AsyncGeneratorPrototype, "%AsyncGeneratorPrototype%");
#endif
		AsyncGeneratorFunction_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(Function_prototype);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(AsyncGeneratorFunction_prototype, "%AsyncGeneratorFunction%.prototype");
#endif
		AsyncGeneratorFunction = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::AsyncGeneratorFunction_Call, 1, u"AsyncGeneratorFunction", AsyncGeneratorFunction_prototype, &realm);
		Define(AsyncGeneratorFunction_prototype, u"constructor", AsyncGeneratorFunction);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(AsyncGeneratorFunction, "%AsyncGeneratorFunction%");
#endif
	}

	//// global Package

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Package_class = JS::New<JS::Runtime::Builtin::PackageClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Package_class, "Package");
#endif
	}
#endif

	//// global Boolean

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Boolean_prototype = new JS::Class::OrdinaryObject();
	}
	else
#endif
	{
		Boolean_prototype = new JS::Class::Boolean(false);
	}
	Boolean_prototype->Extensible = true;
	Boolean_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Boolean_prototype, "Boolean.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Boolean_class = JS::New<JS::Runtime::Builtin::BooleanClass>(&realm);
		JS::Class::Class::AssignType(Boolean_prototype, Object_class);

		Boolean = Boolean_class;
	}
	else
#endif
	{
		Boolean = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Boolean_Call, 1, u"Boolean", Boolean_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Boolean, "Boolean");
#endif

	//// global GeneralNumber

#if ENABLE_ES4
	if(IsVersionES4())
	{
		GeneralNumber_prototype = new JS::Class::OrdinaryObject();
		GeneralNumber_prototype->Extensible = true;
		GeneralNumber_prototype->Prototype = Object_prototype;
		JS::Class::Class::AssignType(GeneralNumber_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneralNumber_prototype, "GeneralNumber.prototype");
#endif
		GeneralNumber_class = JS::New<JS::Runtime::Builtin::GeneralNumberClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(GeneralNumber_class, "GeneralNumber");
#endif

		Long_prototype = new JS::Class::OrdinaryObject();
		Long_prototype->Extensible = true;
		Long_prototype->Prototype = GeneralNumber_prototype;
		JS::Class::Class::AssignType(Long_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Long_prototype, "long.prototype");
#endif
		Long_class = JS::New<JS::Runtime::Builtin::LongClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Long_class, "long");
#endif

		Ulong_prototype = new JS::Class::OrdinaryObject();
		Ulong_prototype->Extensible = true;
		Ulong_prototype->Prototype = GeneralNumber_prototype;
		JS::Class::Class::AssignType(Ulong_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Ulong_prototype, "ulong.prototype");
#endif
		Ulong_class = JS::New<JS::Runtime::Builtin::UlongClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Ulong_class, "ulong");
#endif

		Float_prototype = new JS::Class::OrdinaryObject();
		Float_prototype->Extensible = true;
		Float_prototype->Prototype = GeneralNumber_prototype;
		JS::Class::Class::AssignType(Float_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Float_prototype, "float.prototype");
#endif
		Float_class = JS::New<JS::Runtime::Builtin::FloatClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Float_class, "float");
#endif

#if ENABLE_DEC128
		Decimal_prototype = new JS::Class::OrdinaryObject();
		Decimal_prototype->Extensible = true;
		Decimal_prototype->Prototype = GeneralNumber_prototype;
		JS::Class::Class::AssignType(Decimal_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Decimal_prototype, "decimal.prototype");
#endif
		Decimal_class = JS::New<JS::Runtime::Builtin::DecimalClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Decimal_class, "decimal");
#endif
#endif
	}
#endif

	//// global Number

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Number_prototype = new JS::Class::OrdinaryObject();
		Number_prototype->Prototype = GeneralNumber_prototype;
	}
	else
#endif
	{
		Number_prototype = new JS::Class::Number(0.0);
		Number_prototype->Prototype = Object_prototype;
	}
	Number_prototype->Extensible = true;
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Number_prototype, "Number.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Number_class = JS::New<JS::Runtime::Builtin::NumberClass>(&realm);
		JS::Class::Class::AssignType(Number_prototype, Object_class);

		Number = Number_class;
	}
	else
#endif
	{
		Number = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Number_Call, 1, u"Number", Number_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Number, "Number");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Sbyte_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"sbyte", INT8_MIN, INT8_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Sbyte_class, "sbyte");
#endif
		Byte_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"byte", 0, UINT8_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Byte_class, "byte");
#endif
		Short_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"short", INT16_MIN, INT16_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Short_class, "short");
#endif
		Ushort_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"ushort", 0, UINT16_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Ushort_class, "ushort");
#endif
		Int_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"int", INT32_MIN, INT32_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Int_class, "int");
#endif
		Uint_class = JS::New<JS::Runtime::Builtin::IntegerClass>(u"uint", 0, UINT32_MAX, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Uint_class, "uint");
#endif
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Char_prototype = new JS::Class::OrdinaryObject();
		Char_prototype->Extensible = true;
		Char_prototype->Prototype = String_prototype;
		JS::Class::Class::AssignType(Char_prototype, Object_class);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Char_prototype, "char.prototype");
#endif
		Char_class = JS::New<JS::Runtime::Builtin::CharClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Char_class, "char");
#endif

		Attribute_class = JS::New<JS::Runtime::Builtin::AttributeClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Attribute_class, "Attribute");
#endif
	}
#endif

	//// global String

#if ENABLE_ES4
	if(IsVersionES4())
	{
		String_prototype = new JS::Class::OrdinaryObject();
	}
	else
#endif
	{
		String_prototype = new JS::Class::String(u"");
	}
	String_prototype->Extensible = true;
	String_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(String_prototype, "String.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		String_class = JS::New<JS::Runtime::Builtin::StringClass>(&realm);
		JS::Class::Class::AssignType(String_prototype, String_class);

		String = String_class;
	}
	else
#endif
	{
		String = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::String_Call, 1, u"String", String_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(String, "String");
#endif

	//// global Symbol

	if(Version::ES6 <= CurrentVersion)
	{
		Symbol_prototype = new JS::Class::OrdinaryObject();
		Symbol_prototype->Extensible = true;
		Symbol_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Symbol_prototype, "Symbol.prototype");
#endif

		Symbol = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Symbol_Call, 0, u"Symbol", Symbol_prototype, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Symbol, "Symbol");
#endif
	}

	//// global BigInt

	if(Version::ESNEXT <= CurrentVersion)
	{
		BigInt_prototype = new JS::Class::OrdinaryObject();
		BigInt_prototype->Extensible = true;
		BigInt_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(BigInt_prototype, "BigInt.prototype");
#endif

		BigInt = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::BigInt_Call, 1, u"BigInt", BigInt_prototype, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(BigInt, "BigInt");
#endif
	}

	//// global Date

	Date_prototype = new JS::Class::OrdinaryObject();
	Date_prototype->Extensible = true;
	Date_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Date_prototype, "Date.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Date_class = JS::New<JS::Runtime::Builtin::DateClass>(&realm);
		JS::Class::Class::AssignType(Date_prototype, Date_class);

		Date = Date_class;
	}
	else
#endif
	{
		Date = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Date_Call, 7, u"Date", Date_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Date, "Date");
#endif

	//// global Array

	// requires Array_class
	Array_prototype = new JS::Class::Array(0);
	Array_prototype->Extensible = true;
	Array_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Array_prototype, "Array.prototype");
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Array_class = JS::New<JS::Runtime::Builtin::ArrayClass>(&realm);
		JS::Class::Class::AssignType(Array_prototype, Array_class);

		Array = Array_class;
	}
	else
#endif
	{
		Array = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Array_Call, 1, u"Array", Array_prototype, &realm);
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Array, "Array");
#endif

	//// global RegExp

	if(Version::ES3 <= CurrentVersion)
	{
		RegExp_prototype = new JS::Class::OrdinaryObject();
		RegExp_prototype->Extensible = true;
		RegExp_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(RegExp_prototype, "RegExp.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			RegExp_class = JS::New<JS::Runtime::Builtin::RegExpClass>(&realm);
			JS::Class::Class::AssignType(RegExp_prototype, RegExp_class);

			RegExp = RegExp_class;
		}
		else
#endif
		{
			RegExp = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::RegExp_Call, 2, u"RegExp", RegExp_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(RegExp, "RegExp");
#endif
	}

	//// global Error types

	if(Version::ES3 <= CurrentVersion)
	{
		// requires Error_class
		Error_prototype = new JS::Class::OrdinaryObject();
		Error_prototype->Extensible = true;
		Error_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Error_prototype, "Error.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			Error_class = JS::New<JS::Runtime::Builtin::ErrorClass>(&realm);
			JS::Class::Class::AssignType(Error_prototype, Error_class);

			Error = Error_class;
		}
		else
#endif
		{
			Error = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Error_Call, 1, u"Error", Error_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Error, "Error");
#endif

		EvalError = CreateErrorConstructor(
			u"EvalError",
			EvalError_prototype,
#if ENABLE_ES4
			EvalError_class,
#endif
			realm);

		RangeError = CreateErrorConstructor(
			u"RangeError",
			RangeError_prototype,
#if ENABLE_ES4
			RangeError_class,
#endif
			realm);

		ReferenceError = CreateErrorConstructor(
			u"ReferenceError",
			ReferenceError_prototype,
#if ENABLE_ES4
			ReferenceError_class,
#endif
			realm);

		SyntaxError = CreateErrorConstructor(
			u"SyntaxError",
			SyntaxError_prototype,
#if ENABLE_ES4
			SyntaxError_class,
#endif
			realm);

		TypeError = CreateErrorConstructor(
			u"TypeError",
			TypeError_prototype,
#if ENABLE_ES4
			TypeError_class,
#endif
			realm);

		URIError = CreateErrorConstructor(
			u"URIError",
			URIError_prototype,
#if ENABLE_ES4
			URIError_class,
#endif
			realm);
	}
#if ENABLE_ES4
	if(IsVersionES4())
	{
		CreateErrorConstructor(u"ArgumentError", ArgumentError_prototype, ArgumentError_class, realm);
		CreateErrorConstructor(u"AttributeError", AttributeError_prototype, AttributeError_class, realm);
		CreateErrorConstructor(u"ConstantError", ConstantError_prototype, ConstantError_class, realm);
		CreateErrorConstructor(u"DefinitionError", DefinitionError_prototype, DefinitionError_class, realm);
		CreateErrorConstructor(u"UninitializedError", UninitializedError_prototype, UninitializedError_class, realm);
	}
#endif

	//// global Namespace

#if ENABLE_ES4
	if(Supports_E4X() || IsVersionES4())
#else
	if(Supports_E4X())
#endif
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			Namespace_prototype = new JS::Class::OrdinaryObject();
		}
		else
#endif
		{
			Namespace_prototype = new JS::Class::Namespace(u"", u"");
		}
		Namespace_prototype->Extensible = true;
		Namespace_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Namespace_prototype, "Namespace.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			// needs Function_class
			Namespace_class = JS::New<JS::Runtime::Builtin::NamespaceClass>(&realm);
			JS::Class::Class::AssignType(Namespace_prototype, Namespace_class);

			Namespace = Namespace_class;
		}
		else
#endif
		{
			Namespace = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Namespace_Call, 2, u"Namespace", Namespace_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Namespace, "Namespace");
#endif
		Define(Namespace_prototype, u"constructor", Namespace); // TODO: move
	}

	//// QName

	if(Supports_E4X())
	{
		// requires QName_class
		QName_prototype = new JS::Class::QName();
		QName_prototype->Extensible = true;
		QName_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(QName_prototype, "QName.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			// needs Function_class
			QName_class = JS::New<JS::Runtime::Builtin::QNameClass>(&realm);
			JS::Class::Class::AssignType(QName_prototype, QName_class);

			QName = QName_class;
		}
		else
#endif
		{
			QName = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::QName_Call, 2, u"QName", QName_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(QName, "QName");
#endif
		Define(QName_prototype, u"constructor", QName); // TODO: move

	//// global XML

		XML_prototype = new JS::Class::XML();
		XML_prototype->Extensible = true;
		XML_prototype->Prototype = Object_prototype;
		XML_prototype->Class = JS::Class::XML::ClassType::Text;
		XML_prototype->Value = u"";
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(XML_prototype, "XML.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			// needs Function_class
			XML_class = JS::New<JS::Runtime::Builtin::XMLClass>(&realm);
			JS::Class::Class::AssignType(XML_prototype, XML_class);

			XML = XML_class;
		}
		else
#endif
		{
			XML = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::XML_Call, 1, u"XML", XML_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(XML, "XML");
#endif

	//// global XMLList

		XMLList_prototype = new JS::Class::XMLList();
		XMLList_prototype->Extensible = true;
		XMLList_prototype->Prototype = Object_prototype;
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(XMLList_prototype, "XMLList.prototype");
#endif

#if ENABLE_ES4
		if(IsVersionES4())
		{
			// needs Function_class
			XMLList_class = JS::New<JS::Runtime::Builtin::XMLListClass>(&realm);
			JS::Class::Class::AssignType(XMLList_prototype, XMLList_class);

			XMLList = XMLList_class;
		}
		else
#endif
		{
			XMLList = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::XMLList_Call, 1, u"XMLList", XMLList_prototype, &realm);
		}
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(XMLList, "XMLList");
#endif
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		//// global Never
		Never_class = JS::New<JS::Runtime::Builtin::NeverClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Never_class, "Never");
#endif

		//// global Void
		Void_class = JS::New<JS::Runtime::Builtin::VoidClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Void_class, "Void");
#endif

		//// global Null
		Null_class = JS::New<JS::Runtime::Builtin::NullClass>(&realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Null_class, "Null");
#endif
	}
#endif

	//// global Proxy
#if ENABLE_ES4
	if(!IsVersionES4())
#else
	//if(Version::ES6 <= CurrentVersion) // allow Proxy objects to be created in earlier versions
#endif
	{
		Proxy = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Proxy_Call, 2, u"Proxy", std::optional<JSObject>{}, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Proxy, "Proxy");
#endif

	//// global Promise
		Promise_prototype = JS::New<JS::Class::OrdinaryObject>();
		Promise = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Promise_Call, 1, u"Promise", Promise_prototype, &realm);
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(Promise, "Promise");
#endif
		Define(Promise_prototype, u"constructor", Promise, JS::PropertyAttributes().WithEnumerable(false));
	}

	//// global Math

#if ENABLE_ES4
	if(IsVersionES4())
	{
		// based on ActionScript and ES4.2008
		// ActionScript: public final class Math
		// ES4.2008: singleton object of helper class Math, Math.[[Prototype]] has no constructor property, Math.constructor is defined
		Math_prototype = JS::New<JS::Class::OrdinaryObject>();
		Math_class = JS::New<JS::Runtime::Builtin::SingletonClass>(u"Math", Math_prototype, &realm);
		Math_object = JS::New<JS::Class::Math>();
	}
	else
#endif
	{
#if ENABLE_ES4
		Math_prototype = Object_prototype; // to make sure Math object has the prototype Object.prototype
#endif
		Math_object = JS::New<JS::Class::Math>();
	}
#if ENABLE_DEBUG
	JS::Runtime::Debugger::SetObjectLabel(Math_object, "Math");
#endif

	//// global JSON

#if ENABLE_ES4
	if(IsVersionES4())
	{
		// based on ActionScript
		// ActionScript: public final class JSON
		JSON_prototype = JS::New<JS::Class::OrdinaryObject>();
		JSON_class = JS::New<JS::Runtime::Builtin::SingletonClass>(u"JSON", JSON_prototype, &realm);
		JSON_object = JS::New<JS::Class::JSON>();
	}
	else
#endif
	if(Version::ES5 <= CurrentVersion)
	{
		JSON_object = JS::New<JS::Class::JSON>();
#if ENABLE_DEBUG
		JS::Runtime::Debugger::SetObjectLabel(JSON_object, "JSON");
#endif
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Function_class->InstanceMembers.Set(u"length" PUBLIC_NAMESPACES READ_WRITE_ACCESS,
			new JS::Runtime::InstanceVariable(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Immutable = */ true,
				/* Type = */ Number_class,
				/* DefaultValue = */ std::optional<JSValue>()));
		// note: this is not present in the ECMAScript 4 Netscape proposal, but it seems reasonable to include it for compatibility reasons
		Function_class->InstanceMembers.Set(u"name" PUBLIC_NAMESPACES READ_WRITE_ACCESS,
			new JS::Runtime::InstanceVariable(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Immutable = */ true,
				/* Type = */ String_class,
				/* DefaultValue = */ std::optional<JSValue>()));

		PrototypeFunction_class->InstanceMembers.Set(u"prototype" PUBLIC_NAMESPACES READ_WRITE_ACCESS,
			new JS::Runtime::InstanceVariable(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Immutable = */ false,
				/* Type = */ Object_class,
				/* DefaultValue = */ std::optional<JSValue>()));
	}
#endif

	eval = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"eval", {}, &realm);
	parseInt = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 2, u"parseInt", {}, &realm);
	parseFloat = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 2, u"parseFloat", {}, &realm);
	escape = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"escape", {}, &realm);
	unescape = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"unescape", {}, &realm);
	isNaN = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"isNaN", {}, &realm);
	isFinite = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"isFinite", {}, &realm);

	decodeURI = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"decodeURI", {}, &realm);
	decodeURIComponent = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"decodeURIComponent", {}, &realm);
	encodeURI = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"encodeURI", {}, &realm);
	encodeURIComponent = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 1, u"encodeURIComponent", {}, &realm);

#if ENABLE_ES4
	if(IsVersionES4())
	{
		override = JS::CreateBuiltinFunction(JS::Runtime::Builtin::override_Call, 1, u"override", {}, &realm);
	}
#endif

	print = JS::CreateBuiltinFunction(JS::Runtime::Builtin::print_Call, 0, u"print", {}, &realm);
	console = JS::New<JS::Class::OrdinaryObject>();
	Define(console, u"log", JS::CreateBuiltinFunction(JS::Runtime::Builtin::console_log_Call, 0, u"log", {}, &realm));

	if(Enable_CommonJS)
	{
		require = JS::CreateBuiltinFunction(JS::Runtime::Builtin::require_Call, 1, u"require", {}, &realm);
	}
	if(Enable_AMD)
	{
		define = JS::CreateBuiltinFunction(JS::Runtime::Builtin::define_Call, 3, u"define", {}, &realm); // TODO: length?
	}

#if ENABLE_DEBUG
	_context_stack_size = JS::CreateBuiltinFunction(JS::Runtime::Builtin::_context_stack_size_Call, 0, u"context_stack_size", {}, &realm);
	_log = JS::CreateBuiltinFunction(JS::Runtime::Builtin::_log_Call, 0, u"log", {}, &realm);
	_relog = JS::CreateBuiltinFunction(JS::Runtime::Builtin::_relog_Call, 0, u"relog", {}, &realm);
#endif

	//// TypedArray and typed arrays

	if(Version::ES6 <= CurrentVersion)
	{
		ArrayBuffer_prototype = JS::New<JS::Class::OrdinaryObject>();
		ArrayBuffer_object = JS::CreateBuiltinFunction(JS::Runtime::Builtin::ArrayBuffer_Call, 1, u"ArrayBuffer", ArrayBuffer_prototype, &realm);

		TypedArray_prototype = JS::New<JS::Class::OrdinaryObject>();
		TypedArray_object = JS::CreateBuiltinFunction(JS::Runtime::Builtin::TypedArray_Call, 0, u"TypedArray", TypedArray_prototype, &realm);

		Int8Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Int8Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Int8, &Intrinsics::Int8Array_prototype);

		Uint8Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Uint8Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Uint8, &Intrinsics::Uint8Array_prototype);

		Uint8ClampedArray_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Uint8ClampedArray = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Uint8Clamped, &Intrinsics::Uint8ClampedArray_prototype);

		Int16Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Int16Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Int16, &Intrinsics::Int16Array_prototype);

		Uint16Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Uint16Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Uint16, &Intrinsics::Uint16Array_prototype);

		Int32Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Int32Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Int32, &Intrinsics::Int32Array_prototype);

		Uint32Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Uint32Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Uint32, &Intrinsics::Uint32Array_prototype);

		Float32Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Float32Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Float32, &Intrinsics::Float32Array_prototype);

		Float64Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Float64Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Float64, &Intrinsics::Float64Array_prototype);
	}

	if(Version::ES11 <= CurrentVersion)
	{
		BigInt64Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		BigInt64Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::BigInt64, &Intrinsics::BigInt64Array_prototype);

		BigUint64Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		BigUint64Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::BigUint64, &Intrinsics::BigUint64Array_prototype);
	}

	if(Version::ES16 <= CurrentVersion)
	{
		Float16Array_prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(TypedArray_prototype);
		Float16Array = JS::New<JS::Runtime::Builtin::TypedArray>(&realm, JS::Class::TypedArray::ElementType::Float16, &Intrinsics::Float16Array_prototype);
	}

	if(Version::ES6 <= CurrentVersion)
	{
		Map_prototype = JS::New<JS::Class::OrdinaryObject>();
		Map = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Map_Call, 0, u"Map", Map_prototype, &realm);

		Set_prototype = JS::New<JS::Class::OrdinaryObject>();
		Set = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::Set_Call, 0, u"Set", Set_prototype, &realm);

		WeakMap_prototype = JS::New<JS::Class::OrdinaryObject>();
		WeakMap = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::WeakMap_Call, 0, u"WeakMap", WeakMap_prototype, &realm);

		WeakSet_prototype = JS::New<JS::Class::OrdinaryObject>();
		WeakSet = JS::CreateBuiltinConstructor(JS::Runtime::Builtin::WeakSet_Call, 0, u"WeakSet", WeakSet_prototype, &realm);
	}

	//// TODO: missing
	//// Map, Set, WeakMap, WeakSet
	//// ArrayBuffer, SharedArrayBuffer, DataView
	//// Atomics
	//// WeakRef, FinalizationRegistry
	//// ?Iterator objects
	//// Promise
	//// ?GeneratorFunction
	//// Reflect

/*
#if ENABLE_ES4
	if(IsVersionES4())
	{
		internal = JS::New<JS::Class::Namespace>(u"internal");
	}
#endif*/

	////

//	Object->DefineOwnPropertyKey(u"prototype", JSProperty::MakeDynamic(Object_prototype, false, false, false));

	if(CurrentVersion >= Version::ES5)
	{
		Define(Object, u"create", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_create_Call, 2, u"create", {}, &realm));
		Define(Object, u"defineProperties", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_defineProperties_Call, 2, u"defineProperties", {}, &realm));
		Define(Object, u"defineProperty", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_defineProperty_Call, 3, u"defineProperty", {}, &realm));
		Define(Object, u"freeze", JS_null); // TODO
		Define(Object, u"getPrototypeOf",
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_getPrototypeOf_Call, 1, u"getPrototypeOf", {}, &realm));
		Define(Object, u"getOwnPropertyDescriptor",
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_getOwnPropertyDescriptor_Call, 2, u"getOwnPropertyDescriptor", {}, &realm));
		Define(Object, u"getOwnPropertyNames", JS_null); // TODO
		Define(Object, u"isExtensible", JS_null); // TODO
		Define(Object, u"isFrozen", JS_null); // TODO
		Define(Object, u"isSealed", JS_null); // TODO
		Define(Object, u"keys", JS_null); // TODO
		Define(Object, u"preventExtensions", JS_null); // TODO
		Define(Object, u"seal", JS_null); // TODO
	}

	if(CurrentVersion >= Version::ES6)
	{
		Define(Object, u"assign", JSNumber(0)); // TODO
		Define(Object, u"getOwnPropertySymbols", JS_null); // TODO
		Define(Object, u"getOwnPropertyKeys", JS_null); // TODO
		Define(Object, u"is", JS_null); // TODO
		Define(Object, u"setPrototypeOf",
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_setPrototypeOf_Call, 2, u"setPrototypeOf", {}, &realm));
	}

	Define(Object_prototype, u"constructor", Object, JS::PropertyAttributes().WithEnumerable(false));
	Define(Object_prototype, u"toString", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Object_prototype_toString_Call, 0, u"toString", {}, &realm), JS::PropertyAttributes().WithEnumerable(false));
	Define(Object_prototype, u"valueOf", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO

	if(CurrentVersion >= Version::ES3)
	{
		Define(Object_prototype, u"toLocaleString", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO
		Define(Object_prototype, u"hasOwnProperty", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO
		Define(Object_prototype, u"isPrototypeOf", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO
		Define(Object_prototype, u"prototypeIsEnumerable", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Define(Object_prototype, u"sealProperty", JS_null, JS::PropertyAttributes().WithEnumerable(false)); // TODO
	}
#endif

//	Function->DefineOwnProperty(u"prototype", JSProperty::MakeDynamic(Function_prototype, false, false, false) PUBLIC_NAMESPACE);

	static_cast<JS::Class::OrdinaryObject *>(Function_prototype)->Prototype = Object_prototype;

#if ENABLE_ES4
	if(!IsVersionES4())
#endif
	{
		Define(Function_prototype, u"constructor", Function);
	}
	Define(Function_prototype, u"toString", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Function_prototype_toString_Call, 0, u"toString", {}, &realm), JS::PropertyAttributes().WithEnumerable(false));

	if(CurrentVersion >= Version::ES3)
	{
		Define(Function_prototype, u"apply", JS_null); // TODO
		Define(Function_prototype, u"call", JS::CreateBuiltinFunction(JS::Runtime::Builtin::Function_prototype_call_Call, 1, u"call", {}, &realm));
	}

	if(CurrentVersion >= Version::ES3)
	{
		Define(Error_prototype, u"message", u"");
		Define(Error_prototype, u"name", u"Error");

		Define(EvalError_prototype, u"message", u"");
		Define(EvalError_prototype, u"name", u"EvalError");

		Define(RangeError_prototype, u"message", u"");
		Define(RangeError_prototype, u"name", u"RangeError");

		Define(ReferenceError_prototype, u"message", u"");
		Define(ReferenceError_prototype, u"name", u"ReferenceError");

		Define(SyntaxError_prototype, u"message", u"");
		Define(SyntaxError_prototype, u"name", u"SyntaxError");

		Define(TypeError_prototype, u"message", u"");
		Define(TypeError_prototype, u"name", u"TypeError");

		Define(URIError_prototype, u"message", u"");
		Define(URIError_prototype, u"name", u"URIError");
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Define(ArgumentError_prototype, u"message", u"");
		Define(ArgumentError_prototype, u"name", u"ArgumentError");

		Define(AttributeError_prototype, u"message", u"");
		Define(AttributeError_prototype, u"name", u"AttributeError");

		Define(ConstantError_prototype, u"message", u"");
		Define(ConstantError_prototype, u"name", u"ConstantError");

		Define(DefinitionError_prototype, u"message", u"");
		Define(DefinitionError_prototype, u"name", u"DefinitionError");

		Define(UninitializedError_prototype, u"message", u"");
		Define(UninitializedError_prototype, u"name", u"UninitializedError");
	}
#endif

	if(CurrentVersion >= Version::ES5)
	{
		Define(Function_prototype, u"bind", JS_null); // TODO
	}

	if(Supports_E4X())
	{
		Define(XML_prototype, u"toString", JS::CreateBuiltinFunction(JS::Runtime::Builtin::toString_Call, 0, u"toString", {}, &realm));
	}

	if(CurrentVersion >= Version::ES6)
	{
		Define(Function_prototype, JS::Symbols::hasInstance, JS_null); // TODO
	}
	// TODO

	Define(String_prototype, u"constructor", String);

	if(Version::ES6 <= CurrentVersion)
	{
		Define(Symbol, u"hasInstance", JS::Symbols::hasInstance);
		Define(Symbol, u"isConcatSpreadable", JS::Symbols::isConcatSpreadable);
		Define(Symbol, u"iterator", JS::Symbols::iterator);
		Define(Symbol, u"match", JS::Symbols::match);
		Define(Symbol, u"replace", JS::Symbols::replace);
		Define(Symbol, u"search", JS::Symbols::search);
		Define(Symbol, u"species", JS::Symbols::species);
		Define(Symbol, u"split", JS::Symbols::split);
		Define(Symbol, u"toPrimitive", JS::Symbols::toPrimitive);
		Define(Symbol, u"toStringTag", JS::Symbols::toStringTag);
		Define(Symbol, u"unscopables", JS::Symbols::unscopables);
	}

	if(Version::ESNEXT <= CurrentVersion)
	{
		Define(Symbol, u"asyncIterator", JS::Symbols::asyncIterator);
		Define(Symbol, u"matchAll", JS::Symbols::matchAll);

	}
	if(Version::ES6 <= CurrentVersion)
	{
		Define(IteratorPrototype, JS::Symbols::iterator, JS::CreateBuiltinFunction(JS::Runtime::Builtin::IteratorPrototype_iterator_Call, 0, u"toString", {}, &realm));

		Define(GeneratorPrototype, u"next", JS::CreateBuiltinFunction(JS::Runtime::Builtin::GeneratorPrototype_next_Call, 1, u"next", {}, &realm));
		Define(GeneratorPrototype, u"return", JS::CreateBuiltinFunction(JS::Runtime::Builtin::GeneratorPrototype_return_Call, 1, u"return", {}, &realm));
		Define(GeneratorPrototype, u"throw", JS::CreateBuiltinFunction(JS::Runtime::Builtin::GeneratorPrototype_throw_Call, 1, u"throw", {}, &realm));
		Define(GeneratorPrototype, JS::Symbols::toStringTag, u"Generator");

		Define(GeneratorFunction_prototype, JS::Symbols::toStringTag, u"GeneratorFunction");
	}

	if(Supports_E4X())
	{
		Define(XML, u"ignoreComments", true, JS::PropertyAttributes().WithEnumerable(false).WithConfigurable(false));
		Define(XML, u"ignoreProcessingInstructions", true, JS::PropertyAttributes().WithEnumerable(false).WithConfigurable(false));
		Define(XML, u"ignoreWhitespace", true, JS::PropertyAttributes().WithEnumerable(false).WithConfigurable(false));
		Define(XML, u"prettyPrinting", true, JS::PropertyAttributes().WithEnumerable(false).WithConfigurable(false));
		Define(XML, u"prettyIndent", 2.0, JS::PropertyAttributes().WithEnumerable(false).WithConfigurable(false));
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Class_class->InstanceMembers.Set(u"prototype" PUBLIC_NAMESPACES READ_ACCESS,
			JS::Runtime::InstanceFunction::Getter(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Signature = */ JS::Runtime::SignatureType{},
				/* Function = */ JS::CreateBuiltinFunction(JS::Runtime::Builtin::Class_prototype_get, 0, u"", {}, &realm)));

		Define(Class_prototype, u"constructor", Class_class, Class_class, PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(Class_prototype, u"toString", Function_class,
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::Class_prototype_toString_Call, 0, u"toString", std::optional<JSObject>{}, &realm));
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		String_class->InstanceMembers.Set(u"length" PUBLIC_NAMESPACES READ_ACCESS,
			JS::Runtime::InstanceFunction::Getter(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Signature = */ JS::Runtime::SignatureType{},
				/* Function = */ JS::CreateBuiltinFunction(JS::Runtime::Builtin::String_length_get, 0, u"", {}, &realm)));
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		Array_private = JS::New<JS::Class::Namespace>(u"private");

		auto length_variable =
			JS::Runtime::InstanceVariable::Variable(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Immutable = */ false,
				/* Type = */ Number_class,
				/* DefaultValue = */ 0.0);

		Array_class->InstanceMembers.Set(u"length", {Array_private} READ_WRITE_ACCESS,
			length_variable);

		Array_class->InstanceMembers.Set(u"length" PUBLIC_NAMESPACES READ_ACCESS,
			JS::Runtime::InstanceFunction::Getter(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Signature = */ JS::Runtime::SignatureType{},
				/* Function = */ JS::CreateBuiltinFunction(JS::Runtime::Builtin::Array_length_get, 0, u"", {}, &realm)));

		Array_class->InstanceMembers.Set(u"length" PUBLIC_NAMESPACES WRITE_ACCESS,
			JS::Runtime::InstanceFunction::Setter(
				/* Final = */ true,
				/* Enumerable = */ false,
				/* Signature = */ JS::Runtime::SignatureType{},
				/* Function = */ JS::CreateBuiltinFunction(JS::Runtime::Builtin::Array_length_set, 1, u"", {}, &realm)));

		//Array_prototype->Slots[length_variable] = 0.0;
		Array_prototype->add_slot(Array_prototype->GetClass(), u"length", {Array_private}, 0.0);
	}
#endif

	if(Version::ES6 <= CurrentVersion)
	{
		Array_prototype_values = JS::CreateBuiltinFunction(JS::Runtime::Builtin::Array_prototype_values_Call, 0, u"values", {}, &realm);
		Define(Array_prototype, u"values", Array_prototype_values, JS::PropertyAttributes().WithEnumerable(false));
		Define(Array_prototype, JS::Symbols::iterator, Array_prototype_values, JS::PropertyAttributes().WithEnumerable(false));
	}

	if(Version::ES6 <= CurrentVersion)
	{
		Define(ArrayIteratorPrototype, u"next",
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::ArrayIteratorPrototype_next_Call, 0, u"next", {}, &realm));
	}

	if(Version::ES6 <= CurrentVersion)
	{
		Define(Promise_prototype, u"then",
			JS::CreateBuiltinFunction(JS::Runtime::Builtin::Promise_prototype_then_Call, 2, u"then", {}, &realm));
	}

	if(Version::ES6 <= CurrentVersion)
	{
		Define(Symbol_prototype, Symbols::toStringTag, u"Symbol", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(Math_object, Symbols::toStringTag, u"Math", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		//Define(StringIteratorPrototype, Symbols::toStringTag, u"String Iterator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(RegExpStringIteratorPrototype, Symbols::toStringTag, u"RegExp String Iterator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		Define(ArrayIteratorPrototype, Symbols::toStringTag, u"Array Iterator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(TypedArray_prototype, Symbols::toStringTag,
			Accessor::Getter(JS::CreateBuiltinFunction(JS::Runtime::Builtin::TypedArray_prototype_toStringTag_get_Call, 0, u"get [Symbol.toStringTag]", {}, &realm)),
			PropertyAttributes{}.WithEnumerable(false));
		//Define(Map_prototype, Symbols::toStringTag, u"Map", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(MapIteratorPrototype, Symbols::toStringTag, u"Map Iterator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(Set_prototype, Symbols::toStringTag, u"Set", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(SetIteratorPrototype, Symbols::toStringTag, u"Set Iterator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(WeakMap_prototype, Symbols::toStringTag, u"WeakMap", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(WeakSet_prototype, Symbols::toStringTag, u"WeakSet", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(ArrayBuffer_prototype, Symbols::toStringTag, u"ArrayBuffer", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(SharedArrayBuffer_prototype, Symbols::toStringTag, u"SharedArrayBuffer", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(DataView_prototype, Symbols::toStringTag, u"DataView", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(Atomics_Object, Symbols::toStringTag, u"Atomics", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		Define(JSON_object, Symbols::toStringTag, u"JSON", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		//Define(WeakRef_prototype, Symbols::toStringTag, u"WeakRef", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(FinalizationRegistry_prototype, Symbols::toStringTag, u"FinalizationRegistry", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(IteratorHelperPrototype, Symbols::toStringTag, u"Iterator Helper", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(Iterator_prototype, Symbols::toStringTag, get: "Iterator", set: ??? // TODO
		//Define(Promise_prototype, Symbols::toStringTag, u"Promise", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		Define(GeneratorFunction_prototype, Symbols::toStringTag, u"GeneratorFunction", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(GeneratorPrototype, Symbols::toStringTag, u"Generator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		//Define(Reflect, Symbols::toStringTag, u"Reflect", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
		//Define(???module namespace object, Symbols::toStringTag, u"Module", PropertyAttributes{}.WithWritable(false).WithEnumerable(false)); // TODO
	}

	if(Version::ESNEXT <= CurrentVersion)
	{
		Define(BigInt_prototype, Symbols::toStringTag, u"BigInt", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(AsyncGeneratorFunction_prototype, Symbols::toStringTag, u"AsyncGeneratorFunction", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(AsyncGeneratorPrototype, Symbols::toStringTag, u"AsyncGenerator", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
		Define(AsyncFunction_prototype, Symbols::toStringTag, u"AsyncFunction", PropertyAttributes{}.WithWritable(false).WithEnumerable(false));
	}
}
#endif

//// Realm

std::shared_ptr<JS::Runtime::GlobalEnvironmentRecord> JS::Runtime::Realm::InitializeGlobalObject(JSObject GlobalObject, JSObject global_this)
{
	std::shared_ptr<JS::Runtime::GlobalEnvironmentRecord> global_env = std::make_shared<JS::Runtime::GlobalEnvironmentRecord>(GlobalObject, global_this ? global_this : GlobalObject);
	if(Supports_E4X())
	{
		global_env->DefaultNamespace = JS::New<JS::Class::Namespace>();
	}

	Intrinsics.InitializeGlobalObject(*this, GlobalObject);

	return global_env;
}

JS::Runtime::Realm::Realm(Agent * AgentSignifier, JSObject global_object, JSObject global_this)
	: AgentSignifier(AgentSignifier)
{
	JS::Runtime::ExecutionContext * context = new JS::Runtime::ExecutionContext;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(context); // must start here, since Intrinsics::Initialize accesses the current context
	context->Realm = this; // must make this available as well
	Intrinsics.Initialize(*this);
	// TODO: AgentSignifier = new AgentSignifier (ESNext)
	// TODO: TemplateMap = {}
	context->Function = nullptr;
	// TODO: context->ScriptOrModule = nullptr
	if(global_object != nullptr)
	{
		GlobalObject = global_object;
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		GlobalObject = JS::New<JS::Class::Package>();
	}
#endif
	else
	{
		GlobalObject = JS::New<JS::Class::Singleton>(u"global");
	}
	JS::Runtime::Debugger::SetObjectLabel(GlobalObject, "global");

	context->LexicalEnvironment = context->VariableEnvironment = GlobalEnv = InitializeGlobalObject(GlobalObject, global_this);
}

