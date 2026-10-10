
// TODO: simplify includes
// TODO: rename to scope.cc?

//#include <bit>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <gmpxx.h>
#include <optional>
#include <sstream>
#include <stdfloat>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "runtime.hh"
#include "execute.hh" // TODO: move ExecutionContext to execute.cc?
#include "environment.hh"
#include "modules.hh"
#include "parser.hh" // required to check character types
#include "ast.hh" // required for Functional related behavior
#include "builtins.hh"
#include "debugger.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// IdentifierReference

bool JS::Runtime::IdentifierReference::IsName()
{
	return std::get_if<JSString>(&PropertyKey);
}

JSString JS::Runtime::IdentifierReference::GetName()
{
	if(auto name = std::get_if<JSString>(&PropertyKey))
	{
		return *name;
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("cannot convert symbol to string"));
	}
}

#if ENABLE_ES4
std::set<JSNamespace> JS::Runtime::IdentifierReference::GetNamespaces(ReferenceContext cxt)
{
	if(Namespace.has_value())
	{
		return {Namespace.value()};
	}
	else
	{
		return cxt.OpenNamespaces;
	}
}
#endif

JSString JS::Runtime::IdentifierReference::ToString(FOR_PHASE(phase))
{
	JSString name = GetName();
	if(Namespace.has_value())
	{
		if(Namespace.value() == WildcardNamespace)
		{
			name = u"*::" + name;
		}
		else
		{
			name = Namespace.value()->uri + u"::" + name;
		}
	}
	if(Attribute)
	{
		name = u"@" + name;
	}
	if(Private)
	{
		name = u"#" + name;
	}
	return name;
}

JS::Class::QName * JS::Runtime::IdentifierReference::ToQName()
{
	if(Attribute || Private)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(!Namespace.has_value())
	{
		return new JS::Class::QName(u"", GetName(), u"");
	}
	else if(Namespace.has_value() && Namespace.value() != WildcardNamespace)
	{
		return new JS::Class::QName(Namespace.value()->uri, GetName(), Namespace.value()->prefix);
	}
	else
	{
		return new JS::Class::QName(GetName());
	}
}

//// EnvironmentRecord

bool JS::Runtime::EnvironmentRecord::HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	return HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
}

JSValue JS::Runtime::EnvironmentRecord::BindThisValue([[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<InternalError> __LOC(u"environment cannot have this-binding"));
}

JSValue JS::Runtime::EnvironmentRecord::GetThisBinding()
{
	JS::Throw(JS::New<InternalError> __LOC(u"environment cannot have this-binding"));
}

JSValue JS::Runtime::EnvironmentRecord::GetSuperBase()
{
	JS::Throw(JS::New<InternalError> __LOC(u"environment cannot have super base"));
}

#if ENABLE_ES4
std::shared_ptr<JS::Runtime::ClassEnvironmentRecord> JS::Runtime::EnvironmentRecord::GetClassEnvironment()
{
	for(auto env = shared_from_this(); env != nullptr; env = env->OuterEnvironment)
	{
		if(auto class_env = std::dynamic_pointer_cast<JS::Runtime::ClassEnvironmentRecord>(env))
		{
			return class_env;
		}
	}
	//JS::Throw(JS::New<InternalError> __LOC());
	return nullptr;
}
#endif

JSNamespace JS::Runtime::EnvironmentRecord::GetDefaultNamespace()
{
	return OuterEnvironment->GetDefaultNamespace();
}

//// DeclarativeEnvironmentRecord

bool JS::Runtime::DeclarativeEnvironmentRecord::HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	// reference.Namespace is ignored
	assert(!reference.Private);
	if(reference.Attribute)
	{
		return false;
	}

	return bindings.Get(reference.GetName() WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)).has_value();
}

void JS::Runtime::DeclarativeEnvironmentRecord::CreateHoistedBinding(JSString name, JSValue value)
{
	// TODO: ParameterFrame behaves differently
	if(HasBinding(JSIdentifierReference(name) PUBLIC_NAMESPACES READ_WRITE_ACCESS COMPILE_TIME_PHASE))
	{
		JS::Throw(JS::New<InternalError> __LOC(u"redefining hoisted name " + name + u""));
	}

	bindings.Set(name PUBLIC_NAMESPACES READ_WRITE_ACCESS, JSBinding::MakeMutable(value, /* Deletable = */ false));
}

void JS::Runtime::DeclarativeEnvironmentRecord::SetHoistedBinding(JSString name, JSValue value)
{
	auto binding_opt = bindings.Get(name PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	if(!binding_opt)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"setting unassigned hoisted name " + name + u""));
	}
	auto binding = binding_opt.value();
#if ENABLE_ES4
	if(binding->PropertyType != JSProperty::Type::Dynamic)
	{
		if(IsVersionES4())
		{
			JS::Throw(JS::New<DefinitionError> __LOC(u"hoisted definition conflicts with non-hoisted one for " + name + u""));
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC(u"abnormal binding to be assigned to by hoisted binding"));
		}
	}
	if(!IsVersionES4() && !binding->IsWritable())
	{
		JS::Throw(JS::New<TypeError> __LOC(u"setting immutable binding"));
	}
	if(!binding->SetValue(value))
	{
		JS::Throw(JS::New<InternalError> __LOC(u"error setting binding"));
	}
	binding->Configurable = false; // TODO: is this redundant?
	binding->Enumerable = true; // TODO: is this redundant?
#else
	switch(binding->type)
	{
	case JSBinding::BindingType::Strict:
		JS::Throw(JS::New<TypeError> __LOC(u"setting immutable binding"));
	case JSBinding::BindingType::Immutable:
		break;
	case JSBinding::BindingType::Mutable:
	case JSBinding::BindingType::Deletable:
		binding->SetValue(value);
		break;
	}
#endif
}

JS::Runtime::Result JS::Runtime::DeclarativeEnvironmentRecord::SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces))
{
	// reference.Namespace is ignored
	assert(!reference.Private);
	if(reference.Attribute)
	{
		return Result::ReferenceError;
	}

	// must be checked beforehand
	/*if(!HasBinding(name, attribute WITH_NAMESPACES(namespaces) WRITE_ACCESS RUNTIME_PHASE))
	{
		return Result::ReferenceError;
	}*/

	auto binding_opt = bindings.Get(reference.GetName() PUBLIC_NAMESPACES READ_WRITE_ACCESS);
	if(!binding_opt)
	{
		// assuming non-strict
		bindings.Set(reference.GetName() PUBLIC_NAMESPACES READ_WRITE_ACCESS, JSBinding::MakeMutable(value, /* Deletable = */ false));
		return Result::Success;
	}
	auto binding = binding_opt.value();
#if ENABLE_ES4
	if(!IsVersionES4() && !binding->IsInitialized())
	{
		return Result::ReferenceError;
	}

	switch(binding->PropertyType)
	{
	case JSProperty::Type::Dynamic:
	case JSProperty::Type::Variable:
		switch(binding->Mutable)
		{
		case JSProperty::Mutability::Mutable:
			break;
		case JSProperty::Mutability::Immutable:
			if(IsVersionES4())
			{
				return Result::ReferenceError;
			}
			else
			{
				return Result::TypeError;
			}
		case JSProperty::Mutability::Strict_Immutable:
			if(IsVersionES4())
			{
				JS::Throw(JS::New<ReferenceError> __LOC(u"cannot initialize const variable twice"));
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC(u"assigning to immutable variable")); // even in non-strict mode
			}
		}
		break;
	case JSProperty::Type::Accessor:
		//if(!binding->Set)
		//{
		//	return false;
		//}
		//break;
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal variable type for assignment"));
	case JSProperty::Type::Getter:
		//return false;
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal variable type for assignment"));
	case JSProperty::Type::Setter:
		break;
	case JSProperty::Type::Forbidden:
		JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
	}

	if(binding->SetValue(value) || !IsVersionES4())
	{
		return Result::Success;
	}
	else
	{
		return Result::TypeError;
	}
#else
	switch(binding->type)
	{
	case JSBinding::BindingType::Strict:
		JS::Throw(JS::New<TypeError> __LOC(u"assigning to immutable variable")); // even in non-strict mode
	case JSBinding::BindingType::Immutable:
		return Result::TypeError;
	case JSBinding::BindingType::Mutable:
	case JSBinding::BindingType::Deletable:
		binding->SetValue(value);
		return Result::Success;
	default:
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal binding type"));
	}
#endif
}

void JS::Runtime::DeclarativeEnvironmentRecord::CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding)
{
	// TODO: check explicit, only allowed for package frames
	if(HasBinding(JSIdentifierReference(name) WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) COMPILE_TIME_PHASE))
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			JS::Throw(JS::New<DefinitionError> __LOC(u"duplicate definition in the same scope for " + name + u""));
		}
		else
#endif
		{
			JS::Throw(JS::New<InternalError> __LOC(u"duplicate definition for " + name + u""));
		}
	}
	// TODO: check the regional environment for clashes
	if(!bindings.Set(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), binding))
	{
		JS::Throw(JS::New<InternalError> __LOC(u"unable to create singleton binding"));
	}
	// TODO: set forbidden bindings?
}

#if ENABLE_ES4
void JS::Runtime::DeclarativeEnvironmentRecord::ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	auto binding_opt = bindings.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(!binding_opt)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"attempt to initialize undefined singleton variable " + name + u""));
	}
	auto binding = binding_opt.value();
	if(binding->PropertyType != JSProperty::Type::Variable)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"abnormal property type for singleton initialization " + name + u""));
	}
	binding->Initialize(JS::Runtime::ExecutionPhase::Run);
}

void JS::Runtime::DeclarativeEnvironmentRecord::SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto binding = bindings.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		if(binding.value()->PropertyType == JSProperty::Type::Variable)
		{
			binding.value()->Type = type;
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC(u"abnormal property type for singleton type assignment for " + name + u""));
		}
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC(u"non-existent property for singleton type assignment for " + name + u""));
	}
}

void JS::Runtime::DeclarativeEnvironmentRecord::SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto binding = bindings.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		if(binding.value()->PropertyType == JSProperty::Type::Variable)
		{
			binding.value()->SetValue(value);
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC(u"abnormal property type for singleton assignment for " + name + u""));
		}
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC(u"non-existent property for singleton assignment for " + name + u""));
	}
}
#endif

// TODO: error messages

void JS::Runtime::DeclarativeEnvironmentRecord::InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value)
{
	auto binding_opt = bindings.Get(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	if(!binding_opt)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"missing binding for singleton initialization"));
	}
	auto binding = binding_opt.value();
	if(binding->IsInitialized())
	{
		JS::Throw(JS::New<InternalError> __LOC(u"double initialization of singleton binding"));
	}
	binding->SetValue(value);
}

JSValue JS::Runtime::DeclarativeEnvironmentRecord::GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase))
{
	// reference.Namespace is ignored
	assert(!reference.Private);
	if(reference.Attribute)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	auto binding_opt = bindings.Get(reference.GetName() WITH_NAMESPACES(namespaces) READ_ACCESS);
	if(!binding_opt)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto binding = binding_opt.value();
#if ENABLE_ES4
	switch(binding->PropertyType)
	{
	case JSProperty::Type::Dynamic:
		if(phase == JS::Runtime::ExecutionPhase::Compile)
		{
			JS::Throw(JS::New<ConstantError> __LOC(u"accessing dynamic variable before compile time"));
		}
		if(!binding->IsInitialized())
		{
			JS::Throw(JS::New<ReferenceError> __LOC(u"accessing binding before initialization"));
		}
		else
		{
			return binding->GetValue();
		}
	case JSProperty::Type::Variable:
		if(!IsVersionES4())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		if(phase == JS::Runtime::ExecutionPhase::Compile && binding->Mutable == JSProperty::Mutability::Mutable)
		{
			JS::Throw(JS::New<ConstantError> __LOC());
		}
		if(!binding->IsInitialized())
		{
			if(binding->Mutable == JSProperty::Mutability::Mutable)
			{
				JS::Throw(JS::New<UninitializedError> __LOC());
			}
			else if(binding->Initializer)
			{
				binding->Initialize(JS::Runtime::ExecutionPhase::Compile);
			}
			else
			{
				switch(phase)
				{
				case JS::Runtime::ExecutionPhase::Compile:
					JS::Throw(JS::New<ConstantError> __LOC());
				case JS::Runtime::ExecutionPhase::Run:
					JS::Throw(JS::New<UninitializedError> __LOC());
				default:
					JS::Throw(JS::New<InternalError> __LOC());
				}
			}
		}
		return binding->GetValue();
	case JSProperty::Type::Accessor:
	case JSProperty::Type::Setter:
	default:
		JS::Throw(JS::New<InternalError> __LOC());
	case JSProperty::Type::Getter:
		if(!IsVersionES4())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		return binding->Get.Call(JS_undefined /* TODO: should raise ReferenceError instead of silently passing through */, {} WITH_PHASE(phase));
	}
#else
	if(!binding->IsInitialized())
	{
		JS::Throw(JS::New<ReferenceError> __LOC());
	}
	else
	{
		return binding->GetValue();
	}
#endif
}

bool JS::Runtime::DeclarativeEnvironmentRecord::DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces))
{
	// reference.Namespace is ignored
	assert(!reference.Private);
	if(reference.Attribute)
	{
		return false;
	}

	if(auto binding_opt = bindings.Get(reference.GetName() WITH_NAMESPACES(namespaces) WRITE_ACCESS))
	{
#if ENABLE_ES4
		if(!binding_opt.value()->Configurable)
		{
			return false;
		}
#else
		if(binding_opt.value()->type != JSBinding::BindingType::Deletable)
		{
			return false;
		}
#endif
	}
	return bindings.Delete(reference.GetName() WITH_NAMESPACES(namespaces));
}

bool JS::Runtime::DeclarativeEnvironmentRecord::HasThisBinding()
{
	return false;
}

bool JS::Runtime::DeclarativeEnvironmentRecord::HasSuperBinding()
{
	return false;
}

std::optional<JSObject> JS::Runtime::DeclarativeEnvironmentRecord::WithBaseObject()
{
	return {};
}

//// ObjectEnvironmentRecord

JS::Runtime::ObjectEnvironmentRecord::ObjectEnvironmentRecord(JSObject BindingObject, bool WithEnvironment, std::shared_ptr<EnvironmentRecord> OuterEnvironment)
	: EnvironmentRecord(OuterEnvironment), BindingObject(BindingObject), WithEnvironment(WithEnvironment)
{
	if(BindingObject == nullptr)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

bool JS::Runtime::ObjectEnvironmentRecord::HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	auto reference_option = BindingObject->CreateReference(reference, JS::Runtime::ReferenceContext(false WITH_NAMESPACES(namespaces) WITH_PHASE(phase))).get_if<JS::Runtime::MemberReference>();
	if(!reference_option)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto key = assert_property_key(reference_option.value().ReferencedName);

#if ENABLE_ES4
	if(IsVersionES4() && std::dynamic_pointer_cast<JS::Class::Class>(BindingObject))
	{
		if(!BindingObject->HasOwnProperty(key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
		{
			return false;
		}
	}
	else
#endif
	if(!BindingObject->HasProperty(key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase)))
	{
		return false;
	}
	if(!WithEnvironment)
	{
		return true;
	}
	auto unscopables = Get(BindingObject, JS::Symbols::unscopables);
	if(auto object = JS::GetNonNullObject(unscopables))
	{
		if(auto property_key = GetPropertyKey(key))
		{
			if(!IsUndefined(GetByKey(object.value(), property_key.value())))
			{
				return false;
			}
		}
	}
	return true;
}

#if ENABLE_ES4
bool JS::Runtime::ObjectEnvironmentRecord::HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(IsVersionES4())
	{
		if(auto package = std::dynamic_pointer_cast<JS::Class::Package>(BindingObject))
		{
			auto reference_option = BindingObject->CreateReference(reference, JS::Runtime::ReferenceContext(false WITH_NAMESPACES(namespaces) WITH_PHASE(phase))).get_if<JS::Runtime::MemberReference>();
			if(!reference_option)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			auto key = assert_property_key(reference_option.value().ReferencedName);

			return package->HasActualProperty(key WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
		}
	}

	return HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
}
#endif

void JS::Runtime::ObjectEnvironmentRecord::CreateHoistedBinding(JSString name, JSValue value)
{
	if(WithEnvironment)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	// ES6 GlobalEnvironment and ES4 package frame
	if(!BindingObject->DefineOwnProperty(name, JSPropertyDescriptor().withValue(value).withWritable(true).withEnumerable(true).withConfigurable(false) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

void JS::Runtime::ObjectEnvironmentRecord::SetHoistedBinding(JSString name, JSValue value)
{
	if(WithEnvironment)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	// ES6 GlobalEnvironment and ES4 package frame
	if(!BindingObject->Set(name, value, BindingObject PUBLIC_NAMESPACES WITH_CLASS(BindingObject->GetClass()) NO_ENVIRONMENT))
	{
		JS::Throw(JS::New<InternalError> __LOC()); // TODO: is this the correct behavior?
	}
}

JS::Runtime::Result JS::Runtime::ObjectEnvironmentRecord::SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces))
{
	assert(!reference.Private);
	assert(!reference.Attribute);
	// TODO: ES4 permits the with binding to be empty
	if(BindingObject->Set(reference.GetName(), value, BindingObject WITH_NAMESPACES(namespaces) WITH_CLASS(BindingObject->GetClass()) WITH_ENVIRONMENT(GetCurrentExecutionContext()->LexicalEnvironment)))
	{
		return Result::Success;
	}
#if ENABLE_ES4
	else if(IsVersionES4())
	{
		return Result::ReferenceError;
	}
#endif
	else
	{
		return Result::TypeError;
	}
}

void JS::Runtime::ObjectEnvironmentRecord::CreateSingletonBinding([[maybe_unused]] JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), [[maybe_unused]] JSBinding binding)
{
#if ENABLE_ES4
	// object environments cannot hold let/const declarations in ES6+
	if(IsVersionES4())
	{
		if(binding.Explicit)
		{
			if(!std::dynamic_pointer_cast<JS::Class::Package>(BindingObject))
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}
		}

		for(auto environment = GetCurrentExecutionContext()->LexicalEnvironment;
			environment != nullptr;
			environment = environment->OuterEnvironment)
		{
			bool duplicate = false;
			if(environment->HasBinding(JSIdentifierReference(name) WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) COMPILE_TIME_PHASE))
			{
				duplicate = true;
				if(auto global_env = std::dynamic_pointer_cast<JS::Runtime::GlobalEnvironmentRecord>(environment))
				{
					if(auto package = std::dynamic_pointer_cast<JS::Class::Package>(global_env->ObjectRecord.BindingObject))
					{
						if(!package->HasActualProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) COMPILE_TIME_PHASE))
						{
							// allow overriding imported names
							duplicate = false;
						}
					}
				}
			}

			if(duplicate)
			{
				JS::Throw(JS::New<DefinitionError> __LOC("duplicate definition in the same scope"));
			}

			// TODO: improve check
			if(std::dynamic_pointer_cast<FunctionEnvironmentRecord>(environment)
			|| std::dynamic_pointer_cast<GlobalEnvironmentRecord>(environment))
			{
				break;
			}
		}

		// TODO: check does not share name with class?
		// TODO: check forbidden bindings
		BindingObject->Define(name, binding WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		// TODO: set forbidden bindings?
	}
	else
#endif
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

#if ENABLE_ES4
void JS::Runtime::ObjectEnvironmentRecord::ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(auto property_reference = BindingObject->GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		if(property_reference.value().PropertyType != JSProperty::Type::Variable
		|| property_reference.value().direct_access == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		property_reference.value().direct_access->Initialize(JS::Runtime::ExecutionPhase::Run);
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

void JS::Runtime::ObjectEnvironmentRecord::SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(WithEnvironment)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(auto binding = BindingObject->GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		if(binding.value().PropertyType != JSProperty::Type::Variable
		|| binding.value().direct_access == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		binding.value().direct_access->Type = type;
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

void JS::Runtime::ObjectEnvironmentRecord::SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(WithEnvironment)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	if(auto binding = BindingObject->GetOwnProperty(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses)))
	{
		if(binding.value().PropertyType != JSProperty::Type::Variable
		|| binding.value().direct_access == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		binding.value().direct_access->SetValue(value);
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}
#endif

void JS::Runtime::ObjectEnvironmentRecord::InitializeSingletonBinding([[maybe_unused]] JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), [[maybe_unused]] JSValue value)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		SetSingletonBindingValue(name, value WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	else
#endif
	{
		// only required for ES6, but object environments do not get singleton bindings
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

JSValue JS::Runtime::ObjectEnvironmentRecord::GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase))
{
	auto reference_option = BindingObject->CreateReference(reference, JS::Runtime::ReferenceContext(false WITH_NAMESPACES(namespaces) WITH_PHASE(phase))).get_if<JS::Runtime::MemberReference>();
	if(!reference_option)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto key = assert_property_key(reference_option.value().ReferencedName);

	return BindingObject->Get(key, BindingObject, Intent::Read WITH_NAMESPACES(namespaces) WITH_CLASS(BindingObject->GetClass())
		WITH_ENVIRONMENT(GetCurrentExecutionContext()->LexicalEnvironment) WITH_PHASE(phase));
}

bool JS::Runtime::ObjectEnvironmentRecord::DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces))
{
	auto reference_option = BindingObject->CreateReference(reference, JS::Runtime::ReferenceContext(false WITH_NAMESPACES(namespaces) RUNTIME_PHASE)).get_if<JS::Runtime::MemberReference>();
	if(!reference_option)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto key = assert_property_key(reference_option.value().ReferencedName);

	return BindingObject->Delete(key WITH_NAMESPACES(namespaces) WITH_CLASS(BindingObject->GetClass()) WITH_FLAG(true));
}

bool JS::Runtime::ObjectEnvironmentRecord::HasThisBinding()
{
	return false;
}

bool JS::Runtime::ObjectEnvironmentRecord::HasSuperBinding()
{
	return false;
}

std::optional<JSObject> JS::Runtime::ObjectEnvironmentRecord::WithBaseObject()
{
	if(WithEnvironment)
	{
		return std::make_optional<>(BindingObject);
	}
	else
	{
		return {};
	}
}

JSNamespace JS::Runtime::ObjectEnvironmentRecord::GetDefaultNamespace()
{
	if(auto xml_object = std::dynamic_pointer_cast<JS::Class::XML>(BindingObject))
	{
		if(xml_object->DefaultNamespace.has_value())
		{
			return xml_object->DefaultNamespace.value();
		}
	}
	return OuterEnvironment->GetDefaultNamespace();
}

//// FunctionEnvironmentRecord

JS::Runtime::FunctionEnvironmentRecord::FunctionEnvironmentRecord(
	gcn::gc_ptr<JS::Class::Function> FunctionObject,
	std::optional<JSFunction> NewTarget)
		: FunctionEnvironmentRecord(
			FunctionObject,
			FunctionObject->ThisMode == Class::Function::ThisModeType::Lexical
				? JS::Runtime::FunctionEnvironmentRecord::BindingStatus::Lexical
				: JS::Runtime::FunctionEnvironmentRecord::BindingStatus::Uninitialized,
			NewTarget,
			FunctionObject->Environment)
{
}

bool JS::Runtime::FunctionEnvironmentRecord::HasThisBinding()
{
	if(CurrentVersion < Version::ES6)
	{
		// note: ES4 will return all function environments, but those that are set to Static ("Lexical") will not provide a valid "this" value
		// this permits the GetThisEnvironment routine to stop and the GetThisBinding to fail
		return true;
	}
	else
	{
		return ThisBindingStatus != BindingStatus::Lexical;
	}
}

bool JS::Runtime::FunctionEnvironmentRecord::HasSuperBinding()
{
	if(CurrentVersion < Version::ES6)
	{
		return false;
	}
	else if(ThisBindingStatus == BindingStatus::Lexical)
	{
		return false;
	}
	else
	{
		return FunctionObject->HomeObject != nullptr;
	}
}

JSValue JS::Runtime::FunctionEnvironmentRecord::BindThisValue(JSValue value)
{
	if(ThisBindingStatus == BindingStatus::Initialized)
	{
		JS::Throw(JS::New<ReferenceError> __LOC());
	}
	ThisValue = value;
	ThisBindingStatus = BindingStatus::Initialized;
	return value;
}

JSValue JS::Runtime::FunctionEnvironmentRecord::GetThisBinding()
{
	if(ThisBindingStatus == BindingStatus::Uninitialized)
	{
		JS::Throw(JS::New<ReferenceError> __LOC(u"uninitialized 'this' binding"));
	}
#if ENABLE_ES4
	if(IsVersionES4() && ThisBindingStatus == BindingStatus::Static)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"plain function does not define 'this'"));
	}
#endif
	return ThisValue;
}

JSValue JS::Runtime::FunctionEnvironmentRecord::GetSuperBase()
{
	if(FunctionObject->HomeObject == nullptr)
	{
		return JS_undefined;
	}
	else
	{
		auto prototype = JS::GetNonNullObject(FunctionObject->HomeObject).value()->GetPrototypeOf();
		if(prototype.has_value())
		{
			return prototype.value();
		}
		else
		{
			return JS_undefined;
		}
	}
}

JSNamespace JS::Runtime::FunctionEnvironmentRecord::GetDefaultNamespace()
{
	if(DefaultNamespace.has_value())
	{
		return DefaultNamespace.value();
	}
	return OuterEnvironment->GetDefaultNamespace();
}

//// ClassEnvironmentRecord

#if ENABLE_ES4
JS::Runtime::ClassEnvironmentRecord::ClassEnvironmentRecord(JSClass class_, std::shared_ptr<EnvironmentRecord> OuterEnvironment)
	: GlobalEnvironmentRecord(JSObject(class_), JSObject(class_), OuterEnvironment)
{
}

JSClass JS::Runtime::ClassEnvironmentRecord::GetClass()
{
	return std::static_pointer_cast<JS::Class::Class>(ObjectRecord.BindingObject);
}

JSString JS::Runtime::ClassEnvironmentRecord::GetClassName()
{
	return GetClass()->Name;
}

std::optional<JSInstanceProperty> JS::Runtime::ClassEnvironmentRecord::CreateInstanceBinding(JSString name, std::set<JSNamespace> namespaces, std::set<JSNamespace> open_namespaces, JS::Runtime::OverrideModifier override, [[maybe_unused]] /* TODO */ bool explicit_, JSInstanceProperty instance_property)
{
	AccessSet accesses = instance_property->GetAccessSet();
	std::set<JSNamespace> defined_namespaces = namespaces.size() == 0 ? std::set<JSNamespace>{JS_public} : namespaces;
	std::set<JSNamespace> searched_namespaces = namespaces.size() == 0 ? open_namespaces : namespaces;

	std::optional<JSInstanceProperty> overridden_property;
	std::optional<JSInstanceProperty> base_property =
		GetClass()->SearchForOverrides(name, searched_namespaces, accesses);

	if(base_property.has_value())
	{
		overridden_property = GetClass()->GetDerivedInstanceProperty(name, base_property.value()->namespaces, accesses).value();
		if(!std::includes(
			overridden_property.value()->namespaces.begin(), overridden_property.value()->namespaces.end(),
			namespaces.begin(), namespaces.end()))
		{
			JS::Throw(JS::New<DefinitionError> __LOC(u"attempt to extend namespaces"));
		}

		defined_namespaces = overridden_property.value()->namespaces;

		if(auto function = dynamic_cast<JSInstanceFunction>(overridden_property.value()))
		{
			if(auto new_function = dynamic_cast<JSInstanceFunction>(instance_property))
			{
				if(function->InstanceType != new_function->InstanceType)
				{
					JS::Throw(JS::New<DefinitionError> __LOC(u"cannot override incompatible functions (getter/setter/method)"));
				}
			}
			else
			{
				JS::Throw(JS::New<DefinitionError> __LOC(u"cannot override function with variable"));
			}
		}
		else
		{
			// overrides a variable
			if(auto function = dynamic_cast<JSInstanceFunction>(instance_property))
			{
				if(function->InstanceType == JS::Runtime::InstanceFunction::Type::Method)
				{
					JS::Throw(JS::New<DefinitionError> __LOC(u"cannot override variable with function"));
				}
			}
		}

		if(overridden_property.value()->Final)
		{
			JS::Throw(JS::New<DefinitionError> __LOC(u"cannot override final property"));
		}
	}

	switch(override)
	{
	case JS::Runtime::OverrideModifier::None:
		if(base_property.has_value())
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		else if(GetClass()->SearchForOverrides(name, open_namespaces, accesses).has_value())
		{
			JS::Throw(JS::New<DefinitionError> __LOC(u"hides a visible member in a separate namespace"));
		}
		break;
	case JS::Runtime::OverrideModifier::False:
		if(base_property.has_value())
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		break;
	case JS::Runtime::OverrideModifier::True:
		if(!base_property.has_value())
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		break;
	case JS::Runtime::OverrideModifier::Undefined:
		break;
	}

	instance_property->name = name;
	instance_property->namespaces = defined_namespaces;

	auto target_class = GetClass();
	target_class->InstanceMembers.Set(name, defined_namespaces, accesses, instance_property);

	if(defined_namespaces.find(JS_public) != defined_namespaces.end()
	&& std::find(target_class->instance_member_names.begin(),
		target_class->instance_member_names.end(),
		name) == target_class->instance_member_names.end())
	{
		target_class->instance_member_names.push_back(name);
	}

	return overridden_property;
}

void JS::Runtime::ClassEnvironmentRecord::CreateConstructorBinding(JSFunction instance_method)
{
	if(GetClass()->Constructor != nullptr)
	{
		JS::Throw(JS::New<DefinitionError> __LOC());
	}
	GetClass()->Constructor = instance_method;
	if(auto function = std::dynamic_pointer_cast<JS::Class::Function>(GetNonNullObject(instance_method).value_or(nullptr)))
	{
		function->ClassBeingConstructed = GetClass();
	}
}
#endif

//// GlobalEnvironmentRecord

bool JS::Runtime::GlobalEnvironmentRecord::HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(Version::ES6 <= CurrentVersion && DeclarativeRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase)))
	{
		return true;
	}
	else
	{
		return ObjectRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
	}
}

#if ENABLE_ES4
bool JS::Runtime::GlobalEnvironmentRecord::HasActualBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	if(Version::ES6 <= CurrentVersion && DeclarativeRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase)))
	{
		return true;
	}
	else
	{
		return ObjectRecord.HasActualBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
	}
}
#endif

void JS::Runtime::GlobalEnvironmentRecord::CreateHoistedBinding(JSString name, JSValue value)
{
	ObjectRecord.CreateHoistedBinding(name, value);
}

void JS::Runtime::GlobalEnvironmentRecord::SetHoistedBinding(JSString name, JSValue value)
{
	ObjectRecord.SetHoistedBinding(name, value);
}

JS::Runtime::Result JS::Runtime::GlobalEnvironmentRecord::SetMutableBinding(JSIdentifierReference reference, JSValue value AND_NAMESPACES(namespaces))
{
	if(Version::ES6 <= CurrentVersion && DeclarativeRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WRITE_ACCESS RUNTIME_PHASE))
	{
		return DeclarativeRecord.SetMutableBinding(reference, value WITH_NAMESPACES(namespaces));
	}
	else
	{
		return ObjectRecord.SetMutableBinding(reference, value WITH_NAMESPACES(namespaces));
	}
}

void JS::Runtime::GlobalEnvironmentRecord::CreateSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSBinding binding)
{
	if(Version::ES6 <= CurrentVersion)
	{
		DeclarativeRecord.CreateSingletonBinding(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), binding);
	}
	else
	{
		// TODO: only ES4
		ObjectRecord.CreateSingletonBinding(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), binding);
	}
}

#if ENABLE_ES4
void JS::Runtime::GlobalEnvironmentRecord::ExecuteSingletonBindingInitializer(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(Version::ES6 <= CurrentVersion)
	{
		DeclarativeRecord.ExecuteSingletonBindingInitializer(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	else
	{
		// TODO: only ES4
		ObjectRecord.ExecuteSingletonBindingInitializer(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}

void JS::Runtime::GlobalEnvironmentRecord::SetSingletonBindingType(JSString name, JSType type AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(Version::ES6 <= CurrentVersion)
	{
		DeclarativeRecord.SetSingletonBindingType(name, type WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	else
	{
		// TODO: only ES4
		ObjectRecord.SetSingletonBindingType(name, type WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}

void JS::Runtime::GlobalEnvironmentRecord::SetSingletonBindingValue(JSString name, JSValue value AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses))
{
	if(Version::ES6 <= CurrentVersion)
	{
		DeclarativeRecord.SetSingletonBindingValue(name, value WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
	else
	{
		// TODO: only ES4
		ObjectRecord.SetSingletonBindingValue(name, value WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
	}
}
#endif

void JS::Runtime::GlobalEnvironmentRecord::InitializeSingletonBinding(JSString name AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses), JSValue value)
{
	if(Version::ES6 <= CurrentVersion)
	{
		DeclarativeRecord.InitializeSingletonBinding(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), value);
	}
	else
	{
		// TODO: only ES4
		ObjectRecord.InitializeSingletonBinding(name WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses), value);
	}
}

JSValue JS::Runtime::GlobalEnvironmentRecord::GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase))
{
	if(Version::ES6 <= CurrentVersion && DeclarativeRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WRITE_ACCESS WITH_PHASE(phase)))
	{
		return DeclarativeRecord.GetBindingValue(reference WITH_NAMESPACES(namespaces) WITH_PHASE(phase));
	}
	else
	{
		return ObjectRecord.GetBindingValue(reference WITH_NAMESPACES(namespaces) WITH_PHASE(phase));
	}
}

bool JS::Runtime::GlobalEnvironmentRecord::DeleteBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces))
{
	if(Version::ES6 <= CurrentVersion && DeclarativeRecord.HasBinding(reference WITH_NAMESPACES(namespaces) WRITE_ACCESS RUNTIME_PHASE))
	{
		return DeclarativeRecord.DeleteBinding(reference WITH_NAMESPACES(namespaces));
	}
	else
	{
		auto reference_option = ObjectRecord.BindingObject->CreateReference(reference, JS::Runtime::ReferenceContext(false WITH_NAMESPACES(namespaces) RUNTIME_PHASE)).get_if<JS::Runtime::MemberReference>();
		if(!reference_option)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		auto key = assert_property_key(reference_option.value().ReferencedName);

		if(ObjectRecord.BindingObject->GetOwnProperty(key WITH_NAMESPACES(namespaces) WRITE_ACCESS))
		{
			return ObjectRecord.DeleteBinding(reference WITH_NAMESPACES(namespaces));
		}
		else
		{
			return true;
		}
	}
}

bool JS::Runtime::GlobalEnvironmentRecord::HasThisBinding()
{
	return true;
}

bool JS::Runtime::GlobalEnvironmentRecord::HasSuperBinding()
{
	return false;
}

std::optional<JSObject> JS::Runtime::GlobalEnvironmentRecord::WithBaseObject()
{
	return {};
}

JSValue JS::Runtime::GlobalEnvironmentRecord::GetThisBinding()
{
#if ENABLE_ES4
	if(IsVersionES4() && StrictMode)
	{
		JS::Throw(JS::New<SyntaxError> __LOC(u"'this' cannot be accessed outside a function in strict mode"));
	}
#endif
	return ObjectRecord.BindingObject;
}

// TODO: these are not used currently

bool JS::Runtime::GlobalEnvironmentRecord::HasVarDeclaration(JSString name)
{
	return VarNames.find(name) != VarNames.end();
}

bool JS::Runtime::GlobalEnvironmentRecord::HasLexicalDeclaration([[maybe_unused]] JSString name)
{
	//return DeclarativeRecord.HasBinding(name, false PUBLIC_NAMESPACES); // TODO
	// TODO
	return false;
}

bool JS::Runtime::GlobalEnvironmentRecord::HasRestrictedGlobalProperty(JSString name)
{
	auto property = ObjectRecord.BindingObject->GetOwnProperty(name WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/}) READ_ACCESS);
	if(property.has_value())
	{
		return !property.value().Configurable;
	}
	else
	{
		return false;
	}
}

bool JS::Runtime::GlobalEnvironmentRecord::CanDeclareGlobalVar(JSString name)
{
	return ObjectRecord.BindingObject->HasOwnProperty(name WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/} READ_WRITE_ACCESS/*TODO*/))
		|| ObjectRecord.BindingObject->IsExtensible();
}

bool JS::Runtime::GlobalEnvironmentRecord::CanDeclareGlobalFunction(JSString name)
{
	auto property = ObjectRecord.BindingObject->GetOwnProperty(name WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/}) WRITE_ACCESS);
	if(property.has_value())
	{
		return property.value().Configurable
			|| (property.value().PropertyType != JSProperty::Type::Accessor && property.value().IsWritable() && property.value().Enumerable);
	}
	else
	{
		return ObjectRecord.BindingObject->IsExtensible();
	}
}

void JS::Runtime::GlobalEnvironmentRecord::CreateGlobalVariableBinding(JSString name, [[maybe_unused]] bool deletable) // TODO: unused
{
#if 0
	if(!ObjectRecord.BindingObject->HasOwnProperty(name WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/} READ_WRITE_ACCESS/*TODO*/)) && ObjectRecord.BindingObject->IsExtensible())
	{
		ObjectRecord.CreateMutableBinding(name, deletable);
		ObjectRecord.InitializeBinding(name, JS_undefined);
	}
#endif
	// TODO
	VarNames.insert(name);
}

void JS::Runtime::GlobalEnvironmentRecord::CreateGlobalFunctionBinding(JSString name, JSValue value, bool deletable) // TODO: unused
{
	auto property = ObjectRecord.BindingObject->GetOwnProperty(name WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/}) WRITE_ACCESS);
	JSPropertyDescriptor descriptor = JSPropertyDescriptor().withValue(value);
	if(property)
	{
		if(property.value().Configurable)
		{
			descriptor = descriptor.withEnumerable(true).withConfigurable(deletable);
		}
	}
	if(!ObjectRecord.BindingObject->DefineOwnProperty(name, descriptor PUBLIC_NAMESPACES WITH_ACCESS_SET(AccessSet::ReadWrite /* TODO */)))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
	ObjectRecord.BindingObject->Set(name, value, ObjectRecord.BindingObject WITH_NAMESPACES(std::set<JSNamespace>{JS_public/*TODO*/}) WITH_CLASS(ObjectRecord.BindingObject->GetClass()) NO_ENVIRONMENT);
	// TODO: record that the binding for name in ObjectRecord has been initialized
	VarNames.insert(name);
}

JSNamespace JS::Runtime::GlobalEnvironmentRecord::GetDefaultNamespace()
{
	if(DefaultNamespace.has_value())
	{
		return DefaultNamespace.value();
	}
	return OuterEnvironment->GetDefaultNamespace();
}

//// ModuleEnvironmentRecord

bool JS::Runtime::ModuleEnvironmentRecord::HasBinding(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_ACCESS_SET(accesses) AND_PHASE(phase))
{
	return indirectBindings.find(reference.GetName()) != indirectBindings.end()
	|| DeclarativeEnvironmentRecord::HasBinding(reference WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses) WITH_PHASE(phase));
}

JSValue JS::Runtime::ModuleEnvironmentRecord::GetBindingValue(JSIdentifierReference reference AND_NAMESPACES(namespaces) AND_PHASE(phase))
{
	auto indirect_binding_iterator = indirectBindings.find(reference.GetName());
	if(indirect_binding_iterator != indirectBindings.end())
	{
		auto indirectBinding = indirect_binding_iterator->second;
		auto targetEnv = indirectBinding.Module->Environment;
		if(targetEnv == nullptr)
		{
			JS::Throw(JS::New<ReferenceError> __LOC());
		}
		return targetEnv->GetBindingValue(JSIdentifierReference(indirectBinding.Name) PUBLIC_NAMESPACES WITH_PHASE(phase));
	}
	else
	{
		return DeclarativeEnvironmentRecord::GetBindingValue(reference WITH_NAMESPACES(namespaces) WITH_PHASE(phase));
	}
}

bool JS::Runtime::ModuleEnvironmentRecord::DeleteBinding([[maybe_unused]] JSIdentifierReference reference AND_NAMESPACES(namespaces))
{
	JS::Throw(JS::New<InternalError> __LOC());
}

bool JS::Runtime::ModuleEnvironmentRecord::HasThisBinding()
{
	return true;
}

JSValue JS::Runtime::ModuleEnvironmentRecord::GetThisBinding()
{
	return JS_undefined;
}

void JS::Runtime::ModuleEnvironmentRecord::CreateImportBinding(JSString bound_name, JS::Runtime::ModuleRecord * module_record, JSString binding_name)
{
	if(HasBinding(bound_name PUBLIC_NAMESPACES READ_WRITE_ACCESS RUNTIME_PHASE))
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	if(indirectBindings.find(bound_name) != indirectBindings.end())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	indirectBindings[bound_name] = IndirectBinding{module_record, binding_name};
}

//// PrivateEnvironmentRecord

JSPrivateName JS::Runtime::PrivateEnvironmentRecord::ResolvePrivateIdentifier(std::shared_ptr<PrivateEnvironmentRecord> PrivateEnvironment, JSString string)
{
	for(auto private_env = PrivateEnvironment;
		private_env != nullptr;
		private_env = private_env->OuterPrivateEnvironment)
	{
		auto private_name_iter = private_env->Names.find(string);
		if(private_name_iter != private_env->Names.end())
		{
			return private_name_iter->second;
		}
	};
	JS::Throw(JS::New<SyntaxError> __LOC("unresolvable private reference")); // TODO: Node.js and SpiderMonkey report SyntaxError
}

//// LexicalReference

JS::Runtime::LexicalReference JS::Runtime::LexicalReference::ResolveBinding(FOR_PHASE(phase) AND_ACCESS_SET(accesses))
{
	LexicalReference copy = *this;
	for(std::shared_ptr<EnvironmentRecord> env = Base; env != nullptr; env = env->OuterEnvironment)
	{
		if(env->HasBinding(GetName() WITH_NAMESPACES(GetNamespaces(VIA_PHASE(phase))) WITH_ACCESS_SET(accesses) WITH_PHASE(phase)))
		{
			// TODO: restrict namespaces?
			copy.Base = env;
			return copy;
		}
	}
	copy.Base = nullptr;
	return copy;
}

std::optional<JSValue> JS::Runtime::LexicalReference::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	auto reference = *this;

#if ENABLE_ES4
	if(IsVersionES4())
	{
		reference = ResolveBinding(VIA_PHASE(phase) READ_ACCESS);
	}
#endif

	if(reference.Base == nullptr)
	{
		JS::Throw(JS::New<ReferenceError> __LOC(u"undefined reference"));
	}
	else if(reference.Base->HasBinding(reference.GetName() WITH_NAMESPACES(reference.GetNamespaces(VIA_PHASE(phase))) READ_ACCESS WITH_PHASE(phase)))
	{
		return reference.Base->GetBindingValue(reference.GetName() WITH_NAMESPACES(reference.GetNamespaces(VIA_PHASE(phase))) WITH_PHASE(phase));
	}
	else if(reference.Strict)
	{
		JS::Throw(JS::New<ReferenceError> __LOC());
	}
	else
	{
		return {};
	}
}

//// MemberReference

//// BracketReference

//// SuperReference

//// PrivateReference

//// Primitive<JS::Runtime::LexicalReference>

bool JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::LexicalReference);
}

const void * JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::get_if(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::LexicalReference) ? &reference : nullptr;
}

JSString JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::Typeof()
{
	if(reference.Base == nullptr)
	{
		return u"undefined";
	}
	else
	{
		return GetValue().Typeof();
	}
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::GetValue(Intent intent AND_PHASE(phase))
{
	return reference.GetValue(intent WITH_PHASE(phase)).value_or(JS_undefined);
}

void JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::PutValue(JSValue value)
{
	auto resolved_reference = reference;

#if ENABLE_ES4
	if(IsVersionES4())
	{
		resolved_reference = resolved_reference.ResolveBinding(VIA_RUNTIME_PHASE WRITE_ACCESS);
	}
#endif

	if(resolved_reference.Base == nullptr)
	{
		if(resolved_reference.Strict)
		{
			JS::Throw(JS::New<ReferenceError> __LOC());
		}
		auto global_object = GetGlobalObject();
#if ENABLE_ES4
		if(IsVersionES4())
		{
			// check if a read property was overlooked
			if(global_object->GetClass()->GetBaseInstanceProperty(resolved_reference.GetName(), {JS_public}, AccessSet::Read)
			|| global_object->GetOwnProperty(resolved_reference.GetName(), {JS_public}, AccessSet::Read))
			{
				JS::Throw(JS::New<ReferenceError> __LOC("attempting to write to name declared only read-only in global scope"));
			}
		}
#endif
		global_object->Set(
			resolved_reference.GetName(),
			value,
			global_object
			WITH_NAMESPACES(resolved_reference.GetNamespaces(VIA_RUNTIME_PHASE))
			WITH_CLASS(global_object->GetClass())
			NO_ENVIRONMENT);
	}
	else
	{
		auto success = resolved_reference.Base->SetMutableBinding(resolved_reference, value WITH_NAMESPACES(resolved_reference.GetNamespaces(VIA_RUNTIME_PHASE)));
		if(resolved_reference.Strict && success != JS::Runtime::Result::Success)
		{
			switch(success)
			{
			case JS::Runtime::Result::Success:
				break;
			case JS::Runtime::Result::ReferenceError:
				JS::Throw(JS::New<ReferenceError> __LOC());
			case JS::Runtime::Result::TypeError:
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}
	}
}

bool JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::DeleteReference()
{
	auto resolved_reference = reference;

	if(Version::ES5 <= CurrentVersion && reference.Strict)
	{
		JS::Throw(JS::New<ReferenceError> __LOC(u"deleting lexical references not allowed in strict mode"));
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		resolved_reference = resolved_reference.ResolveBinding(VIA_RUNTIME_PHASE WRITE_ACCESS);
	}
#endif

	return resolved_reference.Base->DeleteBinding(
		resolved_reference.GetName()
		WITH_NAMESPACES(resolved_reference.GetNamespaces(VIA_RUNTIME_PHASE)));
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::ResolveMethod()
{
	return GetValue(Intent::Call);
}

void JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JS::Runtime::LexicalReference>::inspect_value(out, reference, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Runtime::LexicalReference>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("lexical-reference")
		// TODO
		->CloseXMLElement();
}
#endif

//// Primitive<JS::Runtime::MemberReference>

bool JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::MemberReference);
}

const void * JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::get_if(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::MemberReference) ? &reference : nullptr;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::GetValue(Intent intent AND_PHASE(phase))
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return reference.Base.Get(reference.ReferencedName, reference.ThisValue, intent WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT WITH_PHASE(phase));
	}
	else
#endif
	if(CurrentVersion < Version::ES16)
	{
		return reference.Base.ToObject()->Get(reference.ReferencedName, reference.ThisValue, intent WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT WITH_PHASE(phase));
	}
	else
	{
		return reference.Base.ToObject()->Get(
			// must be converted to PropertyKey (if appropriate)
			reference.Base.ResolvePropertyKey(reference.ReferencedName),
			reference.ThisValue,
			intent
			WITH_NAMESPACES(reference.Namespaces)
			WITH_CLASS(reference.Limit)
			NO_ENVIRONMENT
			WITH_PHASE(phase));
	}
}

void JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::PutValue(JSValue value)
{
	bool success;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		success = reference.Base.Set(reference.ReferencedName, value, reference.ThisValue WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT);
	}
	else
#endif
	if(CurrentVersion < Version::ES16)
	{
		success = reference.Base.ToObject()->Set(reference.ReferencedName, value, reference.ThisValue WITH_NAMESPACES(reference.Namespaces) WITH_CLASS(reference.Limit) NO_ENVIRONMENT);
	}
	else
	{
		// must be converted to PropertyKey (if appropriate)
		success = reference.Base.ToObject()->Set(
			reference.Base.ResolvePropertyKey(reference.ReferencedName),
			value,
			reference.ThisValue
			WITH_NAMESPACES(reference.Namespaces)
			WITH_CLASS(reference.Limit)
			NO_ENVIRONMENT);
	}

	if(!success && reference.Strict)
	{
		JS::Throw(JS::New<TypeError> __LOC(u"unable to set property"));
	}
}

bool JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::DeleteReference()
{
	auto resolved_reference = reference;
	if(resolved_reference.Base != resolved_reference.ThisValue)
	{
		JS::Throw(JS::New<ReferenceError> __LOC(u"deleting super references not allowed"));
	}
	if(Version::ES5 <= CurrentVersion)
	{
		resolved_reference.Base = JS::ToObject(resolved_reference.Base);
	}
	if(Version::ES16 <= CurrentVersion)
	{
		resolved_reference.ReferencedName = reference.Base.ResolvePropertyKey(resolved_reference.ReferencedName);
	}
	bool deleteStatus = resolved_reference.Base.Delete(
		resolved_reference.ReferencedName
		WITH_NAMESPACES(resolved_reference.Namespaces)
		WITH_CLASS(resolved_reference.Limit)
		WITH_FLAG(false));
	if(Version::ES5 <= CurrentVersion && !deleteStatus && resolved_reference.Strict)
	{
		JS::Throw(JS::New<TypeError> __LOC(u"unsuccessful delete"));
	}
	return deleteStatus;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::ResolveMethod()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return reference.Base.ResolveMethod(reference);
	}
	else
#endif
	if(CurrentVersion < Version::ES16)
	{
		return reference.Base.ToObject()->ResolveMethod(reference);
	}
	else
	{
		// must be converted to PropertyKey (if appropriate)
		reference.ReferencedName = reference.Base.ResolvePropertyKey(reference.ReferencedName);
		return reference.Base.ToObject()->ResolveMethod(reference);
	}
}

void JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JS::Runtime::MemberReference>::inspect_value(out, reference, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Runtime::MemberReference>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("member-reference")
		// TODO
		->CloseXMLElement();
}
#endif

#if ENABLE_ES4
//// Primitive<JS::Runtime::BracketReference>

bool JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::BracketReference);
}

const void * JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::get_if(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::BracketReference) ? &reference : nullptr;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::GetValue(Intent intent AND_PHASE(phase))
{
	return reference.Base.GetBracket(reference.Arguments, reference.ThisValue, intent, reference.Limit WITH_PHASE(phase));
}

void JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::PutValue(JSValue value)
{
	bool success = reference.Base.ToObject()->SetBracket(reference.Arguments, value, reference.ThisValue, reference.Limit);
	if(!success && reference.Strict)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

bool JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::DeleteReference()
{
	bool deleteStatus = reference.Base.DeleteBracket(
		reference.Arguments
		WITH_CLASS(reference.Limit));
	return deleteStatus;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::ResolveMethod()
{
	return reference.Base.ResolveMethod(reference);
}

void JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JS::Runtime::BracketReference>::inspect_value(out, reference, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Runtime::BracketReference>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("bracket-reference")
		// TODO
		->CloseXMLElement();
}
#endif
#endif

//// Primitive<JS::Runtime::SuperReference>

bool JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::SuperReference);
}

const void * JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::get_if(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::SuperReference) ? &reference : nullptr;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	JS::Throw(JS::New<InternalError> __LOC());
}

void JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::PutValue([[maybe_unused]] JSValue value)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::CreateReference(JSIdentifierReference identifier_reference, JS::Runtime::ReferenceContext cxt)
{
	if(identifier_reference.Attribute)
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}

	return JS::Runtime::MemberReference{
		reference.Instance,
		identifier_reference.PropertyKey,
		reference.ThisValue, // ES6
		cxt.Strict
//		false
		WITH_NAMESPACES(identifier_reference.GetNamespaces(cxt))
		WITH_CLASS(reference.Limit) // ES4
	};
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::CreateBracketReference(std::vector<JSValue> arguments, JS::Runtime::ReferenceContext cxt)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return JS::Runtime::BracketReference{
			reference.Instance,
			arguments,
			reference.ThisValue, // ES6
			cxt.Strict
			WITH_CLASS(reference.Limit) // ES4
		};
	}
	else
#endif
	{
		JSValue index = assert_singlet(arguments);

		if(CurrentVersion < Version::ES6)
		{
			index = index.ToString(VIA_PHASE(cxt.Phase));
		}
		else if(CurrentVersion < Version::ES16)
		{
			index = index.ToPropertyKey(VIA_PHASE(cxt.Phase));
		}

		return JS::Runtime::MemberReference{
			reference.Instance,
			index,
			reference.ThisValue, // ES6
			cxt.Strict
			PUBLIC_NAMESPACES
			WITH_CLASS(reference.Limit) // ES4
		};
	}
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::ResolveMethod()
{
	return GetValue(Intent::Call);
}

void JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JS::Runtime::SuperReference>::inspect_value(out, reference, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("super-reference")
		// TODO
		->CloseXMLElement();
}
#endif

bool JS::Class::Internal::Primitive<JS::Runtime::SuperReference>::DeleteReference()
{
	assert(false);
}

//// Primitive<JS::Runtime::PrivateReference>

bool JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::has_value(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::PrivateReference);
}

const void * JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::get_if(const std::type_info& type) const
{
	return type == typeid(JS::Runtime::PrivateReference) ? &reference : nullptr;
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::GetValue([[maybe_unused]] Intent intent AND_PHASE(phase))
{
	return reference.Base.ToObject()->GetPrivate(reference.ReferencedName);
}

void JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::PutValue(JSValue value)
{
	reference.Base.ToObject()->SetPrivate(reference.ReferencedName, value);
}

void JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::inspect(std::ostream& out, size_t depth, std::map<JSObject, std::string>& visited, std::optional<std::string> access)
{
	::inspect<JS::Runtime::PrivateReference>::inspect_value(out, reference, depth, visited, access);
}

#if ENABLE_DEBUG
void JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::serialize(JS::Runtime::Debugger * debugger, [[maybe_unused]] bool force_output)
{
	debugger
		->OpenXMLElement("private-reference")
		// TODO
		->CloseXMLElement();
}
#endif

bool JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::DeleteReference()
{
	assert(false);
}

JSValue JS::Class::Internal::Primitive<JS::Runtime::PrivateReference>::ResolveMethod()
{
	return GetValue(Intent::Call);
}

