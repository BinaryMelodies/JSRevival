
#include "common.hh"
#include "macros.hh"
#include "core.hh"	
#include "iterator.hh"
#include "environment.hh"
#include "execute.hh"
#include "builtins.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//std::vector<JS::Runtime::ExecutionContext *> JS::Runtime::ExecutionContext::ContextStack;

//// ExecutionContext

bool JS::Runtime::ExecutionContext::DebugStack = false;

std::vector<JS::Runtime::ExecutionContext *>& JS::Runtime::ExecutionContext::GetContextStack()
{
	return GetSurroundingAgent()->ContextStack;
}

std::shared_ptr<JS::Runtime::EnvironmentRecord> JS::Runtime::ExecutionContext::GetThisEnvironment()
{
	return GetThisEnvironment(LexicalEnvironment);
}

std::shared_ptr<JS::Runtime::EnvironmentRecord> JS::Runtime::ExecutionContext::GetThisEnvironment(std::shared_ptr<JS::Runtime::EnvironmentRecord> environment)
{
#if ENABLE_ES4
	bool ReachedClass = false;
#endif
	for(auto env = environment; env != nullptr; env = env->OuterEnvironment)
	{
#if ENABLE_ES4
		if(IsVersionES4())
		{
			if(std::dynamic_pointer_cast<JS::Runtime::ClassEnvironmentRecord>(env))
			{
				if(JS::StrictMode)
				{
					JS::Throw(JS::New<SyntaxError> __LOC(u"this cannot be used outside a function in strict mode"));
				}
				ReachedClass = true;
			}
			else if(env->HasThisBinding())
			{
				if(!ReachedClass || std::dynamic_pointer_cast<JS::Runtime::GlobalEnvironmentRecord>(env))
				{
					return env;
				}
			}
		}
		else
#endif
		if(env->HasThisBinding())
		{
			return env;
		}
	}
	JS::Throw(JS::New<InternalError> __LOC(u"no this-environment found"));
}

#if ENABLE_ES4
std::shared_ptr<JS::Runtime::ClassEnvironmentRecord> JS::Runtime::ExecutionContext::GetClassEnvironment()
{
	return LexicalEnvironment->GetClassEnvironment();
}

gcn::gc_ptr<JS::Class::Package> JS::Runtime::ExecutionContext::GetPackage()
{
	for(auto env = LexicalEnvironment; env != nullptr; env = env->OuterEnvironment)
	{
		if(auto global_env = std::dynamic_pointer_cast<JS::Runtime::GlobalEnvironmentRecord>(env))
		{
			if(auto package = std::dynamic_pointer_cast<JS::Class::Package>(global_env->ObjectRecord.BindingObject))
			{
				return package;
			}
		}
	}
	JS::Throw(JS::New<InternalError> __LOC(u"no package environment found"));
}
#endif

void JS::Runtime::ExecutionContext::Action_Remove()
{
	if(stack.size() == 0)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"execution stack underflow"));
	}
	stack.pop_back();
}

void JS::Runtime::ExecutionContext::Action_JumpToHandler(CompletionRecord resulting_completion)
{
	if(resulting_completion.Type == CompletionRecord::CompletionType::Normal)
	{
		Action_Push<std::optional<JSValue>>(resulting_completion.Value);
		completion = CompletionRecord::Normal();
		return;
	}

	completion = resulting_completion;

	size_t stack_size = 0;
	size_t target_pointer = code->instruction_sequence.size();
	bool reached_target = false;
	while(!reached_target && ranges.size() > 0)
	{
		auto range = ranges.back();
		switch(range.type)
		{
		case JS::Interpreter::Range::RangeType::Block:
			RemoveRange();
			break;
		case JS::Interpreter::Range::RangeType::Function:
			if(completion.Type == CompletionRecord::CompletionType::Return)
			{
				target_pointer = range.end;
				stack_size = range.stack_size;
				reached_target = true;
			}
			else
			{
				RemoveRange();
			}
			break;
		case JS::Interpreter::Range::RangeType::Try:
			if(completion.Type == CompletionRecord::CompletionType::Throw && code_pointer <= range.handle) // throw from a catch clause takes us to the finally clause
			{
				target_pointer = range.handle;
			}
			else
			{
				target_pointer = range.end;
			}
			stack_size = range.stack_size;
			reached_target = true;
			break;
		case JS::Interpreter::Range::RangeType::Loop:
			if(completion.Type == CompletionRecord::CompletionType::Continue && (!completion.Target.has_value() || completion.Target == range.label))
			{
				target_pointer = range.handle;
				stack_size = range.stack_size;
				reached_target = true;
				break;
			}
			// fall through
		case JS::Interpreter::Range::RangeType::Switch:
			if(completion.Type == CompletionRecord::CompletionType::Break && (!completion.Target.has_value() || completion.Target == range.label))
			{
				target_pointer = range.end;
				stack_size = range.stack_size;
				reached_target = true;
				break;
			}
			else
			{
				RemoveRange();
			}
			break;
		}
	}
	code_pointer = target_pointer;

	while(stack.size() > stack_size)
	{
		stack.pop_back();
	}

	// must resume to processing to be able to handle completion
	state = ExecutionState::Executing;
}

// removes the top most range from the range stack, executing anything required for it
void JS::Runtime::ExecutionContext::RemoveRange()
{
	auto range = ranges.back();
	ranges.pop_back();

	if(range.Context)
	{
		if(JS::Runtime::ExecutionContext::GetContextStack().size() > 0)
		{
			JS::Runtime::ExecutionContext::GetContextStack().back()->LexicalEnvironment = range.LexicalEnvironment;
			JS::Runtime::ExecutionContext::GetContextStack().back()->VariableEnvironment = range.VariableEnvironment;
			JS::Runtime::ExecutionContext::GetContextStack().back()->PrivateEnvironment = range.PrivateEnvironment;
		}
		StrictMode = range.previous_strict_mode;
#if ENABLES_ES4
		CurrentVersion = range.previous_version;
#endif
	}
}

// once the range is exited, this function sets up the completion record appropriately
void JS::Runtime::ExecutionContext::FinalizeRange()
{
	auto range = ranges.back();
	RemoveRange();

	switch(range.type)
	{
	case JS::Interpreter::Range::RangeType::Block:
		break;
	case JS::Interpreter::Range::RangeType::Function:
		// replaces normal completion with a "return undefined;"
		// catches any return attempt and pushes the return value onto the stack for post procession
		// any throw is forwarded to the next handler, which is most likely the end of the code sequence
		// TODO: async functions

		if(range.is_generator)
		{
			if(range.is_async)
			{
				// TODO
				//AsyncGenerator->AsyncGeneratorState = JS::Class::AsyncGenerator::StateType::DrainingQueue;
			}
			else
			{
				Generator->GeneratorState = JS::Class::Generator::StateType::Completed;
			}
		}

		switch(completion.Type)
		{
		case JS::Runtime::CompletionRecord::CompletionType::Normal:
			if(range.is_async && range.is_generator)
			{
				// TODO
				//AsyncGenerator->CompleteStep(CompletionRecord::Normal(JS_undefined), true)
				//AsyncGenerator->DrainQueue()
				Action_Push<JSValue>(JS_undefined);
			}
			else if(range.is_async)
			{
				JS::Call(promiseCapability.value().Resolve, JS_undefined, {JS_undefined} RUNTIME_PHASE);
				Action_Push<JSValue>(JS_undefined);
			}
			else if(range.is_generator)
			{
				Action_Push<JSValue>(CreateIteratorResultObject(JS_undefined, true));
			}
			else if(range.is_arrow)
			{
				// TODO: make sure arrow bodies always push an optional<JSValue> (should be the case if CompileStatement was used)
				JSValue result = Action_Pop<std::optional<JSValue>>().value_or(JS_undefined);
				Action_Push<JSValue>(result);
			}
			else
			{
				Action_Push<JSValue>(JS_undefined);
			}
			break;
		case JS::Runtime::CompletionRecord::CompletionType::Return:
			if(range.is_async && range.is_generator)
			{
				// TODO
				//AsyncGenerator->CompleteStep(CompletionRecord::Normal(completion.Value.value()), true)
				//AsyncGenerator->DrainQueue()
				Action_Push<JSValue>(JS_undefined);
			}
			else if(range.is_async)
			{
				JS::Call(promiseCapability.value().Resolve, JS_undefined, {completion.Value.value()} RUNTIME_PHASE);
				Action_Push<JSValue>(JS_undefined);
			}
			else if(range.is_generator)
			{
				Action_Push<JSValue>(CreateIteratorResultObject(completion.Value.value(), true));
			}
			else
			{
				Action_Push<JSValue>(completion.Value.value());
			}
			completion = CompletionRecord::Normal();
			break;
		case JS::Runtime::CompletionRecord::CompletionType::Throw:
			if(range.is_async && range.is_generator)
			{
				// TODO
				//AsyncGenerator->CompleteStep(completion, true)
				//AsyncGenerator->DrainQueue()
				Action_Push<JSValue>(JS_undefined);
			}
			else if(range.is_async)
			{
				JS::Call(promiseCapability.value().Reject, JS_undefined, {completion.Value.value()} RUNTIME_PHASE);
			}
			else
			{
				GetContextStack().back()->Action_JumpToHandler(completion);
			}
			break;
		default:
			JS::Throw(JS::New<InternalError> __LOC(u"break/continue cannot leave function scope"));
		}
		break;
	case JS::Interpreter::Range::RangeType::Try:
		// code must have arrived here exactly
		// this is always the case, as finally branches always get executed
		if(completion.Type == CompletionRecord::CompletionType::Normal)
		{
			completion.Value = Action_Pop<std::optional<JSValue>>();
		}
		Action_Push<CompletionRecord>(completion);
		completion = CompletionRecord::Normal();
		break;
	case JS::Interpreter::Range::RangeType::Loop:
		if(completion.Type == CompletionRecord::CompletionType::Continue)
		{
			completion = CompletionRecord::Normal();
		}
		// fall through
	case JS::Interpreter::Range::RangeType::Switch:
		if(completion.Type == CompletionRecord::CompletionType::Break)
		{
			completion = CompletionRecord::Normal();
		}
		break;
	}
}

JS::Runtime::ExecutionContext * JS::Runtime::ExecutionContext::Execute()
{
	size_t initial_stack_size = GetContextStack().size();
	JS::Runtime::ExecutionContext * final_context = nullptr;
	if(initial_stack_size == 0)
	{
		return final_context;
	}

	GetContextStack().back()->completion = CompletionRecord::Normal();

	while(GetContextStack().size() >= initial_stack_size)
	{
		if(GetContextStack().back()->state != ExecutionState::Executing)
		{
			bool main_context = GetContextStack().size() == initial_stack_size;
			final_context = GetContextStack().back();
			GetContextStack().pop_back();
			if(final_context->state == ExecutionState::Completed)
			{
				// if this is not the main call, this terminates a function call, so a JSValue needs to be passed from the stack of the old context
				// to the stack of the current top context, otherwise leave the stack intact and let the caller decide how to handle a return
				// terminate execution and resume to previous thread
				switch(final_context->completion.Type)
				{
				case JS::Runtime::CompletionRecord::CompletionType::Normal:
					if(!main_context) // && final_context->code != nullptr)
					{
						JSValue result = final_context->Action_Pop<JSValue>();
						GetContextStack().back()->Action_Push<JSValue>(result);
					}
					break;
				case JS::Runtime::CompletionRecord::CompletionType::Throw:
#if ENABLE_ES4
					if(main_context || GetContextStack().back()->code == nullptr)
#else
					if(main_context)
					{
						// do nothing
					}
					else if(GetContextStack().back()->code == nullptr) // note: this only happens during setting up initializers, so the error should still be returned
#endif
					{
						if(auto object = final_context->completion.Value.value().get_if<JSObject>())
						{
							if(auto error = std::dynamic_pointer_cast<JS::Class::Error>(object.value()))
							{
								JS::Throw(error);
							}
						}
						JS::Throw(final_context->completion.Value.value());
					}
					else
					{
						GetContextStack().back()->Action_JumpToHandler(final_context->completion);
					}
					break;
				default:
					// return completion should have been processed by this point as well, likely by a FunctionRange
					JS::Throw(JS::New<InternalError> __LOC(u"cannot leave via return/break/continue"));
				}

				if(!main_context)
				{
					GetContextStack().back()->state = ExecutionState::Executing; // resume previous context
				}
			}
		}
		else
		{
			final_context = GetContextStack().back(); // as long as another context does not take over
			GetContextStack().back()->ExecuteContext();
		}
	}

	return final_context;
}

void JS::Runtime::ExecutionContext::ProcessHandlers()
{
	while(ranges.size() > 0 && code_pointer > ranges.back().end)
	{
		RemoveRange();
	}

	while(ranges.size() > 0)
	{
		if(ranges.back().type == JS::Interpreter::Range::RangeType::Try && code_pointer == ranges.back().handle && code_pointer < ranges.back().end)
		{
			/* Try handlers are composed of three parts: the try block, the catch block and the finally block.
			 * The catch block follows the try block immediately. A try block begins with an empty stack
			 * and exits with an optional value or an abrupt completion. A catch block begins with a value
			 * on the stack, the exception object, and exits with an optional value or an abrupt completion.
			 * The finally block begins with a completion record and terminates by reactivating the completion
			 * record or abrupt completion during its execution.
			 *
			 * When execution reaches the catch branch normally, the catch branch will be skipped over. The
			 * catch branch only gets executed when a throw transfer occurs. Other abrupt completions will send
			 * the execution to the finally branch.
			 *
			 * When execution reaches the finally branch, either normally or via abrupt completion, the completion
			 * record that led here is pushed onto the stack. Once this happens, the handler range is removed from
			 * the stack. The finally branch must terminate with an instruction that reactivates the completion for
			 * correct execution of the program.
			 */

			if(completion.Type == CompletionRecord::CompletionType::Throw)
			{
				Action_Push<JSValue>(completion.Value.value());
				completion = CompletionRecord::Normal();
			}
			else
			{
				// we arrived here via Normal completion, skip to the finally block
				code_pointer = ranges.back().end;
			}
		}

		if(code_pointer == ranges.back().end)
		{
			// we either arrive here through normal completion, or this was the result of jumping to the handler
			FinalizeRange();
		}
		else
		{
			// no more ranges to remove for now
			break;
		}
	}
}

void JS::Runtime::ExecutionContext::ExecuteContext()
{
	while(state == ExecutionState::Executing)
	{
		if(DebugStack) // && code != nullptr)
		{
			std::cerr << "cxt#" << GetContextStack().size() - 1 << "@" << code_pointer << "(/" << code->instruction_sequence.size() << ")" << std::endl;
			std::cerr << "stk>";
			for(auto ptr = stack.rbegin(); ptr != stack.rend(); ptr++)
			{
				std::cerr << " [";
				(*ptr)->print(std::cerr);
				std::cerr << "]";
			}
			std::cerr << std::endl;
		}

		if(/*code == nullptr ||*/ code_pointer >= code->instruction_sequence.size())
		{
			//bool main_context = GetContextStack().size() == 1;
			state = ExecutionState::Completed;
			return;
		}

		auto instruction = code->instruction_sequence[code_pointer++];
		location = instruction->Location;
		instruction->Execute(this);

		ProcessHandlers();
	}
}

//// Code

JS::Interpreter::State<> JS::Interpreter::Code::Begin()
{
	return State<>(this);
}

void JS::Interpreter::Code::Action_Yield(JSValue value)
{
	// we leave it on the top of the stack so it can be removed by any later cals (done in ExecutionContext::Execute() or Generator::Resume())
	JS::Runtime::ExecutionContext::GetContextStack().back()->Action_Push<JSValue>(CreateIteratorResultObject(value, false));
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended; // we need to suspend it in order for the execution loop to realize that its execution has been suspended
	JS::Runtime::ExecutionContext::GetContextStack().back()->Generator->GeneratorState = JS::Class::Generator::StateType::SuspendedYield;
	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
}

void JS::Interpreter::Code::Action_Await()
{
	// we leave it on the top of the stack so it can be removed by any later cals (done in ExecutionContext::Execute() or Generator::Resume())
	JS::Runtime::ExecutionContext::GetContextStack().back()->Action_Push<JSValue>(JS_undefined);
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended; // we need to suspend it in order for the execution loop to realize that its execution has been suspended
	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
}

//// BlockRange

//// LoopRange

//// TryRange

//// Instruction

JS::Interpreter::Instruction::Instruction()
	: Location(JS::Parser::SourceLocationContext::CurrentLocation)
{
}

//// Instruction_Apply

//// Instruction_Obtain

//// Instruction_Throw

void JS::Interpreter::Instruction_Throw::Execute(JS::Runtime::ExecutionContext * cxt)
{
	JSValue value = cxt->Action_Pop<JSValue>();
	cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(value));
}

//// Instruction_Return

//// Instruction_Break

void JS::Interpreter::Instruction_Break::Execute(JS::Runtime::ExecutionContext * cxt)
{
	cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Break(target));
}

//// Instruction_Continue

void JS::Interpreter::Instruction_Continue::Execute(JS::Runtime::ExecutionContext * cxt)
{
	cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Continue(target));
}

//// Instruction_SetupRange

void JS::Interpreter::Instruction_SetupRange::Execute(JS::Runtime::ExecutionContext * cxt)
{
	cxt->ranges.push_back(*this);
	cxt->ranges.back().stack_size = cxt->stack.size();

	auto Context = GetCurrentExecutionContext();
	cxt->ranges.back().Context = Context;
	cxt->ranges.back().previous_strict_mode = JS::StrictMode;
#if ENABLE_ES4
	cxt->ranges.back().previous_version = JS::CurrentVersion;
#endif
	cxt->ranges.back().LexicalEnvironment = Context->LexicalEnvironment;
	cxt->ranges.back().VariableEnvironment = Context->VariableEnvironment;
	cxt->ranges.back().PrivateEnvironment = Context->PrivateEnvironment;
}

#if ENABLE_ES4
//// Instruction_AlterRange

void JS::Interpreter::Instruction_AlterRange::Execute(JS::Runtime::ExecutionContext * cxt)
{
	if(cxt->ranges.size() == 0 || cxt->ranges.back().type != JS::Interpreter::Range::RangeType::Try)
	{
		JS::Throw(JS::New<InternalError> __LOC(u"not in a try-catch block"));
	}
	cxt->ranges.back().handle = new_handle;
}
#endif

//// Instruction_ActivateCompletion

void JS::Interpreter::Instruction_ActivateCompletion::Execute(JS::Runtime::ExecutionContext * cxt)
{
	auto completion = cxt->Action_Pop<JS::Runtime::CompletionRecord>();
	cxt->Action_JumpToHandler(completion);
}

//// Instruction_Jump

void JS::Interpreter::Instruction_Jump::Execute(JS::Runtime::ExecutionContext * cxt)
{
	cxt->code_pointer = target;
}

//// Instruction_JumpIf

void JS::Interpreter::Instruction_JumpIf::Execute(JS::Runtime::ExecutionContext * cxt)
{
	JSValue condition = cxt->Action_Pop<JSValue>();
	if(ToBoolean(condition)) // TODO: double check
	{
		cxt->code_pointer = target;
	}
}

//// Instruction_JumpIfNot

void JS::Interpreter::Instruction_JumpIfNot::Execute(JS::Runtime::ExecutionContext * cxt)
{
	JSValue condition = cxt->Action_Pop<JSValue>();
	if(!ToBoolean(condition)) // TODO: double check
	{
		cxt->code_pointer = target;
	}
}

