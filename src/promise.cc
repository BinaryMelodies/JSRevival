
#include "promise.hh"
#include "execute.hh"
#include "modules.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// Promise

JSObject JS::Runtime::Intrinsics::* JS::Class::Promise::IntrinsicPrototype = &JS::Runtime::Intrinsics::Promise_prototype;

void JS::Class::Promise::Fulfill(JSValue reason)
{
	assert(PromiseState == StateType::Pending);
	std::vector<JS::Runtime::PromiseReaction> reactions = PromiseFulfillReactions;
	PromiseResult = reason;
	PromiseFulfillReactions.clear();
	PromiseRejectReactions.clear();
	PromiseState = StateType::Fulfilled;
	TriggerPromiseReactions(reactions, reason);
}

void JS::Class::Promise::Reject(JSValue reason)
{
	assert(PromiseState == StateType::Pending);
	std::vector<JS::Runtime::PromiseReaction> reactions = PromiseRejectReactions;
	PromiseResult = reason;
	PromiseFulfillReactions.clear();
	PromiseRejectReactions.clear();
	PromiseState = StateType::Rejected;
	if(!PromiseIsHandled)
	{
		JS::Host->PromiseRejectionTracker(gcn::gc_ptr<Promise>(this), JS::Runtime::Host::OperationType::Reject);
	}
	TriggerPromiseReactions(reactions, reason);
}

void JS::Class::Promise::TriggerPromiseReactions(std::vector<JS::Runtime::PromiseReaction>& reactions, JSValue argument)
{
	for(auto reaction : reactions)
	{
		JS::Runtime::Realm * realm;
		auto job = JS::Runtime::PromiseReactionJob::New(reaction, argument, realm);
		JS::Host->EnqueuePromiseJob(job, realm);
	}
}

std::map<JSString, JSValue> JS::Class::Promise::GetInternalSlots(bool unified)
{
	std::map<JSString, JSValue> slots = OrdinaryObject::GetInternalSlots(unified);
	switch(PromiseState)
	{
	case StateType::Pending:
		slots[u"PromiseState"] = u"pending";
		break;
	case StateType::Fulfilled:
		slots[u"PromiseState"] = u"fulfilled";
		break;
	case StateType::Rejected:
		slots[u"PromiseState"] = u"rejected";
		break;
	}
	slots[u"PromiseResult"] = PromiseResult.value_or(JS_undefined); // TODO: signal emptiness
	slots[u"PromiseFulfillReactions"] = JS_null; // TODO
	slots[u"PromiseRejectReactions"] = JS_null; // TODO
	slots[u"PromiseIsHandled"] = PromiseIsHandled;
	return slots;
}

JSValue JS::Class::Promise::PerformPromiseThen(JSValue onFulfilled, JSValue onRejected, std::optional<JS::Runtime::PromiseCapability<>> resultCapability)
{
	JS::Runtime::JobCallback * onFulfilledJobCallback = nullptr;
	if(auto onFulfilled_function = GetFunction(onFulfilled))
	{
		onFulfilledJobCallback = JS::Host->MakeJobCallback(onFulfilled_function.value());
	}

	JS::Runtime::JobCallback * onRejectedJobCallback = nullptr;
	if(auto onRejected_function = GetFunction(onRejected))
	{
		onRejectedJobCallback = JS::Host->MakeJobCallback(onRejected_function.value());
	}

	JS::Runtime::PromiseReaction fulfillReaction { /* Capability = */ resultCapability, /* Type = */ JS::Runtime::PromiseReaction::ReactionType::Fulfill, /* Handler = */ onFulfilledJobCallback };
	JS::Runtime::PromiseReaction rejectReaction { /* Capability = */ resultCapability, /* Type = */ JS::Runtime::PromiseReaction::ReactionType::Reject, /* Handler = */ onRejectedJobCallback };

	switch(PromiseState)
	{
	case StateType::Pending:
		PromiseFulfillReactions.push_back(fulfillReaction);
		PromiseRejectReactions.push_back(rejectReaction);
		break;
	case StateType::Fulfilled:
		{
			JS::Runtime::Realm * realm;
			auto fulfillJob = JS::Runtime::PromiseReactionJob::New(fulfillReaction, PromiseResult.value_or(JS_undefined), realm);
			JS::Host->EnqueuePromiseJob(fulfillJob, realm);
		}
		break;
	case StateType::Rejected:
		{
			JS::Runtime::Realm * realm;
			if(!PromiseIsHandled)
			{
				JS::Host->PromiseRejectionTracker(gcn::gc_ptr<Promise>(this), JS::Runtime::Host::OperationType::Handle);
			}
			auto rejectJob = JS::Runtime::PromiseReactionJob::New(rejectReaction, PromiseResult.value_or(JS_undefined), realm);
			JS::Host->EnqueuePromiseJob(rejectJob, realm);
		}
		break;
	default:
		assert(false);
	}
	PromiseIsHandled = true;
	if(resultCapability.has_value())
	{
		return resultCapability.value().Promise;
	}
	else
	{
		return JS_undefined;
	}
}

gcn::gc_ptr<JS::Class::Promise> JS::Class::Promise::Resolve(JSObject constructor, JSValue value)
{
	if(auto promise = value.cast_object<JS::Class::Promise>())
	{
		if(JS::SameValue(JS::Get(value, u"constructor"), constructor))
		{
			return promise;
		}
	}

	JS::Runtime::PromiseCapability<> promiseCapability(constructor);
	JS::Call(promiseCapability.Resolve, JS_undefined, {value} RUNTIME_PHASE);
	return std::dynamic_pointer_cast<Promise>(promiseCapability.Promise);
}

//// PromiseCapabilityBase

void JS::Runtime::PromiseCapabilityBase::InitializePromise(JSValue constructor_value, JSObject& promise)
{
	if(auto constructor = GetConstructor(constructor_value))
	{
		auto executor = JS::New<JS::Runtime::Builtin::PromiseCapabilityExecutor>(GetCurrentRealm());
		promise = JS::Construct(constructor.value(), {executor} RUNTIME_PHASE).cast_object<JS::Class::Object>();
		if(auto resolve = GetFunction(executor->Resolve))
		{
			Resolve = resolve.value();
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("resolve is not callable"));
		}
		if(auto reject = GetFunction(executor->Reject))
		{
			Reject = reject.value();
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("reject is not callable"));
		}
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC("promise capability can only be created with a constructor parameter"));
	}
}

void JS::Runtime::PromiseCapabilityBase::ContinueDynamicImport(JS::Runtime::PromiseCapability<> promiseCapability, JS::Runtime::ModuleRecord * moduleRecord)
{
	auto loadPromise = moduleRecord->LoadRequestedModules();

	JSValue onRejected = JS::CreateBuiltinFunction(
		[promiseCapability]([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase)) -> JSValue
	{
		auto reason = arguments_get_index(arguments, 0);
		JS::Call(promiseCapability.Reject, JS_undefined, { reason } RUNTIME_PHASE);
		return JS_undefined;
	}, 1, u"", {}, GetCurrentRealm());

	JSValue linkAndEvaluate = JS::CreateBuiltinFunction(
		[promiseCapability, moduleRecord, onRejected]([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase)) -> JSValue
	{
		std::optional<JSValue> abruptResult;
		try
		{
			moduleRecord->Link();
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			abruptResult = JSValue::WrapValue(thrown_value);
		}

		if(abruptResult.has_value())
		{
			JS::Call(promiseCapability.Reject, JS_undefined, { abruptResult.value() } RUNTIME_PHASE);
			return JS_undefined;
		}

		auto evaluatePromise = moduleRecord->Evaluate();
		JSValue onFulfilled = JS::CreateBuiltinFunction(
			[promiseCapability, moduleRecord]([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase)) -> JSValue
		{
			auto namespace_ = moduleRecord->GetModuleNamespace();
			JS::Call(promiseCapability.Resolve, JS_undefined, { namespace_ } RUNTIME_PHASE);
			return JS_undefined;
		}, 0, u"", {}, GetCurrentRealm());

		evaluatePromise->PerformPromiseThen(onFulfilled, onRejected);

		return JS_undefined;
	}, 0, u"", {}, GetCurrentRealm());

	loadPromise->PerformPromiseThen(linkAndEvaluate, onRejected);
}

//// DynamicPayload


//// DynamicPayload

void JS::Runtime::DynamicPayload::NormalCompletion(ModuleRecord * moduleRecord)
{
	// ContinueDynamicImport
	PromiseCapability.ContinueDynamicImport(moduleRecord);
}

void JS::Runtime::DynamicPayload::ThrowCompletion(JSValue error_result)
{
	// ContinueDynamicImport
	JS::Call(PromiseCapability.Reject, JS_undefined, { error_result } RUNTIME_PHASE);
}

//// PromiseResolveThenableJob

void JS::Runtime::PromiseResolveThenableJob::Execute()
{
	gcn::gc_ptr<JS::Runtime::Builtin::PromiseResolveFunction> resolve;
	gcn::gc_ptr<JS::Runtime::Builtin::PromiseRejectFunction>  reject;

	CreateResolvingFunctions(promiseToResolve, resolve, reject);
	try
	{
		JS::Host->CallJobCallback(then, thenable, {resolve, reject});
	}
	catch(JS::Class::Internal::Value * thrown_value)
	{
		JS::Call(JSFunction(std::static_pointer_cast<JS::Runtime::Builtin::BuiltinFunction>(reject)), JS_undefined, {JSValue::WrapValue(thrown_value)} RUNTIME_PHASE);
	}
}

JS::Runtime::PromiseResolveThenableJob * JS::Runtime::PromiseResolveThenableJob::New(gcn::gc_ptr<JS::Class::Promise> promiseToResolve, JSObject thenable, JS::Runtime::JobCallback * then, JS::Runtime::Realm *& realm)
{
	try
	{
		realm = then->Callback.GetFunctionRealm();
	}
	catch(...)
	{
		realm = GetCurrentRealm();
	}
	return new (GC) JS::Runtime::PromiseResolveThenableJob(promiseToResolve, thenable, then);
}

//// PromiseReactionJob

void JS::Runtime::PromiseReactionJob::Execute()
{
	bool resolve;
	JSValue result;
	if(reaction.Handler != nullptr)
	{
		try
		{
			result = JS::Host->CallJobCallback(reaction.Handler, JS_undefined, {argument});
			resolve = true;
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			result = JSValue::WrapValue(thrown_value);
			resolve = false;
		}
	}
	else
	{
		switch(reaction.Type)
		{
		case JS::Runtime::PromiseReaction::ReactionType::Fulfill:
			resolve = true;
			result = argument;
			break;
		case JS::Runtime::PromiseReaction::ReactionType::Reject:
			resolve = false;
			result = argument;
			break;
		}
		assert(false);
	}

	if(resolve)
	{
		if(reaction.Capability.has_value())
		{
			JS::Call(reaction.Capability.value().Resolve, JS_undefined, {result} RUNTIME_PHASE);
		}
	}
	else
	{
		if(reaction.Capability.has_value())
		{
			JS::Call(reaction.Capability.value().Reject, JS_undefined, {result} RUNTIME_PHASE);
		}
		else
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}
	}
}

JS::Runtime::PromiseReactionJob * JS::Runtime::PromiseReactionJob::New(JS::Runtime::PromiseReaction reaction, JSValue argument, JS::Runtime::Realm *& realm)
{
	if(reaction.Handler != nullptr)
	{
		try
		{
			realm = reaction.Handler->Callback.GetFunctionRealm();
		}
		catch(...)
		{
			realm = GetCurrentRealm();
		}
	}
	else
	{
		realm = nullptr;
	}
	return new (GC) JS::Runtime::PromiseReactionJob(reaction, argument);
}

//// builtin PromiseResolveFunction

JSValue JS::Runtime::Builtin::PromiseResolveFunction::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(AlreadyResolved->Value)
	{
		return JS_undefined;
	}

	auto resolution_value = arguments.size() > 0 ? arguments[0] : JS_undefined;
	if(JS::SameValue(resolution_value, Promise))
	{
		Promise->Reject(JS::New<TypeError> __LOC());
	}
	else if(auto resolution_object_option = GetNonNullObject(resolution_value))
	{
		auto resolution_object = resolution_object_option.value();
		JSValue then;
		try
		{
			then = JS::Get(resolution_object, u"then");
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			Promise->Reject(JSValue::WrapValue(thrown_value));
			return JS_undefined;
		}

		if(auto then_callable = GetFunction(then))
		{
			auto then_job_callback = JS::Host->MakeJobCallback(then_callable.value());
			JS::Runtime::Realm * realm;
			auto job = JS::Runtime::PromiseResolveThenableJob::New(Promise, resolution_object, then_job_callback, realm);
			JS::Host->EnqueuePromiseJob(job, realm);
		}
		else
		{
			Promise->Fulfill(resolution_value);
		}
	}
	else
	{
		Promise->Fulfill(resolution_value);
	}

	return JS_undefined;
}

//// builtin PromiseRejectFunction

JSValue JS::Runtime::Builtin::PromiseRejectFunction::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	auto reason = arguments.size() > 0 ? arguments[0] : JS_undefined;
	if(!AlreadyResolved->Value)
	{
		AlreadyResolved->Value = true;
		Promise->Reject(reason);
	}
	return JS_undefined;
}

//// builtin PromiseCapabilityExecutor

JSValue JS::Runtime::Builtin::PromiseCapabilityExecutor::Call([[maybe_unused]] JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase))
{
	if(!IsUndefined(Resolve) || !IsUndefined(Reject))
	{
		JS::Throw(JS::New<TypeError> __LOC("promise capability invoked on non-pending promise"));
	}
	Resolve = arguments.size() > 0 ? arguments[0] : JS_undefined;
	Reject  = arguments.size() > 1 ? arguments[1] : JS_undefined;
	return JS_undefined;
}

void JS::Runtime::Builtin::CreateResolvingFunctions(gcn::gc_ptr<JS::Class::Promise> promise, gcn::gc_ptr<PromiseResolveFunction>& resolve, gcn::gc_ptr<PromiseRejectFunction>& reject)
{
	auto already_resolved = std::make_shared<JS::Runtime::ResolutionRecord>();

	resolve = JS::New<JS::Runtime::Builtin::PromiseResolveFunction>(GetCurrentRealm(), promise, already_resolved);
	reject  = JS::New<JS::Runtime::Builtin::PromiseRejectFunction> (GetCurrentRealm(), promise, already_resolved);
}

//// JS

void JS::Await(JSValue value)
{
	auto asyncContext = GetCurrentExecutionContext();
	auto promise = JS::Class::Promise::Resolve(GetNonNullObject(JS::Intrinsics()->Promise).value(), value);

	JSValue onFulfilled = JS::CreateBuiltinFunction(
		[asyncContext](JS::Interpreter::State<> initial_state, [[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target) -> JS::Interpreter::State<JSValue>
	{
		auto value = arguments.size() > 0 ? arguments[0] : JS_undefined;
		return initial_state.Apply(std::function<void()>([asyncContext, value]()
		{
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
			JS::Runtime::ExecutionContext::GetContextStack().push_back(asyncContext);
			asyncContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			asyncContext->Action_Push<JSValue>(value);
		})).WaitForReturn();
	}, 1, u"", {}, GetCurrentRealm());

	JSValue onRejected = JS::CreateBuiltinFunction(
		[asyncContext](JS::Interpreter::State<> initial_state, [[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target) -> JS::Interpreter::State<JSValue>
	{
		auto reason = arguments.size() > 0 ? arguments[0] : JS_undefined;
		return initial_state.Apply(std::function<void()>([asyncContext, reason]()
		{
			GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
			JS::Runtime::ExecutionContext::GetContextStack().push_back(asyncContext);
			asyncContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
			asyncContext->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(reason));
		})).WaitForReturn();
	}, 1, u"", {}, GetCurrentRealm());

	promise->PerformPromiseThen(onFulfilled, onRejected);
	// current execution context must be suspended
}

