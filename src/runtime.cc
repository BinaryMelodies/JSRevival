
#include <bit>
#include <cmath>
#include <endian.h>
#include <fstream>

#include "common.hh"
#include "macros.hh"
#include "core.hh"
#include "runtime.hh"
#include "execute.hh"
#include "environment.hh"
#include "ast.hh"
#include "parser.hh"
#include "builtins.hh"
#include "modules.hh" // TODO: move?
#if 0
#include "endian.hh"
#endif

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

JS::Runtime::Host * JS::Host = nullptr;

JS::Version JS::SelectedVersion = JS::Version::ESNEXT;
JS::Version JS::CurrentVersion = JS::Version::ESNEXT;
bool JS::StrictMode = false;
bool JS::Enable_E4X = false;
bool JS::Enable_CommonJS = false;
bool JS::Enable_AMD = false;
bool JS::Enable_WebExtensions = false;

std::string JS::VersionNames[] =
{
	/* [JS1_0] = */ "js1.0(1995)",
	/* [JS1_1] = */ "js1.1(1996)",
	/* [ES1] = */ "es1(1997)",
	/* [ES2] = */ "es2(1998)",
	/* [ES3] = */ "es3(1999)",
#if ENABLE_ES4
	/* [ES4_2003] = */ "es4(2003)",
	/* [ES4_2008] = */ "es4(2008)",
#endif
	/* [ES5] = */ "es5(2009)",
	/* [ES5.1] = */ "es5.1(2011)",
	/* [ES6] = */ "es6(2015)",
	/* [ES7] = */ "es7(2016)",
	/* [ES8] = */ "es8(2017)",
	/* [ES9] = */ "es9(2018)",
	/* [ES10] = */ "es10(2019)",
	/* [ES11] = */ "es11(2020)",
	/* [ES12] = */ "es12(2021)",
	/* [ES13] = */ "es13(2022)",
	/* [ES14] = */ "es14(2023)",
	/* [ES15] = */ "es15(2024)",
	/* [ES16] = */ "es16(2025)",
	/* [ESNEXT] = */ "esnext(now)",
};

static inline bool is_valid_identifier(JSString string)
{
	if(!JS::Parser::Tokenizer::IsIdentifierStart(string[0]))
	{
		return false;
	}
	for(auto c : string)
	{
		if(!JS::Parser::Tokenizer::IsIdentifierPart(c))
		{
			return false;
		}
	}
	return true;
}

static inline void output_string(std::ostream& out, JSString string)
{
	out << "\"";
	std::ostringstream oss;
	JS::PrintString(oss, string);
	for(auto c : oss.str())
	{
		if(c == '\\' || c == '"')
		{
			out.put('\\');
			out.put(c);
		}
		else
		{
			out.put(c);
		}
	}
	out << "\"";
}

//// Initialization

#if ENABLE_ES4
JS::Runtime::Initialization * JS::Runtime::Initialization::Busy = new JS::Runtime::BusyInitializer();
#endif

//// BusyInitializer

#if ENABLE_ES4
JSValue JS::Runtime::BusyInitializer::Initialize([[maybe_unused]] JS::Runtime::ExecutionPhase phase)
{
	JS::Throw(JS::New<ReferenceError> __LOC(u"recursive constant definition"));
}
#endif

//// Property

#if ENABLE_ES4
void JS::Runtime::Property::Initialize(JS::Runtime::ExecutionPhase phase)
{
	if(Initializer != nullptr)
	{
		Initialization * initializer = Initializer;
		Initializer = Initialization::Busy;

		auto type = Type.value();
		try
		{
			auto result = type.Coerce(initializer->Initialize(phase));
			if(!result.has_value())
			{
				JS::Throw(JS::New<TypeError> __LOC(u"coercion failed"));
			}
			Value = result.value();
			Initializer = nullptr;
		}
		catch(...)
		{
			Initializer = initializer;
			throw;
		}
	}
}
#endif

#if ENABLE_ES4
JSProperty JS::Runtime::Property::MakeUninitializedDynamic(bool Writable, bool Enumerable, bool Configurable)
{
	Property property;
	property.PropertyType = Type::Dynamic;
	property.Enumerable = Enumerable;
	property.Configurable = Configurable;
	// property.Value not yet initialized
	property.Mutable = Writable ? JSProperty::Mutability::Mutable : JSProperty::Mutability::Strict_Immutable;
	return property;
}
#endif

JSProperty JS::Runtime::Property::MakeData(JSValue Value, bool Writable, bool Enumerable, bool Configurable AND_FLAG(Explicit))
{
	Property property;
	property.PropertyType = Type::Data;
	property.Enumerable = Enumerable;
	property.Configurable = Configurable;
	property.Value = Value;
#if ENABLE_ES4
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Immutable;
#else
	property.Writable = Writable;
#endif
#if ENABLE_ES4
	property.Explicit = Explicit;
#endif
	return property;
}

JSProperty JS::Runtime::Property::MakeData(JSValue Value, bool Writable, bool Configurable)
{
	Property property;
	property.PropertyType = Type::Data;
	property.Enumerable = true;
	property.Configurable = Configurable;
	property.Value = Value;
#if ENABLE_ES4
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Immutable;
#else
	property.Writable = Writable;
#endif
	return property;
}

JSProperty JS::Runtime::Property::MakeInternal(Internal Value, bool Writable, bool Enumerable)
{
	Property property;
	property.PropertyType = Type::Data;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Value = Value;
#if ENABLE_ES4
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Immutable;
#else
	property.Writable = Writable;
#endif
	return property;
}

/*JSProperty JS::Runtime::Property::MakeInternal(JSValue& Value, JS::Runtime::Property::Mutability Writable, bool Enumerable)
{
	Property property;
	property.PropertyType = Type::Data;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Value = new InternalValue(&Value);
	property.Mutable = Writable;
	return property;
}*/

#if ENABLE_ES4
JSProperty JS::Runtime::Property::MakeVariable(std::optional<JSValue> value, bool Writable, bool Enumerable, bool Explicit, std::optional<JSDeclaredType> type)
{
	Property property;
	property.PropertyType = Type::Variable;
	property.Type = type;
	if(value.has_value())
		property.Value = value.value();
	else
		property.Value = Uninitialized();
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Strict_Immutable;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Explicit = Explicit;
	return property;
}

JSProperty JS::Runtime::Property::MakeVariable(Initialization * initializer, bool Writable, bool Enumerable, bool Explicit, std::optional<JSDeclaredType> type)
{
	Property property;
	property.PropertyType = Type::Variable;
	property.Type = type;
	property.Value = Uninitialized();
	property.Initializer = initializer;
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Strict_Immutable;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Explicit = Explicit;
	return property;
}

JSProperty JS::Runtime::Property::MakeGetter(JSFunction Get, bool Enumerable, bool Explicit)
{
	Property property;
	property.PropertyType = Type::Getter;
	property.Get = Get;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Explicit = Explicit;
	return property;
}

JSProperty JS::Runtime::Property::MakeSetter(JSFunction Set, bool Enumerable, bool Explicit)
{
	Property property;
	property.PropertyType = Type::Setter;
	property.Set = Set;
	property.Enumerable = Enumerable;
	property.Configurable = false;
	property.Explicit = Explicit;
	return property;
}

/*JSProperty JS::Runtime::Property::MakeAlias(std::shared_ptr<JSProperty> reference, bool Writable, bool Enumerable, bool Configurable, bool Explicit)
{
	Property property;
	property.PropertyType = Type::Alias;
	property.alias_reference = reference;
	property.Mutable = Writable ? Mutability::Mutable : Mutability::Strict_Immutable;
	property.Enumerable = Enumerable;
	property.Configurable = Configurable;
	property.Explicit = Explicit;
	return property;
}*/

JSProperty JS::Runtime::Property::MakeForbidden()
{
	Property property;
	property.PropertyType = Type::Forbidden;
	property.Enumerable = true;
	property.Configurable = false;
	property.Explicit = true;
	return property;
}
#endif

JSProperty JS::Runtime::Property::MakeAccessor(JSFunction Get, JSFunction Set, bool Enumerable, bool Configurable)
{
	Property property;
	property.PropertyType = Type::Accessor;
	property.Enumerable = Enumerable;
	property.Configurable = Configurable;
	property.Get = Get;
	property.Set = Set;
	return property;
}

void JS::Runtime::Property::ConvertToData()
{
#if ENABLE_ES4
	if(PropertyType == Type::Variable || PropertyType == Type::Getter || PropertyType == Type::Setter)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal property conversion"));
	}
#endif
	if(PropertyType != Type::Data)
	{
		Get = {};
		Set = {};
	}
}

void JS::Runtime::Property::ConvertToAccessor()
{
#if ENABLE_ES4
	if(PropertyType == Type::Variable || PropertyType == Type::Getter || PropertyType == Type::Setter)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal property conversion"));
	}
#endif
	if(PropertyType != Type::Accessor)
	{
		Value = Uninitialized();
#if ENABLE_ES4
		Mutable = Mutability::Mutable;
#else
		Writable = true;
#endif
	}
}

bool JS::Runtime::Property::IsWritable() const
{
	switch(PropertyType)
	{
	case Type::Data:
#if ENABLE_ES4
	case Type::Variable:
#endif
	default:
#if ENABLE_ES4
		return Mutable == Mutability::Mutable;
#else
		return Writable;
#endif
	case Type::Accessor:
		return Set != nullptr;
#if ENABLE_ES4
	case Type::Getter:
		return false;
	case Type::Setter:
		return true;
#endif
	}
}

void JS::Runtime::Property::SetWritable(bool Writable)
{
#if ENABLE_ES4
	switch(PropertyType)
	{
	case Type::Data:
	case Type::Variable:
	default:
		Mutable = Writable ? Mutability::Mutable : Mutability::Immutable;
		break;
	case Type::Getter:
	case Type::Setter:
	case Type::Accessor:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal write to property"));
	}
#else
	switch(PropertyType)
	{
	case Type::Data:
	default:
		Writable = Writable;
		break;
	case Type::Accessor:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal write to property"));
	}
#endif
}

bool JS::Runtime::Property::IsInitialized() const
{
#if ENABLE_ES4
	switch(PropertyType)
	{
	case Type::Dynamic:
	case Type::Variable:
		return !std::get_if<Uninitialized>(&Value);
	default:
		return true;
	}
#else
	return true;
#endif
}

JSValue JS::Runtime::Property::GetValue() const
{
	return std::visit([](auto&& value) -> JSValue
	{
		using T = std::decay_t<decltype(value)>;
		if constexpr(std::is_same_v<T, Uninitialized>)
		{
			JS::Throw(JS::New<InternalError> __LOC(u"reading from uninitialized property"));
		}
		else if constexpr(std::is_same_v<T, Initialized>)
		{
			return value;
		}
		else if constexpr(std::is_same_v<T, Internal>)
		{
			auto result = value->Get();
			if(result.has_value())
			{
				return result.value();
			}
			else
			{
				JS::Throw(JS::New<InternalError> __LOC(u"invalid read from internal property"));
			}
		}
		else
		{
			static_assert(false);
		}
	}, Value);
}

bool JS::Runtime::Property::SetValue(JSValue value)
{
	return std::visit([this, value](auto&& content)
	{
		using T = std::decay_t<decltype(content)>;
		if constexpr(std::is_same_v<T, Internal>)
		{
			return content->Set(value);
		}
		else
		{
#if ENABLE_ES4
			if(PropertyType == Type::Variable)
			{
				auto result = Type.value().Coerce(value);
				if(!result.has_value())
				{
					return false;
				}
				Value = result.value();
			}
			else
#endif
			{
				Value = value;
			}
			return true;
		}
	}, Value);
}

#if 0
#if ENABLE_ES4
JS::Runtime::Property JS::Runtime::Property::ResolveProperty()
{
	if(PropertyType == Type::Alias)
	{
		Property property = ResolvePropertyAccess();
		property.Enumerable = Enumerable;
		if(IsVersionES4())
		{
			property.Explicit = Explicit;
		}
		else
		{
			property.Mutable = Mutable;
			property.Configurable = Configurable;
		}
		return property;
	}
	else
	{
		return *this;
	}
}

JS::Runtime::Property& JS::Runtime::Property::ResolvePropertyAccess()
{
	if(PropertyType == Type::Alias)
	{
		Property * property = alias_reference.get();
		while(property->PropertyType == Type::Alias)
		{
			alias_reference = property->alias_reference;
			property = alias_reference.get();
		}
		return *property;
	}
	else
	{
		return *this;
	}
}
#endif
#endif

//// Binding
#if !ENABLE_ES4
JS::Runtime::Binding JS::Runtime::Binding::MakeMutable(std::optional<JSValue> value, bool Deletable)
{
	Binding binding;
	binding.type = Deletable ? BindingType::Deletable : BindingType::Mutable;
	binding.value = value;
	return binding;
}

JS::Runtime::Binding JS::Runtime::Binding::MakeImmutable(std::optional<JSValue> value, bool Strict)
{
	Binding binding;
	binding.type = Strict ? BindingType::Strict : BindingType::Immutable;
	binding.value = value;
	return binding;
}
#endif

//// PropertyDescriptor

JS::Runtime::PropertyDescriptor::PropertyDescriptor(const JSProperty& property)
{
	switch(property.PropertyType)
	{
	case Property::Type::Data:
		Value = property.GetValue();
		Writable = property.IsWritable();
		break;
	case Property::Type::Accessor:
		Get = property.Get;
		Set = property.Set;
		break;
#if ENABLE_ES4
	case Property::Type::Variable:
	case Property::Type::Getter:
	case Property::Type::Setter:
#endif
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal property descriptor type"));
	}
	Enumerable = property.Enumerable;
	Configurable = property.Configurable;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withoutValue()
{
	PropertyDescriptor descriptor = *this;
	descriptor.Value = {};
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withValue(JSValue value)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Value = std::make_optional<>(value);
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withWritable(bool writable)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Writable = std::make_optional<>(writable);
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withEnumerable(bool enumerable)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Enumerable = std::make_optional<>(enumerable);
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withoutEnumerable()
{
	PropertyDescriptor descriptor = *this;
	descriptor.Enumerable = {};
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withConfigurable(bool configurable)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Configurable = std::make_optional<>(configurable);
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withoutConfigurable()
{
	PropertyDescriptor descriptor = *this;
	descriptor.Configurable = {};
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withGetter(JSFunction get)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Get = std::make_optional<>(get);
	return descriptor;
}

JSPropertyDescriptor JS::Runtime::PropertyDescriptor::withSetter(JSFunction set)
{
	PropertyDescriptor descriptor = *this;
	descriptor.Set = std::make_optional<>(set);
	return descriptor;
}

bool JS::Runtime::PropertyDescriptor::is_accessor_descriptor()
{
	return Get || Set;
}

bool JS::Runtime::PropertyDescriptor::is_data_descriptor()
{
	return Value || Writable;
}

JS::Runtime::PropertyDescriptor::operator Property()
{
	if(is_accessor_descriptor())
	{
		return Property::MakeAccessor(
			Get ? Get.value() : nullptr,
			Set ? Set.value() : nullptr,
			Enumerable && Enumerable.value(),
			Configurable && Configurable.value());
	}
	else
	{
		return Property::MakeData(
			Value ? Value.value() : JS_undefined,
			/* Writable = */ Writable && Writable.value(),
			Enumerable && Enumerable.value(),
			Configurable && Configurable.value());
	}
}

bool JS::Runtime::PropertyDescriptor::is_compatible(JSProperty property)
{
	auto& descriptor = *this;
	bool configurable = property.Configurable;

	if(descriptor.Configurable)
	{
		if(!configurable)
		{
			if(descriptor.Configurable.value())
			{
				return false;
			}
		}
	}

	if(descriptor.Enumerable)
	{
		if(!configurable)
		{
			if(property.Enumerable != descriptor.Enumerable.value())
			{
				return false;
			}
		}
	}

	if(descriptor.is_data_descriptor())
	{
		if(property.PropertyType != JSProperty::Type::Data)
		{
			if(!configurable)
			{
				return false;
			}
		}
		else
		{
			bool writable = property.IsWritable();

			if(descriptor.Value)
			{
				if(!configurable)
				{
					if(!writable)
					{
						if(!SameValue(property.GetValue(), descriptor.Value.value()))
						{
							return false;
						}
					}
				}
			}

			if(descriptor.Writable)
			{
				if(!configurable && !writable)
				{
					return false;
				}
			}
		}
	}
	else if(descriptor.is_accessor_descriptor())
	{
		if(property.PropertyType != JSProperty::Type::Accessor)
		{
			if(!configurable)
			{
				return false;
			}
		}
		else
		{
			if(descriptor.Get)
			{
				if(!configurable)
				{
					if(!SameValue(property.Get, descriptor.Get.value()))
					{
						return false;
					}
				}
			}

			if(descriptor.Set)
			{
				if(!configurable)
				{
					if(!SameValue(property.Set, descriptor.Set.value()))
					{
						return false;
					}
				}
			}
		}
	}

	return true;
}

//// PrivateElement

#if ENABLE_ES4
//// InstanceProperty

//// InstanceVariable

JSValue JS::Runtime::InstanceVariable::GetValue(JSValue this_argument, ExecutionPhase phase)
{
	auto value = this_argument.get<JSObject>()->FindSlot(this).value()->second;
	if(!value.has_value())
	{
		switch(phase)
		{
		case ExecutionPhase::Compile:
			JS::Throw(JS::New<ConstantError> __LOC(u"reading uninitialized constant variable"));
		case ExecutionPhase::Run:
			JS::Throw(JS::New<UninitializedError> __LOC(u"reading uninitialized constant variable"));
		}
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal phase while reading uninitialized constant"));
	}
	return value.value();
}

void JS::Runtime::InstanceVariable::SetValue(JSValue this_argument, JSValue value)
{
	auto new_value = Type.value().Coerce(value);
	if(!new_value.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC(u"coercion failed"));
	}

	auto& reference = this_argument.get<JSObject>()->FindSlot(this).value()->second;
	if(Immutable && reference.has_value())
	{
		JS::Throw(JS::New<ReferenceError> __LOC(u"cannot initialize const instance variable twice"));
	}
	reference = std::make_optional(new_value.value());
}

void JS::Runtime::InstanceVariable::InitValue(JSValue this_argument, JSValue value)
{
	auto new_value = Type.value().Coerce(value);
	if(!new_value.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC(u"coercion failed"));
	}

	std::dynamic_pointer_cast<JS::Class::OrdinaryObject>(this_argument.get<JSObject>())->Slots[this] = std::make_optional(new_value.value());
}

JS::AccessSet JS::Runtime::InstanceVariable::GetAccessSet()
{
	return AccessSet::ReadWrite;
}

//// InstanceFunction

JSValue JS::Runtime::InstanceFunction::GetValue(JSValue this_argument, ExecutionPhase phase)
{
	switch(InstanceType)
	{
	case Type::Method:
		return JSValue(JSMethodClosure(this_argument, this));
	case Type::Getter:
		return Function.Call(this_argument, {}, phase);
	case Type::Setter:
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal instance property type for reading"));
	}
}

void JS::Runtime::InstanceFunction::SetValue(JSValue this_argument, JSValue value)
{
	switch(InstanceType)
	{
	case Type::Method:
		JS::Throw(JS::New<ReferenceError> __LOC(u"cannot write to an instance method"));
	case Type::Getter:
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal instance property type for writing"));
	case Type::Setter:
		Function.Call(this_argument, {value}, JS::Runtime::ExecutionPhase::Run);
		break;
	}
}

JS::AccessSet JS::Runtime::InstanceFunction::GetAccessSet()
{
	switch(InstanceType)
	{
	case Type::Method:
		return AccessSet::ReadWrite;
	case Type::Getter:
		return AccessSet::Read;
	case Type::Setter:
		return AccessSet::Write;
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal instance property type"));
	}
}
#endif

//// Agent

JS::Runtime::Agent::Agent()
#if 0
	: LittleEndian(JS::GetNativeEndianness() == JS::Endian::LittleEndian)
#endif
	: LittleEndian(std::endian::native == std::endian::little)
{
}

std::vector<JS::Parser::Location> JS::Runtime::Agent::GetCurrentLocationStack()
{
	std::vector<JS::Parser::Location> locationStack;
	for(auto context : ContextStack)
	{
		if(context != nullptr && context->location.has_value())
		{
			locationStack.push_back(context->location.value());
		}
#if 0
		else
		{
			locationStack.push_back(
				JS::Parser::Location{std::make_optional(std::filesystem::path("<unknown>")), 0, 0}
			);
		}
#endif
	}
	return locationStack;
}

//// PackageImportRecord

#if ENABLE_ES4
JS::Runtime::PackageImportRecord::PackageImportRecord(PackageRecord * ScriptOrModule)
	: ScriptOrModule(ScriptOrModule), Package(ScriptOrModule == nullptr ? nullptr : ScriptOrModule->Package)
{
}
#endif

//// AsynchronousModuleDefinition
JS::Runtime::AsynchronousModuleDefinition JS::Runtime::AsynchronousModuleDefinition::Create(JSValue Identifier, JSValue Dependencies, JSValue Factory)
{
	AsynchronousModuleDefinition definition;
	std::optional<std::filesystem::path> filename;

	auto scriptOrModule = JS::GetActiveScriptOrModule();
	if(scriptOrModule)
	{
		definition.Script = dynamic_cast<ScriptRecord *>(scriptOrModule);
		filename = scriptOrModule->sourcefilename;
	}

	if(IsNull(Identifier))
	{
		if(filename.has_value())
		{
			definition.Identifier = filename.value().replace_extension().u16string();
		}
		else
		{
			//JS::Throw(JS::New<Error> __LOC("no identifier or file name for asynchronous module define"));
			definition.Identifier = u".";
		}
	}
	else
	{
		if(auto identifier = GetString(Identifier))
		{
			definition.Identifier = identifier.value();
		}
		else
		{
			JS::Throw(JS::New<Error> __LOC("identifier for asynchronous module define must be string"));
		}
	}

	if(!IsNull(Dependencies))
	{
		uint32_t length = JS::ToUint32(JS::Get(Dependencies, u"length") RUNTIME_PHASE);
		definition.Dependencies = std::vector<JSString>();
		for(uint32_t index = 0; index < length; index ++)
		{
			auto value = JS::Get(Dependencies, JS::ToString(JSNumber(index) RUNTIME_PHASE));
			if(auto dependency = GetString(value))
			{
				definition.Dependencies.value().push_back(dependency.value());
			}
			else
			{
				JS::Throw(JS::New<Error> __LOC("dependency for asynchronous module define must be string"));
			}
		}
	}

	definition.Factory = Factory.cast_object<JS::Class::Object>();

	return definition;
}

//// Host

JS::Runtime::JobCallback * JS::Runtime::Host::MakeJobCallback(JSFunction callback)
{
	return new (GC) JobCallback{callback};
}
JSValue JS::Runtime::Host::CallJobCallback(JobCallback * jobCallback, JSValue value, std::vector<JSValue> arguments)
{
	return JS::Call(jobCallback->Callback, value, arguments RUNTIME_PHASE);
}

#if 0
void JS::Runtime::Host::EnqueueGenericJob(Job * job, JS::Runtime::Realm * realm)
{
	// TODO
}
#endif

void JS::Runtime::Host::EnqueuePromiseJob(Job * job, [[maybe_unused]] /* TODO */ JS::Runtime::Realm * realm)
{
	GetSurroundingAgent()->EnqueuedJobs.push(job); // TODO: also realm
}

void JS::Runtime::Host::PromiseRejectionTracker([[maybe_unused]] gcn::gc_ptr<JS::Class::Promise> promise, [[maybe_unused]] OperationType operation)
{
}

void JS::Runtime::Host::SetupWrappers(std::shared_ptr<JS::Runtime::EnvironmentRecord> using_environment)
{
	if(!JS::Enable_CommonJS && !JS::Enable_AMD)
	{
		return;
	}

	auto context = GetCurrentExecutionContext();
	auto script = dynamic_cast<JS::Runtime::ScriptRecord *>(context->ScriptOrModule);

	JSObject exports;

	if(JS::Enable_CommonJS)
	{
		exports = JS::New<JS::Class::OrdinaryObject>();
	}

	if(using_environment == nullptr)
	{
		auto environment =
			std::make_shared<JS::Runtime::FunctionEnvironmentRecord>(
				nullptr,
				JS::Runtime::FunctionEnvironmentRecord::BindingStatus::Uninitialized,
				std::optional<JSFunction>{},
				context->LexicalEnvironment);
		context->VariableEnvironment = context->LexicalEnvironment = environment;

		if(JS::Enable_CommonJS)
		{
			environment->BindThisValue(exports);
		}

		using_environment = environment;
	}

	if(JS::Enable_AMD)
	{
		using_environment->CreateHoistedBinding(u"define", JS::Intrinsics()->define);
	}

	if(JS::Enable_CommonJS)
	{
		JSValue module;

		if(script != nullptr)
		{
			module = script->CommonJSModule;
		}
		else
		{
			// running from interactive shell, create the module object on the fly
			module = JS::New<JS::Class::OrdinaryObject>();
		}

		JS::Define(module, u"exports", exports);
		if(script != nullptr && script->sourcefilename.has_value())
		{
			JS::Define(module, u"id", std::filesystem::weakly_canonical(std::filesystem::absolute(script->sourcefilename.value())).u16string());
		}
		else
		{
			JS::Define(module, u"id", u"<repl>");
		}
		JS::Define(module, u"loaded", false);

		using_environment->CreateHoistedBinding(u"exports", exports);
		using_environment->CreateHoistedBinding(u"module", module);
		using_environment->CreateHoistedBinding(u"require", JS::Intrinsics()->require);
	}
}

void JS::Runtime::Host::FinishLoadingImportedModule_NormalCompletion(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload, ModuleRecord * module_result)
{
	auto record = referrer_loaded_modules->find(moduleRequest);
	if(record != referrer_loaded_modules->end())
	{
		if(record->Module != module_result)
		{
			JS::Throw(JS::New<InternalError> __LOC("unexpected circumstance: same module loaded via different module records"));
		}
	}
	else
	{
		referrer_loaded_modules->insert(LoadedModuleRequestRecord(moduleRequest, module_result));
	}
	payload->NormalCompletion(module_result);
}

void JS::Runtime::Host::FinishLoadingImportedModule_ThrowCompletion([[maybe_unused]] std::set<LoadedModuleRequestRecord> * _referrer_loaded_modules, [[maybe_unused]] ModuleRequestRecord _moduleRequest, std::shared_ptr<Payload> payload, JSValue error_result)
{
	payload->ThrowCompletion(error_result);
}

JSValue JS::Runtime::Host::LoadJSONFile(std::filesystem::path host_path)
{
	JS::Parser::JSON::Parser jsonParser(LoadTextFile(host_path));
	return jsonParser.ParseJSON();
}

JSString JS::Runtime::Host::LoadTextFile(std::filesystem::path host_path)
{
	std::ifstream input(host_path);
	JSString string;

	// TODO: duplicate of JS::Parser::UTF8StreamTokenizer::NextChar

	char32_t result;
	int c = input.get();

	while(c != EOF)
	{
		if((c & 0x80) == 0)
		{
			string += char32_t(c);
		}
		else if((c & 0xE0) == 0xC0)
		{
			result = (c & 0x1F) << 6;

			c = input.get();
			if(c == EOF)
			{
				string += result;
				break;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= c & 0x3F;

			string += result;
		}
		else if((c & 0xF0) == 0xE0)
		{
			result = (c & 0x0F) << 12;

			c = input.get();
			if(c == EOF)
			{
				string += result;
				break;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= (c & 0x3F) << 6;

			c = input.get();
			if(c == EOF)
			{
				string += result;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= c & 0x3F;

			string += result;
		}
		else if((c & 0xF8) == 0xF0)
		{
			result = (char32_t)(c & 0x07) << 18;

			c = input.get();
			if(c == EOF)
			{
				string += result;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= (c & 0x3F) << 12;

			c = input.get();
			if(c == EOF)
			{
				string += result;
				break;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= (c & 0x3F) << 6;

			c = input.get();
			if(c == EOF)
			{
				string += result;
				break;
			}
			else if((c & 0xC0) != 0x80)
			{
				// invalid
				continue;
			}
			result |= c & 0x3F;

			string += result;
		}
		else
		{
			// invalid
			continue;
		}
		c = input.get();
	}

	return string;
}

//// CallContextCapture

JS::Runtime::CallContextCapture::CallContextCapture()
	:
		callerContext(GetCurrentExecutionContext()),
		previous_strict_mode(JS::StrictMode),
		previous_version(JS::CurrentVersion),
		LexicalEnvironment(callerContext ? callerContext->LexicalEnvironment : nullptr),
		VariableEnvironment(callerContext ? callerContext->VariableEnvironment : nullptr),
		PrivateEnvironment(callerContext ? callerContext->PrivateEnvironment : nullptr)
{
}

void JS::Runtime::CallContextCapture::release()
{
	if(callerContext)
	{
		while(JS::Runtime::ExecutionContext::GetContextStack().size() > 0
			&& JS::Runtime::ExecutionContext::GetContextStack().back() != callerContext)
		{
			JS::Runtime::ExecutionContext::GetContextStack().pop_back();
		}
		callerContext = nullptr;
		if(JS::Runtime::ExecutionContext::GetContextStack().size() > 0)
		{
			JS::Runtime::ExecutionContext::GetContextStack().back()->LexicalEnvironment = LexicalEnvironment;
			JS::Runtime::ExecutionContext::GetContextStack().back()->VariableEnvironment = VariableEnvironment;
			JS::Runtime::ExecutionContext::GetContextStack().back()->PrivateEnvironment = PrivateEnvironment;
		}
		StrictMode = previous_strict_mode;
		CurrentVersion = previous_version;
	}
}

//// SymbolInstance

std::map<JSString, std::vector<JSSymbol>> JS::Runtime::SymbolInstance::allocated_symbols;

void JS::Runtime::SymbolInstance::RegisterSymbol(JSSymbol symbol)
{
	auto symbol_description = symbol->Description.value_or(u"");
	auto allocated_symbols_iter = allocated_symbols.find(symbol_description);
	if(allocated_symbols_iter ==  allocated_symbols.end())
	{
		allocated_symbols[symbol_description] = {};
	}
	auto& allocated_symbol_vector = allocated_symbols[symbol_description];
	auto symbol_iter = std::find(allocated_symbol_vector.begin(), allocated_symbol_vector.end(), symbol);
	if(symbol_iter == allocated_symbol_vector.end())
	{
		allocated_symbol_vector.push_back(symbol);
	}
}

//// Bindings

//

bool JS::is_array_index(JSString string, uint32_t& result)
{
	uint32_t value = JSValue(string).ToUint32(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run/*TODO*/));
	if(ToString(JSNumber(value) RUNTIME_PHASE/*TODO*/) != string || value == 0xFFFFFFFF)
		return false;
	result = value;
	return true;
}


#if ENABLE_ES4
std::optional<JS::Runtime::SignatureType> JS::GetSignature(JSFunction callable)
{
	return callable.GetSignature();
}
#endif

JSString JS::EscapeElementValue(JSString string)
{
	JSString result;
	for(auto c : string)
	{
		switch(c)
		{
		case u'<':
			result += u"&lt;";
			break;
		case u'>':
			result += u"&gt;";
			break;
		case u'&':
			result += u"&amp;";
			break;
		default:
			result += c;
			break;
		}
	}
	return result;
}

JSString JS::EscapeAttributeValue(JSString string)
{
	JSString result;
	for(auto c : string)
	{
		switch(c)
		{
		case u'"':
			result += u"&quot;";
			break;
		case u'<':
			result += u"&lt;";
			break;
		case u'&':
			result += u"&amp;";
			break;
		case u'\u0009':
			result += u"&#x9;";
			break;
		case u'\u000A':
			result += u"&#xA;";
			break;
		case u'\u000D':
			result += u"&#xD;";
			break;
		default:
			result += c;
			break;
		}
	}
	return result;
}

// inspect

void inspect<JSBoolean>::inspect_type(std::ostream& out)
{
	out << "boolean";
}

void inspect<JSBoolean>::inspect_value(std::ostream& out, const JSBoolean& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << (value ? "true" : "false") << "\33[m";
}

void inspect<JSNumber>::inspect_type(std::ostream& out)
{
#if ENABLE_ES4
	if(JS::IsVersionES4())
	{
		out << "float64";
		return;
	}
#endif
	out << "number";
}

void inspect<JSNumber>::inspect_value(std::ostream& out, const JSNumber& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << value << "\33[m";
}

void inspect<JSString>::inspect_type(std::ostream& out)
{
	out << "string";
}

void inspect<JSString>::inspect_value(std::ostream& out, const JSString& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[92m";
	output_string(out, value);
	out << "\33[m";
}

void inspect<JSObject>::inspect_type(std::ostream& out)
{
	out << "object";
}

#if ENABLE_ES4
static bool inspect_instance_variables(std::ostream& out, JSObject value, JSClass limit, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	gcn::gc_ptr<JS::Class::OrdinaryObject> object = std::dynamic_pointer_cast<JS::Class::OrdinaryObject>(value);
	bool started = false;
	if(!limit || !object)
	{
		return started;
	}

	if(limit->Super.has_value())
	{
		started = inspect_instance_variables(out, value, JS::GetClassForType(limit->Super.value()), depth, visited, access);
	}

	for(auto property_pair : limit->InstanceMembers.bindings)
	{
		for(auto& property_entry : property_pair.second)
		{
			if(auto instance_variable = dynamic_cast<JSInstanceVariable>(*property_entry.value))
			{
				if(started)
				{
					out << ", ";
				}
				else
				{
					started = true;
				}

				JS::PrintString(out, limit->Name);
				out << ".";
				// TODO: namespace
				JS::PrintString(out, std::get<JSString>(property_pair.first));
				out << ": ";

				if(object->Slots[instance_variable].has_value())
				{
					inspect<JSValue>::inspect_value(out, object->Slots[instance_variable].value(), depth, visited, access);
				}
			}
		}
	}
	return started;
}
#endif

void inspect<JSObject>::inspect_value(std::ostream& out, const JSObject& value, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	// __proto__.constructor.name [__proto__[Symbol.toStringTag]]
	// [Number: <>]
	// [String: <>]
	// Boolean, Symbol, BigInt
	// arrays

	// TODO: Namespace, XML/XMLList, external?
	auto iter = visited.find(value);
	if(iter != visited.end())
	{
		out << "\33[96m[Circular " << iter->second << "]\33[m";
		return;
	}

	if(auto array = std::dynamic_pointer_cast<JS::Class::Array>(value))
	{
		if(depth == 0)
		{
			out << "[...]";
		}
		else
		{
			out << "[ ";
			bool started = false;
			for(size_t index = 0; index < array->elements.size(); index ++)
			{
				if(started)
				{
					out << ", ";
				}
				else
				{
					started = true;
				}

				auto property = array->elements[index];
				std::optional<std::string> member;

				switch(property.PropertyType)
				{
				case JSProperty::Type::Data:
#if ENABLE_ES4
				case JSProperty::Type::Variable:
#endif
					if(access.has_value())
					{
						std::ostringstream oss;
						oss << access.value() << "[" << index << "]";
						member = oss.str();
					}
					::inspect<JSValue>::inspect_value(out, property.GetValue(), depth - 1, visited, member);
					break;
				case JSProperty::Type::Accessor:
					using namespace JS;
					if(property.Get == nullptr)
					{
						out << "\33[96m[Setter]\33[m";
					}
					else if(property.Set == nullptr)
					{
						out << "\33[96m[Getter]\33[m";
					}
					else
					{
						out << "\33[96m[Getter/Setter]\33[m";
					}
					break;
#if ENABLE_ES4
				case JSProperty::Type::Getter:
					out << "\33[96m[Getter]\33[m";
					break;
				case JSProperty::Type::Setter:
					out << "\33[96m[Setter]\33[m";
					break;
				case JSProperty::Type::Forbidden:
					std::cerr << "Invalid entry: found shadowed property in object" << std::endl;
					out << "\33[1m[Forbidden]\33[m";
					break;
#endif
				}
			}

			if(started)
			{
				out << " ]";
			}
			else
			{
				out << "]";
			}
		}
		return; // TODO
		out << " "; // TODO: only if other fields are also present
	}
	else if(std::dynamic_pointer_cast<JS::Class::XML>(value))
	{
		out << "\33[92m";
		JS::PrintString(out, JSValue(value).ToXMLString());
		out << "\33[m";
		return; // TODO
	}
	else if(std::dynamic_pointer_cast<JS::Class::XMLList>(value))
	{
		out << "\33[92m";
		JS::PrintString(out, JSValue(value).ToXMLString());
		out << "\33[m";
		return; // TODO
	}
	else
	{
		try
		{
			// needed to avoid getting captured in a [[Get]] override
			auto constructor = value->Get(u"constructor", value, JS::Intent::Read PUBLIC_NAMESPACES WITH_CLASS(nullptr) NO_ENVIRONMENT RUNTIME_PHASE);
			if(auto constructor_name = JS::GetString(constructor.Get(u"name", constructor, JS::Intent::Read PUBLIC_NAMESPACES WITH_CLASS(nullptr) NO_ENVIRONMENT RUNTIME_PHASE)))
			//if(auto constructor_name = JS::GetString(JS::Get(JS::Get(value, u"constructor"), u"name")))
			{
				JS::PrintString(out, constructor_name.value());
				out << " ";
			}
		}
		catch(...)
		{
			// constructor did not exist, ignore failure
		}

		if(JS::Version::ES6 <= JS::CurrentVersion)
		{
			if(auto to_string_tag = JS::GetString(JS::Get(value, JS::Symbols::toStringTag)))
			{
				out << "[";
				JS::PrintString(out, to_string_tag.value());
				out << "] ";
			}
		}
		// TODO: other versions
	}

	if(depth == 0)
	{
		out << "{...}";
		return;
	}

	out << "{ ";

	if(access.has_value())
	{
		visited[value] = access.value();
	}

	bool started = false;
#if ENABLE_ES4
	if(JS::IsVersionES4())
	{
		started = inspect_instance_variables(out, value, value->GetClass(), depth, visited, access);
	}
#endif

	for(auto key : value->OwnPropertyKeys(VIA_CLASS(value->GetClass())))
	{
		if(started)
		{
			out << ", ";
		}
		else
		{
			started = true;
		}
		std::string member = std::visit([&out, access](auto&& key)
		{
			using T = std::decay_t<decltype(key)>;
			if constexpr (std::is_same_v<T, JSString>)
			{
				if(is_valid_identifier(key))
				{
					JS::PrintString(out, key);

					if(access.has_value())
					{
						std::ostringstream oss;
						JS::PrintString(oss, key);
						return access.value() + "." + oss.str();
					}
				}
				else
				{
					out << "\33[92m";
					output_string(out, key);
					out << "\33[m";

					if(access.has_value())
					{
						std::ostringstream oss;
						output_string(oss, key);
						return access.value() + "[" + oss.str() + "]";
					}
				}
			}
			else if constexpr (std::is_same_v<T, JSSymbol>)
			{
				out << "\33[2mSymbol(";
				if(key->Description.has_value())
				{
					output_string(out, key->Description.value());
				}
				out << ")\33[m";

				if(access.has_value())
				{
					std::ostringstream oss;
					if(key->Description.has_value())
					{
						output_string(oss, key->Description.value());
					}
					return access.value() + "[Symbol(" + oss.str() + ")]";
				}
			}
			else
			{
				static_assert(false);
			}
			return std::string("");
		}, key);
		out << ": ";
		out << std::flush; // to make debugging easier

		std::optional<JSProperty> property_option = value->GetOwnProperty(key PUBLIC_NAMESPACES READ_ACCESS);
		if(!property_option.has_value())
		{
#if ENABLE_ES4
			property_option = value->GetOwnProperty(key PUBLIC_NAMESPACES WRITE_ACCESS);
			if(!property_option.has_value())
			{
				std::cerr << "Invalid entry: missing both read and write access entries" << std::endl;
			}
			else
			{
				out << "\33[96m[Setter]\33[m";
			}
#else
			std::cerr << "Invalid entry: missing" << std::endl;
#endif
		}
		else
		{
			auto property = property_option.value();
			switch(property.PropertyType)
			{
			case JSProperty::Type::Data:
#if ENABLE_ES4
			case JSProperty::Type::Variable:
#endif
				::inspect<JSValue>::inspect_value(out, property.GetValue(), depth - 1, visited, access.has_value() ? member : std::optional<std::string>());
				break;
			case JSProperty::Type::Accessor:
				using namespace JS;
				if(property.Set == nullptr)
				{
					out << "\33[96m[Getter]\33[m";
				}
				else if(property.Get == nullptr)
				{
					out << "\33[96m[Setter]\33[m";
				}
				else
				{
					out << "\33[96m[Getter/Setter]\33[m";
				}
				break;
#if ENABLE_ES4
			case JSProperty::Type::Getter:
				property_option = value->GetOwnProperty(key PUBLIC_NAMESPACES WRITE_ACCESS);
				if(!property_option.has_value())
				{
					out << "\33[96m[Getter]\33[m";
				}
				else
				{
					out << "\33[96m[Getter/Setter]\33[m";
				}
				break;
			case JSProperty::Type::Setter:
				std::cerr << "Invalid entry: found setter in write access" << std::endl;
				out << "\33[96m[Setter]\33[m";
				break;
			case JSProperty::Type::Forbidden:
				std::cerr << "Invalid entry: found shadowed property in object" << std::endl;
				out << "\33[1m[Forbidden]\33[m";
				break;
#endif
			}
		}
	}
	if(started)
		out << " }";
	else
		out << "}";
}

void inspect<JSSymbol>::inspect_type(std::ostream& out)
{
	out << "Symbol";
}

void inspect<JSSymbol>::inspect_value(std::ostream& out, const JSSymbol& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[92mSymbol(";
	if(value->Description.has_value())
	{
		output_string(out, value->Description.value());
	}
	out << ")\33[m";
}

void inspect<JSBigInt>::inspect_type(std::ostream& out)
{
	out << "BigInt";
}

void inspect<JSBigInt>::inspect_value(std::ostream& out, const JSBigInt& value, [[maybe_unused]] size_t depth,[[maybe_unused]]  std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << value << "n\33[m";
}

#if ENABLE_ES4
void inspect<JSLong>::inspect_type(std::ostream& out)
{
	out << "long";
}

void inspect<JSLong>::inspect_value(std::ostream& out, const JSLong& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << value << "l\33[m";
}

void inspect<JSUlong>::inspect_type(std::ostream& out)
{
	out << "ulong";
}

void inspect<JSUlong>::inspect_value(std::ostream& out, const JSUlong& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << value << "ul\33[m";
}

void inspect<JSFloat>::inspect_type(std::ostream& out)
{
	out << "float32";
}

void inspect<JSFloat>::inspect_value(std::ostream& out, const JSFloat& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m" << value << "f\33[m";
}


#if ENABLE_DEC128
void inspect<JSDecimal>::inspect_type(std::ostream& out)
{
	out << "decimal128";
}

void inspect<JSDecimal>::inspect_value(std::ostream& out, const JSDecimal& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[93m";
	JS::PrintString(out, JS::ToString(JSValue(JSDecimal(value)) WITH_PHASE(JS::Runtime::ExecutionPhase::Compile)));
	out << "m\33[m" << std::endl;
}
#endif

void inspect<JSChar>::inspect_type(std::ostream& out)
{
	out << "char16";
}

void inspect<JSChar>::inspect_value(std::ostream& out, const JSChar& value, [[maybe_unused]] size_t depth, [[maybe_unused]] [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[92mchar(";
	output_string(out, JSString(1, value));
	out << ")\33[m" << std::endl;
}

void inspect<JSAttributeName>::inspect_type(std::ostream& out)
{
	out << "AttributeName";
}

void inspect<JSAttributeName>::inspect_value(std::ostream& out, const JSAttributeName& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[31m@";
	JS::PrintString(out, value.Name.ToString());
	out << "\33[m";
}
#endif

void inspect<JS::Runtime::LexicalReference>::inspect_type(std::ostream& out)
{
	out << "LexicalReference";
}

void inspect<JSNamespace>::inspect_type(std::ostream& out)
{
	out << "namespace";
}

void inspect<JSNamespace>::inspect_value(std::ostream& out, const JSNamespace& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "\33[94m";
	JS::PrintString(out, value->uri);
	out << "\33[m";
}

void inspect<JS::Runtime::LexicalReference>::inspect_value(std::ostream& out, const JS::Runtime::LexicalReference& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	if(value.Strict)
	{
		out << "\33[1mstrict\33[m ";
	}
	::inspect<JS::Runtime::EnvironmentRecord *>::inspect_value(out, value.Base.get(), depth, visited, std::optional<std::string>());
	out << ".";
	if(value.Attribute)
	{
		out << "@";
	}
	JS::PrintString(out, const_cast<JS::Runtime::LexicalReference&>(value).GetName()); // TODO
	if(const_cast<JS::Runtime::LexicalReference&>(value).GetNamespaceSet().size() != 0) // TODO
	{
		out << " in {";
		bool started = false;
		for(auto namespace_ : const_cast<JS::Runtime::LexicalReference&>(value).GetNamespaceSet()) // TODO
		{
			if(started)
			{
				out << ", ";
			}
			else
			{
				started = true;
			}
			::inspect<JSNamespace>::inspect_value(out, namespace_, depth, visited, std::optional<std::string>());
		}
		out << "}";
	}
}

void inspect<JS::Runtime::MemberReference>::inspect_type(std::ostream& out)
{
	out << "MemberReference";
}

void inspect<JS::Runtime::MemberReference>::inspect_value(std::ostream& out, const JS::Runtime::MemberReference& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	if(value.Strict)
	{
		out << "\33[1mstrict\33[m ";
	}
	::inspect<JSValue>::inspect_value(out, value.Base, depth, visited, std::optional<std::string>());
	bool print_as_identifier = false;

	if(auto string_option = JS::GetString(value.ReferencedName))
	{
		if(is_valid_identifier(string_option.value()))
		{
			out << ".";
			JS::PrintString(out, string_option.value());
			print_as_identifier = true;
		}
	}

	if(!print_as_identifier)
	{
#if ENABLE_ES4
		if(JS::IsVersionES4())
		{
			out << ".";
			::inspect<JSValue>::inspect_value(out, value.ReferencedName, depth, visited, std::optional<std::string>());
		}
		else
#endif
		{
			out << "[";
			::inspect<JSValue>::inspect_value(out, value.ReferencedName, depth, visited, std::optional<std::string>());
			out << "]";
		}
	}

	using namespace JS;
	if(value.Base != value.ThisValue)
	{
		out << " this: ";
		::inspect<JSValue>::inspect_value(out, value.ThisValue, depth, visited, std::optional<std::string>());
	}

#if ENABLE_ES4
	if(value.Namespaces.size() != 0)
	{
		out << " in {";
		bool started = false;
		for(auto namespace_ : value.Namespaces)
		{
			if(started)
			{
				out << ", ";
			}
			else
			{
				started = true;
			}
			::inspect<JSNamespace>::inspect_value(out, namespace_, depth, visited, std::optional<std::string>());
		}
		out << "}";
	}

	if(value.Limit != GetClass(value.Base))
	{
		out << " limit: ";
		::inspect<JSClass>::inspect_value(out, value.Limit, depth, visited, std::optional<std::string>());
	}
#endif
}

#if ENABLE_ES4
void inspect<JS::Runtime::BracketReference>::inspect_type(std::ostream& out)
{
	out << "BracketReference";
}

void inspect<JS::Runtime::BracketReference>::inspect_value(std::ostream& out, const JS::Runtime::BracketReference& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	if(value.Strict)
	{
		out << "\33[1mstrict\33[m ";
	}
	::inspect<JSValue>::inspect_value(out, value.Base, depth, visited, std::optional<std::string>());
	out << "[";

	bool started = false;
	for(auto argument : value.Arguments)
	{
		if(started)
		{
			out << ", ";
		}
		else
		{
			started = true;
		}
		::inspect<JSValue>::inspect_value(out, argument, depth, visited, std::optional<std::string>());
	}

	out << "]";

	using namespace JS;
	if(value.Base != value.ThisValue)
	{
		out << " this: ";
		::inspect<JSValue>::inspect_value(out, value.ThisValue, depth, visited, std::optional<std::string>());
	}

#if ENABLE_ES4
	if(value.Limit != GetClass(value.Base))
	{
		out << " limit: ";
		::inspect<JSClass>::inspect_value(out, value.Limit, depth, visited, std::optional<std::string>());
	}
#endif
}
#endif

void inspect<JS::Runtime::SuperReference>::inspect_type(std::ostream& out)
{
	out << "SuperReference";
}

void inspect<JS::Runtime::SuperReference>::inspect_value(std::ostream& out, const JS::Runtime::SuperReference& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	out << "super(";
	::inspect<JSValue>::inspect_value(out, value.ThisValue, depth, visited, std::optional<std::string>());
	out << ")";
	using namespace JS;
	if(value.Instance != value.ThisValue)
	{
		out << " from ";
		::inspect<JSValue>::inspect_value(out, value.Instance, depth, visited, std::optional<std::string>());
	}
#if ENABLE_ES4
	if(value.Limit != GetClass(value.ThisValue))
	{
		out << " limit: ";
		::inspect<JSClass>::inspect_value(out, value.Limit, depth, visited, std::optional<std::string>());
	}
#endif
}

void inspect<JS::Runtime::PrivateReference>::inspect_type(std::ostream& out)
{
	out << "PrivateReference";
}

void inspect<JS::Runtime::PrivateReference>::inspect_value(std::ostream& out, const JS::Runtime::PrivateReference& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	::inspect<JSValue>::inspect_value(out, value.Base, depth, visited, std::optional<std::string>());
	out << ".#";
	JS::PrintString(out, value.ReferencedName->Description);
}

#if ENABLE_ES4
void inspect<JSTypeApplication>::inspect_type(std::ostream& out)
{
	out << "TypeApplication";
}

void inspect<JSTypeApplication>::inspect_value(std::ostream& out, const JSTypeApplication& value, size_t depth, std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	inspect<JSClass>::inspect_value(out, value.parametrized_type, depth, visited, std::optional<std::string>());
	out << ".<";
	bool started = false;
	for(auto parameter : value.parameters)
	{
		if(started)
		{
			out << ", ";
		}
		else
		{
			started = true;
		}
		::inspect<JSType>::inspect_value(out, value, depth, visited, std::optional<std::string>());
	}
	out << ">";
}

void inspect<JSMethodClosure>::inspect_type(std::ostream& out)
{
	out << "MethodClosure";
}

void inspect<JSMethodClosure>::inspect_value(std::ostream& out, const JSMethodClosure& value, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	inspect<JSValue>::inspect_value(out, value.this_, depth, visited, access.has_value() ? access.value() + ".\33[1mthis\33[m" : std::optional<std::string>());
	out << ".";
	if(value.name.has_value())
	{
		JS::PrintString(out, value.name.value());
	}
	else
	{
		out << "\33[37manonymous\33[m";
	}
}

void inspect<JSCompoundAttribute>::inspect_type(std::ostream& out)
{
	out << "Attribute";
}

void inspect<JSCompoundAttribute>::inspect_value(std::ostream& out, const JSCompoundAttribute& value, [[maybe_unused]] size_t depth, [[maybe_unused]] std::map<JSObject, std::string>& visited, [[maybe_unused]] std::optional<std::string> access)
{
	if(value.IsEmpty())
	{
		out << "\33[1mempty\33[m";
	}
	else if(!value.Enabled)
	{
		out << "\33[93mfalse\33[m";
	}
	else
	{
		bool started = false;
		for(auto namespace_ : value.Namespaces)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[92m";
			JS::PrintString(out, namespace_->uri);
			out << "\33[m";
		}
		if(value.Explicit)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mexplicit\33[m";
		}
		if(value.Enumerable)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1menumerable\33[m";
		}
		if(value.Dynamic)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mdynamic\33[m";
		}

		switch(value.Category)
		{
		case JS::Runtime::MemberModifier::None:
			break;
		case JS::Runtime::MemberModifier::Static:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mstatic\33[m";
			break;
		case JS::Runtime::MemberModifier::Virtual:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mvirtual\33[m";
			break;
		case JS::Runtime::MemberModifier::Final:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mfinal\33[m";
			break;
		//case JS::Runtime::MemberModifier::Constructor:
		case JS::Runtime::MemberModifier::Abstract:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mabstract\33[m";
			break;
		}

		switch(value.Override)
		{
		case JS::Runtime::OverrideModifier::None:
			break;
		case JS::Runtime::OverrideModifier::True:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m";
			break;
		case JS::Runtime::OverrideModifier::False:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m(\33[93mfalse\33[m)";
			break;
		case JS::Runtime::OverrideModifier::Undefined:
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1moverride\33[m(\33[37mundefined\33[m)";
			break;
		}
		if(value.Prototype)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mprototype\33[m";
		}
		if(value.Unused)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1munused\33[m";
		}
		if(value.Native)
		{
			if(started)
				out << " ";
			else
				started = true;
			out << "\33[1mnative\33[m";
		}
	}
}
#endif

void inspect<JSValue>::inspect_type(std::ostream& out)
{
	out << "value";
}

void inspect<JSValue>::inspect_value(std::ostream& out, const JSValue& value, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	const_cast<JSValue&>(value).inspect(out, depth, visited, access); // TODO: how to avoid const cast
}

//// JS

JSString JS::UInt64ToString(uint64_t value, int radix)
{
	JSString result;
	do
	{
		int digit = value % radix;
		if(digit < 10)
		{
			result = char16_t(u'0' + digit) + result;
		}
		else
		{
			result = char16_t(u'a' + digit - 10) + result;
		}
		value /= radix;
	} while(value != 0);
	return result;
}

JSString JS::NumberToString(JSNumber value, int radix)
{
	switch(std::fpclassify(value))
	{
	case FP_NAN:
		return u"NaN";
	case FP_ZERO:
		return u"0";
	case FP_INFINITE:
		return std::signbit(value) ? u"-Infinity" : u"Infinity";
	default:
		break;
	}
	JSString result = std::signbit(value) ? u"-" : u"";
	value = std::copysign(value, 1.0);
	int shift = 0;
	if(std::floor(value) == value)
	{
		for(JSNumber shifted = value / radix;
			std::floor(shifted) == shifted;
			shifted /= radix)
		{
			shift++;
			value = shifted;
		}
	}
	else
	{
		JSNumber original_value = value;
		shift--;
		for(value *= radix;
			std::floor(value) != value;
			value *= radix)
		{
			JSNumber approximate = std::floor(value);
			for(int i = 0; i > shift; i--)
			{
				approximate /= radix;
			}
//std::cerr << value << "," << shift << ":" << (approximate - original_value) << std::endl;
			if(approximate == original_value)
				break;
			shift--;
		}
	}
	JSString intpart = UInt64ToString(std::floor(value), radix);
	shift += intpart.size();
	if(radix != 10 || (-5 <= shift && shift <= 21))
	{
		if(shift >= int(intpart.size()))
		{
			result += intpart;
			result += std::u16string(shift - intpart.size(), u'0');
		}
		else if(shift > 0)
		{
			result += intpart.substr(0, shift);
			result += u".";
			result += intpart.substr(shift);
		}
		else
		{
			result += u"0.";
			result += std::u16string(-shift, u'0');
			result += intpart;
		}
	}
	else
	{
		if(intpart.size() == 1)
		{
			result += intpart;
		}
		else
		{
			result += intpart.substr(0, 1);
			result += u".";
			result += intpart.substr(1);
		}
		result += u"e";
		result += shift < 0 ? u"-" : u"+";
		result += UInt64ToString(std::abs(shift));
	}
	return result;
}

#if ENABLE_ES4
JSClass JS::GetClassForType(JSType type)
{
	return std::visit([](auto&& type)
	{
		using T = std::decay_t<decltype(type)>;
		if constexpr(std::is_same_v<T, JSClass>)
		{
			return type;
		}
		else if constexpr(std::is_same_v<T, JSTypeApplication>)
		{
			return type.parametrized_type;
		}
		else
		{
			static_assert(false);
		}
	}, type);
}
#endif

JSObject JS::CreateIteratorResultObject(JSValue value, bool done)
{
	auto result = JS::New<JS::Class::OrdinaryObject>();
	Define(result, u"value", value);
	Define(result, u"done", done);
	return result;
}

JSObject JS::FromPropertyDescriptor(JSPropertyDescriptor descriptor)
{
	auto obj = JS::New<JS::Class::OrdinaryObject>();
	if(descriptor.Value.has_value())
	{
		Define(obj, u"value", descriptor.Value.value());
	}
	if(descriptor.Writable.has_value())
	{
		Define(obj, u"writable", descriptor.Writable.value());
	}
	if(descriptor.Get.has_value())
	{
		Define(obj, u"get", descriptor.Get.value());
	}
	if(descriptor.Set.has_value())
	{
		Define(obj, u"set", descriptor.Set.value());
	}
	if(descriptor.Enumerable.has_value())
	{
		Define(obj, u"enumerable", descriptor.Enumerable.value());
	}
	if(descriptor.Configurable.has_value())
	{
		Define(obj, u"configurable", descriptor.Configurable.value());
	}
	return obj;
}

JSPropertyDescriptor JS::ToPropertyDescriptor(JSValue value)
{
	auto object_option = GetNonNullObject(value);
	if(!object_option.has_value())
	{
		JS::Throw(JS::New<TypeError> __LOC("non-object passed as property descriptor"));
	}
	auto object = object_option.value();
	JSPropertyDescriptor descriptor;
	if(object->HasProperty(u"enumerable" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		descriptor.Enumerable = JS::ToBoolean(JS::Get(object, u"enumerable"));
	}
	if(object->HasProperty(u"configurable" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		descriptor.Configurable = JS::ToBoolean(JS::Get(object, u"configurable"));
	}
	if(object->HasProperty(u"value" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		descriptor.Value = JS::Get(object, u"value");
	}
	if(object->HasProperty(u"writable" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		descriptor.Writable = JS::ToBoolean(JS::Get(object, u"writable"));
	}
	if(object->HasProperty(u"get" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		auto get = JS::Get(object, u"get");
		if(auto get_function = JS::GetFunction(get))
		{
			descriptor.Get = get_function;
		}
		else if(IsUndefined(get))
		{
			descriptor.Get = nullptr;
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("passed non-function as property descriptor getter"));
		}
	}
	if(object->HasProperty(u"set" PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		auto set = JS::Get(object, u"set");
		if(auto set_function = JS::GetFunction(set))
		{
			descriptor.Set = set_function;
		}
		else if(IsUndefined(set))
		{
			descriptor.Set = nullptr;
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("passed non-function as property descriptor setter"));
		}
	}
	return descriptor;
}

JSPropertyKey JS::ToPropertyKey(JSValue value AND_PHASE(phase))
{
	return value.ToPropertyKey(VIA_PHASE(phase));
}

/*JSPropertyId JS::asPropertyId(JSString name)
{
#if ENABLE_ES4
	return JSQualifiedName{JS_public, name};
#else
	return name;
#endif
}*/

bool JS::IsUndefined(JSValue value)
{
	return value.has_value<JS::Type::Internal::Undefined>();
}

bool JS::IsNull(JSValue value)
{
	return value.has_value<JS::Type::Internal::Null>();
}

bool JS::IsPrimitive(JSValue value)
{
	// TODO: Namespace, Attribute, MethodClosure and TypeApplication should all arrive here
	return !GetNonNullObject(value);
}

std::optional<JSBoolean> JS::GetBoolean(JSValue value)
{
	return value.get_if<JSBoolean>();
}

std::optional<JSNumber> JS::GetNumber(JSValue value)
{
	return value.get_if<JSNumber>();
}

std::optional<JSString> JS::GetString(JSValue value)
{
	return value.get_if<JSString>();
}

std::optional<JSSymbol> JS::GetSymbol(JSValue value)
{
	return value.get_if<JSSymbol>();
}

std::optional<JSBigInt> JS::GetBigInt(JSValue value)
{
	return value.get_if<JSBigInt>();
}

std::optional<JSObject> JS::GetObjectOrNull(JSValue value)
{
	if(value.has_value<JS::Type::Internal::Null>())
	{
		return nullptr;
	}
	else
	{
		return value.get_if<JSObject>();
	}
}

std::optional<JSObject> JS::GetNonNullObject(JSValue value)
{
	return value.get_if<JSObject>();
}

#if ENABLE_ES4
std::optional<JSLong> JS::GetLong(JSValue value)
{
	return value.get_if<JSLong>();
}

std::optional<JSUlong> JS::GetUlong(JSValue value)
{
	return value.get_if<JSUlong>();
}

std::optional<JSFloat> JS::GetFloat(JSValue value)
{
	return value.get_if<JSFloat>();
}

std::optional<JSDecimal> JS::GetDecimal(JSValue value)
{
	return value.get_if<JSDecimal>();
}

std::optional<JSMethodClosure> JS::GetMethodClosure(JSValue value)
{
	return value.get_if<JSMethodClosure>();
}
#endif

std::optional<JSFunction> JS::GetFunction(JSValue value)
{
	return JSFunction::GetFunction(value);
}

std::optional<JS::Type::Function> JS::GetConstructor(JSValue value)
{
	if(value.GetInvocability() == JS::Invocability::Constructor)
	{
		return JSFunction::GetFunction(value);
	}
	else
	{
		return {};
	}
}

bool JS::IsCallable(JS::Public::JSValue value)
{
	return value.GetInvocability() != JS::Invocability::NonCallable;
}

bool JS::IsConstructor(JS::Public::JSValue value)
{
	return value.GetInvocability() == JS::Invocability::Constructor;
}

JSValue JS::Call(JSFunction callable, JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	return callable.Call(this_argument, arguments WITH_PHASE(phase));
}

bool JS::Begin_Call(JSFunction callable, JSValue this_argument, std::vector<JSValue> arguments)
{
	return callable.Begin_Call(this_argument, arguments);
}

JS::Public::JSValue JS::Construct(JS::Public::JSFunction callable, std::vector<JS::Public::JSValue> arguments AND_PHASE(phase))
{
	return Construct(callable, arguments, callable WITH_PHASE(phase));
}

JS::Public::JSValue JS::Construct(JS::Public::JSFunction callable, std::vector<JS::Public::JSValue> arguments, JSFunction new_target AND_PHASE(phase))
{
	if(!IsConstructor(callable))
	{
		JS::Throw(JS::New<TypeError> __LOC("attempting to construct using a non-constructor"));
	}
	return callable.Construct(arguments, new_target WITH_PHASE(phase));
}

#if ENABLE_ES4
std::optional<JSClass> JS::GetClass(JSValue value)
{
	if(auto object = GetNonNullObject(value))
	{
		if(auto class_ = std::dynamic_pointer_cast<JS::Class::Class>(object.value()))
		{
			return std::make_optional(class_);
		}
	}
	return {};
}

std::optional<JSType> JS::GetType(JSValue value)
{
	if(auto type_application = value.get_if<JSTypeApplication>())
	{
		return type_application.value();
	}
	else
	{
		return GetClass(value);
	}
}
#endif

std::optional<JSFunction> JS::GetMethod(JSValue value, JSPropertyKey property_key)
{
	auto func = JS::GetByKey(value.ToObject(), property_key);
	if(IsNull(func) || IsUndefined(func))
	{
		return {};
	}
	else if(auto function = GetFunction(func))
	{
		return function;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

std::optional<JSPropertyKey> JS::GetPropertyKey(JSValue value)
{
	if(auto string_option = GetString(value))
	{
		return string_option.value();
	}
	else if(auto symbol_option = GetSymbol(value))
	{
		return symbol_option.value();
	}
	else
	{
		return {};
	}
}

JSPropertyKey JS::assert_property_key(JSValue property_key_value)
{
	auto property_key_option = GetPropertyKey(property_key_value);
	if(!property_key_option)
	{
		JS::Throw(JS::New<InternalError> __LOC("not a property key: expected string or symbol for non-E4X objects"));
	}
	return property_key_option.value();
}

bool JS::SameValue(JSValue a, JSValue b)
{
	return a.SameValue(b);
}

bool JS::IsStrictlyEqual(JSValue a, JSValue b)
{
	return a.IsStrictlyEqual(b);
}

bool JS::operator ==(JSValue a, JSValue b)
{
	return a.SameValueZero(b);
}

bool JS::operator !=(JSValue a, JSValue b)
{
	return !a.SameValueZero(b);
}

bool JS::SameType(JSValue a, JSValue b)
{
	if(IsUndefined(a))
	{
		return IsUndefined(b);
	}
	else if(IsNull(a))
	{
		return IsNull(b);
	}
	else if(GetBoolean(a))
	{
		return GetBoolean(b).has_value();
	}
	else if(GetNumber(a))
	{
		return GetNumber(b).has_value();
	}
	else if(Version::ES11 <= CurrentVersion && GetBigInt(a))
	{
		return GetBigInt(b).has_value();
	}
	else if(Version::ES6 <= CurrentVersion && GetSymbol(a))
	{
		return GetSymbol(b).has_value();
	}
	else if(GetString(a))
	{
		return GetString(b).has_value();
	}
	else if(GetNonNullObject(a))
	{
		return GetNonNullObject(b).has_value();
	}
	else
	{
		return false;
	}
}

#if ENABLE_ES4
bool JS::IsInteger(JSValue arg)
{
	if(GetLong(arg) || GetUlong(arg))
	{
		return true;
	}
	else if(auto number = GetNumber(arg))
	{
		return trunc(*number) == *number;
	}
	else if(auto floatval = GetFloat(arg))
	{
		return trunc(*floatval) == *floatval;
	}
	// TODO: decimal?
	// TODO: do we consider BigInt? this function is only intended for ES4
	else
	{
		return false;
	}
}

bool JS::IsZero(JSValue arg)
{
	if(auto longval = GetLong(arg))
	{
		return *longval == 0;
	}
	else if(auto ulongval = GetUlong(arg))
	{
		return *ulongval == 0;
	}
	else if(auto number = GetNumber(arg))
	{
		return *number == 0;
	}
	else if(auto floatval = GetFloat(arg))
	{
		return *floatval == 0;
	}
	// TODO: decimal?
	// TODO: do we consider BigInt? this function is only intended for ES4
	else
	{
		return false;
	}
}

JSValue JS::GetInteger(JSValue arg) // TODO: do we use this?
{
	if(GetLong(arg) || GetUlong(arg))
	{
		return arg;
	}
	else if(auto number = GetNumber(arg))
	{
		if(*number < 0)
		{
			JSLong value = JSLong(trunc(*number));
			if(JSNumber(value) == *number)
			{
				return JSValue(value);
			}
		}
		else
		{
			JSUlong value = JSUlong(trunc(*number));
			if(JSNumber(value) == *number)
			{
				return JSValue(value);
			}
		}
		return JS_undefined;
	}
	else if(auto floatval = GetFloat(arg))
	{
		if(*floatval < 0)
		{
			JSLong value = JSLong(trunc(*floatval));
			if(JSNumber(value) == *floatval)
			{
				return JSValue(value);
			}
		}
		else
		{
			JSUlong value = JSUlong(trunc(*floatval));
			if(JSNumber(value) == *floatval)
			{
				return JSValue(value);
			}
		}
		return JS_undefined;
	}
	// TODO: decimal?
	// TODO: do we consider BigInt? this function is only intended for ES4
	else
	{
		return false;
	}
}
#endif

JSValue JS::ToPrimitive(JSValue arg AND_PHASE(phase), Runtime::TypeHint hint)
{
	return arg.ToPrimitive(hint WITH_PHASE(phase));
}

JSBoolean JS::ToBoolean(JSValue arg)
{
	return arg.ToBoolean();
}

JSNumber JS::ToNumber(JSValue arg AND_PHASE(phase))
{
	return arg.ToNumber(__ES4_ONLY(phase));
}

JSValue JS::ToNumeric(JSValue arg AND_PHASE(phase))
{
	return arg.ToNumeric(__ES4_ONLY(phase));
}

JSBigInt JS::ToBigInt(JSValue arg)
{
	return arg.ToBigInt();
}

JSNumber JS::ToInteger(JSValue arg AND_PHASE(phase))
{
	return arg.ToInteger(__ES4_ONLY(phase));
}

int8_t JS::ToInt8(JSValue arg)
{
	return (int8_t)arg.ToInteger(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

uint8_t JS::ToUint8(JSValue arg)
{
	return (int8_t)arg.ToInteger(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

uint8_t JS::ToUint8Clamp(JSValue arg)
{
	return arg.ToInteger(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

int16_t JS::ToInt16(JSValue arg)
{
	return (int8_t)arg.ToInteger(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

uint16_t JS::ToUint16(JSValue arg)
{
	return (int16_t)arg.ToInteger(__ES4_ONLY(JS::Runtime::ExecutionPhase::Run));
}

int32_t JS::ToInt32(JSValue arg AND_PHASE(phase))
{
	return arg.ToInt32(__ES4_ONLY(phase));
}

uint32_t JS::ToUint32(JSValue arg AND_PHASE(phase))
{
	return arg.ToUint32(__ES4_ONLY(phase));
}

#if ENABLE_ES4
int64_t JS::ToInt64(JSValue arg AND_PHASE(phase))
{
	return arg.ToInt64(__ES4_ONLY(phase));
}

uint64_t JS::ToUint64(JSValue arg AND_PHASE(phase))
{
	return arg.ToUint64(__ES4_ONLY(phase));
}
#endif

int64_t JS::ToBigInt64(JSValue arg)
{
	JSBigInt value = arg.ToBigInt();
	int64_t result;
	if /*constexpr*/(sizeof(unsigned long) < sizeof(uint64_t))
	{
		union
		{
			unsigned long words[sizeof(uint64_t) / sizeof(unsigned long)];
			uint64_t value;
		} u;
		mpz_export(
			(void *)u.words,
			nullptr,
			(u.value = 1, u.words[0] != 1 ? 1 : -1),
			sizeof(unsigned long),
			0, 0,
			value.get_mpz_t());
		result = u.value;
	}
	else
	{
		result = value.get_ui();
	}
	return sgn(value) < 0 ? -result : result;
}

uint64_t JS::ToBigUint64(JSValue arg)
{
	return (uint64_t)ToBigInt64(arg);
}

intptr_t JS::ToIndex(JSValue arg)
{
	auto intval = ToInteger(arg RUNTIME_PHASE);
	if(intval < 0 || (int64_t)intval > 0x1FFFFFFFFFFFFF)
	{
		JS::Throw(JS::New<RangeError> __LOC(u"integer outside index range"));
	}
	return (int64_t)intval;
}

uintptr_t JS::ToLength(JSValue arg)
{
	auto intval = ToInteger(arg RUNTIME_PHASE);
	if(intval < 0)
	{
		return (uintptr_t)0;
	}
	else if((int64_t)intval > 0x1FFFFFFFFFFFFF)
	{
		return (uint64_t)0x1FFFFFFFFFFFFF;
	}
	else
	{
		return (uint64_t)intval;
	}
}

JSString JS::ToString(JSValue arg AND_PHASE(phase))
{
	return arg.ToString(__ES4_ONLY(phase));
}

JSObject JS::ToObject(JSValue arg)
{
	return arg.ToObject();
}

JSString JS::TypeOf(JSValue arg)
{
	return arg.Typeof();
}

JSString JS::ToStringTag(JSValue arg)
{
	return arg.ToStringTag();
}

std::optional<JSNumber> JS::CanonicalNumericIndexString(JSString argument)
{
	if(argument == "-0")
	{
		return -0.0;
	}
	JSNumber value = JS::ToNumber(argument RUNTIME_PHASE);
	if(argument == JS::ToString(value RUNTIME_PHASE))
	{
		return value;
	}
	else
	{
		return {};
	}
}

JSValue JS::Get(JSValue object, JSString string)
{
	return object.Get(string, object, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
}

JSValue JS::Get(JSValue object, JSSymbol symbol)
{
	return object.Get(symbol, object, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
}

JSValue JS::GetByKey(JSValue object, JSPropertyKey property_key)
{
	return object.Get(property_key, object, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
}

JSProperty JS::GetOwnPropertyByKey(JSObject object, JSPropertyKey property_key)
{
	return object->GetOwnProperty(property_key PUBLIC_NAMESPACES READ_WRITE_ACCESS).value();
}

#if ENABLE_ES4
JSValue JS::Get(JSValue object, JSQualifiedName qualified_name)
{
	return object.Get(qualified_name.Id, object, Intent::Read, std::set<JSNamespace>{qualified_name.Namespace}, object.GetClass() NO_ENVIRONMENT RUNTIME_PHASE);
}

JSValue JS::GetById(JSValue object, JSPropertyId property_id)
{
	return std::visit([&object](auto&& key)
	{
		using T = std::decay_t<decltype(key)>;
		if constexpr (std::is_same_v<T, JSPropertyName>)
		{
			return object.Get(key.Id, object, Intent::Read, std::set<JSNamespace>{key.Namespace}, object.GetClass() NO_ENVIRONMENT RUNTIME_PHASE);
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			return object.Get(key, object, Intent::Read, std::set<JSNamespace>{JS_public}, object.GetClass() NO_ENVIRONMENT RUNTIME_PHASE);
		}
		else
		{
			static_assert(false);
		}
	}, property_id);
}

std::optional<JSProperty> JS::HasOwnPropertyById(JSObject object, JSPropertyId property_id)
{
	return std::visit([&object](auto&& key)
	{
		using T = std::decay_t<decltype(key)>;
		if constexpr (std::is_same_v<T, JSPropertyName>)
		{
			return object->GetOwnProperty(key.Id, std::set<JSNamespace>{key.Namespace}, JS::AccessSet::ReadWrite);
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			return object->GetOwnProperty(key, std::set<JSNamespace>{JS_public}, JS::AccessSet::ReadWrite);
		}
		else
		{
			static_assert(false);
		}
	}, property_id);
}

JSProperty JS::GetOwnPropertyById(JSObject object, JSPropertyId property_id)
{
	return HasOwnPropertyById(object, property_id).value();
}
#endif

JS::Runtime::Realm * JS::GetFunctionRealm(JS::Public::JSValue value)
{
	if(auto function = JS::GetFunction(value))
	{
		return function.value().GetFunctionRealm();
	}
	JS::Throw(JS::New<TypeError> __LOC("not callable"));
}

bool JS::Set(JSValue object, JSString string, JSValue value)
{
	return object.Set(string, value, object PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT);
}

bool JS::Set(JSValue object, JSSymbol symbol, JSValue value)
{
	return object.Set(symbol, value, object PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT);
}

bool JS::SetByKey(JSValue object, JSPropertyKey property_key, JSValue value)
{
	return object.Set(property_key, value, object PUBLIC_NAMESPACES WITH_CLASS(object.GetClass()) NO_ENVIRONMENT);
}

#if ENABLE_ES4
bool JS::Set(JSValue object, JSQualifiedName qualified_name, JSValue value)
{
	return object.Set(qualified_name.Id, value, object, std::set<JSNamespace>{qualified_name.Namespace}, object.GetClass() NO_ENVIRONMENT);
}

#if 0
bool JS::Set(JSValue object, JSQualifiedName qualified_name, JSDeclaredType type, JSValue value, bool is_const, bool is_explicit)
{
	GetNonNullObject(object).value()->OverwriteOwnProperty(qualified_name.Id,
		JSProperty::MakeVariable(
			value,
			/* Writable = */ !is_const,
			false,
			is_explicit,
			type)
		WITH_NAMESPACES({qualified_name.Namespace}) READ_WRITE_ACCESS);
	return true;
}

bool JS::Set(JSValue object, JSString string, JSDeclaredType type, JSValue value, bool is_const, bool is_explicit)
{
	GetNonNullObject(object).value()->OverwriteOwnProperty(string,
		JSProperty::MakeVariable(
			value,
			/* Writable = */ !is_const,
			false,
			is_explicit,
			type)
		PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	return true;
}
#endif

bool JS::SetById(JSValue object, JSPropertyId property_id, JSValue value)
{
	return std::visit([&object, value](auto&& key)
	{
		using T = std::decay_t<decltype(key)>;
		if constexpr (std::is_same_v<T, JSPropertyName>)
		{
			return object.Set(key.Id, value, object, std::set<JSNamespace>{key.Namespace}, object.GetClass() NO_ENVIRONMENT);
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			return object.Set(key, value, object, std::set<JSNamespace>{JS_public}, object.GetClass() NO_ENVIRONMENT);
		}
		else
		{
			static_assert(false);
		}
	}, property_id);
}
#endif

bool JS::Define(JSValue object, JSString string, JSValue value, PropertyAttributes attributes)
{
	return object.Define(string, JSProperty::MakeData(value, attributes.Writable, attributes.Enumerable, attributes.Configurable WITH_FLAG(attributes.Explicit)) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
}

bool JS::Define(JSValue object, JSSymbol symbol, JSValue value, PropertyAttributes attributes)
{
	return object.Define(symbol, JSProperty::MakeData(value, attributes.Writable, attributes.Enumerable, attributes.Configurable WITH_FLAG(attributes.Explicit)) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
}

bool JS::DefineKey(JSValue object, JSPropertyKey property_key, JSValue value, PropertyAttributes attributes)
{
	return object.Define(property_key, JSProperty::MakeData(value, attributes.Writable, attributes.Enumerable, attributes.Configurable WITH_FLAG(attributes.Explicit)) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
}

#if ENABLE_ES4
bool JS::Define(JSValue object, JSQualifiedName qualified_name, JSValue value, PropertyAttributes attributes)
{
	return object.Define(qualified_name.Id, JSProperty::MakeDynamic(value, attributes.Writable, attributes.Enumerable, attributes.Configurable, attributes.Explicit), std::set<JSNamespace>{qualified_name.Namespace} READ_WRITE_ACCESS);
}

bool JS::Define(JSValue object, JSQualifiedName qualified_name, JSDeclaredType type, JSValue value, PropertyAttributes attributes)
{
	if(IsVersionES4())
	{
		GetNonNullObject(object).value()->Define(qualified_name.Id,
			JSProperty::MakeVariable(
				value,
				attributes.Writable,
				attributes.Enumerable,
				attributes.Explicit,
				type)
			WITH_NAMESPACES({qualified_name.Namespace}) READ_WRITE_ACCESS);
		return true;
	}
	else
	{
		return Define(object, qualified_name, value, attributes);
	}
}

bool JS::Define(JSValue object, JSString string, JSDeclaredType type, JSValue value, PropertyAttributes attributes)
{
	if(IsVersionES4())
	{
		GetNonNullObject(object).value()->Define(string,
			JSProperty::MakeVariable(
				value,
				attributes.Writable,
				attributes.Enumerable,
				attributes.Explicit,
				type)
			PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		return true;
	}
	else
	{
		return Define(object, string, value, attributes);
	}
}

bool JS::DefineId(JSValue object, JSPropertyId property_id, JSValue value, PropertyAttributes attributes)
{
	return std::visit([&object, value, attributes](auto&& key)
	{
		using T = std::decay_t<decltype(key)>;
		if constexpr (std::is_same_v<T, JSPropertyName>)
		{
			return object.Define(key.Id, JSProperty::MakeDynamic(value, attributes.Writable, attributes.Enumerable, attributes.Configurable, attributes.Explicit), std::set<JSNamespace>{key.Namespace} READ_WRITE_ACCESS);
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			return object.Define(key, JSProperty::MakeDynamic(value, attributes.Writable, attributes.Enumerable, attributes.Configurable, attributes.Explicit) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		}
		else
		{
			static_assert(false);
		}
	}, property_id);
}
#endif

bool JS::Define(JSValue object, JSString string, Accessor accessor, PropertyAttributes attributes)
{
	if(Version::ES5 <= CurrentVersion)
	{
		return object.Define(
			string,
			JSProperty::MakeAccessor(
				accessor.Get,
				accessor.Set,
				attributes.Enumerable, attributes.Configurable) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	}
	else
	{
		return false;
	}
}

bool JS::Define(JSValue object, JSSymbol symbol, Accessor accessor, PropertyAttributes attributes)
{
	if(Version::ES5 <= CurrentVersion)
	{
		return object.Define(
			symbol,
			JSProperty::MakeAccessor(
				accessor.Get,
				accessor.Set,
				attributes.Enumerable, attributes.Configurable) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	}
	else
	{
		return false;
	}
}

bool JS::DefineKey(JSValue object, JSPropertyKey property_key, Accessor accessor, PropertyAttributes attributes)
{
	if(Version::ES5 <= CurrentVersion)
	{
		return object.Define(
			property_key,
			JSProperty::MakeAccessor(
				accessor.Get,
				accessor.Set,
				attributes.Enumerable, attributes.Configurable) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	}
	else
	{
		return false;
	}
}

#if ENABLE_ES4
bool JS::Define(JSValue object, JSQualifiedName qualified_name, Accessor accessor, PropertyAttributes attributes)
{
	if(Version::ES5 <= CurrentVersion)
	{
		return object.Define(
			qualified_name.Id,
			JSProperty::MakeAccessor(
				accessor.Get,
				accessor.Set,
				attributes.Enumerable, attributes.Configurable),
			std::set<JSNamespace>{qualified_name.Namespace} READ_WRITE_ACCESS);
	}
	else
	{
		return false;
	}
}

/*bool JS::Define(JSValue object, JSQualifiedName qualified_name, JSDeclaredType type, Accessor accessor, PropertyAttributes attributes)
{
	if(IsVersionES4())
	{
		GetNonNullObject(object).value()->OverwriteOwnProperty(qualified_name.Id,
			JSProperty::MakeVariable(
				value,
				attributes.Writable,
				attributes.Enumerable,
				attributes.Explicit,
				type)
			WITH_NAMESPACES({qualified_name.Namespace}) READ_WRITE_ACCESS);
		return true;
	}
	else
	{
		return Define(object, qualified_name, value, attributes);
	}
}

bool JS::Define(JSValue object, JSString string, JSDeclaredType type, Accessor accessor, PropertyAttributes attributes)
{
	if(IsVersionES4())
	{
		GetNonNullObject(object).value()->OverwriteOwnProperty(string,
			JSProperty::MakeVariable(
				value,
				attributes.Writable,
				attributes.Enumerable,
				attributes.Explicit,
				type)
			PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		return true;
	}
	else
	{
		return Define(object, string, value, attributes);
	}
}*/

/*bool JS::DefineId(JSValue object, JSPropertyId property_id, Accessor accessor, PropertyAttributes attributes)
{
	return std::visit([&object, accessor, attributes](auto&& key)
	{
		using T = std::decay_t<decltype(key)>;
		if constexpr (std::is_same_v<T, JSPropertyName>)
		{
			return object.Define(key.Id, JSProperty::MakeDynamic(value, attributes.Writable, attributes.Enumerable, attributes.Configurable), std::set<JSNamespace>{key.Namespace} READ_WRITE_ACCESS);
		}
		else if constexpr (std::is_same_v<T, JSSymbol>)
		{
			return object.Define(key, JSProperty::MakeDynamic(value, attributes.Writable, attributes.Enumerable, attributes.Configurable) PUBLIC_NAMESPACES READ_WRITE_ACCESS);
		}
		else
		{
			static_assert(false);
		}
	}, property_id);
}*/
#endif

/*std::vector<JSValue> JS::CreateListFromArrayLike(JSValue arraylike_value, bool property_keys_only)
{
	auto arraylike = arraylike_value.cast_object<JSObject>();
	if(arraylike == nullptr)
	{
		JS::Throw(JS::New<TypeError> __LOC("expected array-like object, received non-object"));
	}
	auto len = JS::ToLength(JS::Get(arraylike, u"length"))
	std::vector<JSValue> list;
	for(uint64_t index = 0; index < len; index++)
	{
		auto indexName = JS::ToString(JSNumber(index) RUNTIME_PHASE);
		auto next = JS::Get(obj, indexName);
		if(property_keys_only && !GetPropertyKey(next))
		{
			JS::Throw(JS::New<TypeError> __LOC("array element not property key"));
		}
		list.push_back(next);
	}
	return list;
}*/

void JS::RequireObjectCoercible(JSValue argument)
{
	if(IsUndefined(argument))
	{
		JS::Throw(JS::New<TypeError> __LOC("expected object coercible, received undefined"));
	}
	else if(IsNull(argument))
	{
		JS::Throw(JS::New<TypeError> __LOC("expected object coercible, received null"));
	}
}

bool JS::CreateDataProperty(JSObject O, JSPropertyKey P, JSValue V)
{
	return O->DefineOwnProperty(
		P,
		JSPropertyDescriptor().withValue(V).withWritable(true).withEnumerable(true).withConfigurable(true)
		PUBLIC_NAMESPACES
		READ_WRITE_ACCESS);
}

void JS::CreateDataPropertyOrThrow(JSObject O, JSPropertyKey P, JSValue V)
{
	if(!CreateDataProperty(O, P, V))
	{
		JS::Throw(JS::New<TypeError> __LOC("unable to create property"));
	}
}

bool JS::ProcessDirective(JS::ParseNode::ParseNode * statement)
{
	if(Version::ES5 <= SelectedVersion)
	{
		if(auto string = dynamic_cast<JS::ParseNode::StringLiteral *>(statement))
		{
			if(string->StringValue == u"use strict")
			{
				std::cerr << "strict mode enabled" << std::endl;
				JS::StrictMode = true;
			}
			else if(string->StringValue == u"use asm")
			{
				// TODO
				std::cerr << "asm.js mode not implemented" << std::endl;
			}
			return true;
		}
		else
		{
			return false;
		}
	}
#if ENABLE_ES4
	else if(SelectedVersion == Version::ES4_2003 || SelectedVersion == Version::ES4_2008)
	{
		if(auto pragma = dynamic_cast<JS::ParseNode::PragmaDeclaration *>(statement))
		{
			for(auto item : pragma->items)
			{
				if(item->name == u"strict")
				{
					if(!item->argument.has_value())
					{
						std::cerr << "strict mode enabled" << std::endl;
						JS::StrictMode = true;
					}
					else if(auto boolean = std::get_if<JSBoolean>(&item->argument.value()))
					{
						if(*boolean)
							std::cerr << "strict mode enabled" << std::endl;
						else
							std::cerr << "strict mode disabled" << std::endl;
						JS::StrictMode = *boolean;
					}
					else if(!item->optional)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"invalid use strict directive"));
					}
				}
				else if(item->name == u"ecmascript")
				{
					if(!item->argument.has_value())
					{
						// already set to ES4
					}
					else if(auto number = std::get_if<JSNumber>(&item->argument.value()))
					{
						if(*number == 1.0)
						{
							std::cerr << "setting to ECMAScript 1" << std::endl;
							CurrentVersion = Version::ES1;
						}
						else if(*number == 2.0)
						{
							std::cerr << "setting to ECMAScript 2" << std::endl;
							CurrentVersion = Version::ES1; // identical
						}
						else if(*number == 3.0)
						{
							std::cerr << "setting to ECMAScript 3" << std::endl;
							CurrentVersion = Version::ES3;
						}
						else if(*number == 4.0)
						{
							std::cerr << "setting to ECMAScript 4" << std::endl;
							CurrentVersion = SelectedVersion == Version::ES4_2008 ? Version::ES4_2008 : Version::ES4_2003;
						}
						else if(*number == 5.0)
						{
							// note: this will likely not work
							std::cerr << "setting to ECMAScript 5" << std::endl;
							CurrentVersion = Version::ES5;
						}
						else if(*number == 6.0)
						{
							// note: this will likely not work
							std::cerr << "setting to ECMAScript 6" << std::endl;
							CurrentVersion = Version::ES6;
						}
						else if(int(*number) == *number && 7.0 <= *number && *number <+ 16.0)
						{
							// note: this will likely not work
							std::cerr << "setting to ESNext" << std::endl;
							CurrentVersion = Version::ESNEXT;
						}
						else if(!item->optional)
						{
							JS::Throw(JS::New<SyntaxError> __LOC(u"invalid use strict directive"));
						}
					}
					else if(!item->optional)
					{
						JS::Throw(JS::New<SyntaxError> __LOC(u"invalid use strict directive"));
					}
				}
				else if(!item->optional)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"invalid use strict directive"));
				}
			}
			return true;
		}
		else
		{
			return false;
		}
	}
#endif
	else
	{
		return false;
	}
}

void JS::ProcessDirectives(std::vector<JS::ParseNode::ParseNode *>& statements)
{
	if(Version::ES5 <= SelectedVersion)
	{
		for(auto statement : statements)
		{
			if(!ProcessDirective(statement))
			{
				break;
			}
		}
	}
#if ENABLE_ES4
	else if(SelectedVersion == Version::ES4_2003 || SelectedVersion == Version::ES4_2008)
	{
		for(auto statement : statements)
		{
			ProcessDirective(statement);
		}
	}
#endif
}

gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> JS::CreateBuiltinFunction(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JSString name, std::optional<JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JSObject> function_prototype, bool has_constructor_method)
{
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> object;
	if(function_prototype.has_value())
	{
		object = JS::NewWithPrototype<JS::Runtime::Builtin::NativeFunction>(function_prototype.value(), realm, name, length, function, has_constructor_method);
	}
	else
	{
		object = JS::New<JS::Runtime::Builtin::NativeFunction>(realm, name, length, function, has_constructor_method);
	}

	if(prototype.has_value())
	{
		object->Define(
			u"prototype",
			JSProperty::MakeData(prototype.value(), /* Writable = */ true, false, false)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}

	return object;
}

gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> JS::CreateBuiltinFunction(std::function<JS::Runtime::PrecompiledFunction> function, uint32_t length, JSString name, std::optional<JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JSObject> function_prototype, bool has_constructor_method)
{
	gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> object;
	if(function_prototype.has_value())
	{
		object = JS::NewWithPrototype<JS::Runtime::Builtin::NativeFunction>(function_prototype.value(), realm, name, length, function, has_constructor_method);
	}
	else
	{
		object = JS::New<JS::Runtime::Builtin::NativeFunction>(realm, name, length, function, has_constructor_method);
	}

	if(prototype.has_value())
	{
		object->Define(
			u"prototype",
			JSProperty::MakeData(prototype.value(), /* Writable = */ true, false, false)
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS);
	}

	return object;
}

gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> JS::CreateBuiltinFunction(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JSString name, JS::Runtime::Realm * realm)
{
	return CreateBuiltinFunction(function, length, name, {}, realm, {}, false);
}

gcn::gc_ptr<JS::Runtime::Builtin::NativeFunction> JS::CreateBuiltinConstructor(std::function<JS::Runtime::BuiltinFunction> function, uint32_t length, JS::Public::JSString name, std::optional<JS::Public::JSObject> prototype, JS::Runtime::Realm * realm, std::optional<JS::Public::JSObject> function_prototype)
{
	return CreateBuiltinFunction(function, length, name, prototype, realm, function_prototype, true);
}

JSSymbol JS::NewSymbol()
{
	return new JS::Runtime::SymbolInstance();
}

JSSymbol JS::NewSymbol(JSString description)
{
	return new JS::Runtime::SymbolInstance(description);
}

JSString JS::ObjectToString(JSValue value AND_PHASE(phase))
{
	return u"[object " + value.ToStringTag() + u"]";
}

#if ENABLE_ES4
JSString JS::GetTypeNameString(JSType value)
{
	return std::visit([](auto&& type)
	{
		using T = std::decay_t<decltype(type)>;
		if constexpr(std::is_same_v<T, JSClass>)
		{
			return type->Name;
		}
		else if constexpr(std::is_same_v<T, JSTypeApplication>)
		{
			JSString result = type.parametrized_type->Name + u".<";
			bool started = false;
			for(auto parameter : type.parameters)
			{
				if(started)
					result += u", ";
				else
					started = true;
				result += GetTypeNameString(parameter);
			}
			return result + u">";
		}
		else
		{
			static_assert(false);
		}
	}, value);
}
#endif

JS::Runtime::Agent * JS::GetSurroundingAgent()
{
	return JS::Runtime::Agent::CurrentAgent;
}

JS::Runtime::ExecutionContext * JS::GetCurrentExecutionContext()
{
	if(Runtime::ExecutionContext::GetContextStack().size() == 0)
		return nullptr;
	else
		return Runtime::ExecutionContext::GetContextStack().back();
}

JS::Runtime::Realm * JS::GetCurrentRealm()
{
	if(Runtime::ExecutionContext::GetContextStack().size() == 0)
		return nullptr;
	else
		return Runtime::ExecutionContext::GetContextStack().back()->Realm;
}

JS::Runtime::Intrinsics * JS::Intrinsics()
{
	if(Runtime::ExecutionContext::GetContextStack().size() == 0)
		return nullptr;
	else if(Runtime::ExecutionContext::GetContextStack().back()->Realm == nullptr)
		return nullptr;
	else
		return &Runtime::ExecutionContext::GetContextStack().back()->Realm->Intrinsics;
}

JS::Runtime::ScriptOrModuleRecord * JS::GetActiveScriptOrModule()
{
	for(auto stack_iterator = Runtime::ExecutionContext::GetContextStack().rbegin();
		stack_iterator != Runtime::ExecutionContext::GetContextStack().rend();
		stack_iterator ++)
	{
		if(auto scriptOrModule = (*stack_iterator)->ScriptOrModule)
		{
			return scriptOrModule;
		}
	}

	return nullptr;
}

JSObject JS::GetGlobalObject()
{
	if(Runtime::ExecutionContext::GetContextStack().size() == 0)
		return nullptr;
	else if(Runtime::ExecutionContext::GetContextStack().back()->Realm == nullptr)
		return nullptr;
	else
		return Runtime::ExecutionContext::GetContextStack().back()->Realm->GlobalObject;
}

JSFunction JS::GetSpeciesConstructor(JSObject object, JSFunction default_value)
{
	auto constructor = JS::Get(object, u"constructor");
	if(GetNonNullObject(constructor))
	{
		auto species = JS::Get(constructor, Symbols::species);
		if(IsUndefined(species) || IsNull(species))
		{
			return default_value;
		}
		if(auto species_constructor = GetConstructor(species))
		{
			return species_constructor.value();
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("constructor species is not a constructor"));
		}
	}
	else
	{
		return default_value;
	}
}

