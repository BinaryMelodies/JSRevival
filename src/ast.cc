
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
#include "environment.hh"
#include "modules.hh"
#include "iterator.hh"
#include "ast.hh"
#include "debugger.hh"
#include "parser.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// ClassContext

JSObject JS::Runtime::ClassContext::GetHomeObject(bool Static)
{
	if(Static)
	{
		return *CompiledClass;
	}
	else
	{
		return ClassPrototype;
	}
}

void JS::Runtime::ClassContext::DefinePrivateStaticVariable(JSPrivateName name, JS::Runtime::PrivateElement value)
{
	GetCurrentExecutionContext()->PrivateEnvironment->Names[name->Description] = name;
	(*CompiledClass)->PrivateElements[name] = value;
}

void JS::Runtime::ClassContext::DefinePublicStaticVariable(JSPropertyKey name, JSProperty value)
{
	if(!(*CompiledClass)->Define(name, value PUBLIC_NAMESPACES READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

void JS::Runtime::ClassContext::DefinePrivateInstanceVariable(JSPrivateName name, JSFunction Initialization)
{
	GetCurrentExecutionContext()->PrivateEnvironment->Names[name->Description] = name;
	(*CompiledClass)->Fields.push_back(JS::Runtime::ClassFieldDefinition{name, Initialization});
}

void JS::Runtime::ClassContext::DefinePublicInstanceVariable(JSPropertyKey name, JSFunction Initialization)
{
	(*CompiledClass)->Fields.push_back(JS::Runtime::ClassFieldDefinition{name, Initialization});
}

void JS::Runtime::ClassContext::DefinePrivateMethod(bool Static, JSString name, JSFunction method)
{
	std::map<JSPrivateName, JS::Runtime::PrivateElement>& container =
		Static ? (*CompiledClass)->PrivateElements : (*CompiledClass)->PrivateMethods;

	auto private_name = new JS::Runtime::PrivateName(name);
	GetCurrentExecutionContext()->PrivateEnvironment->Names[name] = private_name;
	container[private_name] =
		JS::Runtime::PrivateElement{
			JS::Runtime::PrivateElement::ElementKind::Method,
			method,
			{},
			{}
		};
}

void JS::Runtime::ClassContext::DefinePrivateGetter(bool Static, JSString name, JSFunction method)
{
	std::map<JSPrivateName, JS::Runtime::PrivateElement>& container =
		Static ? (*CompiledClass)->PrivateElements : (*CompiledClass)->PrivateMethods;

	auto private_name_iter = GetCurrentExecutionContext()->PrivateEnvironment->Names.find(name);
	if(private_name_iter == GetCurrentExecutionContext()->PrivateEnvironment->Names.end())
	{
		auto private_name = new JS::Runtime::PrivateName(name);
		GetCurrentExecutionContext()->PrivateEnvironment->Names[name] = private_name;
		container[private_name] =
			JS::Runtime::PrivateElement{
				JS::Runtime::PrivateElement::ElementKind::Accessor,
				{},
				method,
				{}
			};
	}
	else
	{
		auto private_name = private_name_iter->second;
		auto& private_element = container[private_name];
		if(private_element.Kind != JS::Runtime::PrivateElement::ElementKind::Accessor
		|| private_element.Get.has_value())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		private_element.Get = method;
	}
}

void JS::Runtime::ClassContext::DefinePrivateSetter(bool Static, JSString name, JSFunction method)
{
	std::map<JSPrivateName, JS::Runtime::PrivateElement>& container =
		Static ? (*CompiledClass)->PrivateElements : (*CompiledClass)->PrivateMethods;

	auto private_name_iter = GetCurrentExecutionContext()->PrivateEnvironment->Names.find(name);
	if(private_name_iter == GetCurrentExecutionContext()->PrivateEnvironment->Names.end())
	{
		auto private_name = new JS::Runtime::PrivateName(name);
		GetCurrentExecutionContext()->PrivateEnvironment->Names[name] = private_name;
		container[private_name] =
			JS::Runtime::PrivateElement{
				JS::Runtime::PrivateElement::ElementKind::Accessor,
				{},
				{},
				method
			};
	}
	else
	{
		auto private_name = private_name_iter->second;
		auto& private_element = container[private_name];
		if(private_element.Kind != JS::Runtime::PrivateElement::ElementKind::Accessor
		|| private_element.Set.has_value())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		private_element.Set = method;
	}
}

void JS::Runtime::ClassContext::DefinePublicMethod(bool Static, JSPropertyKey name, JSFunction method)
{
	if(!GetHomeObject(Static)->DefineOwnProperty(
		name,
		JSPropertyDescriptor()
			.withValue(method)
			.withWritable(true)
			.withEnumerable(false)
			.withConfigurable(true)
		PUBLIC_NAMESPACES
		READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

void JS::Runtime::ClassContext::DefinePublicGetter(bool Static, JSPropertyKey name, JSFunction method)
{
	if(!GetHomeObject(Static)->DefineOwnProperty(
		name,
		JSPropertyDescriptor()
			.withGetter(method)
			.withEnumerable(false)
			.withConfigurable(true)
		PUBLIC_NAMESPACES
		READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

void JS::Runtime::ClassContext::DefinePublicSetter(bool Static, JSPropertyKey name, JSFunction method)
{
	if(!GetHomeObject(Static)->DefineOwnProperty(
		name,
		JSPropertyDescriptor()
			.withSetter(method)
			.withEnumerable(false)
			.withConfigurable(true)
		PUBLIC_NAMESPACES
		READ_WRITE_ACCESS))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

//// ParseNode
void JS::ParseNode::ParseNode::Validate([[maybe_unused]] JS::Runtime::ValidationContext cxt)
{
}

#if ENABLE_ES4
JSType JS::ParseNode::ParseNode::EvaluateType()
{
	auto result = EvaluateExpression(JS::Runtime::ExecutionPhase::Compile).GetValue(Intent::Read, JS::Runtime::ExecutionPhase::Compile);
	if(auto type = GetType(result))
	{
		return type.value();
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}
#endif

JSValue JS::ParseNode::ParseNode::EvaluateExpression(FOR_PHASE(phase))
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin();
	CompileExpression(initial_state WITH_PHASE(phase));
	return JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>(&code);
}

JS::Interpreter::State<JSValue> JS::ParseNode::ParseNode::CompileExpression([[maybe_unused]] JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ParseNode::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return CompileExpression(initial_state RUNTIME_PHASE).Apply(std::function<JSValue(JSValue)>([](JSValue reference) -> JSValue
	{
		return reference.GetValue();
	})).Option();
}

#if ENABLE_ES4
JSIdentifierReference JS::ParseNode::ParseNode::EvaluateName(FOR_PHASE(phase))
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin();
	CompileName(initial_state WITH_PHASE(phase));
	return JS::Runtime::ExecutionContext::ExecuteAndReturn<JSIdentifierReference>(&code);
}
#endif

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::ParseNode::CompileName([[maybe_unused]] JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Throw(JS::New<InternalError> __LOC());
}

void JS::ParseNode::ParseNode::EvaluateBinding(JS::Runtime::BindingContext context)
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin().Push<JS::Runtime::BindingContext>(context);

	CompileBinding(initial_state);
	JS::Runtime::ExecutionContext::ExecuteAndReturn<void>(&code);
}

JS::Interpreter::State<> JS::ParseNode::ParseNode::CompileInitialization([[maybe_unused]] JS::Interpreter::State<>& initial_state)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JS::Interpreter::State<> JS::ParseNode::ParseNode::CompileBinding([[maybe_unused]] JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

void JS::ParseNode::ParseNode::EvaluateIteratorBinding(JS::Runtime::IteratorBindingContext context)
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin().Push<JS::Runtime::IteratorBindingContext>(context);

	CompileIteratorBinding(initial_state);
	JS::Runtime::ExecutionContext::ExecuteAndReturn<void>(&code);
}

JS::Interpreter::State<> JS::ParseNode::ParseNode::CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<JS::ParseNode::MemberAccessExpression *>(this)
	|| dynamic_cast<JS::ParseNode::IndexExpression *>(this))
	{
		// [IteratorDestructuringAssignment]
		// AssignmentElement : DestructuringAssignmentTarget Initializer:opt
		return binding_state
			.Then(this, &ParseNode::CompileExpression RUNTIME_PHASE)
			.Apply(std::function<void(JSValue, JS::Runtime::IteratorBindingContext)>(
			[](JSValue target_reference, JS::Runtime::IteratorBindingContext iterator_binding_context)
		{
			JSValue value = JS_undefined;
			if(!iterator_binding_context.binding_data->IsDone())
			{
				if(auto next = iterator_binding_context.binding_data->StepValue())
				{
					value = next.value();
				}
			}
			target_reference.PutValue(value);
		}));
	}

	// [IteratorBindingInitialization]
	// BindingElement : BindingPattern Initializer:opt
	return binding_state.Apply(std::function<JS::Runtime::BindingContext(JS::Runtime::IteratorBindingContext)>(
		[](JS::Runtime::IteratorBindingContext iterator_binding_context) -> JS::Runtime::BindingContext
	{
		JS::Runtime::BindingContext binding_context = iterator_binding_context;
		binding_context.Value = JS_undefined;
		if(!iterator_binding_context.binding_data->IsDone())
		{
			if(auto next = iterator_binding_context.binding_data->StepValue())
			{
				binding_context.Value = next.value();
			}
		}
		return binding_context;
	})).Then(this, &ParseNode::CompileBinding);
}

void JS::ParseNode::ParseNode::EvaluatePropertyBinding(JS::Runtime::PropertyBindingContext context)
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin().Push<JS::Runtime::PropertyBindingContext>(context);

	CompilePropertyBinding(initial_state);
	JS::Runtime::ExecutionContext::ExecuteAndReturn<void>(&code);
}

JS::Interpreter::State<> JS::ParseNode::ParseNode::CompilePropertyBinding([[maybe_unused]] JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

void JS::ParseNode::ParseNode::EvaluateKeyedBinding(JS::Runtime::KeyedBindingContext context)
{
	JS::Interpreter::Code code;
	auto initial_state = code.Begin().Push<JS::Runtime::KeyedBindingContext>(context);

	CompileKeyedBinding(initial_state);
	JS::Runtime::ExecutionContext::ExecuteAndReturn<void>(&code);
}

JS::Interpreter::State<> JS::ParseNode::ParseNode::CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<JS::ParseNode::MemberAccessExpression *>(this)
	|| dynamic_cast<JS::ParseNode::IndexExpression *>(this))
	{
		// [KeyedDestructuringAssignment]
		// AssignmentElement : DestructuringAssignmentTarget Initializer:opt
		return binding_state
			.Then(this, &ParseNode::CompileExpression RUNTIME_PHASE)
			.Apply(std::function<void(JSValue, JS::Runtime::KeyedBindingContext)>(
				[](JSValue reference, JS::Runtime::KeyedBindingContext keyed_binding_context)
		{
			reference.PutValue(GetByKey(ToObject(keyed_binding_context.Value), keyed_binding_context.property_key));
		}));
	}
	else
	{
		return
			binding_state.Obtain(std::function<JS::Runtime::BindingContext(JS::Runtime::KeyedBindingContext)>([](JS::Runtime::KeyedBindingContext keyed_binding_context) -> JS::Runtime::BindingContext
		{
			JS::Runtime::BindingContext binding_context = keyed_binding_context;
			binding_context.Value = GetByKey(ToObject(keyed_binding_context.Value), keyed_binding_context.property_key);
			return binding_context;
		})).Then(this, &ParseNode::CompileBinding).Pop();
	}
}

bool JS::ParseNode::ParseNode::CollectBoundNames([[maybe_unused]] std::multiset<JSString>& names)
{
	// no operation
	return false;
}

bool JS::ParseNode::ParseNode::ContainsInitialization()
{
	// no operation
	return false;
}

void JS::ParseNode::ParseNode::CreateBindings([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
	// no operation
}

void JS::ParseNode::ParseNode::CreateDeclarationBindings([[maybe_unused]] JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	JS::Throw(JS::New<InternalError> __LOC());
}

#if ENABLE_ES4
void JS::ParseNode::ParseNode::SetupContent([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
}

void JS::ParseNode::ParseNode::SetupDeclarationContent([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
}
#endif

JS::Interpreter::State<JSObject> JS::ParseNode::ParseNode::CompilePropertyDefinition([[maybe_unused]] JS::Interpreter::State<JSObject> initial_state)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::ParseNode::CompileClassElement([[maybe_unused]] JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Throw(JS::New<InternalError> __LOC());
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ParseNode::Process([[maybe_unused]] JS::Runtime::ValidationContext cxt, [[maybe_unused]] JS::Interpreter::State<>& initial_state)
{
	JS::Throw(JS::New<InternalError> __LOC(u"Process called on non-script/module/package"));
}

//// Node::Empty

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::Empty::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(std::optional<JSValue>{});
}

JS::Interpreter::State<> JS::ParseNode::Empty::CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// [IteratorBindingInitialization]
	// ArrayBindingPattern : '[' Elision ']'
	// [IteratorDestructuringAssignmentEvaluation]
	// Elision : ','
	// Elision : Elision ','
	return binding_state.Apply(std::function<void(JS::Runtime::IteratorBindingContext)>([](JS::Runtime::IteratorBindingContext iterator_binding_context)
	{
		if(!iterator_binding_context.binding_data->IsDone())
		{
			iterator_binding_context.binding_data->StepValue(); // TODO: just Step
		}
	}));
}

//// Node::Identifier

void JS::ParseNode::Identifier::Assign(JSValue reference, JSValue value)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		switch(entity_mode)
		{
		case JS::Runtime::EntityMode::Unknown:
		case JS::Runtime::EntityMode::Hoisted:
			reference.PutValue(value);
			break;
		case JS::Runtime::EntityMode::Singleton:
			if(auto lexical_reference_option = reference.get_if<JS::Runtime::LexicalReference>())
			{
				auto lexical_reference = lexical_reference_option.value();
				lexical_reference = lexical_reference.ResolveBinding(VIA_RUNTIME_PHASE WRITE_ACCESS);
				lexical_reference.Base->InitializeSingletonBinding(
					std::get<JSString>(lexical_reference.PropertyKey)
					WITH_NAMESPACES(lexical_reference.GetNamespaces(VIA_RUNTIME_PHASE))
					READ_WRITE_ACCESS,
					value);
			}
			else
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			break;
		case JS::Runtime::EntityMode::Instance:
			JS::Throw(JS::New<InternalError> __LOC());
		default:
			assert(false);
		}
	}
	else
#endif
	{
		switch(declaration_kind)
		{
		case DeclarationKind::None:
		case DeclarationKind::Var:
			reference.PutValue(value);
			break;
		case DeclarationKind::Const:
		case DeclarationKind::Let:
			if(auto lexical_reference_option = reference.get_if<JS::Runtime::LexicalReference>())
			{
				auto lexical_reference = lexical_reference_option.value();
				lexical_reference.Base->InitializeSingletonBinding(
					std::get<JSString>(lexical_reference.PropertyKey)
					WITH_NAMESPACES(lexical_reference.GetNamespaces(VIA_RUNTIME_PHASE))
					READ_WRITE_ACCESS,
					value);
			}
			else
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			break;
		default:
			assert(false);
		}
	}
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompileAssign(JS::Interpreter::State<JSValue, JSValue> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Apply(std::function<void(JSValue, JSValue)>(
		[this](JSValue value, JSValue reference)
	{
		Assign(reference, value);
	}));
}

void JS::ParseNode::Identifier::Validate(JS::Runtime::ValidationContext cxt)
{
	strict = cxt.IsStrict();
	open_namespaces = cxt.GetOpenNamespaces();
#if ENABLE_ES4
	if(IsVersionES4() && type.has_value())
	{
		type.value()->Validate(cxt);
	}
#endif

	initializer = cxt.Initializer;

	declaration_kind = cxt.declaration_kind;
	if(cxt.entity_mode != JS::Runtime::EntityMode::Unknown)
	{
		entity_mode = cxt.entity_mode;
#if ENABLE_ES4
		if(IsVersionES4())
		{
			attributes = cxt.Attributes;
		}
#endif
	}

	if(Version::ES6 <= CurrentVersion && cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
		Static = cxt.Static;
	}

	if(Version::ES6 <= CurrentVersion)
	{
		if(auto function_definition = dynamic_cast<FunctionDefinition *>(initializer))
		{
			if(!function_definition->name.has_value())
			{
				function_definition->name = this;
			}
		}
		else if(auto class_definition = dynamic_cast<ClassDefinition *>(initializer))
		{
			if(!class_definition->name.has_value())
			{
				class_definition->name = this;
			}
		}
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::Identifier::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Apply(std::function<JSValue()>([this WITH_PHASE(phase)]() -> JSValue
	{
		// ES2  11.1.2: identifier resolution, 10.1.4
		// ES3  11.1.2: identifier resolution, 10.1.4
		// ES5  11.1.2: identifier resolution, 10.3.1
		// ES6  12.1.6: ResolveBinding(StringValue of Identifier)
		// ES16 13.1.3: ResolveBinding(StringValue of Identifier)
		JSValue value = JS::Runtime::LexicalReference(GetCurrentExecutionContext()->LexicalEnvironment, strict WITH_NAMESPACES(open_namespaces), StringValue);
#if ENABLE_ES4
		if(!IsVersionES4())
#endif
		{
			value = value.get<JS::Runtime::LexicalReference>().ResolveBinding(VIA_PHASE(phase) READ_ACCESS);
		}
		return value;
	}));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::Identifier::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(is_private, StringValue));
}

void JS::ParseNode::Identifier::CreateDeclarationBindings([[maybe_unused]] JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	switch(entity_mode)
	{
	case JS::Runtime::EntityMode::Unknown:
		JS::Throw(JS::New<InternalError> __LOC());
	case JS::Runtime::EntityMode::Hoisted:
		if(!GetCurrentExecutionContext()->VariableEnvironment->HasActualBinding(StringValue PUBLIC_NAMESPACES READ_WRITE_ACCESS COMPILE_TIME_PHASE))
		{
			GetCurrentExecutionContext()->VariableEnvironment->CreateHoistedBinding(StringValue, JS_undefined);
		}
		break;
	case JS::Runtime::EntityMode::Singleton:
#if ENABLE_ES4
		if(IsVersionES4())
		{
			if(attributes.Override != JS::Runtime::OverrideModifier::None)
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}

			if(as_clause_parameter)
			{
				// creates an uninitialized variable, with the type field set up
				GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
					StringValue,
					attributes.GetNamespaces(),
					AccessSet::ReadWrite,
					JSProperty::MakeVariable(
						{},
						/* Writable = */ declaration_kind != DeclarationKind::Const,
						/* Enumerable = */ true,
						attributes.Explicit,
						type.has_value()
							? type.value()->EvaluateType()
							: Intrinsics()->Object_class));
			}
			else if(initializer && declaration_kind == DeclarationKind::Const)
			{
				variable =
					JSProperty::MakeVariable(
						new DeferredValue(initializer), // TODO: for compile time, use the validationcontext, for run time, use current exec cxt
						/* Writable = */ declaration_kind != DeclarationKind::Const,
						/* Enumerable = */ true,
						attributes.Explicit,
						std::optional<JSType>());
				GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
					StringValue,
					attributes.GetNamespaces(),
					AccessSet::ReadWrite,
					variable.value());
			}
			else
			{
				variable =
					JSProperty::MakeVariable(
						JS_undefined,
						/* Writable = */ declaration_kind != DeclarationKind::Const,
						true,
						attributes.Explicit,
						std::optional<JSType>());
				GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
					StringValue,
					attributes.GetNamespaces(),
					AccessSet::ReadWrite,
					variable.value());
			}
		}
		else
#endif
		{
			GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
				StringValue
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				declaration_kind == DeclarationKind::Const
					? JSBinding::MakeImmutable({}, /* Strict = */ true)
					: JSBinding::MakeMutable({}, /* Deletable = */ false));
		}
		break;
#if ENABLE_ES4
	case JS::Runtime::EntityMode::Instance:
		{
			instance_variable =
				new JS::Runtime::InstanceVariable(
					attributes.Category == JS::Runtime::MemberModifier::Final,
					attributes.Enumerable,
					declaration_kind == DeclarationKind::Const,
					std::optional<JSType>(),
					std::optional<JSValue>());
			auto overridden_property =
				GetCurrentExecutionContext()->GetClassEnvironment()->CreateInstanceBinding(
					StringValue,
					attributes.GetNamespaces(),
					open_namespaces,
					attributes.Override,
					attributes.Explicit,
					instance_variable.value());
			if(overridden_property.has_value())
			{
				overridden_variable = static_cast<JSInstanceVariable>(overridden_property.value());
				if(overridden_variable.value()->Enumerable)
				{
					instance_variable.value()->Enumerable = true;
				}
			}
		}
		break;
#endif
	}
}

#if ENABLE_ES4
void JS::ParseNode::Identifier::SetupDeclarationContent([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
	if(variable.has_value())
	{
		JSType actual_type;
		if(type.has_value())
		{
			actual_type = type.value()->EvaluateType();
		}
		else
		{
			actual_type = Intrinsics()->Object_class;
		}

		GetCurrentExecutionContext()->LexicalEnvironment->SetSingletonBindingType(
			StringValue,
			actual_type,
			attributes.GetNamespaces(),
			AccessSet::ReadWrite);

		if(variable.value().Mutable == JSProperty::Mutability::Mutable)
		{
			auto default_value = GetClassForType(actual_type)->GetDefaultValue();
			if(default_value.has_value())
			{
				GetCurrentExecutionContext()->LexicalEnvironment->SetSingletonBindingValue(
					StringValue,
					default_value.value(),
					attributes.GetNamespaces(),
					AccessSet::ReadWrite);
			}
			else
			{
				JS::Throw(JS::New<UninitializedError> __LOC()); // only happens for type Never
			}
		}
	}

	if(instance_variable.has_value())
	{
		if(type.has_value())
		{
			instance_variable.value()->Type = type.value()->EvaluateType();
		}
		else if(overridden_variable.has_value())
		{
			instance_variable.value()->Type = overridden_variable.value()->Type;
		}
		else
		{
			instance_variable.value()->Type = Intrinsics()->Object_class;
		}

		if(initializer != nullptr)
		{
			instance_variable.value()->DefaultValue = initializer->EvaluateExpression(JS::Runtime::ExecutionPhase::Compile);
		}
		else if(!instance_variable.value()->Immutable)
		{
			auto default_value = instance_variable.value()->Type.value().DefaultValue();
			if(default_value.has_value())
			{
				instance_variable.value()->DefaultValue = default_value.value();
			}
			else
			{
				JS::Throw(JS::New<UninitializedError> __LOC()); // only happens for type Never
			}
		}
	}
}
#endif

bool JS::ParseNode::Identifier::CollectBoundNames(std::multiset<JSString>& names)
{
	bool has_duplicates = names.count(StringValue) != 0;
	names.insert(StringValue);
	return has_duplicates;
}

bool JS::ParseNode::Identifier::ContainsInitialization()
{
	return false;
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompileInitialization(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
#if ENABLE_ES4
	if(IsVersionES4())
	{
		switch(entity_mode)
		{
		case JS::Runtime::EntityMode::Unknown:
			JS::Throw(JS::New<InternalError> __LOC());
		case JS::Runtime::EntityMode::Hoisted:
			if(initializer != nullptr)
			{
				return initial_state.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<void(JSValue)>([this](JSValue value)
				{
					GetCurrentExecutionContext()->VariableEnvironment->SetHoistedBinding(StringValue, value);
				}));
			}
			break;
		case JS::Runtime::EntityMode::Singleton:
			if(declaration_kind == DeclarationKind::Const)
			{
				return initial_state.Apply(std::function<void(void)>([this]()
				{
					GetCurrentExecutionContext()->LexicalEnvironment->ExecuteSingletonBindingInitializer(
						StringValue,
						attributes.GetNamespaces(),
						AccessSet::ReadWrite);
				}));
			}
			else if(initializer != nullptr)
			{
				return initial_state.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<void(JSValue)>([this](JSValue value)
				{
					GetCurrentExecutionContext()->LexicalEnvironment->SetSingletonBindingValue(
						StringValue,
						value,
						attributes.GetNamespaces(),
						AccessSet::ReadWrite);
				}));
			}
			break;
		case JS::Runtime::EntityMode::Instance:
			break;
		}

		return initial_state;
	}
	else
#endif
	{
		// TODO: comment the Let/Var statements
		if(initializer == nullptr)
		{
			// [BindingInitialization]
			// BindingIdentifier : Identifier
			switch(declaration_kind)
			{
			case DeclarationKind::Var:
				return initial_state;
			case DeclarationKind::Const:
				JS::Throw(JS::New<InternalError> __LOC()); // TODO: this should have been caught at parse time
			case DeclarationKind::Let:
				return initial_state.Apply(std::function<void()>([this]()
				{
					auto lhs = JS::Runtime::LexicalReference(
						GetCurrentExecutionContext()->LexicalEnvironment,
						strict
						WITH_NAMESPACES({}),
						StringValue).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);
					lhs.Base->InitializeSingletonBinding(
						lhs.GetName()
						PUBLIC_NAMESPACES
						READ_WRITE_ACCESS,
						JS_undefined); // this is the first time it gets assigned a value, so we need to set it
				}));
				break;
			default:
				assert(false);
			}
		}
		else
		{
			JS::Interpreter::State<JS::Runtime::LexicalReference> state1 =
				initial_state.Apply(std::function<JS::Runtime::LexicalReference()>([this]() -> JS::Runtime::LexicalReference
			{
				return JS::Runtime::LexicalReference(
					GetCurrentExecutionContext()->LexicalEnvironment,
					strict
					WITH_NAMESPACES({}),
					StringValue).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);
			}));

			JS::Interpreter::State<JSValue, JS::Runtime::LexicalReference> state2 =
				state1.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE);

			switch(declaration_kind)
			{
			case DeclarationKind::Var:
				return state2.Apply(std::function<void(JSValue, JS::Runtime::LexicalReference)>([](JSValue value, JS::Runtime::LexicalReference reference)
				{
					reference.Base->SetHoistedBinding(reference.GetName(), value.GetValue());
				}));
			case DeclarationKind::Const:
			case DeclarationKind::Let:
				return state2.Apply(std::function<void(JSValue, JS::Runtime::LexicalReference)>([](JSValue value, JS::Runtime::LexicalReference reference)
				{
					reference.Base->InitializeSingletonBinding(
						reference.GetName()
						PUBLIC_NAMESPACES
						READ_WRITE_ACCESS,
						value.GetValue()); // this is the first time it gets assigned a value, so we need to set it
				}));
				break;
			default:
				assert(false);
			}
		}
	}
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// ES4 uses singleton binding for catch clauses
	// ES4 for-in loops use hoisted binding if:
	// - not strict
	// - not directly under class (or a block inside class)
	// - uses var
	// - no attributes are present
	// - no type specified (and no initializer, but that's a given for for-in bindings)

	return binding_state.Apply(std::function<void(JS::Runtime::BindingContext)>(
		[this](JS::Runtime::BindingContext binding_context)
	{
		auto reference = JS::Runtime::LexicalReference(
			GetCurrentExecutionContext()->LexicalEnvironment,
			strict
			WITH_NAMESPACES(open_namespaces),
			StringValue);
#if ENABLE_ES4
		if(!IsVersionES4())
#endif
		{
			reference = reference.ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);
		}
		Assign(reference, binding_context.Value);
	}));
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	struct runcontext
	{
		JS::Runtime::LexicalReference reference;
		JSValue value;
	};

	// [IteratorBindingInitialization]
	// SingleNameBinding : BindingIdentifier Initializer:opt

	JS::Interpreter::State<runcontext, JS::Runtime::IteratorBindingContext> state = binding_state.Obtain(std::function<runcontext(JS::Runtime::IteratorBindingContext)>(
		[this](JS::Runtime::IteratorBindingContext iterator_binding_context) -> runcontext
	{
		runcontext context;
		context.reference = JS::Runtime::LexicalReference(
			GetCurrentExecutionContext()->LexicalEnvironment,
			strict
			PUBLIC_NAMESPACES, //WITH_NAMESPACES({}),
			StringValue).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);

		context.value = JS_undefined;
#if ENABLE_ES4
		bool had_argument = false;
#endif

		if(!iterator_binding_context.binding_data->IsDone())
		{
			if(auto next = iterator_binding_context.binding_data->StepValue())
			{
				context.value = next.value();
#if ENABLE_ES4
				had_argument = true;
#endif
			}
		}
#if ENABLE_ES4
		if(IsVersionES4() && !had_argument && iterator_binding_context.exact_match_required && !DefaultValue.has_value())
		{
			JS::Throw(JS::New<ArgumentError> __LOC(u"fewer arguments than parameters were supplied"));
		}
#endif

		return context;
	}));

#if ENABLE_ES4
	if(IsVersionES4())
	{
		// this function only gets invoked for parameter passing, which fills in DefaultValue
		if(DefaultValue.has_value())
		{
#if ENABLE_ES4
			state = state.Apply(std::function<runcontext(runcontext)>([this](runcontext context) -> runcontext
			{
				if(IsUndefined(context.value))
				{
					context.value = DefaultValue.value();
				}
				return context;
			}));
#endif
		}
	}
	else
#endif
	if(initializer != nullptr)
	{
		// TODO: anonymous functions and classes
		JS::Interpreter::State<JSValue, runcontext, JS::Runtime::IteratorBindingContext> state1 = state.Obtain(std::function<JSValue(runcontext)>([](runcontext context) -> JSValue
		{
			return IsUndefined(context.value);
		}));
		JS::Interpreter::IfRange<void, runcontext, JS::Runtime::IteratorBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue default_value, runcontext context) -> runcontext
		{
			context.value = default_value;
			return context;
		}));
		state = if_range.EndIfRange(state);
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(entity_mode == JS::Runtime::EntityMode::Singleton)
		{
			return state.Apply(std::function<void(runcontext, JS::Runtime::IteratorBindingContext)>([this](runcontext context, [[maybe_unused]] JS::Runtime::IteratorBindingContext _1)
			{
				GetCurrentExecutionContext()->LexicalEnvironment->InitializeSingletonBinding(
					StringValue
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS,
					context.value);
			}));
		}
		else
		{
			return state.Apply(std::function<void(runcontext, JS::Runtime::IteratorBindingContext)>([this](runcontext context, [[maybe_unused]] JS::Runtime::IteratorBindingContext _1)
			{
				GetCurrentExecutionContext()->LexicalEnvironment->SetHoistedBinding(StringValue, context.value);
			}));
		}
	}
	else
#endif
	{
		return state.Apply(std::function<void(runcontext)>([this](runcontext context)
		{
			Assign(context.reference, context.value);
		})).Pop();
	}
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return binding_state.Apply(std::function<JS::Runtime::KeyedBindingContext(JS::Runtime::PropertyBindingContext)>(
		[this](JS::Runtime::PropertyBindingContext property_binding_context) -> JS::Runtime::KeyedBindingContext
	{
		JS::Runtime::KeyedBindingContext keyed_binding_context = property_binding_context;
		keyed_binding_context.property_key = StringValue;
		keyed_binding_context.binding_data->bound_names.insert(StringValue);
		return keyed_binding_context;
	})).Then(this, &Identifier::CompileKeyedBinding);
}

JS::Interpreter::State<> JS::ParseNode::Identifier::CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// [KeyedBindingInitialization]
	// SingleNameBinding : BindingIdentifier Initializer:opt

	JS::Interpreter::State<JSValue, JSValue, JS::Runtime::KeyedBindingContext> state =
		binding_state.Obtain(std::function<JSValue()>([this]() -> JSValue
	{
		return JS::Runtime::LexicalReference(
			GetCurrentExecutionContext()->LexicalEnvironment,
			strict
			WITH_NAMESPACES({}),
			StringValue).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);
	})).Obtain(std::function<JSValue(JSValue, JS::Runtime::KeyedBindingContext)>(
		[this]([[maybe_unused]] JSValue _1, JS::Runtime::KeyedBindingContext property_binding_context)
	{
		return GetByKey(ToObject(property_binding_context.Value), property_binding_context.property_key);
	}));

	if(initializer != nullptr)
	{
		JS::Interpreter::State<JSValue, JSValue, JSValue, JS::Runtime::KeyedBindingContext> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) -> JSValue
		{
			return IsUndefined(value);
		}));
		JS::Interpreter::IfRange<void, JSValue, JSValue, JS::Runtime::KeyedBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue default_value, [[maybe_unused]] JSValue _1) -> JSValue
		{
			return default_value;
		}));
		state = if_range.EndIfRange(state);
	}

	return state.Then(this, &Identifier::CompileAssign).Pop();
}

JS::Interpreter::State<JSObject> JS::ParseNode::Identifier::CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Then(this, &Identifier::CompileExpression RUNTIME_PHASE)
		.Apply(std::function<JSObject (JSValue, JSObject)>([this](JSValue value_reference, JSObject object) -> JSObject
	{
		if(!object->DefineOwnProperty(StringValue, JSPropertyDescriptor().withValue(value_reference.GetValue(Intent::Read RUNTIME_PHASE)).withWritable(true).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		return object;
	}));
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::Identifier::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(Static)
	{
		JS::Interpreter::State<JSValue, JS::Runtime::ClassContext> state;
		if(initializer)
		{
			state = initial_state.Then(initializer, &ParseNode::CompileExpression RUNTIME_PHASE);
		}
		else
		{
			state = initial_state.Push<JSValue>(JS_undefined);
		}

		if(is_private)
		{
			return state.Apply(std::function<JS::Runtime::ClassContext(JSValue, JS::Runtime::ClassContext)>([this](JSValue initial_value, JS::Runtime::ClassContext context)
			{
				context.DefinePrivateStaticVariable(
					new JS::Runtime::PrivateName(StringValue),
					JS::Runtime::PrivateElement{
						JS::Runtime::PrivateElement::ElementKind::Field,
						initial_value,
						{},
						{}
					});
				return context;
			}));
		}
		else
		{
			return state.Apply(std::function<JS::Runtime::ClassContext(JSValue, JS::Runtime::ClassContext)>([this](JSValue initial_value, JS::Runtime::ClassContext context)
			{
				context.DefinePublicStaticVariable(
					JSPropertyKey(StringValue),
					JSProperty::MakeData(
						initial_value,
						/* Writable = */ true,
						true,
						true));
				return context;
			}));
		}
	}
	else
	{
		JS::Interpreter::State<JSFunction, JS::Runtime::ClassContext> state = initial_state.Obtain(std::function<JSFunction(JS::Runtime::ClassContext)>([this](JS::Runtime::ClassContext context)
		{
			gcn::gc_ptr<JS::Class::Function> field_initializer_function = nullptr;
			if(initializer != nullptr)
			{
				field_initializer_function = JS::New<JS::Class::Function>(
					JS::GetCurrentRealm(), u"", 0, // name and length are not exposed
					new JS::ParseNode::FunctionalElement(JS::Parser::Location(), new ReturnStatement(JS::Parser::Location(), initializer)),
					strict ? JS::Class::Function::ThisModeType::Strict : JS::Class::Function::ThisModeType::Global,
					GetCurrentExecutionContext()->LexicalEnvironment,
					GetCurrentExecutionContext()->PrivateEnvironment
					PUBLIC_NAMESPACES,
					JS::Class::Function::FunctionKindType::Normal);
				field_initializer_function->HomeObject = context.ClassPrototype;
			}
			return field_initializer_function;
		}));

		if(is_private)
		{
			return state.Apply(std::function<JS::Runtime::ClassContext(JSFunction, JS::Runtime::ClassContext)>([this](JSFunction field_initializer_function, JS::Runtime::ClassContext context)
			{
				context.DefinePrivateInstanceVariable(
					new JS::Runtime::PrivateName(StringValue),
					field_initializer_function);
				return context;
			}));
		}
		else
		{
			return state.Apply(std::function<JS::Runtime::ClassContext(JSFunction, JS::Runtime::ClassContext)>([this](JSFunction field_initializer_function, JS::Runtime::ClassContext context)
			{
				context.DefinePublicInstanceVariable(
					JSPropertyKey(StringValue),
					field_initializer_function);
				return context;
			}));
		}
	}
}

//// Node::Parenthesis

void JS::ParseNode::Parenthesis::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position is preserved
	expression->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::Parenthesis::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return expression->CompileExpression(initial_state WITH_PHASE(phase));
}

//// Node::ReservedNamespace

#if ENABLE_ES4
void JS::ParseNode::ReservedNamespace::Validate([[maybe_unused]] JS::Runtime::ValidationContext cxt)
{
	// TODO
}

JS::Interpreter::State<JSValue> JS::ParseNode::ReservedNamespace::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(IsVersionES4())
	{
		switch(type)
		{
		case ReservedNamespaceType::Internal: // TODO: if internal is removed as a reserved word, this might become redundant
			return initial_state.Apply(std::function<JSValue()>([this]() -> JSValue
			{
				return GetCurrentExecutionContext()->GetPackage()->InternalNamespace;
			}));
		case ReservedNamespaceType::Private:
			return initial_state.Apply(std::function<JSValue()>([this]() -> JSValue
			{
				return GetCurrentExecutionContext()->GetClassEnvironment()->GetClass()->PrivateNamespace;
			}));
		case ReservedNamespaceType::Protected:
			// TODO
			break;
		case ReservedNamespaceType::Public:
			return initial_state.Push<JSValue>(JS_public);
		}
	}
	JS::Throw(JS::New<InternalError> __LOC());
}
#endif

//// Node::AbstractAttribute

#if ENABLE_ES4
JS::Interpreter::State<JSValue> JS::ParseNode::AbstractAttribute::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(JSValue(JSCompoundAttribute().WithCategory(JS::Runtime::MemberModifier::Abstract)));
}
#endif

//// Node::NativeAttribute

#if ENABLE_ES4
JS::Interpreter::State<JSValue> JS::ParseNode::NativeAttribute::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(JSValue(JSCompoundAttribute().WithNative()));
}
#endif

//// Node::NewTargetExpression

//// Node::ImportMetaExpression

//// Node::ThisExpression
#if ENABLE_ES4
void JS::ParseNode::ThisExpression::Validate([[maybe_unused]] JS::Runtime::ValidationContext cxt)
{
	if(IsVersionES4())
	{
		// TODO: check if "this" is valid in this context
	}
}
#endif

JS::Interpreter::State<JSValue> JS::ParseNode::ThisExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Apply(std::function<JSValue()>([]() -> JSValue
	{
		// ES2  11.1.1: this value
		// ES3  11.1.1: this value
		// ES5  11.1.1: ThisBinding
		// ES6  12.2.2.1: ResolveThisBinding()
		// ES16 13.2.1.1: ResolveThisBinding()
		return GetCurrentExecutionContext()->GetThisEnvironment()->GetThisBinding();
	}));
}

//// Node::SuperExpression

void JS::ParseNode::SuperExpression::Validate([[maybe_unused]] JS::Runtime::ValidationContext cxt)
{
	// TODO
}

JS::Interpreter::State<JSValue> JS::ParseNode::SuperExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return initial_state.Apply(std::function<JSValue()>([]() -> JSValue
		{
			auto super_type = GetCurrentExecutionContext()->GetClassEnvironment()->GetClass()->Super;
			if(!super_type.has_value())
			{
				JS::Throw(JS::New<SyntaxError> __LOC());
			}
			auto limit_type = super_type.value();
			auto limit = GetClassForType(limit_type);
			auto instance = GetClassForType(limit_type)->Coerce(GetCurrentExecutionContext()->GetThisEnvironment()->GetThisBinding());
			if(!instance.has_value()
			|| instance.value().has_value<JS::Type::Internal::Null>()) // TODO: is this second check necessary? probably not
			{
				return JS_null; // TODO: could we delay this to later? actually, should we raise a ConstantError here?
			}
			else
			{
				return JS::Runtime::SuperReference{instance.value(), instance.value(), limit};
			}
		}));
	}
	else
#endif
	if(Version::ES6 <= CurrentVersion)
	{
		return initial_state.Apply(std::function<JSValue()>([]() -> JSValue
		{
			auto super_base = GetCurrentExecutionContext()->GetThisEnvironment()->GetSuperBase();
			auto instance = GetCurrentExecutionContext()->GetThisEnvironment()->GetThisBinding();
			return JS::Runtime::SuperReference{super_base, instance WITH_CLASS(instance.GetClass())};
		}));
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

//// Node::ImportExpression

//// Node::NullLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::NullLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(JS_null);
}

//// Node::BooleanLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::BooleanLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(BooleanValue);
}

//// abstract Node::Literal

void JS::ParseNode::Literal::Validate(JS::Runtime::ValidationContext cxt)
{
	if(Version::ES6 <= CurrentVersion && cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
		Static = cxt.Static;
		Initializer = cxt.Initializer;
		strict = cxt.IsStrict();
	}
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::Literal::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(Static)
	{
		JS::Interpreter::State<JSValue, JS::Runtime::ClassContext> state;
		if(Initializer)
		{
			state = initial_state.Then(Initializer, &ParseNode::CompileExpression RUNTIME_PHASE);
		}
		else
		{
			state = initial_state.Push<JSValue>(JS_undefined);
		}

		return state.Apply(std::function<JS::Runtime::ClassContext(JSValue, JS::Runtime::ClassContext)>([this](JSValue initial_value, JS::Runtime::ClassContext context)
		{
			context.DefinePublicStaticVariable(
				GetPropertyKey(),
				JSProperty::MakeData(
					initial_value,
					/* Writable = */ true,
					true,
					true));
			return context;
		}));
	}
	else
	{
		JS::Interpreter::State<JSFunction, JS::Runtime::ClassContext> state = initial_state.Obtain(std::function<JSFunction(JS::Runtime::ClassContext)>([this](JS::Runtime::ClassContext context)
		{
			gcn::gc_ptr<JS::Class::Function> field_initializer_function = nullptr;
			if(Initializer != nullptr)
			{
				field_initializer_function = JS::New<JS::Class::Function>(
					JS::GetCurrentRealm(), u"", 0, // name and length are not exposed
					new JS::ParseNode::FunctionalElement(JS::Parser::Location(), new ReturnStatement(JS::Parser::Location(), Initializer)),
					strict ? JS::Class::Function::ThisModeType::Strict : JS::Class::Function::ThisModeType::Global,
					GetCurrentExecutionContext()->LexicalEnvironment,
					GetCurrentExecutionContext()->PrivateEnvironment
					PUBLIC_NAMESPACES,
					JS::Class::Function::FunctionKindType::Normal);
				field_initializer_function->HomeObject = context.ClassPrototype;
			}
			return field_initializer_function;
		}));

		return state.Apply(std::function<JS::Runtime::ClassContext(JSFunction, JS::Runtime::ClassContext)>([this](JSFunction field_initializer_function, JS::Runtime::ClassContext context)
		{
			context.DefinePublicInstanceVariable(
				GetPropertyKey(),
				field_initializer_function);
			return context;
		}));
	}
}

//// Node::NumericLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::NumericLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(NumericValue);
}

JSPropertyKey JS::ParseNode::NumericLiteral::GetPropertyKey()
{
	return JSPropertyKey(ToString(NumericValue RUNTIME_PHASE));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::NumericLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(ToString(NumericValue RUNTIME_PHASE)));
}

//// Node::BigIntNumericLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::BigIntNumericLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(NumericValue);
}

JSPropertyKey JS::ParseNode::BigIntNumericLiteral::GetPropertyKey()
{
	return JSPropertyKey(ToString(NumericValue RUNTIME_PHASE));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::BigIntNumericLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(ToString(NumericValue RUNTIME_PHASE)));
}

#if ENABLE_ES4

//// Node::LongNumericLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::LongNumericLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(JSValue(NumericValue));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::LongNumericLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(ToString(JSValue(NumericValue) RUNTIME_PHASE)));
}

//// Node::UlongNumericLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::UlongNumericLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(JSValue(NumericValue));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::UlongNumericLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(ToString(JSValue(NumericValue) RUNTIME_PHASE)));
}

//// Node::FloatNumericLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::FloatNumericLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push(JSValue(NumericValue));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::FloatNumericLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(ToString(JSValue(NumericValue) RUNTIME_PHASE)));
}
#endif

//// Node::StringLiteral

JS::Interpreter::State<JSValue> JS::ParseNode::StringLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(StringValue);
}

JSPropertyKey JS::ParseNode::StringLiteral::GetPropertyKey()
{
	return JSPropertyKey(StringValue);
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::StringLiteral::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(StringValue));
}

//// Node::RegularExpressionLiteral

//// Node::Template

//// Node::TemplateSubstitution

//// Node::ArrayLiteral

void JS::ParseNode::ArrayLiteral::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	cxt.Initializer = nullptr;
	for(auto element : elements)
	{
		element->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::ArrayLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	struct runcontext
	{
		gcn::gc_ptr<JS::Class::Array> array;
		uint32_t index = 0;
	};

	JS::Interpreter::State<runcontext> state = initial_state.Apply(std::function<runcontext()>([]() -> runcontext
	{
		runcontext context;
		context.array = JS::New<JS::Class::Array>(0);
		context.index = 0;
		return context;
	}));

	for(auto element : elements)
	{
		if(dynamic_cast<Empty *>(element))
		{
			state = state.Apply(std::function<runcontext(runcontext)>([](runcontext context) -> runcontext
			{
				if(!context.array->set_length(context.index + 1))
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}
				context.index++;
				return context;
			}));
		}
		else if(auto rest_element = dynamic_cast<RestExpression *>(element))
		{
			state = state.Then(rest_element->expression, &ParseNode::CompileExpression WITH_PHASE(phase))
				.Apply(
					std::function<runcontext(JSValue, runcontext)>([](JSValue rest_element, runcontext context) -> runcontext
			{
				auto iterator_record = JS::Runtime::IteratorRecord::GetIterator(rest_element.GetValue());
				while(true)
				{
					auto next = iterator_record.StepValue();
					if(next.has_value())
					{
						if(!context.array->set_length(context.index + 1) || !context.array->elements[context.index].SetValue(next.value()))
						{
							JS::Throw(JS::New<TypeError> __LOC());
						}
						context.index ++;
					}
					else
					{
						break;
					}
				}
				return context;
			}));
		}
		else
		{
			state = state.Then(element, &ParseNode::CompileExpression WITH_PHASE(phase))
				.Apply(
					std::function<runcontext(JSValue, runcontext)>([](JSValue result, runcontext context) -> runcontext
			{
				auto value = result.GetValue();
				if(!context.array->set_length(context.index + 1) || !context.array->elements[context.index].SetValue(value))
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}
				context.index ++;
				return context;
			}));
		}
	}

	return state.Apply(std::function<JSValue(runcontext)>([](runcontext context) -> JSValue
	{
		return JSObject(context.array);
	}));
}

void JS::ParseNode::ArrayLiteral::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	for(auto element : elements)
	{
		element->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
	}
}

bool JS::ParseNode::ArrayLiteral::CollectBoundNames(std::multiset<JSString>& names)
{
	bool has_duplicates = false;
	for(auto element : elements)
	{
		has_duplicates |= element->CollectBoundNames(names);
	}
	return has_duplicates;
}

bool JS::ParseNode::ArrayLiteral::ContainsInitialization()
{
	for(auto element : elements)
	{
		if(element->ContainsInitialization())
			return true;
	}
	return false;
}

JS::Interpreter::State<> JS::ParseNode::ArrayLiteral::CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JS::Runtime::IteratorBindingContext> state =
		binding_state.Apply(std::function<JS::Runtime::IteratorBindingContext(JS::Runtime::BindingContext)>([](JS::Runtime::BindingContext binding_context) -> JS::Runtime::IteratorBindingContext
	{
		// [BindingInitialization]
		// BindingPattern : ArrayBindingPattern
		JS::Runtime::IteratorBindingContext iterator_binding_context = binding_context;
		iterator_binding_context.binding_data = new (GC) JS::Runtime::IteratorRecord(JS::Runtime::IteratorRecord::GetIterator(binding_context.Value));
		return iterator_binding_context;
	}));

	for(auto element : elements)
	{
		state = state.Dup().Then(element, &ParseNode::CompileIteratorBinding);
	}

	return state.Apply(std::function<void(JS::Runtime::IteratorBindingContext)>([](JS::Runtime::IteratorBindingContext iterator_binding_context)
	{
		// TODO: this should be called even when an exception is thrown
		if(!iterator_binding_context.binding_data->IsDone())
		{
			iterator_binding_context.binding_data->Close();
		}
		delete iterator_binding_context.binding_data;
	}));
}

//// Node::PropertyDefinition

void JS::ParseNode::PropertyDefinition::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.IsStrict();
#if ENABLE_ES4
	open_namespaces = cxt.GetOpenNamespaces();
#endif

	key->Validate(cxt);
	value->Validate(cxt);
}

void JS::ParseNode::PropertyDefinition::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	value->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
}

bool JS::ParseNode::PropertyDefinition::CollectBoundNames(std::multiset<JSString>& names)
{
	return value->CollectBoundNames(names);
}

bool JS::ParseNode::PropertyDefinition::ContainsInitialization()
{
	return value->ContainsInitialization();
}

JS::Interpreter::State<> JS::ParseNode::PropertyDefinition::CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// [PropertyBindingInitialization]
	// BindingProperty : PropertyName ':' BindingElement

	JS::Interpreter::State<JSPropertyKey, JS::Runtime::PropertyBindingContext> state1 =
		binding_state.Then(key, &ParseNode::CompileName RUNTIME_PHASE)
			.Apply(std::function<JSPropertyKey(JSIdentifierReference)>([](JSIdentifierReference reference) -> JSPropertyKey
	{
		return reference.PropertyKey;
	}));

	// [KeyedBindingInitialization]
	// BindingElement : BindingPattern Initializer:opt

	return
		state1.Apply(std::function<JS::Runtime::KeyedBindingContext(JSPropertyKey, JS::Runtime::PropertyBindingContext)>(
			[](JSPropertyKey PropertyKey, JS::Runtime::PropertyBindingContext property_binding_context) -> JS::Runtime::KeyedBindingContext
	{
		JS::Runtime::KeyedBindingContext keyed_binding_context = property_binding_context;
		keyed_binding_context.property_key = PropertyKey;
		return keyed_binding_context;
	}))
		.Dup()
		.Then(value, &ParseNode::CompileKeyedBinding)
		.Apply(std::function<void(JS::Runtime::KeyedBindingContext)>(
			[](JS::Runtime::KeyedBindingContext keyed_binding_context)
	{
		keyed_binding_context.binding_data->bound_names.insert(keyed_binding_context.property_key);
	}));
}

JS::Interpreter::State<JSObject> JS::ParseNode::PropertyDefinition::CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// TODO: ES5 disallows duplicates

	bool is_proto = false;
	if(Version::ESNEXT <= CurrentVersion) // TODO: or ES6 and enabled
	{
		if(auto identifier = dynamic_cast<Identifier *>(key))
		{
			if(identifier->StringValue == u"__proto__")
			{
				is_proto = true;
			}
		}
		else if(auto string_literal = dynamic_cast<StringLiteral *>(key))
		{
			if(string_literal->StringValue == u"__proto__")
			{
				is_proto = true;
			}
		}

		if(is_proto)
		{
			return initial_state.Then(value, &ParseNode::CompileExpression RUNTIME_PHASE)
				.Apply(std::function<JSObject(JSValue, JSObject)>([this](JSValue value_reference, JSObject object) -> JSObject
			{
				if(auto proto_object = GetObjectOrNull(value_reference.GetValue(Intent::Read RUNTIME_PHASE)))
				{
					object->SetPrototypeOf(proto_object.value());
				}
				return object;
			}));
		}
	}

	JS::Interpreter::State<JSIdentifierReference, JSObject> state =
		initial_state.Then(key, &ParseNode::CompileName RUNTIME_PHASE);

	if(Version::ES6 <= CurrentVersion)
	{
		if(auto function_definition = dynamic_cast<FunctionDefinition *>(value))
		{
			if(!function_definition->name.has_value())
			{
				// anonymous functions
				return state.Obtain(std::function<JSString(JSIdentifierReference)>([](JSIdentifierReference reference) -> JSString
				{
					return GetStringForName(reference.PropertyKey);
				})).Then(function_definition, &FunctionDefinition::CompileNamedExpression)
					.Apply(
						std::function<
							JSObject(gcn::gc_ptr<JS::Class::Function>, JSIdentifierReference, JSObject)>(
						[this](gcn::gc_ptr<JS::Class::Function> function, JSIdentifierReference property_key, JSObject object)
				{
					if(!object->DefineOwnProperty(
						property_key.PropertyKey,
						JSPropertyDescriptor().withValue(function).withWritable(true).withEnumerable(true).withConfigurable(true)
						WITH_NAMESPACES(property_key.GetNamespaces(JS::Runtime::ReferenceContext(strict WITH_NAMESPACES(open_namespaces) COMPILE_TIME_PHASE)))
						READ_WRITE_ACCESS))
					{
						JS::Throw(JS::New<TypeError> __LOC());
					}
					return object;
				}));
			}
		}
	}

	return state.Then(value, &ParseNode::CompileExpression RUNTIME_PHASE)
		.Apply(std::function<JSObject(JSValue, JSIdentifierReference, JSObject)>([this](JSValue value_reference, JSIdentifierReference property_key, JSObject object) -> JSObject
	{
		if(!object->DefineOwnProperty(
			property_key.PropertyKey,
			JSPropertyDescriptor().withValue(value_reference.GetValue(Intent::Read RUNTIME_PHASE)).withWritable(true).withEnumerable(true).withConfigurable(true)
			WITH_NAMESPACES(property_key.GetNamespaces(JS::Runtime::ReferenceContext(strict WITH_NAMESPACES(open_namespaces) COMPILE_TIME_PHASE)))
			READ_WRITE_ACCESS))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
		return object;
	}));
}

//// Node::ObjectLiteral

void JS::ParseNode::ObjectLiteral::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	cxt.Initializer = nullptr;
	for(auto property_definition : property_definitions)
	{
		property_definition->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::ObjectLiteral::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSObject> state = initial_state.Apply(std::function<JSObject()>(
		[]() -> JSObject
	{
		return JS::New<JS::Class::OrdinaryObject>();
	}));

	for(auto property_definition : property_definitions)
	{
		state = property_definition->CompilePropertyDefinition(state);
	}

	return state.Apply(std::function<JSValue(JSObject)>(
		[](JSObject object) -> JSValue
	{
		return JSValue(object);
	}));
}

void JS::ParseNode::ObjectLiteral::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	for(auto property_definition : property_definitions)
	{
		property_definition->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
	}
}

bool JS::ParseNode::ObjectLiteral::CollectBoundNames(std::multiset<JSString>& names)
{
	bool has_duplicates = false;
	for(auto property : property_definitions)
	{
		has_duplicates |= property->CollectBoundNames(names);
	}
	return has_duplicates;
}

bool JS::ParseNode::ObjectLiteral::ContainsInitialization()
{
	for(auto property : property_definitions)
	{
		if(property->ContainsInitialization())
			return true;
	}
	return false;
}

JS::Interpreter::State<> JS::ParseNode::ObjectLiteral::CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JS::Runtime::PropertyBindingContext> state = binding_state.Apply(std::function<JS::Runtime::PropertyBindingContext(JS::Runtime::BindingContext)>([](JS::Runtime::BindingContext binding_context) -> JS::Runtime::PropertyBindingContext
	{
		// [BindingInitialization]
		// BindingPattern : ObjectBindingPattern
		// ObjectBindingPattern : '{' '}'
		// ObjectBindingPattern : '{' BindingPropertyList '}'
		// ObjectBindingPattern : '{' BindingPropertyList ',' '}'
		// ObjectBindingPattern : '{' BindingRestProperty '}'
		// ObjectBindingPattern : '{' BindingPropertyList ',' BindingRestProperty '}'

		if(IsNull(binding_context.Value) || IsUndefined(binding_context.Value))
		{
			JS::Throw(JS::New<TypeError> __LOC("null or undefined provided for object destructuring"));
		}

		JS::Runtime::PropertyBindingContext property_binding_context = binding_context;
		property_binding_context.binding_data = new (GC) JS::Runtime::PropertyBindingData();
		return property_binding_context;
	}));

	for(auto property_definition : property_definitions)
	{
		// [PropertyBindingInitialization]
		// BindingPropertyList : BindingPropertyList ',' BindingProperty
		// BindingProperty : SingleNameBinding

		state = state.Dup().Then(property_definition, &ParseNode::CompilePropertyBinding);
	}

	return state.Apply(std::function<void(JS::Runtime::PropertyBindingContext)>([](JS::Runtime::PropertyBindingContext property_binding_context)
	{
		delete property_binding_context.binding_data;
	}));
}

//// Node::RestExpression

void JS::ParseNode::RestExpression::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	expression->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
}

bool JS::ParseNode::RestExpression::CollectBoundNames(std::multiset<JSString>& names)
{
	return expression->CollectBoundNames(names);
}

bool JS::ParseNode::RestExpression::ContainsInitialization()
{
	return expression->ContainsInitialization();
}

void JS::ParseNode::RestExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.Strict;
	expression->Validate(cxt);
}

JS::Interpreter::State<> JS::ParseNode::RestExpression::CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<MemberAccessExpression *>(expression)
	|| dynamic_cast<IndexExpression *>(expression))
	{
		// [IteratorDestructuringAssignmentEvaluation]
		// AssignmentRestElement : '...' DestructuringAssignmentTarget

		return
			binding_state.Then(expression, &ParseNode::CompileExpression RUNTIME_PHASE)
			.Apply(std::function<void(JSValue, JS::Runtime::IteratorBindingContext)>(
				[](JSValue target_reference, JS::Runtime::IteratorBindingContext iterator_binding_context)
		{
			gcn::gc_ptr<JS::Class::Array> rest_array = JS::New<JS::Class::Array>();
			uint32_t index = 0;
			while(!iterator_binding_context.binding_data->IsDone())
			{
				if(auto next = iterator_binding_context.binding_data->StepValue())
				{
					rest_array->DefineOwnProperty(
						ToString(JSNumber(index) RUNTIME_PHASE),
						JSPropertyDescriptor().withValue(next.value()).withWritable(true).withEnumerable(true).withConfigurable(true)
						PUBLIC_NAMESPACES READ_WRITE_ACCESS);
				}
				else
				{
					break;
				}

				index++;
			}

			target_reference.PutValue(rest_array);
		}));
	}
	else
	{
		// [IteratorBindingInitialization]
		// BindingRestElement : '...' BindingIdentifier
		// BindingRestElement : '...' BindingPattern

		JS::Interpreter::State<JS::Runtime::BindingContext> state = binding_state.Apply(std::function<JS::Runtime::BindingContext(JS::Runtime::IteratorBindingContext)>(
			[this](JS::Runtime::IteratorBindingContext iterator_binding_context) -> JS::Runtime::BindingContext
		{
			JS::Runtime::LexicalReference reference;

			if(auto identifier = dynamic_cast<Identifier *>(expression))
			{
				reference = JS::Runtime::LexicalReference(
					iterator_binding_context.environment
						? iterator_binding_context.environment.value()
						: GetCurrentExecutionContext()->LexicalEnvironment,
					strict
					WITH_NAMESPACES(identifier->open_namespaces),
					identifier->StringValue).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);
			}

			gcn::gc_ptr<JS::Class::Array> rest_array = JS::New<JS::Class::Array>();
			uint32_t index = 0;
			while(!iterator_binding_context.binding_data->IsDone())
			{
				if(auto next = iterator_binding_context.binding_data->StepValue())
				{
					rest_array->DefineOwnProperty(
						ToString(JSNumber(index) RUNTIME_PHASE),
						JSPropertyDescriptor().withValue(next.value()).withWritable(true).withEnumerable(true).withConfigurable(true)
						PUBLIC_NAMESPACES READ_WRITE_ACCESS);
				}
				else
				{
					break;
				}

				index++;
			}

			JS::Runtime::BindingContext binding_context = iterator_binding_context;

			if(auto identifier = dynamic_cast<Identifier *>(expression))
			{
#if ENABLE_ES4
				if(IsVersionES4())
				{
					if(identifier->entity_mode == JS::Runtime::EntityMode::Singleton)
					{
						GetCurrentExecutionContext()->LexicalEnvironment->InitializeSingletonBinding(
							identifier->StringValue
							PUBLIC_NAMESPACES
							READ_WRITE_ACCESS,
							rest_array);
					}
					else
					{
						GetCurrentExecutionContext()->LexicalEnvironment->SetHoistedBinding(identifier->StringValue, rest_array);
					}
				}
				else
#endif
				{
					identifier->Assign(reference, rest_array);
				}
			}
			else
			{
				binding_context.Value = rest_array;
			}

			return binding_context;
		}));

		// binding to identifiers requires special handling
		if(!dynamic_cast<Identifier *>(expression))
		{
			return state.Then(expression, &ParseNode::CompileBinding);
		}
		else
		{
			return state.Pop();
		}
	}
}

JS::Interpreter::State<> JS::ParseNode::RestExpression::CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<MemberAccessExpression *>(expression)
	|| dynamic_cast<IndexExpression *>(expression))
	{
		// [RestDestructuringAssignmentEvaluation]
		// AssignmentRestProperty : '...' DestructuringAssignmentTarget
		return
			// TODO: identifier?
			binding_state
				.Then(expression, &ParseNode::CompileExpression RUNTIME_PHASE)
				.Apply(std::function<void(JSValue, JS::Runtime::PropertyBindingContext)>(
					[](JSValue reference, JS::Runtime::PropertyBindingContext property_binding_context)
			{
				auto rest_object = JS::New<JS::Class::OrdinaryObject>();
				auto from = ToObject(property_binding_context.Value);
				for(auto key : from->OwnPropertyKeys(VIA_CLASS(from->GetClass())))
				{
					if(property_binding_context.binding_data->bound_names.find(key) != property_binding_context.binding_data->bound_names.end())
					{
						// already bound
						continue;
					}
					auto property = from->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
					if(property.has_value() && property->Enumerable)
					{
						auto property_value = from->Get(key, from, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(from->GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
						if(!rest_object->DefineOwnProperty(key,
							JSPropertyDescriptor().withValue(property_value).withWritable(true).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
						{
							JS::Throw(JS::New<TypeError> __LOC());
						}
					}
				}

				reference.PutValue(rest_object);
			}));
	}
	else
	{
		// [RestBindingInitialization]
		// BindingRestProperty : '...' BindingIdentifier

		return binding_state.Apply(std::function<void(JS::Runtime::PropertyBindingContext)>([this](JS::Runtime::PropertyBindingContext property_binding_context)
		{
			auto reference = JS::Runtime::LexicalReference(
				property_binding_context.environment.has_value()
					? property_binding_context.environment.value()
					: GetCurrentExecutionContext()->LexicalEnvironment,
				strict
				WITH_NAMESPACES({}),
				dynamic_cast<Identifier *>(expression)->StringValue
			).ResolveBinding(VIA_RUNTIME_PHASE READ_WRITE_ACCESS);

			auto rest_object = JS::New<JS::Class::OrdinaryObject>();
			auto from = ToObject(property_binding_context.Value);
			for(auto key : from->OwnPropertyKeys(VIA_CLASS(from->GetClass())))
			{
				if(property_binding_context.binding_data->bound_names.find(key) != property_binding_context.binding_data->bound_names.end())
				{
					// already bound
					continue;
				}
				auto property = from->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
				if(property.has_value() && property->Enumerable)
				{
					auto property_value = from->Get(key, from, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(from->GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
					if(!rest_object->DefineOwnProperty(key,
						JSPropertyDescriptor().withValue(property_value).withWritable(true).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
					{
						JS::Throw(JS::New<TypeError> __LOC());
					}
				}
			}
			dynamic_cast<Identifier *>(expression)->Assign(reference, rest_object);
		}));
	}
}

JS::Interpreter::State<JSObject> JS::ParseNode::RestExpression::CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Then(expression, &ParseNode::CompileExpression RUNTIME_PHASE)
		.Apply(std::function<JSObject(JSValue, JSObject)>(
			[](JSValue value_reference, JSObject object) -> JSObject
		{
			auto value = value_reference.GetValue(Intent::Read RUNTIME_PHASE);
			if(!IsUndefined(value) && !IsNull(value))
			{
				auto from = ToObject(value);
				for(auto key : from->OwnPropertyKeys(VIA_CLASS(from->GetClass())))
				{
					auto property = from->GetOwnProperty(key PUBLIC_NAMESPACES READ_WRITE_ACCESS);
					if(property.has_value() && property->Enumerable)
					{
						auto property_value = from->Get(key, from, Intent::Read PUBLIC_NAMESPACES WITH_CLASS(from->GetClass()) NO_ENVIRONMENT RUNTIME_PHASE);
						if(!object->DefineOwnProperty(key,
							JSPropertyDescriptor().withValue(property_value).withWritable(true).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
						{
							JS::Throw(JS::New<TypeError> __LOC());
						}
					}
				}
			}
			return object;
		}));
}

//// Node::ComputedPropertyName

void JS::ParseNode::ComputedPropertyName::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;

	expression->Validate(cxt);

	if(Version::ES6 <= CurrentVersion && cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
		Static = cxt.Static;
		Initializer = cxt.Initializer;
		strict = cxt.IsStrict();
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::ComputedPropertyName::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return expression->CompileExpression(initial_state WITH_PHASE(phase));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::ComputedPropertyName::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	auto state = initial_state;
	return CompileExpression(state RUNTIME_PHASE).Apply(std::function<JSIdentifierReference(JSValue)>([VIA_PHASE(phase)](JSValue value)
	{
		JSValue evaluated_name = value.GetValue();

		if(auto name_object = evaluated_name.get_if<JSObject>())
		{
			if(auto qname = std::dynamic_pointer_cast<JS::Class::QName>(*name_object))
			{
				return JSIdentifierReference(qname->localName);
			}
		}
		if(CurrentVersion < Version::ES6)
		{
			return JSIdentifierReference(evaluated_name.ToString(VIA_PHASE(phase)));
		}
		else
		{
			return JSIdentifierReference(evaluated_name.ToPropertyKey(VIA_PHASE(phase)));
		}
	}));
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::ComputedPropertyName::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSValue, JS::Runtime::ClassContext> state;
	state = initial_state.Then(expression, &ParseNode::CompileExpression RUNTIME_PHASE);

	if(Static)
	{
		JS::Interpreter::State<JSValue, JSValue, JS::Runtime::ClassContext> state1;
		if(Initializer)
		{
			state1 = state.Then(Initializer, &ParseNode::CompileExpression RUNTIME_PHASE);
		}
		else
		{
			state1 = state.Push<JSValue>(JS_undefined);
		}

		return state1.Apply(std::function<JS::Runtime::ClassContext(JSValue, JSValue, JS::Runtime::ClassContext)>([this](JSValue initial_value, JSValue property_value, JS::Runtime::ClassContext context)
		{
			context.DefinePublicStaticVariable(
				JSPropertyKey(property_value.ToPropertyKey(VIA_RUNTIME_PHASE)),
				JSProperty::MakeData(
					initial_value,
					/* Writable = */ true,
					/* Enumerable = */ true,
					/* Configurable = */ true));
			return context;
		}));
	}
	else
	{
		JS::Interpreter::State<JSFunction, JSValue, JS::Runtime::ClassContext> state1 = state.Obtain(std::function<JSFunction(JSValue, JS::Runtime::ClassContext)>([this]([[maybe_unused]] JSValue _1, JS::Runtime::ClassContext context)
		{
			gcn::gc_ptr<JS::Class::Function> field_initializer_function = nullptr;
			if(Initializer != nullptr)
			{
				field_initializer_function = JS::New<JS::Class::Function>(
					JS::GetCurrentRealm(), u"", 0, // name and length are not exposed
					new JS::ParseNode::FunctionalElement(JS::Parser::Location(), new ReturnStatement(JS::Parser::Location(), Initializer)),
					strict ? JS::Class::Function::ThisModeType::Strict : JS::Class::Function::ThisModeType::Global,
					GetCurrentExecutionContext()->LexicalEnvironment,
					GetCurrentExecutionContext()->PrivateEnvironment
					PUBLIC_NAMESPACES,
					JS::Class::Function::FunctionKindType::Normal);
				field_initializer_function->HomeObject = context.ClassPrototype;
			}
			return field_initializer_function;
		}));

		return state1.Apply(std::function<JS::Runtime::ClassContext(JSFunction, JSValue, JS::Runtime::ClassContext)>([this](JSFunction field_initializer_function, JSValue property_value, JS::Runtime::ClassContext context)
		{
			context.DefinePublicInstanceVariable(
				JSPropertyKey(property_value.ToPropertyKey(VIA_RUNTIME_PHASE)),
				field_initializer_function);
			return context;
		}));
	}
}

//// Node::FilteringPredicate

//// Node::CallExpression

void JS::ParseNode::CallExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	in_tail_position = !is_new && cxt.tail_position
		&& !dynamic_cast<SuperExpression *>(function)
		&& !dynamic_cast<ImportExpression *>(function);
	cxt.tail_position = false;

#if ENABLE_ES4
	if(IsVersionES4() && dynamic_cast<SuperExpression *>(function))
	{
		cxt.CurrentValidationContext->superclass_constructor_called = cxt.superclass_constructor_called = true;
	}
#endif

	function->Validate(cxt);
	for(auto argument : arguments)
	{
		argument->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::CallExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<ImportExpression *>(function))
	{
		assert(arguments.size() == 1 || arguments.size() == 2); // TODO: should have been caught earlier
		struct runcontext
		{
			std::set<JS::Runtime::LoadedModuleRequestRecord> * referrer_loaded_modules = nullptr;
			JSValue specifier;
			JSValue options = JS_undefined;
		};

		JS::Interpreter::State<runcontext> state =
			initial_state.Apply(std::function<runcontext()>(
				[]() -> runcontext
		{
			runcontext context;
			if(auto referrer = JS::GetActiveScriptOrModule())
			{
				context.referrer_loaded_modules = referrer->GetLoadedModules();
			}
			else if(auto realm = JS::GetCurrentRealm())
			{
				context.referrer_loaded_modules = &realm->LoadedModules;
			}
			else
			{
				// TODO: this should not happen
			}
			return context;
		})).Then(arguments[0], &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<runcontext(JSValue, runcontext)>(
			[](JSValue specifierRef, runcontext context) -> runcontext
		{
			context.specifier = specifierRef.GetValue();
			return context;
		}));

		if(arguments.size() > 1)
		{
			state = state.Then(arguments[1], &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<runcontext(JSValue, runcontext)>(
				[](JSValue optionsRef, runcontext context) -> runcontext
			{
				context.options = optionsRef.GetValue();
				return context;
			}));
		}

		return state.Apply(std::function<JSValue(runcontext)>(
			[](runcontext context) -> JSValue
		{
			JS::Runtime::PromiseCapability<JS::Class::Promise> promiseCapability(Intrinsics()->Promise);
			JS::Runtime::ModuleRequestRecord moduleRequest;

			std::optional<JSValue> abruptResult;
			try
			{
				moduleRequest.Specifier = context.specifier.ToString(VIA_RUNTIME_PHASE);
				if(GetNonNullObject(context.options))
				{
					auto attributesObj = JS::Get(context.options, u"with");
					if(auto attributesObject = GetNonNullObject(attributesObj))
					{
						auto entries = JS::Runtime::Builtin::EnumerableOwnProperties(attributesObject.value(), JS::Runtime::EnumerationKind::KeyValue); // TODO: move to more permissive namespace?
						for(auto entry : entries)
						{
							auto key = JS::Get(entry, u"0");
							auto value = JS::Get(entry, u"1");
							if(auto keyString = GetString(key))
							{
								if(auto valueString = GetString(value))
								{
									moduleRequest.Attributes[*keyString] = *valueString;
								}
								else
								{
									abruptResult = JS::New<TypeError> __LOC(u"non-string value provided as attribute for dynamic import");
									break;
								}
							}
						}

						if(!abruptResult.has_value())
						{
							if(!JS::Host->AllImportAttributesSupported(moduleRequest.Attributes))
							{
								abruptResult = JS::New<SyntaxError> __LOC(u"unsupported import attribute by host");
							}
						}
					}
					else if(!IsUndefined(attributesObject.value()))
					{
						abruptResult = JS::New<TypeError> __LOC();
					}
				}
				else if(!IsUndefined(context.options))
				{
					abruptResult = JS::New<TypeError> __LOC();
				}
			}
			catch(JS::Class::Internal::Value * thrown_value)
			{
				abruptResult = JSValue::WrapValue(thrown_value);
			}

			if(abruptResult.has_value())
			{
				JS::Call(promiseCapability.Reject, JS_undefined, { abruptResult.value() } RUNTIME_PHASE);
			}
			else
			{
				JS::Host->LoadImportedModule(context.referrer_loaded_modules, moduleRequest, std::make_shared<JS::Runtime::DynamicPayload>(promiseCapability));
			}

			return promiseCapability.Promise;
		}));
	}
	else if(dynamic_cast<SuperExpression *>(function))
	{
		// TODO: ES4 super(X) expression is different from super(X) statement
		if(CurrentVersion < Version::ES6)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		struct runcontext
		{
			std::shared_ptr<JS::Runtime::FunctionEnvironmentRecord> this_environment = nullptr;
			JSFunction new_target = nullptr;
			JSValue function_object = JS_undefined;
			JSFunction function_reference = nullptr;
			std::vector<JSValue> argument_values;
		};

		JS::Interpreter::State<runcontext> state = initial_state.Apply(std::function<runcontext()>([]()
		{
			runcontext context;

			context.this_environment = std::dynamic_pointer_cast<JS::Runtime::FunctionEnvironmentRecord>(GetCurrentExecutionContext()->GetThisEnvironment());

			if(context.this_environment == nullptr)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}

			if(!context.this_environment->NewTarget.has_value())
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}

			context.new_target = context.this_environment->NewTarget.value();
			context.function_object = context.this_environment->FunctionObject->GetPrototypeOf().value(); // TODO: check? should not fail

			return context;
		}));

		for(auto argument : arguments)
		{
			state = state.Then(argument, &ParseNode::CompileExpression WITH_PHASE(phase))
				.Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue evaluated_argument, runcontext context)
			{
				context.argument_values.push_back(evaluated_argument.GetValue());
				return context;
			}));
		}

		return state.Apply(std::function<JSValue(runcontext)>([VIA_PHASE(phase)](runcontext context) -> JSValue
		{
			if(auto function_reference = GetNonNullObject(context.function_object).value_or(nullptr))
			{
				if(function_reference->GetInvocability() != JS::Invocability::NonCallable)
				{
					JSValue this_argument = function_reference->Construct(context.argument_values, context.new_target WITH_PHASE(phase));
					context.this_environment->BindThisValue(this_argument);
					if(Version::ESNEXT <= CurrentVersion)
					{
						if(auto ordinary_function_reference = std::dynamic_pointer_cast<JS::Class::Function>(function_reference))
						{
							ordinary_function_reference->InitializeInstanceElements(GetNonNullObject(this_argument).value());
						}
					}
					return this_argument;
				}
			}
			JS::Throw(JS::New<TypeError> __LOC());
		}));
	}
	else if(is_new)
	{
		struct runcontext
		{
			JSValue reference;
			JSValue function_object;
			JSFunction function_reference;
			std::vector<JSValue> argument_values;
		};

		JS::Interpreter::State<runcontext> state = initial_state.Push(runcontext{});
		state = state.Then(function, &ParseNode::CompileExpression WITH_PHASE(phase))
			.Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue reference, runcontext context)
		{
			context.reference = reference;
			context.function_object = reference.GetValue(Intent::Call);
			return context;
		}));

		for(auto argument : arguments)
		{
			state = state.Then(argument, &ParseNode::CompileExpression WITH_PHASE(phase))
				.Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue evaluated_argument, runcontext context)
			{
				context.argument_values.push_back(evaluated_argument.GetValue());
				return context;
			}));
		}

		return state.Apply(std::function<JSValue(runcontext)>([VIA_PHASE(phase)](runcontext context) -> JSValue
		{
			if(auto function_reference = GetNonNullObject(context.function_object).value_or(nullptr))
			{
				if(function_reference->GetInvocability() == JS::Invocability::Constructor)
				{
					return function_reference->Construct(context.argument_values, JSFunction::MakeFunction(function_reference) WITH_PHASE(phase));
				}
			}
			JS::Throw(JS::New<TypeError> __LOC(u"new expression not a function"));
		}));
	}
	else
	{
		struct runcontext
		{
			JSValue reference = JS_undefined;
			JSValue function_object = JS_undefined;
			JSValue this_argument = JS_undefined;
			std::vector<JSValue> argument_values;
		};

		JS::Interpreter::State<runcontext> state = initial_state.Push(runcontext{})
			.Then(function, &ParseNode::CompileExpression WITH_PHASE(phase))
			.Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue reference, runcontext context)
		{
			context.reference = reference;
			// AS4 evaluates the arguments first, but SpiderMonkey 1.8.5 begins with the function reference
			if(!Supports_E4X() || Version::ES5 <= CurrentVersion)
			{
				context.function_object = reference.ResolveMethod();
			}
			context.this_argument = CurrentVersion < Version::ES5 ? JS_null : JS_undefined;

			if(auto lexical_reference = reference.get_if<JS::Runtime::LexicalReference>())
			{
				if(Version::ES6 <= CurrentVersion)
				{
					auto this_value = lexical_reference->Base->WithBaseObject();
					if(this_value.has_value())
					{
						context.this_argument = this_value.value();
					}
					else
					{
						context.this_argument = JS_undefined;
					}
				}
				// ES5 and earlier set this up during the call invocation instead
			}
			else if(auto property_reference = reference.get_if<JS::Runtime::MemberReference>())
			{
				context.this_argument = property_reference->ThisValue;
			}
#if ENABLE_ES4
			else if(auto property_reference = reference.get_if<JS::Runtime::BracketReference>())
			{
				context.this_argument = property_reference->ThisValue;
			}
#endif
			return context;
		}));

		for(auto argument : arguments)
		{
			state = state.Then(argument, &ParseNode::CompileExpression WITH_PHASE(phase))
				.Apply(std::function<runcontext(JSValue, runcontext)>([](JSValue evaluated_argument, runcontext context)
			{
				context.argument_values.push_back(evaluated_argument.GetValue());
				return context;
			}));
		}

		if(in_tail_position && Version::ES6 <= CurrentVersion)
		{
			return state.Apply(std::function<void(runcontext)>([](runcontext context)
			{
				auto current_context = GetCurrentExecutionContext();
				if(Supports_E4X() && CurrentVersion <= Version::ES5)
				{
					context.function_object = context.reference.ResolveMethod();
				}
				if(auto function = GetFunction(context.function_object)) // in ES4, this can also be a MethodClosure
				{
					// initiate new function
					if(JS::Begin_Call(function.value(), context.this_argument, context.argument_values))
					{
						// terminate currently executing function
						current_context->state = JS::Runtime::ExecutionContext::ExecutionState::Completed;
						// remove currently executing function from the context stack
						JS::Runtime::ExecutionContext::GetContextStack().erase(
							JS::Runtime::ExecutionContext::GetContextStack().end() - 2);
						// execution should continue in the new context
					}
				}
				else
				{
					JS::Throw(JS::New<TypeError> __LOC("not callable"));
				}
			})).WaitForReturn(); // note: only returns if this was a built-in function
		}
		else
		{
			return state.Apply(std::function<JSValue(runcontext)>([VIA_PHASE(phase)](runcontext context) -> JSValue
			{
				if(Supports_E4X() && CurrentVersion <= Version::ES5)
				{
					context.function_object = context.reference.ResolveMethod();
				}
				if(auto function = GetFunction(context.function_object)) // in ES4, this can also be a MethodClosure
				{
					return Call(function.value(), context.this_argument, context.argument_values WITH_PHASE(phase));
				}
				else
				{
					JS::Throw(JS::New<TypeError> __LOC("not callable"));
				}
			}));
		}
	}
}

#if ENABLE_ES4
JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::CallExpression::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(IsVersionES4() && dynamic_cast<SuperExpression *>(function))
	{
		// TODO: invalid outside constructor
		// TODO: must be called only once

		JS::Interpreter::State<std::vector<JSValue>, JSValue> state = initial_state.Apply(std::function<JSValue()>([]() -> JSValue
		{
			return GetCurrentExecutionContext()->GetThisEnvironment()->GetThisBinding();
		})).Push<std::vector<JSValue>>({});

		for(auto argument : arguments)
		{
			state = state.Then(argument, &ParseNode::CompileExpression RUNTIME_PHASE)
				.Apply(std::function<std::vector<JSValue>(JSValue, std::vector<JSValue>)>([](JSValue argument_reference, std::vector<JSValue> argument_values) -> std::vector<JSValue>
			{
				argument_values.push_back(argument_reference.GetValue());
				return argument_values;
			}));
		}

		return state.Apply(std::function<JSValue(std::vector<JSValue>, JSValue)>([](std::vector<JSValue> argument_values, JSValue this_argument) -> JSValue
		{
			GetClassForType(GetCurrentExecutionContext()->GetClassEnvironment()->GetClass()->Super.value())->Init(this_argument, argument_values);
			return this_argument;
		})).Option();
	}
	else
	{
		return ParseNode::CompileStatement(initial_state);
	}
}
#endif

//// Node::IndexExpression

void JS::ParseNode::IndexExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.Strict;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		open_namespaces = cxt.OpenNamespaces;
	}
#endif

	object->Validate(cxt);
	for(auto argument : index)
	{
		argument->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::IndexExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<std::vector<JSValue>, JSValue> state = object->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue object_reference) -> JSValue
	{
		if(!object_reference.get_if<JS::Runtime::SuperReference>())
		{
			object_reference = object_reference.GetValue();
		}
		return object_reference;
	})).Push(std::vector<JSValue>{});
	for(auto argument : index)
	{
		state = state.Then(argument, &ParseNode::CompileExpression WITH_PHASE(phase))
			.Apply(std::function<std::vector<JSValue>(JSValue, std::vector<JSValue>)>([](JSValue argument_reference, std::vector<JSValue> argument_values) -> std::vector<JSValue>
		{
			argument_values.push_back(argument_reference.GetValue());
			return argument_values;
		}));
	}
	return state.Apply(std::function<JSValue(std::vector<JSValue>, JSValue)>([this WITH_PHASE(phase)](std::vector<JSValue> argument_values, JSValue object_reference) -> JSValue
	{
		return object_reference.CreateBracketReference(argument_values, JS::Runtime::ReferenceContext(strict WITH_NAMESPACES(open_namespaces) WITH_PHASE(phase)));
	}));
}

//// Node::MemberAccessExpression

void JS::ParseNode::MemberAccessExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.Strict;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		open_namespaces = cxt.OpenNamespaces;
	}
#endif

	object->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::MemberAccessExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSValue> state = object->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue object_reference) -> JSValue
	{
		if(!object_reference.get_if<JS::Runtime::SuperReference>())
		{
			object_reference = object_reference.GetValue();
		}
		return object_reference;
	}));

	return state.Then(name, &ParseNode::CompileName WITH_PHASE(phase))
		.Apply(std::function<JSValue(JSIdentifierReference, JSValue)>([this WITH_PHASE(phase)](JSIdentifierReference qualified_name, JSValue object_reference) -> JSValue
	{
		return object_reference.CreateReference(qualified_name, JS::Runtime::ReferenceContext(strict WITH_NAMESPACES(open_namespaces) WITH_PHASE(phase)));
	}));
}

#if ENABLE_ES4
//// Node::TypeApplication
#endif

//// Node::DescendentExpression

//// Node::TaggedTemplate

//// Node::OptionalChain

//// Node::FormalParameters

#if ENABLE_ES4
//// Node::VoidType

JSType JS::ParseNode::VoidType::EvaluateType()
{
	return Intrinsics()->Void_class;
}
#endif

//// Node function

JSString JS::ParseNode::GetStringForName(JSBindingName name)
{
	return std::visit([](auto&& name)
	{
		using T = std::decay_t<decltype(name)>;
		if constexpr(std::is_same_v<T, JSPropertyKey>)
		{
			return std::visit([](auto&& name)
			{
				using T = std::decay_t<decltype(name)>;
				if constexpr(std::is_same_v<T, JSString>)
				{
					return name;
				}
				else if constexpr(std::is_same_v<T, JSSymbol>)
				{
					if(name->Description.has_value())
					{
						return u"[" + name->Description.value() + u"]";
					}
					else
					{
						return JSString(u"");
					}
				}
				else
				{
					static_assert(false);
				}
			}, name);
		}
		else if constexpr(std::is_same_v<T, JSPrivateName>)
		{
			return name->Description;
		}
		else
		{
			static_assert(false);
		}
	}, name);
}

//// Node::FunctionalElement

void JS::ParseNode::FunctionalElement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.IsStrict();
}

JS::Class::Function::ThisModeType JS::ParseNode::FunctionalElement::GetThisMode()
{
	// a simplified version of Node::FunctionDefinition
#if ENABLE_ES4
	if(IsVersionES4())
	{
		return !strict
			? JS::Class::Function::ThisModeType::Unchecked // same as Global
			: JS::Class::Function::ThisModeType::Static; // same as Lexical
	}
	else
#endif
	if(Version::ES5 <= CurrentVersion && strict)
	{
		return JS::Class::Function::ThisModeType::Strict;
	}
	else
	{
		return JS::Class::Function::ThisModeType::Global;
	}
}

std::vector<JS::Parser::ParseNode *> JS::ParseNode::FunctionalElement::GetArguments() const
{
	return {};
}

//// Node::FunctionDefinition

JS::Class::Function::FunctionKindType JS::ParseNode::FunctionDefinition::GetFunctionKind()
{
	if(CurrentVersion < Version::ES6)
	{
		return JS::Class::Function::FunctionKindType::Normal;
	}
	else if(function_kind == FunctionKind::Generator)
	{
		if(function_position == FunctionPosition::ClassConstructor)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		if(Version::ES9 <= CurrentVersion && function_type == FunctionType::Async)
		{
			return JS::Class::Function::FunctionKindType::AsyncGenerator;
		}
		else
		{
			return JS::Class::Function::FunctionKindType::Generator;
		}
	}
	else if(function_position == FunctionPosition::ClassConstructor)
	{
		return JS::Class::Function::FunctionKindType::ClassConstructor;
	}
	else
	{
		if(Version::ES8 <= CurrentVersion && function_type == FunctionType::Async)
		{
			return JS::Class::Function::FunctionKindType::Async;
		}
		else
		{
			return JS::Class::Function::FunctionKindType::Normal;
		}
	}
}

JS::Class::Function::ThisModeType JS::ParseNode::FunctionDefinition::GetThisMode()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(class_name.has_value()) // inside class
		{
			assert(function_position == FunctionPosition::Statement);
			if(attributes.Category != JS::Runtime::MemberModifier::Static)
			{
				return JS::Class::Function::ThisModeType::Instance; // same as Strict
			}
			else if(attributes.Prototype)
			{
				return JS::Class::Function::ThisModeType::Prototype; // like Global but checked
			}
			else
			{
				return JS::Class::Function::ThisModeType::Static; // same as Lexical
			}
		}
		else
		{
			assert(function_position == FunctionPosition::Statement || function_type == FunctionType::Normal); // function expressions cannot be getter/setter
			if(!strict && IsVersionES4Plain() && function_type == FunctionType::Normal)
			{
				return JS::Class::Function::ThisModeType::Unchecked; // same as Global
			}
			else if(attributes.Prototype)
			{
				assert(function_position == FunctionPosition::Statement);
				return JS::Class::Function::ThisModeType::Prototype; // like Global but checked
			}
			else
			{
				return JS::Class::Function::ThisModeType::Static; // same as Lexical
			}
		}
	}
	else
#endif
	if(Version::ES6 <= CurrentVersion && function_kind == FunctionKind::Arrow)
	{
		return JS::Class::Function::ThisModeType::Lexical;
	}
	else if(Version::ES5 <= CurrentVersion && strict)
	{
		return JS::Class::Function::ThisModeType::Strict;
	}
	else
	{
		return JS::Class::Function::ThisModeType::Global;
	}
}

JSString JS::ParseNode::FunctionDefinition::GetFunctionName(std::optional<JSString> Identifier)
{
	if(Version::ES6 <= CurrentVersion && function_position == FunctionPosition::ClassConstructor)
	{
		return class_name.value();
	}
	else if(name.has_value())
	{
		JSString prefix = u"";
		if(Version::ES5 <= CurrentVersion)
		{
			switch(function_type)
			{
			case FunctionType::Normal:
			case FunctionType::Async:
				break;
			case FunctionType::Get:
				prefix = u"get ";
				break;
			case FunctionType::Set:
				prefix = u"set ";
				break;
			}
		}

		return prefix + Identifier.value();
	}
	else
	{
		if(function_position == FunctionPosition::Statement)
		{
			return u"default"; // this should be an export default statement
		}
		else
		{
			return u"";
		}
	}
}

JSString JS::ParseNode::FunctionDefinition::GetName()
{
	if((Version::ES6 <= CurrentVersion && function_position == FunctionPosition::ClassConstructor)
	|| !name.has_value())
	{
		return GetFunctionName(std::optional<JSString>{});
	}
	else
	{
		return GetFunctionName(dynamic_cast<JS::ParseNode::Identifier *>(name.value())->StringValue);
	}
}

JS::Interpreter::State<JSPropertyKey> JS::ParseNode::FunctionDefinition::CompileFunctionName(JS::Interpreter::State<> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(Version::ES6 <= CurrentVersion && function_position == FunctionPosition::ClassConstructor)
	{
		return initial_state.Push<JSPropertyKey>(class_name.value());
	}
	else if(!name.has_value())
	{
		return initial_state.Push<JSPropertyKey>(u"default"); // must be default statement
	}
	else if(auto identifier = dynamic_cast<JS::ParseNode::Identifier *>(name.value()))
	{
		return initial_state.Push<JSPropertyKey>(identifier->StringValue);
	}
	else
	{
		return name.value()->CompileExpression(initial_state RUNTIME_PHASE).Apply(std::function<JSPropertyKey(JSValue)>([this](JSValue name_value) -> JSPropertyKey
		{
			if(CurrentVersion < Version::ES6)
			{
				return name_value.GetValue().ToString(VIA_RUNTIME_PHASE);
			}
			else
			{
				return name_value.GetValue().ToPropertyKey(VIA_RUNTIME_PHASE);
			}
		}));
	}
}

uint32_t JS::ParseNode::FunctionDefinition::GetLength()
{
	if(parameters->arguments.size() == 0)
	{
		return 0;
	}
	else if(dynamic_cast<JS::ParseNode::RestExpression *>(parameters->arguments.back()))
	{
		return parameters->arguments.size() - 1;
	}
	else
	{
		return parameters->arguments.size();
	}
}

gcn::gc_ptr<JS::Class::Function> JS::ParseNode::FunctionDefinition::CreateFunction(JSString Name, JSObject ConstructorParent, std::shared_ptr<JS::Runtime::EnvironmentRecord> env)
{
	auto Realm = JS::GetCurrentRealm();
	uint32_t Length = GetLength();

	gcn::gc_ptr<JS::Class::Function> function;
#if ENABLE_ES4
	if(IsVersionES4()
		&& (GetThisMode() == JS::Class::Function::ThisModeType::Unchecked
		|| GetThisMode() == JS::Class::Function::ThisModeType::Prototype))
	{
		function =
			JS::NewWithClass<JS::Class::Function>(
				Intrinsics()->PrototypeFunction_class,
				Realm, Name, Length,
				this,
				GetThisMode(),
				env,
				nullptr
				WITH_NAMESPACES(open_namespaces));
		function->SourceText = SourceText;
	}
	else
#endif
	{
		JSObject function_prototype;

		if(Version::ES6 <= CurrentVersion && function_position == FunctionPosition::ClassConstructor)
		{
			if(function_kind != FunctionKind::Function)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}

			assert(ConstructorParent != nullptr);
			function_prototype = ConstructorParent;
		}
		else
		{
			switch(function_kind)
			{
			case FunctionKind::Generator:
				if(Version::ES6 <= CurrentVersion)
				{
					if(function_type == FunctionType::Async && Version::ESNEXT <= CurrentVersion)
					{
						function_prototype = Intrinsics()->AsyncGeneratorFunction_prototype;
					}
					else
					{
						function_prototype = Intrinsics()->GeneratorFunction_prototype;
					}
					break;
				}
				// pretend it is a regular function
				[[fallthrough]];
			case FunctionKind::Function:
			case FunctionKind::Arrow:
				if(function_type == FunctionType::Async && Version::ESNEXT <= CurrentVersion)
				{
					function_prototype = Intrinsics()->AsyncFunction_prototype;
				}
				else
				{
					function_prototype = Intrinsics()->Function_prototype;
				}
				break;
			}
		}

		function =
			JS::NewWithPrototype<JS::Class::Function>(
				function_prototype,
				Realm, Name, Length,
				this,
				GetThisMode(),
				env,
				Version::ESNEXT <= CurrentVersion ? GetCurrentExecutionContext()->PrivateEnvironment : nullptr
				WITH_NAMESPACES(open_namespaces),
				GetFunctionKind());
		function->SourceText = SourceText;
	}

#if ENABLE_ES4
	if(IsVersionES4() && !IsVersionES4Plain())
	{
		declared_function = function;
	}
#endif

	if(Version::ES6 <= CurrentVersion)
	{
		switch(function_kind)
		{
		case FunctionKind::Function:
			break;
		case FunctionKind::Generator:
			function->is_generator = true;
			break;
		case FunctionKind::Arrow:
			function->is_arrow = true;
			break;
		}
	}

	if(Version::ESNEXT <= CurrentVersion)
	{
		if(function_type == FunctionType::Async)
		{
			function->is_async = true;
		}
	}

	return function;
}

gcn::gc_ptr<JS::Class::Function> JS::ParseNode::FunctionDefinition::CreateFunction(JSString Name, JSObject ConstructorParent)
{
	return CreateFunction(Name, ConstructorParent, GetCurrentExecutionContext()->LexicalEnvironment);
}

gcn::gc_ptr<JS::Class::Function> JS::ParseNode::FunctionDefinition::SetupPrototype(gcn::gc_ptr<JS::Class::Function> function, JSObject ClassPrototype)
{
#if ENABLE_ES4
	if(IsVersionES4() && function->GetClass() != Intrinsics()->PrototypeFunction_class)
	{
		return function;
	}
#endif

	// if the prototype object needs its constructor attribute set to the function
	bool needs_constructor = false;
	// if the prototype attribute of the function object needs to be set
	bool needs_prototype = false;
	bool writable_prototype = true;
	JSValue prototype;

	if(Version::ES6 <= CurrentVersion && function_position == FunctionPosition::ClassConstructor)
	{
		if(member_of_derived_class)
		{
			function->ConstructorKind = JS::Class::Function::ConstructorKindType::Derived;
		}
		else
		{
			function->ConstructorKind = JS::Class::Function::ConstructorKindType::Base;
		}

		needs_prototype = true;
		writable_prototype = false;
		needs_constructor = true;
		if(ClassPrototype == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		prototype = ClassPrototype;
	}
	else
	{
		switch(function_kind)
		{
		case FunctionKind::Function:
			if(Version::ES5 <= CurrentVersion && (function_position == FunctionPosition::Method || function_position == FunctionPosition::Property))
			{
				function->ConstructorKind = JS::Class::Function::ConstructorKindType::NonConstructor;
			}
			else
			{
				function->ConstructorKind = JS::Class::Function::ConstructorKindType::Base;
			}

			if(function_type != FunctionType::Async || CurrentVersion < Version::ESNEXT)
			{
				prototype = JS::New<JS::Class::OrdinaryObject>(); // uses Intrinsics()->Object_prototype as its prototype

				needs_prototype = true;
				needs_constructor = true;
			}
			break;
		case FunctionKind::Generator:
			if(CurrentVersion < Version::ESNEXT)
			{
				function->ConstructorKind = JS::Class::Function::ConstructorKindType::Derived;
			}
			else
			{
				function->ConstructorKind = JS::Class::Function::ConstructorKindType::NonConstructor;
			}

			if(function_type != FunctionType::Async || CurrentVersion < Version::ESNEXT)
			{
				prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(Intrinsics()->GeneratorPrototype);
			}
			else
			{
				prototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(Intrinsics()->AsyncGeneratorPrototype);
			}
			needs_prototype = true;
			break;
		case FunctionKind::Arrow:
			function->ConstructorKind = JS::Class::Function::ConstructorKindType::NonConstructor;
			break;
		}
	}

	if(needs_constructor)
	{
		if(!GetNonNullObject(prototype).value()->DefineOwnProperty(
			JSString(u"constructor"),
			JSPropertyDescriptor()
				.withValue(function)
				.withWritable(writable_prototype)
				.withEnumerable(false)
				.withConfigurable(false)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}

	if(needs_prototype)
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			function->add_slot(JS::Intrinsics()->PrototypeFunction_class, u"prototype" PUBLIC_NAMESPACES, prototype);
			//function->Slots[dynamic_cast<JSInstanceVariable>(JS::Intrinsics()->PrototypeFunction_class->GetDerivedInstanceProperty(u"prototype" PUBLIC_NAMESPACES WRITE_ACCESS).value())] = prototype;
		}
		else
#endif
		if(!function->DefineOwnProperty(
			JSString(u"prototype"),
			JSPropertyDescriptor()
				.withValue(prototype)
				.withWritable(writable_prototype)
				.withEnumerable(false)
				.withConfigurable(false)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS))
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}

	return function;
}

std::vector<JS::Parser::ParseNode *> JS::ParseNode::FunctionDefinition::GetArguments() const
{
	return parameters->arguments;
}

void JS::ParseNode::FunctionDefinition::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position will be changed later
	if(function_position == FunctionPosition::Statement)
	{
		if(!cxt.CurrentValidationContext)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		if(name.has_value())
		{
			JSString Name = GetName();

			switch(cxt.Mode)
			{
			default:
				break;
			case JS::Runtime::EnvironmentMode::Regional:
			case JS::Runtime::EnvironmentMode::Local:
				if(cxt.CurrentValidationContext->functions_to_declare.find(Name) != cxt.CurrentValidationContext->functions_to_declare.end())
				{
					cxt.CurrentValidationContext->functions_to_declare[Name]->active = false;
				}
				cxt.CurrentValidationContext->functions_to_declare[Name] = this;
				break;
			}
		}
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		open_namespaces = cxt.OpenNamespaces;
		attributes = cxt.Attributes;
		cxt.Attributes = JSCompoundAttribute();

		if(cxt.Mode == JS::Runtime::EnvironmentMode::Class)
		{
			if(attributes.Category == JS::Runtime::MemberModifier::Static)
			{
				entity_mode = JS::Runtime::EntityMode::Singleton;
			}
			else
			{
				// also constructors
				entity_mode = JS::Runtime::EntityMode::Instance;
			}
		}
		else
		{
			if(!cxt.Strict
			&& cxt.Mode == JS::Runtime::EnvironmentMode::Regional
			&& function_type == FunctionType::Normal
			&& attributes.IsEmpty()
			&& IsVersionES4Plain())
			{
				entity_mode = JS::Runtime::EntityMode::Hoisted;
			}
			else
			{
				entity_mode = JS::Runtime::EntityMode::Singleton;
			}
		}
	}
#endif

	if(Version::ES5 <= SelectedVersion)
	{
		if(auto statements = dynamic_cast<JS::ParseNode::Block *>(body))
		{
			for(auto statement : statements->statements)
			{
				if(!ProcessDirective(statement))
				{
					break;
				}
			}
		}
		strict = JS::StrictMode;
	}

#if ENABLE_ES4
	if(!IsVersionES4())
#endif
	{
		redeclare_function_required = cxt.Mode == JS::Runtime::EnvironmentMode::Local && !JS::StrictMode;
	}

	if(cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			class_name = cxt.class_name.value();
		}
		else
#endif
		if(Version::ES6 <= CurrentVersion)
		{
			Static = cxt.Static;
			if(cxt.class_name.has_value())
			{
				class_name = cxt.class_name.value();
			}
			member_of_derived_class = cxt.is_derived_class;
		}
	}

	cxt.Strict = JS::StrictMode;
	cxt.Mode = JS::Runtime::EnvironmentMode::Regional;

	strict = cxt.Strict;
#if ENABLE_ES4
	if(IsVersionES4())
	{
		cxt.is_package_range = false;

		if(IsVersionES4Constructor())
		{
			// at this point we do not know yet whether the superclass constructor gets called explicitly
			// validating the block will determine the actual value
			// setting this parameter to true will signal that we might need to call the superclass

			dynamic_cast<Block *>(body)->call_superclass_constructor = true;
		}
	}
#endif

#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(function_position == FunctionPosition::Expression
			? !strict
			: entity_mode == JS::Runtime::EntityMode::Hoisted)
		{
			cxt.entity_mode = JS::Runtime::EntityMode::Hoisted;
		}
		else
		{
			cxt.entity_mode = JS::Runtime::EntityMode::Singleton;
		}
	}
	else
#endif
	{
		cxt.entity_mode = JS::Runtime::EntityMode::Unknown;
	}

	std::multiset<JSString> parameter_names;
	bool has_duplicates = false;
	bool has_parameter_expressions = false;
	for(auto parameter : parameters->arguments)
	{
		has_duplicates |= parameter->CollectBoundNames(parameter_names);
		has_parameter_expressions |= parameter->ContainsInitialization();
	}

	parameter_evaluation_environment_needed = Version::ES6 <= CurrentVersion && !strict && has_parameter_expressions;
	parameters_bound_lexically = !has_duplicates;
	parameter_closure_environment_needed = Version::ES6 <= CurrentVersion && has_parameter_expressions;

	// set up declaration kind of arguments
	if(CurrentVersion < Version::ES5 || has_duplicates)
	{
		cxt.declaration_kind = DeclarationKind::Var;
	}
	else
	{
		cxt.declaration_kind = DeclarationKind::Let;
	}

	if(name.has_value())
	{
		name.value()->Validate(cxt);
	}
	// "name" can influence the top-level await search, but not the rest of the body
	cxt.is_module_range = false;

	for(auto parameter : parameters->arguments)
	{
		parameter->Validate(cxt);
	}

#if ENABLE_ES4
	if(result_type.has_value())
	{
		result_type.value()->Validate(cxt);
	}
#endif

	cxt.tail_position = function_type != FunctionType::Async && function_kind != FunctionKind::Generator;

	cxt.CurrentValidationContext = &cxt;

	body->Validate(cxt);
}

gcn::gc_ptr<JS::Class::Function> JS::ParseNode::FunctionDefinition::EvaluateFunction(JSString Name) // TODO: move into CompileExpression
{
	std::shared_ptr<JS::Runtime::EnvironmentRecord> env;
	gcn::gc_ptr<JS::Class::Function> function;

	if(name.has_value())
	{
		env = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
		function = SetupPrototype(CreateFunction(Name, nullptr, env));
		JSBinding binding;
#if ENABLE_ES4
		if(IsVersionES4())
		{
			binding = JSProperty::MakeVariable(
				function,
				/* Writable = */ false,
				/* Enumerable = */ true,
				/* Explicit = */ false,
				/* type = */ Intrinsics()->Function_class);
		}
		else
#endif
		{
			binding = JSBinding::MakeImmutable(function, /* Strict = */ true);
		}
		env->CreateSingletonBinding(Name PUBLIC_NAMESPACES READ_WRITE_ACCESS, binding);
		return function;
	}
	else
	{
		return SetupPrototype(CreateFunction(Name, nullptr));
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::FunctionDefinition::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	// must be indirect to capture the current PrivateEnvironment
	JSString Name = GetName();
	return initial_state.Apply(std::function<JSValue()>([this, Name]() -> JSValue
	{
		return EvaluateFunction(Name);
	}));
}

JS::Interpreter::State<gcn::gc_ptr<JS::Class::Function>> JS::ParseNode::FunctionDefinition::CompileNamedExpression(JS::Interpreter::State<JSString>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Apply(std::function<gcn::gc_ptr<JS::Class::Function>(JSString)>([this](JSString Name) -> gcn::gc_ptr<JS::Class::Function>
	{
		return EvaluateFunction(Name);
	}));
}

#if ENABLE_ES4
JS::Runtime::SignatureType JS::ParseNode::FunctionDefinition::GetSignature()
{
	JS::Runtime::SignatureType Signature;

	std::optional<JS::Runtime::SignatureType> overridden_function_signature;
	if(overridden_property.has_value())
	{
		if(auto function = dynamic_cast<JSInstanceFunction>(overridden_property.value()))
		{
			overridden_function_signature = function->Signature;
		}
		else if(auto variable = dynamic_cast<JSInstanceVariable>(overridden_property.value()))
		{
			switch(function_type)
			{
			case FunctionType::Normal:
			default:
				JS::Throw(JS::New<InternalError> __LOC());
				break;
			case FunctionType::Get:
				overridden_function_signature = JS::Runtime::SignatureType{
					{}, false, variable->Type.value_or(Intrinsics()->Object_class)
				};
				break;
			case FunctionType::Set:
				overridden_function_signature = JS::Runtime::SignatureType{
					{JS::Runtime::Parameter{variable->Type.value_or(Intrinsics()->Object_class), std::optional<JSValue>{}}},
					false, Intrinsics()->Void_class
				};
				break;
			}
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}

	switch(function_type)
	{
	case FunctionType::Normal:
	default:
		break;
	case FunctionType::Get:
		if(parameters->arguments.size() != 0)
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		break;
	case FunctionType::Set:
		if(parameters->arguments.size() != 1
		|| !dynamic_cast<Identifier *>(parameters->arguments[0])) // rest arguments and default arguments are not allowed
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		break;
	}

	bool had_default_arguments = false;
	size_t parameter_index = 0;
	for(auto parameter : parameters->arguments)
	{
		if([[maybe_unused]] auto rest = dynamic_cast<RestExpression *>(parameter))
		{
			if(overridden_function_signature.has_value())
			{
				if(overridden_function_signature.value().parameters.size() != parameter_index)
				{
					JS::Throw(JS::New<DefinitionError> __LOC());
				}
				if(!overridden_function_signature.value().has_rest_parameter)
				{
					JS::Throw(JS::New<DefinitionError> __LOC());
				}
			}
			Signature.has_rest_parameter = true;
		}
		else
		{
			if(overridden_function_signature.has_value() && overridden_function_signature.value().parameters.size() <= parameter_index)
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}

			std::optional<JSValue> default_value;
			if(auto assignment = dynamic_cast<AssignmentExpression *>(parameter))
			{
				had_default_arguments = true;
				parameter = assignment->target;
				default_value = assignment->source->EvaluateExpression(JS::Runtime::ExecutionPhase::Compile).GetValue(Intent::Read, JS::Runtime::ExecutionPhase::Compile);

				dynamic_cast<Identifier *>(assignment->target)->DefaultValue = default_value;
			}
			else if(overridden_function_signature.has_value() && overridden_function_signature.value().parameters[parameter_index].default_value.has_value())
			{
				had_default_arguments = true;
				default_value = overridden_function_signature.value().parameters[parameter_index].default_value.value();

				dynamic_cast<Identifier *>(parameter)->DefaultValue = default_value;
			}
			else if(had_default_arguments)
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}

			//bool is_const = false;
			JSDeclaredType type = Intrinsics()->Object_class;
			if(auto identifier = dynamic_cast<Identifier *>(parameter))
			{
				//is_const = identifier->is_const;
				if(identifier->type.has_value())
				{
					type = identifier->type.value()->EvaluateType();
				}
			}
			if(overridden_function_signature.has_value() && type != overridden_function_signature.value().parameters[parameter_index].type)
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}

			Signature.parameters.push_back(
				JS::Runtime::Parameter{
					type,
					default_value});
			parameter_index ++;
		}
	}
	if(overridden_function_signature.has_value() && parameter_index < overridden_function_signature.value().parameters.size())
	{
		JS::Throw(JS::New<DefinitionError> __LOC());
	}

	JSDeclaredType return_type = Intrinsics()->Object_class;
	switch(function_type)
	{
	case FunctionType::Normal:
	default:
		if(IsVersionES4Constructor())
		{
			if(result_type.has_value())
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}
			return_type = Intrinsics()->Void_class;
			break;
		}
		// fallthru
	case FunctionType::Get:
		if(result_type.has_value())
		{
			return_type = result_type.value()->EvaluateType();
		}
		else
		{
			return_type = Intrinsics()->Object_class;
		}

		if(overridden_function_signature.has_value() && overridden_function_signature.value().return_type != return_type)
		{
			JS::Throw(JS::New<DefinitionError> __LOC());
		}
		break;
	case FunctionType::Set:
		if(result_type.has_value())
		{
			if(JSDeclaredType(result_type.value()->EvaluateType()) != JSDeclaredType(Intrinsics()->Void_class))
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}
		}
		return_type = Intrinsics()->Void_class;
		break;
	}

	Signature.return_type = return_type;

	return Signature;
}
#endif

bool JS::ParseNode::FunctionDefinition::CollectBoundNames(std::multiset<JSString>& names)
{
	if(auto identifier = dynamic_cast<Identifier *>(name.value_or(nullptr)))
	{
		if(!identifier->is_private)
		{
			names.insert(identifier->StringValue);
			return names.count(identifier->StringValue) > 1;
		}
	}
	return false;
}

void JS::ParseNode::FunctionDefinition::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	if(function_position != FunctionPosition::Statement) // TODO: what about member declarations?
	{
		return;
	}

	JSString Name = GetName();

#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(function_type == FunctionType::Get || function_type == FunctionType::Set)
		{
			if(attributes.Prototype)
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}
		}

		switch(entity_mode)
		{
		case JS::Runtime::EntityMode::Unknown:
			JS::Throw(JS::New<InternalError> __LOC());
		case JS::Runtime::EntityMode::Hoisted:
			if(active)
			{
				assert(GetThisMode() == JS::Class::Function::ThisModeType::Unchecked);
				GetCurrentExecutionContext()->VariableEnvironment->CreateHoistedBinding(Name,
					SetupPrototype(CreateFunction(Name, nullptr)));
			}
			break;
		case JS::Runtime::EntityMode::Singleton:
			if(attributes.Override != JS::Runtime::OverrideModifier::None)
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}

			if(SharesClassName())
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}

			{
				AccessSet accesses;
				JSProperty property;
				switch(function_type)
				{
				case FunctionType::Get:
					accesses = AccessSet::Read;
					property =
						JSProperty::MakeGetter(
							SetupPrototype(CreateFunction(Name, nullptr, GetCurrentExecutionContext()->LexicalEnvironment)),
							/* Enumerable = */ true,
							attributes.Explicit);
					break;
				case FunctionType::Set:
					accesses = AccessSet::Write;
					property =
						JSProperty::MakeSetter(
							SetupPrototype(CreateFunction(Name, nullptr, GetCurrentExecutionContext()->LexicalEnvironment)),
							/* Enumerable = */ true,
							attributes.Explicit);
					break;
				default:
					accesses = AccessSet::ReadWrite;
					property =
						JSProperty::MakeVariable(
							SetupPrototype(CreateFunction(Name, nullptr, GetCurrentExecutionContext()->LexicalEnvironment)),
							/* Writable = */ false,
							true,
							attributes.Explicit,
							Intrinsics()->Function_class);
					break;
				}

				GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
					Name,
					attributes.GetNamespaces(),
					accesses,
					property);
				break;
			}
			break;
		case JS::Runtime::EntityMode::Instance:
			if(attributes.Prototype)
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}

			if(SharesClassName())
			{
				if(function_type != FunctionType::Normal)
				{
					JS::Throw(JS::New<SyntaxError> __LOC());
				}

				assert(GetThisMode() == JS::Class::Function::ThisModeType::Instance);
				GetCurrentExecutionContext()->GetClassEnvironment()->CreateConstructorBinding(
					CreateFunction(Name, nullptr)); // no prototype is created at this point
			}
			else
			{
				assert(GetThisMode() == JS::Class::Function::ThisModeType::Instance);
				gcn::gc_ptr<JS::Class::Function> function = SetupPrototype(CreateFunction(Name, nullptr));
				instance_function =
					function_type == FunctionType::Get
						? JS::Runtime::InstanceFunction::Getter(
							attributes.Category == JS::Runtime::MemberModifier::Final,
							attributes.Enumerable,
							JS::Runtime::SignatureType{}, // filled in later
							function)
					: function_type == FunctionType::Set
						? JS::Runtime::InstanceFunction::Setter(
							attributes.Category == JS::Runtime::MemberModifier::Final,
							attributes.Enumerable,
							JS::Runtime::SignatureType{}, // filled in later
							function)
						: JS::Runtime::InstanceFunction::Method(
							attributes.Category == JS::Runtime::MemberModifier::Final,
							attributes.Enumerable,
							JS::Runtime::SignatureType{}, // filled in later
							function);
				overridden_property =
					GetCurrentExecutionContext()->GetClassEnvironment()->CreateInstanceBinding(
						Name,
						attributes.GetNamespaces(),
						open_namespaces,
						attributes.Override,
						attributes.Explicit,
						instance_function.value());

				if(overridden_property.has_value() && overridden_property.value()->Enumerable)
				{
					instance_function.value()->Enumerable = true;
				}
			}
			break;
		}
	}
	else
#endif
	{
		switch(mode)
		{
		case JS::Runtime::EnvironmentMode::Regional:
		case JS::Runtime::EnvironmentMode::Local:
			// mutable closure
			// declarative environments do not permit redeclaring identifiers
			if(active)
			{
				// since at global scope it still binds to the global object, we call CreateHoistedBinding
				// however we want to keep it local so we use the lexical environment
				GetCurrentExecutionContext()->LexicalEnvironment->CreateHoistedBinding(
					Name,
					SetupPrototype(CreateFunction(Name, nullptr)));
			}
			break;
		case JS::Runtime::EnvironmentMode::Hoisted:
			if(!strict)
			{
				// mutable undefined
				GetCurrentExecutionContext()->VariableEnvironment->CreateHoistedBinding(Name, JS_undefined);
			}
			break;
		case JS::Runtime::EnvironmentMode::Class:
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}
}

#if ENABLE_ES4
void JS::ParseNode::FunctionDefinition::SetupContent([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
	if(IsVersionES4())
	{
		if(instance_function.has_value())
		{
			instance_function.value()->Signature = GetSignature();
		}
		else if(declared_function != nullptr)
		{
			// determine type signature of function
			declared_function->Signature = GetSignature();
		}
	}
}
#endif

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::FunctionDefinition::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(function_position == FunctionPosition::Expression)
	{
		return CompileExpression(initial_state RUNTIME_PHASE).Option();
	}
	else
	{
		return CompileFunctionName(initial_state).Apply(std::function<std::optional<JSValue>(JSPropertyKey)>([this](JSPropertyKey PropertyKey) -> std::optional<JSValue>
		{
#if ENABLE_ES4
			if(!IsVersionES4())
#endif
			{
				if(redeclare_function_required)
				{
					auto Name = std::get<JSString>(PropertyKey);
					GetCurrentExecutionContext()->VariableEnvironment->SetHoistedBinding(Name,
						GetCurrentExecutionContext()->LexicalEnvironment->GetBindingValue(
							Name
							PUBLIC_NAMESPACES
							WITH_PHASE(JS::Runtime::ExecutionPhase::Run)));
				}
			}
			return {};
		}));
	}
}

JS::Interpreter::State<JSObject> JS::ParseNode::FunctionDefinition::CompilePropertyDefinition(JS::Interpreter::State<JSObject> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Then(this, &FunctionDefinition::CompileFunctionName)
		.Apply(std::function<JSObject(JSPropertyKey, JSObject)>([this](JSPropertyKey PropertyKey, JSObject object)
	{
		gcn::gc_ptr<JS::Class::Function> function = SetupPrototype(CreateFunction(GetFunctionName(GetStringForName(PropertyKey)), nullptr), nullptr);

		function->HomeObject = object;

		switch(function_type)
		{
		default:
			if(!object->DefineOwnProperty(PropertyKey, JSPropertyDescriptor().withValue(function).withWritable(true).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			break;
		case FunctionType::Get:
			if(!object->DefineOwnProperty(PropertyKey, JSPropertyDescriptor().withGetter(function).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			break;
		case FunctionType::Set:
			if(!object->DefineOwnProperty(PropertyKey, JSPropertyDescriptor().withSetter(function).withEnumerable(true).withConfigurable(true) PUBLIC_NAMESPACES READ_WRITE_ACCESS))
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			break;
		}

		return object;
	}));
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::FunctionDefinition::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// TODO: static initializer
	return initial_state.Then(this, &FunctionDefinition::CompileFunctionName)
			.Apply(std::function<JS::Runtime::ClassContext(JSPropertyKey, JS::Runtime::ClassContext)>([this](JSPropertyKey PropertyKey, JS::Runtime::ClassContext context)
	{
		gcn::gc_ptr<JS::Class::Function> function = SetupPrototype(CreateFunction(GetFunctionName(GetStringForName(PropertyKey)), context.ConstructorParent), context.ClassPrototype);

		if(function_position == FunctionPosition::ClassConstructor)
		{
			function->HomeObject = context.ClassPrototype;
			if(context.CompiledClass != nullptr)
			{
				*context.CompiledClass = function;
			}
			return context;
		}

		function->HomeObject = context.GetHomeObject(Static);

		if(!IsPrivate())
		{
			switch(function_type)
			{
			case FunctionType::Normal:
			case FunctionType::Async:
				context.DefinePublicMethod(Static, PropertyKey, function);
				break;
			case FunctionType::Get:
				context.DefinePublicGetter(Static, PropertyKey, function);
				break;
			case FunctionType::Set:
				context.DefinePublicGetter(Static, PropertyKey, function);
				break;
			}
		}
		else
		{
			auto Name = std::get<JSString>(PropertyKey);
			switch(function_type)
			{
			case FunctionType::Normal:
			case FunctionType::Async:
				context.DefinePrivateMethod(Static, Name, function);
				break;
			case FunctionType::Get:
				context.DefinePrivateGetter(Static, Name, function);
				break;
			case FunctionType::Set:
				context.DefinePrivateGetter(Static, Name, function);
				break;
			}
		}

		return context;
	}));
}

//// Node::ClassDefinition

JSString JS::ParseNode::ClassDefinition::GetName()
{
	// ES6
	if(name.has_value())
	{
		return name.value()->StringValue;
	}
	else if(is_expression)
	{
		return u"";
	}
	else
	{
		return u"default"; // part of an export default statement
	}
}

void JS::ParseNode::ClassDefinition::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	if(name.has_value())
	{
		name.value()->Validate(cxt);
	}

	if(extends.has_value())
	{
		extends.value()->Validate(cxt);
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		open_namespaces = cxt.GetOpenNamespaces();

		for(auto type_parameter : type_parameters)
		{
			type_parameter->Validate(cxt);
		}

		for(auto implement : implements)
		{
			implement->Validate(cxt);
		}

		cxt.is_package_range = false;
		cxt.CurrentValidationContext = &cxt;
	}
#endif

	cxt.Mode = JS::Runtime::EnvironmentMode::Class;

	cxt.class_name = GetName();
	cxt.is_derived_class = extends.has_value();

#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Runtime::CallContextCapture previous_context; // restores environments on leaving function

		if(cxt.Attributes.Prototype)
		{
			JS::Throw(JS::New<AttributeError> __LOC());
		}

		if(cxt.Attributes.Native)
		{
			JS::Throw(JS::New<AttributeError> __LOC());
		}

		switch(cxt.Attributes.Category)
		{
		default:
			break;
		case JS::Runtime::MemberModifier::Static:
			if(!std::dynamic_pointer_cast<JS::Runtime::ClassEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment))
			{
				JS::Throw(JS::New<AttributeError> __LOC());
			}
			break;
		case JS::Runtime::MemberModifier::Virtual:
			JS::Throw(JS::New<AttributeError> __LOC());
		}

		JSType superclass = Intrinsics()->Object_class;
		if(extends.has_value())
		{
			superclass = extends.value()->EvaluateType();
			if(GetClassForType(superclass)->Final)
			{
				JS::Throw(JS::New<DefinitionError> __LOC());
			}
		}

		CompiledClass = JS::New<JS::Class::Class>(
			name.value()->StringValue,
			superclass,
			GetClassForType(superclass)->ClassPrototype,
			cxt.Attributes.Dynamic,
			cxt.Attributes.Category == JS::Runtime::MemberModifier::Final,
			GetCurrentRealm());
		// TODO: abstract
		CompiledClass->PrivateNamespace = JS::New<JS::Class::Namespace>(u"private");
		open_namespaces.insert(CompiledClass->PrivateNamespace);

		GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
			name.value()->StringValue,
			cxt.Attributes.GetNamespaces(),
			AccessSet::ReadWrite,
			JSProperty::MakeVariable(
				CompiledClass,
				/* Writable = */ false,
				true,
				cxt.Attributes.Explicit,
				Intrinsics()->Class_class));

		cxt.Mode = JS::Runtime::EnvironmentMode::Class;
		cxt.OpenNamespaces.insert(CompiledClass->PrivateNamespace);

		GetCurrentExecutionContext()->LexicalEnvironment = GetCurrentExecutionContext()->VariableEnvironment = std::make_shared<JS::Runtime::ClassEnvironmentRecord>(CompiledClass, GetCurrentExecutionContext()->LexicalEnvironment);

		body->Validate(cxt);

		for(auto directive : static_cast<StatementList *>(body)->statements)
		{
			directive->CreateBindings(JS::Runtime::EnvironmentMode::Class);
		}
	}
	else
#endif
	{
		body->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::ClassDefinition::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	// ES6
	if(CurrentVersion < Version::ES6)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	std::optional<JSString> class_binding;
	JSString class_name = u"";

	if(name.has_value())
	{
		class_binding = name.value()->StringValue;
	}
	class_name = GetName();

	std::shared_ptr<JS::Runtime::EnvironmentRecord> env = GetCurrentExecutionContext()->LexicalEnvironment;
	std::shared_ptr<JS::Runtime::EnvironmentRecord> class_env = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);

	if(class_binding.has_value())
	{
		class_env->CreateSingletonBinding(
			class_binding.value()
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS,
			JSBinding::MakeImmutable({}, /* Strict = */ true));
	}

	struct runcontext
	{
		JSObject proto_parent;
		JSFunction constructor_parent;
	};

	JS::Interpreter::BlockRange block_range = JS::Interpreter::Code::BeginBlockRange(initial_state);
	JS::Interpreter::State<runcontext> state;

	if(extends.has_value())
	{
		state = initial_state.Apply(std::function<void(void)>([class_env]()
		{
			GetCurrentExecutionContext()->LexicalEnvironment = class_env;
		})).Then(extends.value(), &ParseNode::CompileExpression WITH_PHASE(phase))
			.Apply(std::function<runcontext(JSValue)>([class_env](JSValue superclass_ref) -> runcontext
		{
			runcontext context;
			GetCurrentExecutionContext()->LexicalEnvironment = class_env->OuterEnvironment;
			auto superclass = superclass_ref.GetValue();
			if(IsNull(superclass))
			{
				context.proto_parent = nullptr;
				context.constructor_parent = JSFunction::MakeFunction(Intrinsics()->Function_prototype);
			}
			else
			{
				context.constructor_parent = JSFunction::MakeFunction(superclass);
				if(context.constructor_parent == nullptr
				|| context.constructor_parent.GetInvocability() != JS::Invocability::Constructor)
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}

				if(auto prototype_option = GetObjectOrNull(Get(superclass, u"prototype")))
				{
					context.proto_parent = prototype_option.value();
				}
				else
				{
					JS::Throw(JS::New<TypeError> __LOC());
				}
			}
			return context;
		}));
	}
	else
	{
		state = initial_state.Apply(std::function<runcontext(void)>([class_env]() -> runcontext
		{
			runcontext context;
			GetCurrentExecutionContext()->LexicalEnvironment = class_env;
			context.proto_parent = Intrinsics()->Object_prototype;
			context.constructor_parent = JSFunction::MakeFunction(Intrinsics()->Function_prototype);
			return context;
		}));
	}

	JS::Runtime::ClassContext class_cxt;
	class_cxt.is_derived_class = extends.has_value();
	class_cxt.class_name = class_name;
	class_cxt.CompiledClass = new gcn::gc_ptr<JS::Class::Function>(nullptr); // creates a stored location

	JS::Interpreter::State<JS::Runtime::ClassContext, runcontext> state2 = state.Obtain(std::function<JS::Runtime::ClassContext(runcontext)>([class_cxt, class_env](runcontext context) -> JS::Runtime::ClassContext
	{
		JS::Runtime::ClassContext cxt = class_cxt;
		GetCurrentExecutionContext()->LexicalEnvironment = class_env;
		GetCurrentExecutionContext()->PrivateEnvironment = std::make_shared<JS::Runtime::PrivateEnvironmentRecord>(GetCurrentExecutionContext()->PrivateEnvironment);
		cxt.ClassPrototype = JS::NewWithPrototype<JS::Class::OrdinaryObject>(context.proto_parent);
		cxt.ConstructorParent = context.constructor_parent.cast_object<JS::Class::Object>();
		return cxt;
	}));

	bool has_constructor = false;

	for(auto class_element : static_cast<ClassElementList *>(body)->elements)
	{
		if(auto function = dynamic_cast<FunctionDefinition *>(class_element))
		{
			if(function->function_position == FunctionPosition::ClassConstructor)
			{
				has_constructor = true;

				state2 = state2.Then(class_element, &ParseNode::CompileClassElement);
			}
		}
	}

	state2 = state2.Obtain(std::function<void(JS::Runtime::ClassContext, runcontext)>([this, has_constructor, class_name, class_binding, class_env](JS::Runtime::ClassContext class_cxt, runcontext context)
	{
		if(!has_constructor)
		{
			*class_cxt.CompiledClass = JS::NewWithPrototype<JS::Class::DefaultConstructor>(context.constructor_parent, GetCurrentRealm(), class_name);
		}
		(*class_cxt.CompiledClass)->SourceText = SourceText;

		if(class_binding.has_value())
		{
			class_env->InitializeSingletonBinding(
				class_binding.value()
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				*class_cxt.CompiledClass);
		}

		(*class_cxt.CompiledClass)->DefineOwnProperty(
			JSString(u"prototype"),
			JSPropertyDescriptor()
				.withValue(class_cxt.ClassPrototype)
				.withWritable(false)
				.withEnumerable(false)
				.withConfigurable(false)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);

		class_cxt.ClassPrototype->DefineOwnProperty(
			JSString(u"constructor"),
			JSPropertyDescriptor()
				.withValue(*class_cxt.CompiledClass)
				.withWritable(true)
				.withEnumerable(false)
				.withConfigurable(true)
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS);

		if(extends.has_value())
		{
			(*class_cxt.CompiledClass)->ConstructorKind = JS::Class::Function::ConstructorKindType::Derived;
		}
		else
		{
			(*class_cxt.CompiledClass)->ConstructorKind = JS::Class::Function::ConstructorKindType::Base;
		}
	}));

	for(auto class_element : static_cast<ClassElementList *>(body)->elements)
	{
		if(auto function = dynamic_cast<FunctionDefinition *>(class_element))
		{
			if(function->function_position == FunctionPosition::ClassConstructor)
			{
				continue;
			}
		}

		state2 = state2.Then(class_element, &ParseNode::CompileClassElement);
	}

	JS::Interpreter::State<std::optional<JSValue>> state4 = state2.Apply(std::function<JSValue(JS::Runtime::ClassContext, runcontext)>([this, class_binding, env](JS::Runtime::ClassContext class_cxt, [[maybe_unused]] runcontext _1) -> JSValue
	{
		if(class_binding.has_value() && !is_expression)
		{
			env->InitializeSingletonBinding(
				class_binding.value()
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				*class_cxt.CompiledClass);
		}

		return *class_cxt.CompiledClass;
	})).Option();
	state4 = block_range.EndBlockRange(state4);
	return state4.Mandatory();
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ClassDefinition::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// ES4 and ES6
#if ENABLE_ES4
	if(IsVersionES4())
	{
		JS::Interpreter::BlockRange block_range = JS::Interpreter::Code::BeginBlockRange(initial_state);
		JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Apply(std::function<void()>([this]()
		{
			GetCurrentExecutionContext()->LexicalEnvironment =
				GetCurrentExecutionContext()->VariableEnvironment =
					std::make_shared<JS::Runtime::ClassEnvironmentRecord>(CompiledClass, GetCurrentExecutionContext()->LexicalEnvironment);
		})).Then(body, &ParseNode::CompileStatement).Pop().Push<std::optional<JSValue>>({});
		return block_range.EndBlockRange(state);
	}
	else
#endif
	{
		return ParseNode::CompileStatement(initial_state);
	}
}

bool JS::ParseNode::ClassDefinition::CollectBoundNames(std::multiset<JSString>& names)
{
	if(name.has_value())
	{
		names.insert(name.value()->StringValue);
		return names.count(name.value()->StringValue) > 1;
	}
	return false;
}

void JS::ParseNode::ClassDefinition::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	// ES6 (ES4 is handled in Validate)
	if(Version::ES6 <= CurrentVersion)
	{
		switch(mode)
		{
		case JS::Runtime::EnvironmentMode::Regional:
		case JS::Runtime::EnvironmentMode::Local:
			break;
		case JS::Runtime::EnvironmentMode::Hoisted:
			return;
		case JS::Runtime::EnvironmentMode::Class:
			JS::Throw(JS::New<InternalError> __LOC());
		}

		GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
			GetName()
			PUBLIC_NAMESPACES
			READ_WRITE_ACCESS,
			JSBinding::MakeImmutable({}, /* Strict = */ true));
	}
}

#if ENABLE_ES4
void JS::ParseNode::ClassDefinition::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	// ES4
	if(IsVersionES4())
	{
		JS::Runtime::CallContextCapture previous_context; // restores environments on leaving function

		GetCurrentExecutionContext()->LexicalEnvironment = GetCurrentExecutionContext()->VariableEnvironment = std::make_shared<JS::Runtime::ClassEnvironmentRecord>(CompiledClass, GetCurrentExecutionContext()->LexicalEnvironment);

		body->SetupContent(mode);
	}
}
#endif

//// Node::UpdateExpression

void JS::ParseNode::UpdateExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	operand->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::UpdateExpression::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	operand->SetupContent(mode);
}
#endif

JS::Interpreter::State<JSValue> JS::ParseNode::UpdateExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>(
		[this WITH_PHASE(phase)](JSValue reference) -> JSValue
	{
		JSValue old_value = ToNumeric(reference.GetValue() WITH_PHASE(phase));
		JSValue new_value;
		if(auto number = GetNumber(old_value))
		{
			new_value = *number + (increment ? 1.0 : -1.0);
		}
		else if(auto bigint = GetBigInt(old_value))
		{
			if(CurrentVersion < Version::ES11)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			new_value = JSBigInt(*bigint + (increment ? 1_mpz : -1_mpz));
		}
#if ENABLE_ES4
		else if(auto longval = GetLong(old_value))
		{
			if(CurrentVersion != Version::ES4_2003) // TODO: or enabled
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			if(increment && *longval == INT64_MAX)
			{
				new_value = JSValue(JSUlong(*longval) + 1);
			}
			else if(!increment && *longval == INT64_MIN)
			{
				new_value = -0x1.0p64;
			}
			else
			{
				new_value = JSValue(JSLong(*longval + (increment ? 1 : -1)));
			}
		}
		else if(auto ulongval = GetUlong(old_value))
		{
			if(CurrentVersion != Version::ES4_2003) // TODO: or enabled
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			if(increment && *ulongval == UINT64_MAX)
			{
				new_value = 0x1.0p64;
			}
			else if(!increment && *ulongval == 0)
			{
				new_value = JSValue(JSLong(-1));
			}
			else
			{
				new_value = JSValue(JSUlong(*ulongval + (increment ? 1 : -1)));
			}
		}
		else if(auto floatval = GetFloat(old_value))
		{
			new_value = JSNumber(*floatval) + (increment ? 1.0 : -1.0);
		}
		else if(auto decimal = GetDecimal(old_value))
		{
			if(CurrentVersion != Version::ES4_2008) // TODO: or enabled
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			new_value = JSValue(*decimal + (increment ? 1 : -1));
		}
#endif
		else
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		reference.PutValue(new_value);
		if(post_update)
		{
			// x++ and x--
			return old_value;
		}
		else
		{
			// ++x and --x
			return new_value;
		}
	}));
}

//// Node::YieldExpression

void JS::ParseNode::YieldExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	if(operand.has_value())
	{
		cxt.tail_position = false;
		operand.value()->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::YieldExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(operand.has_value())
	{
		return operand.value()->CompileExpression(initial_state WITH_PHASE(phase)).Yield();
	}
	else
	{
		return initial_state.Push<JSValue>(JS_undefined).Yield();
	}
}

//// Node::YieldGeneratorExpression

void JS::ParseNode::YieldGeneratorExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	operand->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::YieldGeneratorExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	// TODO
	assert(false);
	return initial_state.Push<JSValue>(JS_undefined).Yield();
}

//// Node::AwaitExpression

void JS::ParseNode::AwaitExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	if(cxt.is_module_range)
	{
		// register that this module uses a top-level await expression
		cxt.CurrentValidationContext->has_top_level_await = true;
	}
	operand->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::AwaitExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return operand->CompileExpression(initial_state WITH_PHASE(phase)).Await();
}

//// Node::UnaryExpression

void JS::ParseNode::UnaryExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.Strict;
	operand->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::UnaryExpression::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	operand->SetupContent(mode);
}
#endif

JS::Interpreter::State<JSValue> JS::ParseNode::UnaryExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	switch(op)
	{
	case Operator::Delete:
		return operand->CompileExpression(initial_state WITH_PHASE(phase))
			.Apply(std::function<JSValue(JSValue)>([this](JSValue reference) -> JSValue
		{
			return reference.DeleteReference();
		}));
	case Operator::Void:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Pop().Push<JSValue>(JS_undefined);
	case Operator::Typeof:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference)
		{
			// TODO: ES4 seems to disallow unresolved references
			return JS::TypeOf(reference); // unresolved references return a specific value
		}));
	case Operator::Plus:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue)>([VIA_PHASE(phase)](JSValue value) -> JSValue
		{
			JSValue number = value.ToNumeric(VIA_PHASE(phase));
			return number;
		}));
	case Operator::Negate:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue)>([VIA_PHASE(phase)](JSValue value) -> JSValue
		{
			JSValue number = value.ToNumeric(VIA_PHASE(phase));
			return JSValue::unaryMinus(number WITH_PHASE(phase));
		}));
	case Operator::BitwiseNot:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue)>([VIA_PHASE(phase)](JSValue value) -> JSValue
		{
			JSValue number = value.ToNumeric(VIA_PHASE(phase));
			return JSValue::bitwiseNOT(number WITH_PHASE(phase));
		}));
	case Operator::LogicalNot:
		return operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue)>([VIA_PHASE(phase)](JSValue value) -> JSValue
		{
			JSValue number = value.ToNumeric(VIA_PHASE(phase));
			return !JS::ToBoolean(value);
		}));
	//case Operator::Await:
	//	// TODO
	//case Operator::Yield:
	//	// TODO
	//case Operator::YieldGenerator:
	//	// TODO
	}
	return initial_state.Push<JSValue>(JS_undefined); // TODO
}

//// Node::BinaryExpression

void JS::ParseNode::BinaryExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	bool in_tail_position = cxt.tail_position;
	cxt.tail_position = false;
	left_operand->Validate(cxt);
	cxt.tail_position = in_tail_position
		&& (op == Operator::Coalesce
			|| op == Operator::LogicalAnd
			|| op == Operator::LogicalOr
			|| op == Operator::Comma);
	right_operand->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::BinaryExpression::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	left_operand->SetupContent(mode);
	right_operand->SetupContent(mode);
}
#endif

JS::Interpreter::State<JSValue> JS::ParseNode::BinaryExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	switch(op)
	{
	case Operator::Power:
		if(CurrentVersion < Version::ES7)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::exponentiate(lNum, rNum);
			}));
	case Operator::Multiply:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::multiply(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Divide:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::divide(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Modulo:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::remainder(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Add:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				// TODO: string concatenation
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::add(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Subtract:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::subtract(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::ShiftLeft:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::leftShift(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::ShiftRight:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::signedRightShift(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::ShiftRightUnsigned:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::unsignedRightShift(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Less:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				// TODO: non-numeric values
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
				return cmp == ComparisonResult::Less;
			}));
	case Operator::LessEqual:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				// TODO: non-numeric values
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
				return cmp == ComparisonResult::Less || cmp == ComparisonResult::Equal;
			}));
	case Operator::Greater:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				// TODO: non-numeric values
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
				return cmp == ComparisonResult::Greater;
			}));
	case Operator::GreaterEqual:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				// TODO: non-numeric values
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
				return cmp == ComparisonResult::Greater || cmp == ComparisonResult::Equal;
			}));
	case Operator::Instanceof:
		// TODO
	case Operator::In:
		// TODO: left_operand might be PrivateIdentifier
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto rVal_object_option = GetNonNullObject(rVal);
				if(!rVal_object_option.has_value())
				{
					JS::Throw(JS::New<TypeError> __LOC("'in' operator applied to non-object"));
				}
				auto rVal_object = rVal_object_option.value();
				return rVal_object->HasProperty(rVal_object->ConvertToPropertyKey(lVal WITH_PHASE(phase)) PUBLIC_NAMESPACES READ_WRITE_ACCESS WITH_PHASE(phase));
			}));
#if ENABLE_ES4
	case Operator::Is:
		// TODO
	case Operator::As:
		// TODO
#endif
	case Operator::Equal:
		// TODO
	case Operator::NotEqual:
		// TODO
	case Operator::StrictEqual:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
#if ENABLE_ES4
				if((lVal.get_if<JSFloat>() || lVal.get_if<JSNumber>() || lVal.get_if<JSLong>() || lVal.get_if<JSUlong>()) // TODO: decimal
				&& (rVal.get_if<JSFloat>() || rVal.get_if<JSNumber>() || rVal.get_if<JSLong>() || rVal.get_if<JSUlong>())) // TODO: decimal
#else
				if((GetNumber(lVal) && GetNumber(rVal))
				|| (GetBigInt(lVal) && GetBigInt(rVal)))
#endif
				{
					auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
					auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
					auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
					return cmp == ComparisonResult::Equal;
				}
				else
				{
					return SameValue(lVal, rVal);
				}
			}));
	case Operator::StrictNotEqual:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
#if ENABLE_ES4
				if((lVal.get_if<JSFloat>() || lVal.get_if<JSNumber>() || lVal.get_if<JSLong>() || lVal.get_if<JSUlong>()) // TODO: decimal
				&& (rVal.get_if<JSFloat>() || rVal.get_if<JSNumber>() || rVal.get_if<JSLong>() || rVal.get_if<JSUlong>())) // TODO: decimal
#else
				if((GetNumber(lVal) && GetNumber(rVal))
				|| (GetBigInt(lVal) && GetBigInt(rVal)))
#endif
				{
					auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
					auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
					auto cmp = JSValue::compare(lNum, rNum WITH_PHASE(phase));
					return cmp != ComparisonResult::Equal;
				}
				else
				{
					return SameValue(lVal, rVal);
				}
			}));
	case Operator::BitwiseAnd:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::bitwiseAND(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::BitwiseXor:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::bitwiseXOR(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::BitwiseOr:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([VIA_PHASE(phase)](JSValue rVal, JSValue lVal) -> JSValue
			{
				auto lNum = lVal.ToNumeric(VIA_PHASE(phase));
				auto rNum = rVal.ToNumeric(VIA_PHASE(phase));
				return JSValue::bitwiseOR(lNum, rNum WITH_PHASE(phase));
			}));
	case Operator::Coalesce:
		if(CurrentVersion < Version::ES11)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		{
			JS::Interpreter::State<JSValue> state =
				left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));
			JS::Interpreter::State<JSValue, JSValue> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) { return IsUndefined(value) || IsNull(value); }));

			JS::Interpreter::IfRange<void, JSValue> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);

			// the true branch will evaluate the second operand and replace the value on the top of the stack with the result

			state = state.Pop()
				.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));

			// the false branch is empty and keeps the stack intact

			return if_range.EndIfRange(state);
		}
	case Operator::LogicalAnd:
		{
			JS::Interpreter::State<JSValue> state =
				left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));
			JS::Interpreter::State<JSValue, JSValue> state1 = state.Dup();

			JS::Interpreter::IfRange<void, JSValue> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);

			// the true branch will evaluate the second operand and replace the value on the top of the stack with the result

			state = state.Pop()
				.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));

			// the false branch is empty and keeps the stack intact

			return if_range.EndIfRange(state);
		}
#if ENABLE_ES4
	case Operator::LogicalXor:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue rVal, JSValue lVal) -> JSValue
			{
				return JSBoolean(JS::ToBoolean(lVal) ^ JS::ToBoolean(rVal));
			}));
#endif
	case Operator::LogicalOr:
		{
			JS::Interpreter::State<JSValue> state =
				left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));
			JS::Interpreter::State<JSValue, JSValue> state1 = state.Dup();

			JS::Interpreter::IfRange<void, JSValue> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);

			// the true branch is empty and keeps the stack intact

			state = if_range.BeginElseRange(state);

			// the false branch will evaluate the second operand and replace the value on the top of the stack with the result

			state = state.Pop()
				.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));

			return if_range.EndIfRange(state);
		}
	case Operator::Comma:
		return left_operand->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }))
			.Pop()
			.Then(right_operand, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>([](JSValue reference) { return reference.GetValue(); }));
	}
	return initial_state.Push<JSValue>(JS_undefined); // TODO
}

//// Node::ConditionalExpression

void JS::ParseNode::ConditionalExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	bool in_tail_position = cxt.tail_position;
	cxt.tail_position = false;
	condition->Validate(cxt);
	cxt.tail_position = in_tail_position;
	true_branch->Validate(cxt);
	false_branch->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::ConditionalExpression::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	condition->SetupContent(mode);
	true_branch->SetupContent(mode);
	false_branch->SetupContent(mode);
}
#endif

JS::Interpreter::State<JSValue> JS::ParseNode::ConditionalExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSValue> state1 =
		condition->CompileExpression(initial_state WITH_PHASE(phase))
		.Apply(std::function<JSValue(JSValue)>(
			[](JSValue condition) -> JSValue
	{
		return condition.GetValue();
	}));

	JS::Interpreter::State<> state2;
	JS::Interpreter::IfRange<JSValue> if_range = JS::Interpreter::Code::BeginIfRange<JSValue>(state1, state2);

	JS::Interpreter::State<JSValue> state3 = true_branch->CompileExpression(state2 WITH_PHASE(phase));

	state2 = if_range.BeginElseRange(state3);

	state3 = false_branch->CompileExpression(state2 WITH_PHASE(phase));

	return if_range.EndIfRange(state3);
}

//// Node::AssignmentExpression

void JS::ParseNode::AssignmentExpression::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	declaration_kind = cxt.declaration_kind;

	// source must be evaluated first, because anonymous functions need their names set
	// but this can only happen once the function expression has been validated
	bool is_module_range = cxt.is_module_range;
	if(cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
		// class field initializers might impact top-level-async, but only for the target
		cxt.is_module_range = false;
	}
	source->Validate(cxt);
	cxt.Initializer = source; // TODO: also for combined assignment

	cxt.is_module_range = is_module_range;
	target->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::AssignmentExpression::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(dynamic_cast<ArrayLiteral *>(target) || dynamic_cast<ObjectLiteral *>(target))
	{
		return source->CompileExpression(initial_state WITH_PHASE(phase))
			.Apply(std::function<JS::Runtime::BindingContext(JSValue)>([](JSValue source_reference) -> JS::Runtime::BindingContext
		{
			JS::Runtime::BindingContext context;
			context.Value = source_reference.GetValue();
			return context;
		})).Dup().Then(target, &ParseNode::CompileBinding)
			.Apply(std::function<JSValue(JS::Runtime::BindingContext)>([](JS::Runtime::BindingContext context) -> JSValue
		{
			return context.Value;
		}));
	}
	else
	{
		return target->CompileExpression(initial_state WITH_PHASE(phase))
			.Then(source, &ParseNode::CompileExpression WITH_PHASE(phase))
			.Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue source_reference, JSValue target_reference)
		{
			auto source_value = source_reference.GetValue();
			target_reference.PutValue(source_value);
			return source_value;
		}));
	}
}

void JS::ParseNode::AssignmentExpression::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	target->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
}

#if ENABLE_ES4
void JS::ParseNode::AssignmentExpression::SetupDeclarationContent(JS::Runtime::EnvironmentMode mode)
{
	target->SetupDeclarationContent(mode);
}
#endif

bool JS::ParseNode::AssignmentExpression::CollectBoundNames(std::multiset<JSString>& names)
{
	return target->CollectBoundNames(names);
}

bool JS::ParseNode::AssignmentExpression::ContainsInitialization()
{
	return true;
}

JS::Interpreter::State<> JS::ParseNode::AssignmentExpression::CompileInitialization(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// binding to identifiers requires special handling
	if(dynamic_cast<Identifier *>(target))
	{
		return initial_state.Then(target, &ParseNode::CompileInitialization);
	}
	else
	{
		return initial_state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE)
			.Apply(std::function<JS::Runtime::BindingContext(JSValue)>([this](JSValue source_reference)
		{
			JS::Runtime::BindingContext context;
			if(Version::ES6 <= CurrentVersion && declaration_kind != DeclarationKind::Var)
			{
				context.environment = GetCurrentExecutionContext()->LexicalEnvironment;
			}
			context.Value = source_reference.GetValue();
			return context;
		}))
		.Then(target, &ParseNode::CompileBinding);
	}
}

JS::Interpreter::State<> JS::ParseNode::AssignmentExpression::CompileBinding(JS::Interpreter::State<JS::Runtime::BindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	auto state = binding_state;
	// binding to identifiers requires special handling
	if(!dynamic_cast<Identifier *>(target))
	{
		state = state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE)
			.Apply(std::function<JS::Runtime::BindingContext(JSValue, JS::Runtime::BindingContext)>([](JSValue source_reference, JS::Runtime::BindingContext context)
		{
			context.Value = source_reference.GetValue();
			return context;
		}));
	}
	return state.Then(target, &ParseNode::CompileBinding);
}

JS::Interpreter::State<> JS::ParseNode::AssignmentExpression::CompileIteratorBinding(JS::Interpreter::State<JS::Runtime::IteratorBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// binding to identifiers requires special handling
	if(dynamic_cast<Identifier *>(target))
	{
		// [IteratorBindingInitialization]
		// SingleNameBinding : BindingIdentifier Initializer:opt

		// identifiers with initializers are handled slightly differently
		return binding_state.Then(target, &ParseNode::CompileIteratorBinding);
	}
	else if(dynamic_cast<MemberAccessExpression *>(target)
		|| dynamic_cast<IndexExpression *>(target))
	{
		// [IteratorDestructuringAssignment]
		// AssignmentElement : DestructuringAssignmentTarget Initializer:opt

		JS::Interpreter::State<JSValue, JSValue, JS::Runtime::IteratorBindingContext> state =
			binding_state
				.Then(target, &ParseNode::CompileExpression RUNTIME_PHASE)
				.Obtain(std::function<JSValue(JSValue, JS::Runtime::IteratorBindingContext)>(
			[this]([[maybe_unused]] JSValue _1, JS::Runtime::IteratorBindingContext iterator_binding_context) -> JSValue
		{
			JSValue value = JS_undefined;

			if(!iterator_binding_context.binding_data->IsDone())
			{
				if(auto next = iterator_binding_context.binding_data->StepValue())
				{
					value = next.value();
				}
			}

			return value;
		}));

		JS::Interpreter::State<JSValue, JSValue, JSValue, JS::Runtime::IteratorBindingContext> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) -> JSValue
		{
			return IsUndefined(value);
		}));
		JS::Interpreter::IfRange<void, JSValue, JSValue, JS::Runtime::IteratorBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue default_value_reference, [[maybe_unused]] JSValue _1) -> JSValue
		{
			return default_value_reference.GetValue();
		}));
		state = if_range.EndIfRange(state);

		return state.Apply(std::function<void(JSValue, JSValue, JS::Runtime::IteratorBindingContext)>(
			[](JSValue value, JSValue reference, [[maybe_unused]] JS::Runtime::IteratorBindingContext _1)
		{
			reference.PutValue(value);
		}));
	}
	else
	{
		// [IteratorBindingInitialization]
		// BindingElement : BindingPattern Initializer:opt

		JS::Interpreter::State<JSValue, JS::Runtime::IteratorBindingContext> state = binding_state.Obtain(std::function<JSValue(JS::Runtime::IteratorBindingContext)>(
			[this](JS::Runtime::IteratorBindingContext iterator_binding_context) -> JSValue
		{
			JSValue value = JS_undefined;

			if(!iterator_binding_context.binding_data->IsDone())
			{
				if(auto next = iterator_binding_context.binding_data->StepValue())
				{
					value = next.value();
				}
			}

			return value;
		}));

		JS::Interpreter::State<JSValue, JSValue, JS::Runtime::IteratorBindingContext> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) -> JSValue
		{
			return IsUndefined(value);
		}));
		JS::Interpreter::IfRange<void, JSValue, JS::Runtime::IteratorBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue default_value_reference, [[maybe_unused]] JSValue _1) -> JSValue
		{
			return default_value_reference.GetValue();
		}));
		state = if_range.EndIfRange(state);

		return state.Apply(std::function<JS::Runtime::BindingContext(JSValue, JS::Runtime::IteratorBindingContext)>(
			[](JSValue value, JS::Runtime::IteratorBindingContext iterator_binding_context) -> JS::Runtime::BindingContext
		{
			JS::Runtime::BindingContext binding_context = iterator_binding_context;
			binding_context.Value = value;
			return binding_context;
		})).Then(target, &ParseNode::CompileBinding);
	}
}

JS::Interpreter::State<> JS::ParseNode::AssignmentExpression::CompilePropertyBinding(JS::Interpreter::State<JS::Runtime::PropertyBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// binding to identifiers requires special handling: the identifier class handles initialization
	if(dynamic_cast<Identifier *>(target))
	{
		// the identifier class handles initialization
		return target->CompilePropertyBinding(binding_state);
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
}

JS::Interpreter::State<> JS::ParseNode::AssignmentExpression::CompileKeyedBinding(JS::Interpreter::State<JS::Runtime::KeyedBindingContext>& binding_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// binding to identifiers requires special handling: the identifier class handles initialization
	if(dynamic_cast<Identifier *>(target))
	{
		return target->CompileKeyedBinding(binding_state);
	}
	else if(dynamic_cast<MemberAccessExpression *>(target)
		|| dynamic_cast<IndexExpression *>(target))
	{
		JS::Interpreter::State<JSValue, JSValue, JS::Runtime::KeyedBindingContext> state =
			binding_state
				.Then(target, &ParseNode::CompileExpression RUNTIME_PHASE)
				.Obtain(std::function<JSValue(JSValue, JS::Runtime::KeyedBindingContext)>([]([[maybe_unused]] JSValue _1, JS::Runtime::KeyedBindingContext keyed_binding_context) -> JSValue
		{
			return GetByKey(ToObject(keyed_binding_context.Value), keyed_binding_context.property_key);
		}));

		JS::Interpreter::State<JSValue, JSValue, JSValue, JS::Runtime::KeyedBindingContext> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) -> JSValue
		{
			return IsUndefined(value);
		}));

		JS::Interpreter::IfRange<void, JSValue, JSValue, JS::Runtime::KeyedBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue default_value_reference, [[maybe_unused]] JSValue _1) -> JSValue
		{
			return default_value_reference.GetValue();
		}));
		state = if_range.EndIfRange(state);

		return state.Apply(std::function<void(JSValue, JSValue, JS::Runtime::KeyedBindingContext)>(
			[](JSValue property_value, JSValue reference, [[maybe_unused]] JS::Runtime::KeyedBindingContext _1)
		{
			reference.PutValue(property_value);
		}));
	}
	else
	{
		JS::Interpreter::State<JSValue, JS::Runtime::KeyedBindingContext> state =
			binding_state.Obtain(std::function<JSValue(JS::Runtime::KeyedBindingContext)>([](JS::Runtime::KeyedBindingContext keyed_binding_context) -> JSValue
		{
			return GetByKey(ToObject(keyed_binding_context.Value), keyed_binding_context.property_key);
		}));

		JS::Interpreter::State<JSValue, JSValue, JS::Runtime::KeyedBindingContext> state1 = state.Obtain(std::function<JSValue(JSValue)>([](JSValue value) -> JSValue
		{
			return IsUndefined(value);
		}));

		JS::Interpreter::IfRange<void, JSValue, JS::Runtime::KeyedBindingContext> if_range = JS::Interpreter::Code::BeginIfRange<void>(state1, state);
		state = state.Then(source, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue, JSValue)>([](JSValue default_value_reference, [[maybe_unused]] JSValue _1) -> JSValue
		{
			return default_value_reference.GetValue();
		}));
		state = if_range.EndIfRange(state);

		return state.Obtain(std::function<JS::Runtime::BindingContext(JSValue, JS::Runtime::KeyedBindingContext)>(
			[](JSValue property_value, JS::Runtime::KeyedBindingContext keyed_binding_context) -> JS::Runtime::BindingContext
		{
			JS::Runtime::BindingContext binding_context = keyed_binding_context;
			binding_context.Value = property_value;
			return binding_context;
		})).Then(target, &ParseNode::CompileBinding).Pop().Pop();
	}
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::AssignmentExpression::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return target->CompileClassElement(initial_state);
}

//// Node::CombinedAssignmentExpression

//// Node::LocalDeclarator

bool JS::ParseNode::LocalDeclarator::CollectBoundNames(std::multiset<JSString>& names)
{
	return local->CollectBoundNames(names);
}

void JS::ParseNode::LocalDeclarator::CreateDeclarationBindings(JS::Runtime::EnvironmentMode mode AND_FLAG(as_clause_parameter))
{
	// determine if this is the right moment to create the declaration
	switch(mode)
	{
	case JS::Runtime::EnvironmentMode::Regional:
		break;
	case JS::Runtime::EnvironmentMode::Local:
		if(entity_mode == JS::Runtime::EntityMode::Hoisted) // for ESNext, var declarations
			return;
		break;
	case JS::Runtime::EnvironmentMode::Hoisted:
		if(entity_mode != JS::Runtime::EntityMode::Hoisted)
			return;
		break;
	case JS::Runtime::EnvironmentMode::Class:
		break;
	}

	local->CreateDeclarationBindings(mode WITH_FLAG(as_clause_parameter));
}

#if ENABLE_ES4
void JS::ParseNode::LocalDeclarator::SetupDeclarationContent(JS::Runtime::EnvironmentMode mode)
{
	switch(mode)
	{
	case JS::Runtime::EnvironmentMode::Regional:
		break;
	case JS::Runtime::EnvironmentMode::Local:
		if(entity_mode == JS::Runtime::EntityMode::Hoisted) // for ESNext, var declarations
			return;
		break;
	case JS::Runtime::EnvironmentMode::Hoisted:
		if(entity_mode != JS::Runtime::EntityMode::Hoisted)
			return;
		break;
	case JS::Runtime::EnvironmentMode::Class:
		break;
	}

	local->SetupDeclarationContent(mode);
}
#endif

void JS::ParseNode::LocalDeclarator::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	declaration_kind = cxt.declaration_kind;

	// determine the mode of the entity
	// for ESNext, this can only be hoisted or lexical (referred to as "singleton")
#if ENABLE_ES4
	if(IsVersionES4())
	{
		if(cxt.Mode == JS::Runtime::EnvironmentMode::Class)
		{
			if(cxt.Attributes.Category == JS::Runtime::MemberModifier::Static)
			{
				entity_mode = JS::Runtime::EntityMode::Singleton;
			}
			else
			{
				entity_mode = JS::Runtime::EntityMode::Instance;
			}
		}
		else
		{
			if(IsVersionES4Plain() && !cxt.Strict && cxt.Attributes.IsEmpty())
			{
				entity_mode = JS::Runtime::EntityMode::Hoisted;
			}
			else
			{
				entity_mode = JS::Runtime::EntityMode::Singleton;
			}
		}
	}
	else
#endif
	{
		if(declaration_kind == DeclarationKind::Var)
		{
			entity_mode = JS::Runtime::EntityMode::Hoisted;
		}
		else
		{
			entity_mode = JS::Runtime::EntityMode::Singleton;
		}
	}

	cxt.entity_mode = entity_mode;

	local->Validate(cxt);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::LocalDeclarator::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state
		.Then(local, &ParseNode::CompileInitialization)
		.Push(std::optional<JSValue>());
}

//// Node::IdentifierDeclaration

bool JS::ParseNode::IdentifierDeclaration::CollectBoundNames(std::multiset<JSString>& names)
{
	bool has_duplicates = false;
	for(auto declaration : declared_identifiers)
	{
		has_duplicates |= declaration->CollectBoundNames(names);
	}
	return has_duplicates;
}

void JS::ParseNode::IdentifierDeclaration::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	//cxt.declaration_kind = declaration_kind;
	for(auto declaration : declared_identifiers)
	{
		declaration->CreateDeclarationBindings(mode WITH_FLAG(/* as_clause_parameter = */ false));
	}
}

#if ENABLE_ES4
void JS::ParseNode::IdentifierDeclaration::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	for(auto declaration : declared_identifiers)
	{
		declaration->SetupDeclarationContent(mode);
	}
}
#endif

void JS::ParseNode::IdentifierDeclaration::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	cxt.declaration_kind = declaration_kind;
	for(auto declaration : declared_identifiers)
	{
		declaration->Validate(cxt);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::IdentifierDeclaration::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	auto state = initial_state;
	for(auto declaration : declared_identifiers)
	{
		state = state.Then(declaration, &LocalDeclarator::CompileStatement).Pop();
	}
	return state.Push(std::optional<JSValue>());
}

//// Node::LabelledStatement

void JS::ParseNode::LabelledStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position is preserved
	// TODO: check labels
	statement->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::LabelledStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	statement->SetupContent(mode);
}
#endif

void JS::ParseNode::LabelledStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	statement->CreateBindings(mode);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::LabelledStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// the statement sets up the label
	return statement->CompileStatement(initial_state);
}

//// Node::IfStatement

void JS::ParseNode::IfStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	bool in_tail_position = cxt.tail_position;
	cxt.tail_position = false;
	condition->Validate(cxt);
	cxt.tail_position = in_tail_position;
	true_branch->Validate(cxt);
	if(false_branch.has_value())
	{
		false_branch.value()->Validate(cxt);
	}
}

#if ENABLE_ES4
void JS::ParseNode::IfStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	condition->SetupContent(mode);
	true_branch->SetupContent(mode);
	if(false_branch.has_value())
	{
		false_branch.value()->SetupContent(mode);
	}
}
#endif

void JS::ParseNode::IfStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	//condition->CreateBindings(mode);
	true_branch->CreateBindings(mode);
	if(false_branch.has_value())
	{
		false_branch.value()->CreateBindings(mode);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::IfStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSValue> state1 =
		condition->CompileExpression(initial_state RUNTIME_PHASE)
		.Apply(std::function<JSValue(JSValue)>(
			[](JSValue condition) -> JSValue
	{
		return condition.GetValue();
	}));

	JS::Interpreter::State<> state2;
	JS::Interpreter::IfRange<std::optional<JSValue>> if_range = JS::Interpreter::Code::BeginIfRange<std::optional<JSValue>>(state1, state2);

	JS::Interpreter::State<std::optional<JSValue>> state3 =
		true_branch->CompileStatement(state2);

	state2 = if_range.BeginElseRange(state3);

	if(false_branch.has_value())
	{
		state3 = false_branch.value()->CompileStatement(state2);
	}
	else
	{
		state3 = state2.Push<std::optional<JSValue>>({});
	}

	return if_range.EndIfRange(state3);
}

//// Node::WhileStatement

void JS::ParseNode::WhileStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	condition->Validate(cxt);
	body->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::WhileStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	condition->SetupContent(mode);
	body->SetupContent(mode);
}
#endif

void JS::ParseNode::WhileStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	//condition->CreateBindings(mode);
	body->CreateBindings(mode);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::WhileStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Push(std::optional<JSValue>({}));
	JS::Interpreter::LoopRange<std::optional<JSValue>> loop_range = JS::Interpreter::Code::BeginLoopRange(state, label);

	JS::Interpreter::State<JSValue, std::optional<JSValue>> state1 = state.Then(condition, &ParseNode::CompileExpression RUNTIME_PHASE);
	state = loop_range.AddLoopCheck(state1);

	state = state.Then(body, &ParseNode::CompileStatement).Apply(std::function<std::optional<JSValue>(std::optional<JSValue>, std::optional<JSValue>)>(
		[](std::optional<JSValue> second_value, std::optional<JSValue> first_value) -> std::optional<JSValue>
	{
		if(second_value.has_value())
		{
			return second_value.value();
		}
		else
		{
			return first_value; // either empty or has value
		}
	}));

	return loop_range.EndLoopRange(state);
}

//// Node::DoWhileStatement

void JS::ParseNode::DoWhileStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	body->Validate(cxt);
	condition->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::DoWhileStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	body->SetupContent(mode);
	condition->SetupContent(mode);
}
#endif

void JS::ParseNode::DoWhileStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	//condition->CreateBindings(mode);
	body->CreateBindings(mode);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::DoWhileStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Push(std::optional<JSValue>({}));
	JS::Interpreter::LoopRange<std::optional<JSValue>> loop_range = JS::Interpreter::Code::BeginLoopRange(state, label);

	state = state.Then(body, &ParseNode::CompileStatement).Apply(std::function<std::optional<JSValue>(std::optional<JSValue>, std::optional<JSValue>)>(
		[](std::optional<JSValue> second_value, std::optional<JSValue> first_value) -> std::optional<JSValue>
	{
		if(second_value.has_value())
		{
			return second_value.value();
		}
		else
		{
			return first_value; // either empty or has value
		}
	}));

	JS::Interpreter::State<JSValue, std::optional<JSValue>> state1 = state.Then(condition, &ParseNode::CompileExpression RUNTIME_PHASE);
	state = loop_range.AddLoopCheck(state1);

	return loop_range.EndLoopRange(state);
}

//// Node::ForStatement

//// Node::ForEachStatement

void JS::ParseNode::ForEachStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	if(auto declaration = dynamic_cast<IdentifierDeclaration *>(variable))
	{
		assert(declaration->declared_identifiers.size() == 1);
	}

	cxt.tail_position = false;

#if ENABLE_ES4
	if(IsVersionES4())
	{
		cxt.is_package_range = false;
	}
#endif

	variable->Validate(cxt);
	iterator->Validate(cxt);
	body->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::ForEachStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	variable->SetupContent(mode);
	iterator->SetupContent(mode);
	body->SetupContent(mode);
}
#endif

void JS::ParseNode::ForEachStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	switch(mode)
	{
	case JS::Runtime::EnvironmentMode::Regional:
		// lexical declarations not exported
		mode = JS::Runtime::EnvironmentMode::Hoisted;
		break;
	case JS::Runtime::EnvironmentMode::Hoisted:
		break;
	case JS::Runtime::EnvironmentMode::Local:
		return;
	case JS::Runtime::EnvironmentMode::Class:
		JS::Throw(JS::New<InternalError> __LOC());
	}

	variable->CreateBindings(mode);
	//iterator->CreateBindings(mode);
	body->CreateBindings(mode);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ForEachStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	struct runcontext
	{
		JS::Runtime::IteratorBindingData * iterator_record = nullptr; // the data required to iterate
		std::optional<JSValue> next_value; // the next value as returned from the iterator
		std::optional<JSValue> result; // the result of the program body
	};

	enum
	{
		Assignment,
		VarBinding,
		LexicalBinding,
	} binding_type = Assignment;
	bool is_const_var = false;
	std::multiset<JSString> bound_names;
	ParseNode * lhs = variable;

	if(auto declaration = dynamic_cast<IdentifierDeclaration *>(variable))
	{
		if(declaration->declared_identifiers[0]->entity_mode != JS::Runtime::EntityMode::Singleton)
		{
			// var declaration
			binding_type = VarBinding;
			lhs = declaration->declared_identifiers[0]->local;
		}
		else
		{
			// let or const declaration
			binding_type = LexicalBinding;
			declaration->declared_identifiers[0]->local->CollectBoundNames(bound_names); // TODO: check if this is the right approach, probably it is
			is_const_var = declaration->declared_identifiers[0]->declaration_kind == JS::ParseNode::DeclarationKind::Const;
			lhs = declaration->declared_identifiers[0]->local;
		}
	}

	JS::Interpreter::State<runcontext> state = initial_state.Push<runcontext>(runcontext());

	if(binding_type == LexicalBinding && Version::ES6 <= CurrentVersion)
	{
		state = state.Apply(std::function<runcontext(runcontext)>([bound_names](runcontext context) -> runcontext
		{
			// create scope to evaluate the iterator expression in
			GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
			for(auto bound_name : bound_names)
			{
				GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
					bound_name
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS,
					JSBinding::MakeMutable({}, /* Deletable = */ false));
			}
			return context;
		}));
	}

	JS::Interpreter::State<JSValue, runcontext> state1 = state.Then(iterator, &ParseNode::CompileExpression RUNTIME_PHASE).Apply(std::function<JSValue(JSValue)>(
		[](JSValue reference) -> JSValue
	{
		return reference.GetValue();
	}));

	// TODO: block range required
	if(binding_type == LexicalBinding && Version::ES6 <= CurrentVersion)
	{
		state1 = state1.Apply(std::function<void()>([]
		{
			// remove temporary scope for evaluating iterator expression
			GetCurrentExecutionContext()->LexicalEnvironment = GetCurrentExecutionContext()->LexicalEnvironment->OuterEnvironment;
		}));
	}

	switch(kind)
	{
	case ForEachKind::ForIn:
	case ForEachKind::ForEachIn:
		{
			auto kind = this->kind;
			state = state1.Apply(std::function<runcontext(JSValue, runcontext)>(
				[kind](JSValue enumerable, runcontext context) -> runcontext
			{
				// TODO: XML gets converted to singleton XMLList
				JSObject enumerable_object;
				if(IsNull(enumerable) || IsUndefined(enumerable))
				{
					enumerable_object = nullptr;
				}
				else
				{
					enumerable_object = enumerable.ToObject();
				}
				// TODO: ES4 as well (requires Value::GetPrototypeOf and a trivial/Object specific Value::OwnPropertyKeys/GetOwnProperty)
				context.iterator_record = new JS::Runtime::ForInIterator(enumerable_object,
					kind == ForEachKind::ForEachIn
						? JS::Runtime::EnumerationKind::Value // E4X
						: JS::Runtime::EnumerationKind::Key);
				return context;
			}));
		}
		break;
	case ForEachKind::ForOf:
	case ForEachKind::ForAwaitOf:
		state = state1.Apply(std::function<runcontext(JSValue, runcontext)>(
			[this](JSValue iterable, runcontext context) -> runcontext
		{
			context.iterator_record = new JS::Runtime::IteratorRecord(JS::Runtime::IteratorRecord::GetIterator(iterable, kind == ForEachKind::ForAwaitOf));
			return context;
		}));
		break;
	}

	JS::Interpreter::LoopRange<runcontext> loop_range = JS::Interpreter::Code::BeginLoopRange(state, label);

	if(kind == ForEachKind::ForAwaitOf)
	{
		// TODO: await
	}
	else
	{
		state1 = state.Apply(
			std::function<runcontext(runcontext)>(
				[this](runcontext context) -> runcontext
		{
			context.next_value = context.iterator_record->StepValue();
			return context;
		})).Obtain(std::function<JSValue(runcontext)>([](runcontext context) -> JSValue
		{
			return context.next_value.has_value();
		}));
	}

	state = loop_range.AddLoopCheck(state1);

	if(binding_type != LexicalBinding)
	{
		state = state.Obtain(std::function<JS::Runtime::BindingContext(runcontext)>(
			[](runcontext context)
		{
			JS::Runtime::BindingContext binding_context;
			binding_context.environment = nullptr;
			binding_context.Value = context.next_value.value();
			return binding_context;
		})).Then(lhs, &ParseNode::CompileBinding);

		// TODO: call IteratorClose

		state = state.Then(body, &ParseNode::CompileStatement).Apply(std::function<runcontext(std::optional<JSValue>, runcontext)>(
			[](std::optional<JSValue> result, runcontext context) -> runcontext
		{
			if(result.has_value())
			{
				context.result = result;
			}
			return context;
		}));
	}
	else
	{
		JS::Interpreter::BlockRange block_range = JS::Interpreter::Code::BeginBlockRange(state);

		JS::Interpreter::State<JS::Runtime::BindingContext, runcontext> state1;

#if ENABLE_ES4
		if(IsVersionES4())
		{
			//auto identifier = dynamic_cast<Identifier *>(lhs);
			state1 = state.Obtain(std::function<JS::Runtime::BindingContext(runcontext)>(
				[lhs](runcontext context)
			{
				JS::Runtime::BindingContext binding_context;

				binding_context.environment =
					GetCurrentExecutionContext()->LexicalEnvironment =
					std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);

				lhs->CreateDeclarationBindings(JS::Runtime::EnvironmentMode::Local WITH_FLAG(/* as_clause_parameter = */ true));

				binding_context.Value = context.next_value.value();

				return binding_context;
			}));
		}
		else
#endif
		{
			state1 = state.Obtain(std::function<JS::Runtime::BindingContext(runcontext)>(
				[is_const_var, bound_names](runcontext context)
			{
				JS::Runtime::BindingContext binding_context;

				binding_context.environment =
					GetCurrentExecutionContext()->LexicalEnvironment =
					std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);

				for(auto bound_name : bound_names)
				{
					GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
						bound_name
						PUBLIC_NAMESPACES
						READ_WRITE_ACCESS,
							is_const_var
								? JSBinding::MakeImmutable({}, /* Strict = */ true)
								: JSBinding::MakeMutable({}, /* Deletable = */ false));
				}

				binding_context.Value = context.next_value.value();

				return binding_context;
			}));
		}

		state = state1.Then(lhs, &ParseNode::CompileBinding);

		// TODO: call IteratorClose, even if an error occurs

		state = state.Then(body, &ParseNode::CompileStatement).Apply(std::function<runcontext(std::optional<JSValue>, runcontext)>(
			[](std::optional<JSValue> result, runcontext context) -> runcontext
		{
			if(result.has_value())
			{
				context.result = result;
			}
			return context;
		}));

		state = block_range.EndBlockRange(state);
	}

	return loop_range.EndLoopRange(state).Apply(std::function<std::optional<JSValue>(runcontext)>([](runcontext context) -> std::optional<JSValue>
	{
		return context.result;
	}));
}

//// Node::ContinueStatement

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ContinueStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(label.has_value())
	{
		return initial_state.Continue<std::optional<JSValue>>(label.value()->StringValue);
	}
	else
	{
		return initial_state.Continue<std::optional<JSValue>>(std::optional<JSString>());
	}
}

//// Node::BreakStatement

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::BreakStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(label.has_value())
	{
		return initial_state.Break<std::optional<JSValue>>(label.value()->StringValue);
	}
	else
	{
		return initial_state.Break<std::optional<JSValue>>(std::optional<JSString>());
	}
}

//// Node::ReturnStatement

void JS::ParseNode::ReturnStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position is preserved
	if(result.has_value())
	{
		result.value()->Validate(cxt);
	}
}

#if ENABLE_ES4
void JS::ParseNode::ReturnStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	if(result.has_value())
	{
		result.value()->SetupContent(mode);
	}
}
#endif

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ReturnStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(result.has_value())
	{
		return result.value()->CompileExpression(initial_state RUNTIME_PHASE).Apply(std::function<JSValue(JSValue)>(
			[](JSValue reference) { return reference.GetValue(); }
		)).Return<JSValue>();
	}
	else
	{
		return initial_state.Return<void>();
	}
}

//// Node::WithStatement

//// Node::SwitchStatement

//// Node::CaseStatement

//// Node::DefaultStatement

//// Node::ThrowStatement

void JS::ParseNode::ThrowStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	expression->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::ThrowStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	expression->SetupContent(mode);
}
#endif

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ThrowStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return expression->CompileExpression(initial_state RUNTIME_PHASE).Throw<std::optional<JSValue>>();
}

//// Node::CatchStatement

void JS::ParseNode::CatchStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.Mode = JS::Runtime::EnvironmentMode::Local;
	cxt.CurrentValidationContext = &cxt;

#if ENABLE_ES4
	if(IsVersionES4())
	{
		cxt.is_package_range = false;
	}
#endif

	if(parameter.has_value())
	{
		// simulates a let binding
		cxt.declaration_kind = JS::ParseNode::DeclarationKind::Let;
		cxt.entity_mode = JS::Runtime::EntityMode::Singleton;
		bool in_tail_position = cxt.tail_position;
		cxt.tail_position = false;
		parameter.value()->Validate(cxt);
		cxt.tail_position = in_tail_position;
	}
	body->Validate(cxt);
}

#if ENABLE_ES4
void JS::ParseNode::CatchStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	if(parameter.has_value())
	{
		//parameter.value()->SetupContent(mode);
		//parameter.value()->SetupDeclarationContent(mode);
	}
	body->SetupContent(mode);
}
#endif

void JS::ParseNode::CatchStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	switch(mode)
	{
	case JS::Runtime::EnvironmentMode::Regional:
		// lexical declarations not exported
		mode = JS::Runtime::EnvironmentMode::Hoisted;
		break;
	case JS::Runtime::EnvironmentMode::Hoisted:
		break;
	case JS::Runtime::EnvironmentMode::Local:
	case JS::Runtime::EnvironmentMode::Class:
		return;
	}

	body->CreateBindings(mode);
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::CatchStatement::CompileCatchStatement(JS::Interpreter::State<JSValue>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::BlockRange block_range = JS::Interpreter::Code::BeginBlockRange(initial_state);
	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Apply(std::function<void(JSValue)>([this](JSValue exception)
	{
		GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);

#if ENABLE_ES4
		if(IsVersionES4())
		{
			assert(parameter.has_value());
			auto identifier = dynamic_cast<Identifier *>(parameter.value());
			assert(identifier);
			if(identifier->type.has_value())
			{
				JSType evaluated_type = identifier->type.value()->EvaluateType(); // TODO: it should return JSDeclaredType
				JSDeclaredType declared_type = evaluated_type;
				if(!declared_type.Is(exception))
				{
					JS::Throw(exception); // TODO: avoid using throw here
				}
			}
		}
#endif

		JS::Runtime::ContextCapture context_capture; // needed for both bindings and statements
		if(parameter.has_value())
		{
			parameter.value()->CreateDeclarationBindings(JS::Runtime::EnvironmentMode::Local WITH_FLAG(/* as_clause_parameter = */ true));
		}
		body->CreateBindings(JS::Runtime::EnvironmentMode::Local);
		context_capture.release();

#if ENABLE_ES4
		{
			JS::Runtime::ContextCapture context_capture;
			// parameter has already been set up
			body->SetupContent(JS::Runtime::EnvironmentMode::Local);
			context_capture.release();
		}
#endif

		if(parameter.has_value())
		{
			JS::Runtime::BindingContext binding_context;
			binding_context.environment = GetCurrentExecutionContext()->LexicalEnvironment;
			binding_context.Value = exception;

			parameter.value()->EvaluateBinding(binding_context);
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
		}
	})).Then(body, &ParseNode::CompileStatement);

	return block_range.EndBlockRange(state);
}

//// Node::TryStatement

void JS::ParseNode::TryStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	bool in_tail_position = cxt.tail_position;
	cxt.tail_position = false;
	body->Validate(cxt);
	for(auto catch_clause : catch_part)
	{
		// Note: in ES4, only the last catch clause is actually in tail position, but ES4 does not provide tail calls
		cxt.tail_position = in_tail_position && !finally_part.has_value();
		catch_clause->Validate(cxt);
	}
	if(finally_part.has_value())
	{
		cxt.tail_position = in_tail_position;
		finally_part.value()->Validate(cxt);
	}
}

#if ENABLE_ES4
void JS::ParseNode::TryStatement::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	body->SetupContent(mode);
	for(auto catch_clause : catch_part)
	{
		catch_clause->SetupContent(mode);
	}
	if(finally_part.has_value())
	{
		finally_part.value()->SetupContent(mode);
	}
}
#endif

void JS::ParseNode::TryStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	body->CreateBindings(mode);
	for(auto catch_clause : catch_part)
	{
		catch_clause->CreateBindings(mode);
	}
	if(finally_part.has_value())
	{
		finally_part.value()->CreateBindings(mode);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::TryStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::TryRange try_range = JS::Interpreter::Code::BeginTryRange(initial_state);
	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Then(body, &ParseNode::CompileStatement);

	if(catch_part.size() > 0)
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			for(size_t clause_index = 0; clause_index < catch_part.size(); clause_index ++)
			{
				auto catch_clause = catch_part[clause_index];
				JS::Interpreter::State<JSValue> state1 = try_range.BeginCatchRange(state, /* final = */ clause_index == catch_part.size() - 1);
				state = state1.Then(catch_clause, &CatchStatement::CompileCatchStatement);
			}
		}
		else
#endif
		{
			JS::Interpreter::State<JSValue> state1 = try_range.BeginCatchRange(state);
			state = state1.Then(catch_part[0], &CatchStatement::CompileCatchStatement);
		}
	}

	if(finally_part.has_value())
	{
		JS::Interpreter::State<JS::Runtime::CompletionRecord> final_state =
			try_range.BeginFinallyRange(state)
				.Then(finally_part.value(), &ParseNode::CompileStatement)
				.Pop();
		return try_range.EndTryRange(final_state);
	}
	else
	{
		return try_range.EndTryRange(state);
	}
}

//// Node::DebuggerStatement

//// Node::DefaultXmlNamespaceStatement

//// Node::StaticClassElement

void JS::ParseNode::StaticClassElement::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	cxt.Static = true;
	element->Validate(cxt);
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::StaticClassElement::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// TODO: this is definitely the worst solution, ideally this would have been set up through an initial validation stage
	initial_state = initial_state.Apply(std::function<JS::Runtime::ClassContext(JS::Runtime::ClassContext)>([](JS::Runtime::ClassContext context)
	{
		return context; // TODO: remove this entire block
	}));
	return element->CompileClassElement(initial_state);
}

#if ENABLE_ES4
//// Node::AttachedAttribute

void JS::ParseNode::AttachedAttribute::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	if(compiled_attributes.Enabled)
	{
		directive->CreateBindings(mode);
	}
}

void JS::ParseNode::AttachedAttribute::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	if(compiled_attributes.Enabled)
	{
		directive->SetupContent(mode);
	}
}

void JS::ParseNode::AttachedAttribute::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position is preserved
	if(compiled_attributes.Empty)
	{
		attribute->Validate(cxt);

		auto evaluated_attributes = attribute->EvaluateExpression(JS::Runtime::ExecutionPhase::Compile).GetValue();
		if(auto function = GetFunction(evaluated_attributes))
		{
			evaluated_attributes = JS::Call(*function, JS_null, std::vector<JSValue>{} WITH_PHASE(JS::Runtime::ExecutionPhase::Compile));
		}

		if(auto attribute = evaluated_attributes.get_if<JSCompoundAttribute>())
		{
			compiled_attributes = *attribute;
		}
		else if(auto boolean = GetBoolean(evaluated_attributes))
		{
			compiled_attributes = *boolean;
		}
		else if(auto object = GetNonNullObject(evaluated_attributes))
		{
			if(auto namespace_ = std::dynamic_pointer_cast<JS::Class::Namespace>(*object))
			{
				compiled_attributes = namespace_;
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}

	if(compiled_attributes.Enabled)
	{
		cxt.Attributes |= compiled_attributes;
		directive->Validate(cxt);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::AttachedAttribute::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if(compiled_attributes.Enabled)
	{
		return directive->CompileStatement(initial_state);
	}
	else
	{
		return initial_state.Push<std::optional<JSValue>>({});
	}
}
#endif

#if ENABLE_ES4
//// Node::AttributeCombination

void JS::ParseNode::AttributeCombination::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	for(auto node : attributes)
	{
		node->Validate(cxt);
	}
}

JS::Interpreter::State<JSValue> JS::ParseNode::AttributeCombination::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JSCompoundAttribute compound_attribute;
	bool started = false;
	for(auto node : attributes)
	{
		auto value = node->EvaluateExpression(JS::Runtime::ExecutionPhase::Compile).GetValue(Intent::Read, JS::Runtime::ExecutionPhase::Compile);
		if(auto function = GetFunction(value))
		{
			value = JS::Call(*function, JS_null, std::vector<JSValue>{} WITH_PHASE(JS::Runtime::ExecutionPhase::Compile));
		}

		if(auto attribute = value.get_if<JSCompoundAttribute>())
		{
			if(!attribute->Enabled)
			{
				compound_attribute = false;
			}
			else if(started)
			{
				compound_attribute |= *attribute;
			}
			else
			{
				compound_attribute = *attribute;
			}
		}
		else if(auto boolean = GetBoolean(value))
		{
			if(!*boolean)
			{
				compound_attribute = false;
			}
			else if(started)
			{
				compound_attribute |= true;
			}
			else
			{
				compound_attribute = true;
			}
		}
		else if(auto object = GetNonNullObject(value))
		{
			if(auto namespace_ = std::dynamic_pointer_cast<JS::Class::Namespace>(*object))
			{
				if(started)
				{
					compound_attribute |= namespace_;
				}
				else
				{
					compound_attribute = namespace_;
				}
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}

		started = true;
	}
	return initial_state.Push(JSValue(compound_attribute));
}
#endif

//// Node::ClassElementList

void JS::ParseNode::ClassElementList::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	for(auto element : elements)
	{
		element->Validate(cxt);
	}
}

//// Node::StatementList

void JS::ParseNode::StatementList::Validate(JS::Runtime::ValidationContext cxt)
{
	bool in_tail_position = cxt.tail_position;
	for(size_t statement_index = 0; statement_index < statements.size(); statement_index ++)
	{
		cxt.tail_position = in_tail_position && statement_index == statements.size() - 1;
		statements[statement_index]->Validate(cxt);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::StatementList::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	auto state = initial_state.Push<std::optional<JSValue>>({});
	for(auto statement : statements)
	{
#if ENABLE_ES4
		if(dynamic_cast<PackageStatement *>(statement))
		{
			continue;
		}
#endif

		state = state.Then(statement, &ParseNode::CompileStatement)
			.Apply(std::function<std::optional<JSValue>(std::optional<JSValue>, std::optional<JSValue>)>([](std::optional<JSValue> second_value, std::optional<JSValue> first_value)
		{
			if(second_value.has_value())
			{
				return second_value;
			}
			else
			{
				return first_value;
			}
		}));
	}
	return state;
}

#if ENABLE_ES4
void JS::ParseNode::StatementList::SetupContent(JS::Runtime::EnvironmentMode mode)
{
	for(auto statement : statements)
	{
		statement->SetupContent(mode);
	}
}
#endif

//// Node::Block

void JS::ParseNode::Block::Validate(JS::Runtime::ValidationContext cxt)
{
	// cxt.tail_position is changed later
	if(cxt.Mode == JS::Runtime::EnvironmentMode::Class)
	{
		// static block, these do not participate in top-level await
		cxt.is_module_range = false;
	}

	cxt.Mode = JS::Runtime::EnvironmentMode::Local;

#if ENABLE_ES4
	JS::Runtime::CallContextCapture bindings_context; // needed for both bindings and statements
	if(IsVersionES4())
	{
		GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
		if(cxt.is_package_range)
		{
			LexicalEnvironment = GetCurrentExecutionContext()->LexicalEnvironment;
		}
	}
#endif

	cxt.CurrentValidationContext = &cxt;

	bool in_tail_position = cxt.tail_position;
	for(size_t statement_index = 0; statement_index < statements.size(); statement_index ++)
	{
		auto statement = statements[statement_index];
		cxt.tail_position = in_tail_position && statement_index == statements.size() - 1;
		statement->Validate(cxt);
#if ENABLE_ES4
		if(IsVersionES4())
		{
			statement->CreateBindings(JS::Runtime::EnvironmentMode::Local);
		}
#endif
	}

#if ENABLE_ES4
	if(IsVersionES4() && cxt.superclass_constructor_called)
	{
		call_superclass_constructor = false;
	}
#endif
}

#if ENABLE_ES4
void JS::ParseNode::Block::SetupContent([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
	// TODO: this does not work, hoisted variables cannot find their declarations
/*	for(auto statement : statements)
	{
		statement->SetupContent(JS::Runtime::EnvironmentMode::Local);
	}*/
}
#endif

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::Block::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::BlockRange block_range = JS::Interpreter::Code::BeginBlockRange(initial_state);
	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Apply(std::function<void(void)>([this]()
	{
#if ENABLE_ES4
		if(IsVersionES4() && LexicalEnvironment != nullptr)
		{
			GetCurrentExecutionContext()->LexicalEnvironment = LexicalEnvironment;
		}
		else
#endif
		{
			GetCurrentExecutionContext()->LexicalEnvironment = std::make_shared<JS::Runtime::DeclarativeEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
		}

#if ENABLE_ES4
		if(!IsVersionES4() || LexicalEnvironment == nullptr)
#endif
		{
			JS::Runtime::ContextCapture context_capture; // needed for both bindings and statements
			for(auto statement : statements)
			{
				statement->CreateBindings(JS::Runtime::EnvironmentMode::Local);
			}
			context_capture.release();
		}

#if ENABLE_ES4
		{
			JS::Runtime::ContextCapture context_capture;
			for(auto statement : statements)
			{
				statement->SetupContent(JS::Runtime::EnvironmentMode::Local);
			}
			context_capture.release();
		}

		if(IsVersionES4() && call_superclass_constructor)
		{
			auto env = std::dynamic_pointer_cast<JS::Runtime::FunctionEnvironmentRecord>(GetCurrentExecutionContext()->GetThisEnvironment());
			if(env == nullptr)
			{
				JS::Throw(JS::New<ReferenceError> __LOC()); // TODO: what should be thrown?
			}

			if(env->FunctionObject->ClassBeingConstructed->Super.has_value()) // check if there is a superclass (this is always the case)
			{
				GetClassForType(env->FunctionObject->ClassBeingConstructed->Super.value())->Init(env->GetThisBinding(), {});
			}
		}
#endif
	})).Push<std::optional<JSValue>>({});
	for(auto statement : statements)
	{
		state = state.Then(statement, &ParseNode::CompileStatement)
			.Apply(std::function<std::optional<JSValue>(std::optional<JSValue>, std::optional<JSValue>)>([](std::optional<JSValue> second_value, std::optional<JSValue> first_value)
		{
			if(second_value.has_value())
			{
				return second_value;
			}
			else
			{
				return first_value;
			}
		}));
	}
	return block_range.EndBlockRange(state);
}

void JS::ParseNode::Block::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	switch(mode)
	{
	case JS::Runtime::EnvironmentMode::Regional:
		// lexical declarations not exported
		mode = JS::Runtime::EnvironmentMode::Hoisted;
		break;
	case JS::Runtime::EnvironmentMode::Hoisted:
		break;
	case JS::Runtime::EnvironmentMode::Local:
	case JS::Runtime::EnvironmentMode::Class:
		return;
	}

	for(auto statement : statements)
	{
		statement->CreateBindings(mode);
	}
}

JS::Interpreter::State<JS::Runtime::ClassContext> JS::ParseNode::Block::CompileClassElement(JS::Interpreter::State<JS::Runtime::ClassContext> initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	// TODO: static initializer
	return initial_state;
}

//// Node::NamedImportBinding

//// Node::ImportWithEntry

//// Node::NamedImportBindings

//// Node::WithClause

//// Node::ImportStatement

JS::Runtime::ModuleRequestRecord JS::ParseNode::ImportStatement::GetModuleRequest()
{
	std::vector<JS::Runtime::ImportAttribute> Attributes;

	if(with_clause.has_value())
	{
		for(auto entry : with_clause.value()->entries)
		{
			JS::Runtime::ImportAttribute attribute;
			attribute.Key = std::visit([](auto&& key)
			{
				return key->StringValue;
			}, entry->attribute_key);
			attribute.Value = entry->string_literal;
			Attributes.push_back(attribute);
		}
	}

	return JS::Runtime::ModuleRequestRecord(from_module, Attributes);
}

std::vector<JS::Runtime::ImportEntry> JS::ParseNode::ImportStatement::GetImportEntries()
{
	std::vector<JS::Runtime::ImportEntry> importEntries;
	auto moduleRequest = GetModuleRequest();

	if(default_binding.has_value())
	{
		importEntries.push_back(JS::Runtime::ImportEntry(moduleRequest, u"default", default_binding.value()->StringValue));
	}

	if(bindings.has_value())
	{
		std::visit([&](auto&& namedImport)
		{
			using T = std::decay_t<decltype(namedImport)>;
			if constexpr(std::is_same_v<T, Identifier *>)
			{
				// '*' 'as' ImportedBinding
				importEntries.push_back(JS::Runtime::ImportEntry(moduleRequest, /* NamespaceObject, */ namedImport->StringValue));
			}
			else if constexpr(std::is_same_v<T, NamedImportBindings *>)
			{
				for(auto binding : namedImport->bindings)
				{
					auto local_name = binding->name->StringValue;
					JSString import_name;
					if(binding->export_name.has_value())
					{
						import_name = std::visit([](auto&& name)
						{
							return name->StringValue;
						}, binding->export_name.value());
					}
					else
					{
						import_name = local_name;
					}
					importEntries.push_back(JS::Runtime::ImportEntry(moduleRequest, import_name, local_name));
				}
			}
			else
			{
				static_assert(false);
			}
		}, bindings.value());
	}

	return importEntries;
}

void JS::ParseNode::ImportStatement::Validate([[maybe_unused]] JS::Runtime::ValidationContext cxt)
{
	// TODO: is there anything that needs to happen here?
}

void JS::ParseNode::ImportStatement::CreateBindings([[maybe_unused]] JS::Runtime::EnvironmentMode mode)
{
/*
	std::set<JSString> bound_names;
	if(default_binding.has_value())
	{
		bound_names.insert(default_binding.value()->StringValue);
	}
	if(bindings.has_value())
	{
		if(auto identifier = std::get_if<Identifier *>(bindings.value()))
		{
			bound_names.insert((*identifier)->StringValue);
		}
		else if(auto named_import_bindings = std::get_if<NamedImportBindings *>(bindings.value()))
		{
			for(auto named_import_binding : (*named_import_bindings)->bindings)
			{
				bound_names.insert(named_import_binding->name);
			}
		}
	}
	for(auto bound_name : bound_names)
	{
	}
*/
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ImportStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<std::optional<JSValue>>({});
}

//// Node::ExportSpecifier

//// Node::NamedExports

//// Node::ExportFromClause

std::optional<JS::Runtime::ModuleRequestRecord> JS::ParseNode::ExportFromClause::GetModuleRequest()
{
	if(!from_module.has_value())
	{
		return {};
	}

	std::vector<JS::Runtime::ImportAttribute> Attributes;

	if(with_clause.has_value())
	{
		for(auto entry : with_clause.value()->entries)
		{
			JS::Runtime::ImportAttribute attribute;
			attribute.Key = std::visit([](auto&& key)
			{
				return key->StringValue;
			}, entry->attribute_key);
			attribute.Value = entry->string_literal;
			Attributes.push_back(attribute);
		}
	}

	return JS::Runtime::ModuleRequestRecord(from_module.value(), Attributes);
}

//// Node::ExportStatement

std::optional<JS::Runtime::ModuleRequestRecord> JS::ParseNode::ExportStatement::GetModuleRequest()
{
	if(auto clause = dynamic_cast<ExportFromClause *>(expression))
	{
		return clause->GetModuleRequest();
	}
	else
	{
		return {};
	}
}

std::vector<JS::Runtime::ExportEntry> JS::ParseNode::ExportStatement::GetExportEntries()
{
	std::vector<JS::Runtime::ExportEntry> exportEntries;

	if(auto export_from_clause = dynamic_cast<ExportFromClause *>(expression))
	{
		std::optional<JS::Runtime::ModuleRequestRecord> module = export_from_clause->GetModuleRequest();

		if(!export_from_clause->export_clause.has_value())
		{
			// '*'
			exportEntries.push_back(JS::Runtime::ExportEntry(module, JS::Runtime::ExportEntry::Type::AllButDefault, {}, {}));
		}
		else if(auto named_exports = dynamic_cast<NamedExports *>(export_from_clause->export_clause.value()))
		{
			// NamedExports
			for(auto export_specifier : named_exports->export_specifiers)
			{
				JSString sourceName = std::visit([](auto&& name) { return name->StringValue; }, export_specifier->name);
				std::optional<JSString> importName, localName;
				if(module.has_value())
				{
					importName = sourceName;
				}
				else
				{
					localName = sourceName;
				}
				JSString exportName;
				if(export_specifier->export_name.has_value())
				{
					exportName = std::visit([](auto&& name) { return name->StringValue; }, export_specifier->export_name.value());
				}
				else
				{
					exportName = sourceName;
				}
				exportEntries.push_back(JS::Runtime::ExportEntry(module, importName, localName, exportName));
			}
		}
		else
		{
			// '*' as ModuleExportName
			JSString exportName;
			if(auto name = dynamic_cast<Identifier *>(export_from_clause->export_clause.value()))
			{
				exportName = name->StringValue;
			}
			else if(auto name = dynamic_cast<StringLiteral *>(export_from_clause->export_clause.value()))
			{
				exportName = name->StringValue;
			}
			else
			{
				assert(false);
			}
			exportEntries.push_back(JS::Runtime::ExportEntry(module, JS::Runtime::ExportEntry::Type::All, {}, exportName));
		}
	}
	else if(is_default)
	{
		std::multiset<JSString> names;
		expression->CollectBoundNames(names);
		if(names.size() == 0)
		{
			exportEntries.push_back(JS::Runtime::ExportEntry({}, {}, u"default", u"default"));
		}
		else
		{
			assert(names.size() == 1);
			for(auto name : names)
			{
				exportEntries.push_back(JS::Runtime::ExportEntry({}, {}, name, u"default"));
			}
		}
	}
	else
	{
		std::multiset<JSString> names;
		expression->CollectBoundNames(names);
		for(auto name : names)
		{
			exportEntries.push_back(JS::Runtime::ExportEntry({}, {}, name, name));
		}
	}
	return exportEntries;
}

void JS::ParseNode::ExportStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	expression->Validate(cxt);
}

void JS::ParseNode::ExportStatement::CreateBindings(JS::Runtime::EnvironmentMode mode)
{
	if(is_default
	&& !dynamic_cast<ClassDefinition *>(expression)
	&& !dynamic_cast<FunctionDefinition *>(expression))
	{
		// expression
		GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
				u"default"
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				JSBinding::MakeImmutable({}, /* Strict = */ true));
	}
	else
	{
		expression->CreateBindings(mode);
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ExportStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	if([[maybe_unused]] auto export_from_clause = dynamic_cast<ExportFromClause *>(expression))
	{
		return initial_state.Push<std::optional<JSValue>>({});
	}
	else if(is_default)
	{
		if(auto class_definition = dynamic_cast<ClassDefinition *>(expression))
		{
			if(!class_definition->name.has_value())
			{
				return expression->CompileExpression(initial_state RUNTIME_PHASE).Apply(std::function<std::optional<JSValue>(JSValue)>(
					[](JSValue class_value) -> std::optional<JSValue>
				{
					GetCurrentExecutionContext()->LexicalEnvironment->InitializeSingletonBinding(
						u"default"
						PUBLIC_NAMESPACES
						READ_WRITE_ACCESS,
						class_value);
					return {};
				}));
			}
		}

		if(dynamic_cast<ClassDefinition *>(expression) // only named ones
		|| dynamic_cast<FunctionDefinition *>(expression))
		{
			return expression->CompileStatement(initial_state);
		}
		else
		{
			// expression
			return expression->CompileExpression(initial_state RUNTIME_PHASE).Apply(std::function<std::optional<JSValue>(JSValue)>(
				[](JSValue expression_value) -> std::optional<JSValue>
			{
				GetCurrentExecutionContext()->LexicalEnvironment->InitializeSingletonBinding(
					u"default"
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS,
					expression_value);
				return {};
			}));
		}
	}
	else
	{
		return expression->CompileStatement(initial_state);
	}
}

#if ENABLE_ES4
//// Node::NamespaceStatement

void JS::ParseNode::NamespaceStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	if(cxt.Attributes.Dynamic)
	{
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the dynamic attribute"));
	}

	if(cxt.Attributes.Prototype)
	{
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the prototype attribute"));
	}

	switch(cxt.Attributes.Category)
	{
	case JS::Runtime::MemberModifier::None:
		break;
	case JS::Runtime::MemberModifier::Static:
		if(!std::dynamic_pointer_cast<JS::Runtime::ClassEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment))
		{
			JS::Throw(JS::New<AttributeError> __LOC("a non-class property definition cannot have the static attribute"));
		}
		break;
	case JS::Runtime::MemberModifier::Virtual:
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the virtual attribute"));
	case JS::Runtime::MemberModifier::Final:
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the final attribute"));
	//case JS::Runtime::MemberModifier::Constructor,
	case JS::Runtime::MemberModifier::Abstract:
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the abstract attribute"));
	}

	if(cxt.Attributes.Prototype)
	{
		JS::Throw(JS::New<AttributeError> __LOC("a namespace definition cannot have the native attribute"));
	}

	gcn::gc_ptr<JS::Class::Namespace> namespace_;

	if(!uri.has_value())
	{
		namespace_ = JS::New<JS::Class::Namespace>(name->StringValue);
	}
	else if(auto uri_string = dynamic_cast<StringLiteral *>(uri.value()))
	{
		namespace_ = JS::New<JS::Class::Namespace>(uri_string->StringValue);
	}
	else
	{
		uri.value()->Validate(cxt);

		auto nsresult = uri.value()->EvaluateExpression(VIA_COMPILE_TIME_PHASE).GetValue();
		if(auto nsvalue = nsresult.cast_object<JS::Class::Namespace>())
		{
			namespace_ = nsvalue;
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("not a constant namespace"));
		}
	}

	GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
		name->StringValue,
		cxt.Attributes.GetNamespaces(),
		AccessSet::ReadWrite,
		JSProperty::MakeVariable(
			namespace_,
			/* Writable = */ false,
			true,
			cxt.Attributes.Explicit,
			Intrinsics()->Namespace_class));
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::NamespaceStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<std::optional<JSValue>>({});
}
#endif

#if ENABLE_ES4
//// Node::UseNamespaceStatement

void JS::ParseNode::UseNamespaceStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	std::set<JSNamespace> nsvalues;
	for(auto nsspec : namespaces)
	{
		nsspec->Validate(cxt);

		auto nsresult = nsspec->EvaluateExpression(VIA_COMPILE_TIME_PHASE).GetValue();
		if(auto nsvalue = nsresult.cast_object<JS::Class::Namespace>())
		{
			nsvalues.insert(nsvalue);
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("not a namespace"));
		}
	}

	cxt.CurrentValidationContext->OpenNamespaces.insert(nsvalues.begin(), nsvalues.end());
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::UseNamespaceStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<std::optional<JSValue>>({});
}
#endif

#if ENABLE_ES4
//// Node::PackageName
#endif

#if ENABLE_ES4
//// Node::ES4ImportStatement

void JS::ParseNode::ES4ImportStatement::Validate(JS::Runtime::ValidationContext cxt)
{
	// TODO: check attributes
	// if no import_name is provided, none are allowed (aside from false)
	// otherwise dynamic, prototype and override are not allowed
	auto environment = std::dynamic_pointer_cast<JS::Runtime::GlobalEnvironmentRecord>(GetCurrentExecutionContext()->LexicalEnvironment);
	if(environment == nullptr)
	{
		JS::Throw(JS::New<SyntaxError> __LOC("import outside package scope"));
	}

	JS::Runtime::PackageImportRecord record;
	record = std::visit([this](auto&& name) -> JS::Runtime::PackageImportRecord
	{
		using T = std::decay_t<decltype(name)>;
		if constexpr(std::is_same_v<T, JSString>)
		{
			return JS::Host->LocateES4Package(name);
		}
		else if constexpr(std::is_same_v<T, PackageName *>)
		{
			// activate package in import dir
			GetCurrentExecutionContext()->GetPackage()->ActivateImportedPackage(name->package_names);
			auto record = JS::Host->LocateES4Package(name->package_names);
			if(record.Package == nullptr)
			{
				// ActionScript import: not a package but a member of a package, check the parent package
				std::vector<JSString> parent_name = name->package_names;
				parent_name.pop_back();
				GetCurrentExecutionContext()->GetPackage()->ActivateImportedPackage(parent_name, /* open = */ true);
				record.Package = JS::Host->GetES4Package(std::optional<std::filesystem::path>{}, parent_name, true).Package;
			}
			return record;
		}
		else
		{
			static_assert(false);
		}
	}, package_name);

	record.ScriptOrModule->Perform();
	auto package = record.Package;

	if(import_name.has_value())
	{
		// create alias
		GetCurrentExecutionContext()->LexicalEnvironment->CreateSingletonBinding(
			import_name.value(),
			cxt.Attributes.GetNamespaces(),
			AccessSet::ReadWrite,
			JSProperty::MakeVariable(
				package,
				/* Writable = */ false,
				/* Enumerable = */ true,
				cxt.Attributes.Explicit,
				Intrinsics()->Package_class));
	}

	// load members into current space
	for(auto iter = package->properties.begin(); iter != package->properties.end(); ++iter)
	{
		bool blocked = false;
		std::set<JSNamespace> namespaces;
		bool read = false, write = false;

		if((*iter)->Explicit)
		{
			continue;
		}

		// TODO: ignore forbidden entries
		for(auto [namespace_, access] : iter.get_access())
		{
			// TODO: record conflicting imports between different packages (note that the same package might be important repeatedly due to how ActionScript style imports are implemented)
			if(environment->HasBinding(iter.get_name() WITH_NAMESPACES({namespace_}) WITH_ACCESS_SET(AccessSet(int(access)) COMPILE_TIME_PHASE)))
			{
				blocked = true;
				break;
			}

			namespaces.insert(namespace_);
			switch(access)
			{
			case Access::Read:
				read = true;
				break;
			case Access::Write:
				write = true;
				break;
			}
		}

		if(!blocked)
		{
			AccessSet accesses;
			if(!write)
			{
				accesses = AccessSet::Read;
			}
			else if(!read)
			{
				accesses = AccessSet::Write;
			}
			else
			{
				accesses = AccessSet::ReadWrite;
			}

			GetCurrentExecutionContext()->GetPackage()->Import(package, iter.get_name() WITH_NAMESPACES(namespaces) WITH_ACCESS_SET(accesses));
		}
	}
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ES4ImportStatement::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<std::optional<JSValue>>({});
}
#endif

#if ENABLE_ES4
//// Node::PackageStatement

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::PackageStatement::Process(JS::Runtime::ValidationContext cxt, JS::Interpreter::State<>& initial_state)
{
	std::optional<std::filesystem::path> sourcefilename = dynamic_cast<JS::Runtime::SourceTextPackageRecord *>(GetCurrentExecutionContext()->ScriptOrModule)->sourcefilename;

//	JSString name;
	bool open = false; // TODO
	if(!package_name.has_value())
	{
//		name = JS::Host->GetES4PackageName(sourcefilename);
	}
	else if([[maybe_unused]] auto string = dynamic_cast<StringLiteral *>(*package_name))
	{
//		name = JS::Host->GetES4PackageName(sourcefilename, string->StringValue);
	}
	else if(auto identifiers = dynamic_cast<PackageName *>(*package_name))
	{
//		name = JS::Host->GetES4PackageName(sourcefilename, identifiers->package_names);
		if(sourcefilename.has_value())
		{
			open = JS::Host->IsES4PackageOpen(sourcefilename.value(), identifiers->package_names);
		}
	}
	else
	{
		assert(false);
	}

	JS::Runtime::PackageImportRecord * record;

	if(!package_name.has_value())
	{
		record = &JS::Host->GetES4Package(sourcefilename, open);
	}
	else if(auto string = dynamic_cast<StringLiteral *>(*package_name))
	{
		record = &JS::Host->GetES4Package(sourcefilename, string->StringValue, open);
	}
	else if(auto identifiers = dynamic_cast<PackageName *>(*package_name))
	{
		record = &JS::Host->GetES4Package(sourcefilename, identifiers->package_names, open);
	}
	else
	{
		assert(false);
	}

	CompiledPackage = record->Package;

	switch(CompiledPackage->PackageType)
	{
	case JS::Class::Package::Type::Open:
		if(!open)
		{
			JS::Throw(JS::New<ReferenceError> __LOC("conflicting package modes"));
		}
		break;
	case JS::Class::Package::Type::Transient:
		if(open)
		{
			CompiledPackage->PackageType = JS::Class::Package::Type::Open;
		}
		else
		{
			CompiledPackage->PackageType = JS::Class::Package::Type::Closed;
		}
		break;
	case JS::Class::Package::Type::Closed:
		if(open)
		{
			JS::Throw(JS::New<ReferenceError> __LOC("conflicting package modes"));
		}
		break;
	}

	if(CompiledPackage->PackageType == JS::Class::Package::Type::Closed)
	{
		if(record->ScriptOrModule != nullptr)
		{
			JS::Throw(JS::New<ReferenceError> __LOC("closed package loaded from multiple sources"));
		}
		record->ScriptOrModule = dynamic_cast<JS::Runtime::PackageRecord *>(GetCurrentExecutionContext()->ScriptOrModule);
	}

	Define(CompiledPackage, JSQualifiedName{CompiledPackage->InternalNamespace, u"internal"} WITH_TYPE(JS::Intrinsics()->Namespace_class),
		CompiledPackage->InternalNamespace,
		PropertyAttributes{}.WithWritable(false).WithConfigurable(false).WithExplicit(true));

	GetCurrentExecutionContext()->VariableEnvironment = GetCurrentExecutionContext()->LexicalEnvironment =
		std::make_shared<JS::Runtime::GlobalEnvironmentRecord>(
			CompiledPackage, CompiledPackage,
			GetCurrentExecutionContext()->LexicalEnvironment);

	cxt.OpenNamespaces = { JS_public, GetCurrentExecutionContext()->GetPackage()->InternalNamespace };

	cxt.Strict = JS::StrictMode;
	cxt.Mode = JS::Runtime::EnvironmentMode::Regional;
	cxt.tail_position = false;

	cxt.CurrentValidationContext = &cxt;

	JS::Runtime::CallContextCapture bindings_context; // needed for both bindings and statements

	for(auto statement : body->statements)
	{
		statement->Validate(cxt);
		statement->CreateBindings(JS::Runtime::EnvironmentMode::Regional);
	}

	for(auto directive : body->statements)
	{
		directive->SetupContent(JS::Runtime::EnvironmentMode::Regional);
	}

	// TODO: not for open packages
//	CompiledPackage->ScriptOrModule = GetCurrentExecutionContext()->ScriptOrModule;

	ProcessDirectives(body->statements);

	JS::Interpreter::State<std::optional<JSValue>> state = initial_state.Push<std::optional<JSValue>>({});

	for(auto statement : body->statements)
	{
		state = state.Then(statement, &ParseNode::CompileStatement)
			.Apply(std::function<std::optional<JSValue>(std::optional<JSValue>, std::optional<JSValue>)>([](std::optional<JSValue> second_value, std::optional<JSValue> first_value)
		{
			if(second_value.has_value())
			{
				return second_value;
			}
			else
			{
				return first_value;
			}
		}));
	}

	return state;
}

JS::Runtime::SourceTextPackageRecord * JS::ParseNode::PackageStatement::CreatePackageRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename)
{
	return new JS::Runtime::SourceTextPackageRecord(filename, realm, this);
}
#endif

#if ENABLE_ES4
//// Node::PragmaItem
#endif

#if ENABLE_ES4
//// Node::PragmaDeclaration

void JS::ParseNode::PragmaDeclaration::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	ProcessDirective(this);
	// TODO: other directives
	cxt.CurrentValidationContext->Strict = cxt.Strict = JS::StrictMode;
}

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::PragmaDeclaration::CompileStatement(JS::Interpreter::State<>& initial_state)
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<std::optional<JSValue>>({});
}
#endif

//// Node::ScriptOrModule

JS::Interpreter::State<std::optional<JSValue>> JS::ParseNode::ScriptOrModule::Process(JS::Runtime::ValidationContext cxt, JS::Interpreter::State<>& initial_state)
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		cxt.OpenNamespaces = { JS_public, GetCurrentExecutionContext()->GetPackage()->InternalNamespace };
	}
#endif

	if(Version::ES5 <= SelectedVersion)
	{
		for(auto statement : body->statements)
		{
			if(!ProcessDirective(statement))
			{
				break;
			}
		}
	}

	cxt.Strict = JS::StrictMode;
	cxt.Mode = JS::Runtime::EnvironmentMode::Regional;
	cxt.tail_position = false;

	cxt.CurrentValidationContext = &cxt;

#if ENABLE_ES4
	JS::Runtime::CallContextCapture bindings_context; // needed for both bindings and statements
#endif

	for(auto statement : body->statements)
	{
#if ENABLE_ES4
		if(dynamic_cast<PackageStatement *>(statement))
		{
			continue;
		}
#endif

		statement->Validate(cxt);
#if ENABLE_ES4
		if(IsVersionES4())
		{
			statement->CreateBindings(JS::Runtime::EnvironmentMode::Regional);
		}
#endif
	}

	if(auto moduleRecord = dynamic_cast<JS::Runtime::SourceTextModuleRecord *>(cxt.scriptOrModuleRecord))
	{
		moduleRecord->HasTLA = cxt.has_top_level_await;
	}

	ProcessDirectives(body->statements);

//	cxt.Strict = JS::StrictMode;

#if ENABLE_ES4
	if(!IsVersionES4())
#endif
	{
		//JS::Runtime::CallContextCapture bindings_context; // needed for both bindings and statements
		for(auto directive : body->statements)
		{
			directive->CreateBindings(JS::Runtime::EnvironmentMode::Regional);
		}
	}

#if ENABLE_ES4
	if(IsVersionES4())
	{
		//JS::Runtime::CallContextCapture setup_context; // needed for both bindings and statements
		for(auto directive : body->statements)
		{
			directive->SetupContent(JS::Runtime::EnvironmentMode::Regional);
		}
		//setup_context.release();
	}
#endif

	if(dynamic_cast<JS::Runtime::SourceTextModuleRecord *>(cxt.scriptOrModuleRecord)
	&& cxt.has_top_level_await)
	{
		// provide async wrapper so that fulfill methods get called once it is completed
		JS::Interpreter::FunctionRange<> function_range = JS::Interpreter::Code::BeginFunctionRange(initial_state, false, false, true);
		JS::Interpreter::State<std::optional<JSValue>> state1 = body->CompileStatement(initial_state);
		JS::Interpreter::State<JSValue> state2 = function_range.EndFunctionRange(state1);
		return state2.Pop().Push<std::optional<JSValue>>(JS_undefined);
	}
	else
	{
		return body->CompileStatement(initial_state);
	}
}

JS::Runtime::ScriptRecord * JS::ParseNode::ScriptOrModule::CreateScriptRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename)
{
#if ENABLE_ES4
	std::vector<JS::Runtime::PackageRecord *> Packages;
	if(IsVersionES4())
	{
		// collect packages
		for(auto statement : body->statements)
		{
			if(auto package = dynamic_cast<JS::ParseNode::PackageStatement *>(statement))
			{
				Packages.push_back(package->CreatePackageRecord(realm, filename));
			}
		}
	}
#endif

	auto record = new JS::Runtime::ScriptRecord(
		filename,
		realm,
		this
#if ENABLE_ES4
		, Packages
#endif
	);

	if(Enable_CommonJS)
	{
		record->CommonJSModule = JS::New<JS::Class::OrdinaryObject>();
	}

	return record;
}

JS::Runtime::SourceTextModuleRecord * JS::ParseNode::ScriptOrModule::CreateModuleRecord(JS::Runtime::Realm * realm, std::optional<std::filesystem::path> filename)
{
	std::vector<JS::Runtime::ModuleRequestRecord> requestedModules;
	std::map<JSString, JS::Runtime::ImportEntry> importedBoundNames;
	std::vector<JS::Runtime::ImportEntry> importEntries;
	std::vector<JS::Runtime::ExportEntry> localExportEntries;
	std::vector<JS::Runtime::ExportEntry> indirectExportEntries;
	std::vector<JS::Runtime::ExportEntry> starExportEntries;

	for(auto statement : body->statements)
	{
		std::optional<JS::Runtime::ModuleRequestRecord> requested_module;
		if(auto import_statement = dynamic_cast<JS::ParseNode::ImportStatement *>(statement))
		{
			requested_module = import_statement->GetModuleRequest();
			auto import_entries = import_statement->GetImportEntries();
			importEntries.insert(importEntries.end(), import_entries.begin(), import_entries.end());
			for(auto import_entry : import_entries)
			{
				importedBoundNames[import_entry.LocalName] = import_entry;
			}
		}
		else if(auto export_statement = dynamic_cast<JS::ParseNode::ExportStatement *>(statement))
		{
			requested_module = export_statement->GetModuleRequest();
		}

		if(requested_module.has_value() && std::find(requestedModules.begin(), requestedModules.end(), requested_module.value()) == requestedModules.end())
		{
			requestedModules.push_back(requested_module.value());
		}
	}

	for(auto statement : body->statements)
	{
		if(auto export_statement = dynamic_cast<JS::ParseNode::ExportStatement *>(statement))
		{
			for(auto export_entry : export_statement->GetExportEntries())
			{
				if(export_entry.ModuleRequest.has_value())
				{
					if(export_entry.ImportNameType == JS::Runtime::ExportEntry::Type::AllButDefault)
					{
						starExportEntries.push_back(export_entry);
					}
					else
					{
						indirectExportEntries.push_back(export_entry);
					}
				}
				else if(!export_entry.LocalName.has_value())
				{
					localExportEntries.push_back(export_entry);
				}
				else
				{
					auto import_entry = importedBoundNames.find(export_entry.LocalName.value());
					if(import_entry == importedBoundNames.end())
					{
						localExportEntries.push_back(export_entry);
					}
					else if(import_entry->second.ImportName.has_value())
					{
						indirectExportEntries.push_back(JS::Runtime::ExportEntry(
							import_entry->second.ModuleRequest,
							import_entry->second.ImportName,
							{},
							export_entry.ExportName));
					}
					else
					{
						// NamespaceObject
						localExportEntries.push_back(export_entry);
					}
				}
			}
		}
	}

	return new JS::Runtime::SourceTextModuleRecord(
		filename,
		realm,
		// async will be set during validation
		this,
		requestedModules,
		importEntries,
		localExportEntries,
		indirectExportEntries,
		starExportEntries
	);
}

//// Node::Attribute

void JS::ParseNode::Attribute::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.IsStrict();
	selector->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::Attribute::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return CompileName(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSIdentifierReference)>([this WITH_PHASE(phase)](JSIdentifierReference qualified_name) -> JSValue
	{
		auto reference = JS::Runtime::LexicalReference(GetCurrentExecutionContext()->LexicalEnvironment, strict WITH_NAMESPACE(qualified_name.GetNamespaceSet()), qualified_name.Namespace, qualified_name.GetName(), true);
#if ENABLE_ES4
		if(!IsVersionES4())
#endif
		{
			reference = reference.ResolveBinding(VIA_PHASE(phase) READ_ACCESS);
		}
		return reference;
	}));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::Attribute::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return selector->CompileName(initial_state WITH_PHASE(phase)).Apply(std::function<JSIdentifierReference(JSIdentifierReference)>([](JSIdentifierReference qualified_name) -> JSIdentifierReference
	{
		if(qualified_name.Attribute || qualified_name.Private)
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
		return JSIdentifierReference(qualified_name.Namespace, qualified_name.GetName(), /* Attribute = */ true);
	}));
}

//// Node::WildcardIdentifier

#if ENABLE_ES4
JSType JS::ParseNode::WildcardIdentifier::EvaluateType()
{
	return Intrinsics()->Object_class;
}
#endif

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::WildcardIdentifier::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSIdentifierReference>(JSIdentifierReference(u"*"));
}

//// Node::QualifiedIdentifier

void JS::ParseNode::QualifiedIdentifier::Validate(JS::Runtime::ValidationContext cxt)
{
	cxt.tail_position = false;
	strict = cxt.IsStrict();
	namespace_selector->Validate(cxt);
	element_selector->Validate(cxt);
}

JS::Interpreter::State<JSValue> JS::ParseNode::QualifiedIdentifier::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return CompileName(initial_state WITH_PHASE(phase)).Apply(std::function<JSValue(JSIdentifierReference)>([this WITH_PHASE(phase)](JSIdentifierReference qualified_name) -> JSValue
	{
		auto reference = JS::Runtime::LexicalReference(GetCurrentExecutionContext()->LexicalEnvironment, strict WITH_NAMESPACES(qualified_name.GetNamespaceSet()), qualified_name.Namespace.value(), qualified_name.GetName());
#if ENABLE_ES4
		if(!IsVersionES4())
#endif
		{
			reference = reference.ResolveBinding(VIA_PHASE(phase) READ_ACCESS);
		}
		return reference;
	}));
}

JS::Interpreter::State<JSIdentifierReference> JS::ParseNode::QualifiedIdentifier::CompileName(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSNamespace> state;
	if(dynamic_cast<WildcardIdentifier *>(namespace_selector))
	{
		state = initial_state.Push<JSNamespace>(JSIdentifierReference::WildcardNamespace);
	}
	else
	{
		state = namespace_selector->CompileExpression(initial_state WITH_PHASE(phase)).Apply(std::function<JSNamespace(JSValue)>([this](JSValue namespace_result) -> JSNamespace
		{
			if(auto namespace_object = GetNonNullObject(namespace_result.GetValue()))
			{
				if(auto namespace_value = std::dynamic_pointer_cast<JS::Class::Namespace>(namespace_object.value()))
				{
					return namespace_value;
				}
			}
			JS::Throw(JS::New<TypeError> __LOC());
		}));
	}

	return state.Then(element_selector, &ParseNode::CompileName WITH_PHASE(phase))
		.Apply(std::function<JSIdentifierReference(JSIdentifierReference, JSNamespace)>([](JSIdentifierReference evaluated_name, JSNamespace namespace_) -> JSIdentifierReference
		{
			if(evaluated_name.Attribute || evaluated_name.Private)
			{
				JS::Throw(JS::New<InternalError> __LOC());
			}
			return JSIdentifierReference(namespace_, evaluated_name.GetName());
		}));
}

// the following parse nodes convert the input into string and then parse it again as XML, as defined in the specification

//// Node::XMLIdentifier

JS::Interpreter::State<JSValue> JS::ParseNode::XMLIdentifier::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(StringValue);
}

//// Node::XMLAttributeValue

JS::Interpreter::State<JSValue> JS::ParseNode::XMLAttributeValue::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(u"\"" + EscapeAttributeValue(StringValue) + u"\""); // TODO: normalized value?
}

//// Node::XMLElement

JS::Interpreter::State<JSValue> JS::ParseNode::XMLElement::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSString, JSString> state = initial_state.Then(tag, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSString(JSValue)>(
		[VIA_PHASE(phase)](JSValue tag_name_reference) -> JSString
	{
		return tag_name_reference.GetValue().ToString(VIA_PHASE(phase));
	})).Obtain(std::function<JSString(JSString)>([](JSString startTag) -> JSString
	{
		return u"<" + startTag;
	}));

	for(auto attribute : attributes)
	{
		state = state.Then(attribute, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSString(JSValue, JSString)>(
			[VIA_PHASE(phase)](JSValue attribute_reference, JSString xml) -> JSString
		{
			return xml + u" " + attribute_reference.GetValue().ToString(VIA_PHASE(phase));
		}));
	}

	if(endtag.has_value() || content.size() != 0)
	{
		state = state.Apply(std::function<JSString(JSString)>([](JSString xml) -> JSString { return xml + u">"; }));
		for(auto element : content)
		{
			state = state.Then(element, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSString(JSValue, JSString)>(
				[VIA_PHASE(phase)](JSValue element_value_reference, JSString xml) -> JSString
			{
				auto element_value = element_value_reference.GetValue();
				if(auto element_object = GetNonNullObject(element_value))
				{
					if(std::dynamic_pointer_cast<JS::Class::XML>(element_object.value())
					|| std::dynamic_pointer_cast<JS::Class::XMLList>(element_object.value()))
					{
						return xml + element_value.ToXMLString();
					}
				}
				return xml + element_value.ToString(VIA_PHASE(phase));
			}));
		}

		return state.Apply(std::function<JSValue(JSString, JSString)>([](JSString xml, JSString startTag) -> JSValue { return xml + u"</" + startTag + u">"; }));
	}
	else
	{
		return state.Apply(std::function<JSValue(JSString, JSString)>([](JSString xml, [[maybe_unused]] JSString _1) -> JSValue { return xml + u"/>"; }));
	}
}

//// Node::XMLAttribute

JS::Interpreter::State<JSValue> JS::ParseNode::XMLAttribute::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Then(name, &ParseNode::CompileExpression WITH_PHASE(phase)).Then(value, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(
		std::function<JSValue(JSValue, JSValue)>(
			[VIA_PHASE(phase)](JSValue value_reference, JSValue name_reference)
	{
		return name_reference.GetValue().ToString(VIA_PHASE(phase)) + u"=" + value_reference.GetValue().ToString(VIA_PHASE(phase));
	}));
}

//// Node::XMLPI

JS::Interpreter::State<JSValue> JS::ParseNode::XMLPI::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(u"<?" + StringValue + u"?>");
}

//// Node::XMLComment

JS::Interpreter::State<JSValue> JS::ParseNode::XMLComment::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(u"<!--" + StringValue + u"-->");
}

//// Node::XMLCDATA

JS::Interpreter::State<JSValue> JS::ParseNode::XMLCDATA::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(u"<![CDATA[" + StringValue + u"]]>");
}

//// Node::XMLText

JS::Interpreter::State<JSValue> JS::ParseNode::XMLText::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Push<JSValue>(StringValue);
}

//// Node::XMLInitializer

JS::Interpreter::State<JSValue> JS::ParseNode::XMLInitializer::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	return initial_state.Then(content, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSValue(JSValue)>(
		[](JSValue xml_string) -> JSValue
	{
		JSNamespace DefaultNamespace = GetCurrentExecutionContext()->LexicalEnvironment->GetDefaultNamespace();

		JS::Parser::XML::Parser xml_parser(u"<parent xmlns=\"" + DefaultNamespace->uri + u"\">" + GetString(xml_string).value() + u"</parent>");
		auto parsed_xml = xml_parser.ParseXML();
		if(parsed_xml->children.size() == 0)
		{
			auto result = JS::New<JS::Class::XML>();
			result->Class = JS::Class::XML::ClassType::Text;
			result->Value = u"";
			delete parsed_xml.get();
			return JSObject(result);
		}
		else if(parsed_xml->children.size() == 1)
		{
			auto result = parsed_xml->children[0];
			result->Parent = nullptr;
			delete parsed_xml.get();
			return result;
		}
		else
		{
			JS::Throw(JS::New<SyntaxError> __LOC());
		}
	}));
}

//// Node::XMLList

JS::Interpreter::State<JSValue> JS::ParseNode::XMLList::CompileExpression(JS::Interpreter::State<>& initial_state AND_PHASE(phase))
{
	JS::Parser::SourceLocationContext captureContext(location);
	JS::Interpreter::State<JSString> state = initial_state.Push<JSString>(u"");

	for(auto item : content)
	{
		state.Then(item, &ParseNode::CompileExpression WITH_PHASE(phase)).Apply(std::function<JSString(JSValue, JSString)>(
			[VIA_PHASE(phase)](JSValue item_result_reference, JSString xml) -> JSString
		{
			return xml + item_result_reference.GetValue().ToString(VIA_PHASE(phase));
		}));
	}

	return state.Apply(std::function<JSValue(JSString)>(
		[](JSString xml_string) -> JSValue
	{
		JSNamespace DefaultNamespace = GetCurrentExecutionContext()->LexicalEnvironment->GetDefaultNamespace();

		JS::Parser::XML::Parser xml_parser(u"<parent xmlns=\"" + DefaultNamespace->uri + u"\">" + xml_string + u"</parent>");
		auto parsed_xml = xml_parser.ParseXML();
		auto result = JS::New<JS::Class::XMLList>();
		result->children = parsed_xml->children;
		delete parsed_xml.get();
		return JSObject(result);
	}));
}

