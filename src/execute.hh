#ifndef _EXECUTE_HH
#define _EXECUTE_HH

/* Functionality related to execution context management */

#include <cassert>
#include <functional>
#include <iostream>
#include <optional>
#include <type_traits>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "utils.hh"
#include "value.hh"
#include "runtime.hh"
#include "core.hh"
#include "promise.hh"

namespace JS
{
	using namespace Public;

	namespace Interpreter
	{
		class Instruction : public gc_cleanup
		{
		public:
			/** Keeps track of what callpath this instruction occurs in */
			JS::Parser::Location Location;

			Instruction();

			virtual void Execute(JS::Runtime::ExecutionContext * cxt) = 0;
		};

		// removes the top elements of the stack and (optionally) replaces them with a result value
		template <typename FunctionType>
			class Instruction_Apply : public Instruction
		{
		public:
			std::function<FunctionType> function;
			Instruction_Apply(std::function<FunctionType> function) : function(function) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		// checks the top elements of the stack and (optionally) pushes a result value
		template <typename FunctionType>
			class Instruction_Obtain : public Instruction
		{
		public:
			std::function<FunctionType> function;
			Instruction_Obtain(std::function<FunctionType> function) : function(function) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		// takes value on top of stack and uses it as throw target
		class Instruction_Throw : public Instruction
		{
		public:
			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		// (optionally) takes value on top of stack and returns
		template <bool HasReturnValue>
			class Instruction_Return : public Instruction
		{
		public:
			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		class Instruction_Break : public Instruction
		{
		public:
			std::optional<JSString> target;
			Instruction_Break() : target() { }
			Instruction_Break(std::optional<JSString> target) : target(target) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		class Instruction_Continue : public Instruction
		{
		public:
			std::optional<JSString> target;
			Instruction_Continue() : target() { }
			Instruction_Continue(std::optional<JSString> target) : target(target) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		// takes a completion record on the top of stack and activates it
		class Instruction_ActivateCompletion : public Instruction
		{
		public:
			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		// initializes a block/loop/try range
		class Instruction_SetupRange : public Instruction, public Range
		{
		public:
			Instruction_SetupRange(RangeType type, std::optional<JSString> label = {})
				: Range(type, label)
			{
			}

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

#if ENABLE_ES4
		// changes the handle location, used for ES4 multi-catch statements
		class Instruction_AlterRange : public Instruction
		{
		public:
			size_t new_handle;
			Instruction_AlterRange(size_t new_handle) : new_handle(new_handle) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};
#endif

		class Instruction_Jump : public Instruction
		{
		public:
			size_t target;
			Instruction_Jump(size_t target) : target(target) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		class Instruction_JumpIf : public Instruction_Jump
		{
		public:
			Instruction_JumpIf(size_t target) : Instruction_Jump(target) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		class Instruction_JumpIfNot : public Instruction_Jump
		{
		public:
			Instruction_JumpIfNot(size_t target) : Instruction_Jump(target) { }

			void Execute(JS::Runtime::ExecutionContext * cxt) override;
		};

		/* A strongly typed thin wrapper around an instruction sequence that represents the types of the value stack, leftmost type being the top of the stack */
		template <typename ... Types>
			class State;

		namespace StateHelper
		{
			template <typename FunctionType>
				struct get_return_type_for;

			template <typename ResultType, typename ... Types>
				struct get_return_type_for<ResultType(Types...)>
			{
				typedef ResultType type;
			};

			template <typename T, typename StateType>
				struct append_non_void_to_state;

			template <typename ... Types>
				struct append_non_void_to_state<void, State<Types...>>
			{
				typedef State<Types...> type;
			};

			template <typename T, typename ... Types>
				struct append_non_void_to_state<T, State<Types...>>
			{
				typedef State<T, Types...> type;
			};

			// the first type parameter is a function type only so two parameter packs can be processed at the same time, the return type should be void
			template <typename FunctionType, typename ... Types>
				struct calculate_state_for_begin;

			template <typename ... Types>
				struct calculate_state_for_begin<void(), Types...>
			{
				typedef State<Types...> type;
			};

			template <typename FirstType, typename ... RemainingTypes, typename ... Types>
				struct calculate_state_for_begin<void(FirstType, RemainingTypes...), FirstType, Types...>
			{
				typedef typename calculate_state_for_begin<void(RemainingTypes...), Types...>::type type;
			};

			template <typename FunctionType, typename ... Types>
				struct calculate_state_for_apply;

			template <typename ... Types>
				struct calculate_state_for_apply<void(), Types...>
			{
				typedef State<Types...> type;
			};

			template <typename ResultType, typename ... Types>
				struct calculate_state_for_apply<ResultType(), Types...>
			{
				typedef State<ResultType, Types...> type;
			};

			template <typename ResultType, typename FirstType, typename ... RemainingTypes, typename ... Types>
				struct calculate_state_for_apply<ResultType(FirstType, RemainingTypes...), FirstType, Types...>
			{
				typedef typename calculate_state_for_apply<ResultType(RemainingTypes...), Types...>::type type;
			};

			template <typename StateType, typename FunctionType, typename ... Types>
				struct calculate_state_for_obtain_;

			template <typename StateType, typename ... Types>
				struct calculate_state_for_obtain_<StateType, void(), Types...>
			{
				typedef StateType type;
			};

			template <typename StateType, typename ResultType, typename ... Types>
				struct calculate_state_for_obtain_<StateType, ResultType(), Types...>
			{
				typedef StateType type;
			};

			template <typename StateType, typename ResultType, typename FirstType, typename ... RemainingTypes, typename ... Types>
				struct calculate_state_for_obtain_<StateType, ResultType(FirstType, RemainingTypes...), FirstType, Types...>
			{
				typedef typename calculate_state_for_obtain_<StateType, ResultType(RemainingTypes...), Types...>::type type;
			};

			template <typename FunctionType, typename ... Types>
				struct calculate_state_for_obtain
			{
				typedef typename calculate_state_for_obtain_<typename append_non_void_to_state<typename get_return_type_for<FunctionType>::type, State<Types...>>::type, FunctionType, Types...>::type type;
			};

			template <size_t N, typename FunctionType, typename StateType, typename ... Types>
				struct calculate_state_for_replicate_via
			{
				typedef calculate_state_for_replicate_via<N, FunctionType, StateType, Types...> function_struct;
				typedef void type;
				typedef void top_type;
				typedef void function_type;
			};

			template <typename ... ArgumentTypes, typename ... StateTypes, typename NextType, typename ... Types>
				struct calculate_state_for_replicate_via<0, void(ArgumentTypes...), State<StateTypes...>, NextType, Types...>
			{
				typedef calculate_state_for_replicate_via<0, void(ArgumentTypes...), State<StateTypes...>, NextType, Types...> function_struct;
				typedef State<NextType, StateTypes...> type;
				typedef NextType top_type;
				typedef NextType function_type(ArgumentTypes ... _ignored_arguments, NextType argument);

				static inline NextType action(ArgumentTypes ... _ignored_arguments, NextType argument)
				{
					return argument;
				}
			};

			template <size_t N, typename ... ArgumentTypes, typename StateType, typename NextType, typename ... Types>
				struct calculate_state_for_replicate_via<N, void(ArgumentTypes...), StateType, NextType, Types...>
			{
				typedef typename calculate_state_for_replicate_via<N - 1, void(ArgumentTypes..., NextType), StateType, Types...>::function_struct function_struct;
				typedef typename function_struct::type type;
				typedef typename function_struct::top_type top_type;
				typedef typename function_struct::function_type function_type;
			};

			template <size_t N, typename ... Types>
				struct calculate_state_for_replicate
			{
				typedef typename calculate_state_for_replicate_via<N, void(), State<Types...>, Types...>::function_struct function_struct;
				typedef typename function_struct::type type;
				typedef typename function_struct::top_type top_type;
				typedef typename function_struct::function_type function_type;
			};

			template <typename ... Types>
				struct calculate_state_for_pop
			{
				typedef void type;
				typedef void top_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_pop<T, Types...>
			{
				typedef State<Types...> type;
				typedef T top_type;
			};

			template <typename ... Types>
				struct calculate_state_for_option
			{
				typedef void type;
				typedef void top_type;
				typedef void option_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_option<T, Types...>
			{
				typedef State<std::optional<T>, Types...> type;
				typedef T top_type;
				typedef std::optional<T> option_type;
			};

			template <typename ... Types>
				struct calculate_state_for_mandatory
			{
				typedef void type;
				typedef void top_type;
				typedef void mandatory_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_mandatory<std::optional<T>, Types...>
			{
				typedef State<T, Types...> type;
				typedef std::optional<T> top_type;
				typedef T mandatory_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_value_or
			{
				typedef void type;
				typedef void top_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_value_or<T, std::optional<T>, Types...>
			{
				typedef State<T, Types...> type;
				typedef std::optional<T> top_type;
			};

			template <typename F, typename ActualState, typename ExpectedState>
				struct calculate_state_for_then_via
			{
				typedef void actual;
			};

			template <typename ... ResultTypes, typename ... RemainingTypes, typename ... ExpectedTypes>
				struct calculate_state_for_then_via<State<ResultTypes...>(State<>), State<RemainingTypes...>, State<ExpectedTypes...>>
			{
				typedef calculate_state_for_then_via actual;
				typedef State<ResultTypes..., RemainingTypes...> type;
				typedef State<ExpectedTypes...> initial_state_type;
			};

			template <typename ResultType, typename NextType, typename ... ExpectedTypes, typename ... ActualTypes, typename ... CollectedTypes>
				struct calculate_state_for_then_via<ResultType(State<NextType, ExpectedTypes...>), State<NextType, ActualTypes...>, State<CollectedTypes...>>
			{
				typedef typename calculate_state_for_then_via<ResultType(State<ExpectedTypes...>), State<ActualTypes...>, State<CollectedTypes..., NextType>>::actual actual;
			};

			template <typename F, typename ... Types>
				struct calculate_state_for_then
			{
				typedef typename calculate_state_for_then_via<F, State<Types...>, State<>>::actual actual;
				typedef typename actual::type type;
				typedef typename actual::initial_state_type initial_state_type;
			};

			template <typename ReturnType, typename ... ArgumentTypes, typename ... Types>
				struct calculate_state_for_then<ReturnType(State<ArgumentTypes...>&), Types...>
			{
				typedef typename calculate_state_for_then<ReturnType(State<ArgumentTypes...>), Types...>::actual actual;
				typedef typename actual::type type;
				typedef typename actual::initial_state_type initial_state_type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_throw;

			template <typename T, typename ... Types>
				struct calculate_state_for_throw<T, JSValue, Types...>
			{
				typedef State<T, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_throw<void, JSValue, Types...>
			{
				typedef State<Types...> type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_break_and_continue
			{
				typedef State<T, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_break_and_continue<void, Types...>
			{
				typedef State<Types...> type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_return;

			template <typename T, typename ... Types>
				struct calculate_state_for_return<T, JSValue, Types...>
			{
				typedef State<std::optional<JSValue>, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_return<void, Types...>
			{
				typedef State<std::optional<JSValue>, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_yield
			{
				typedef void type;
			};

			template <typename ... Types>
				struct calculate_state_for_yield<JSValue, Types...>
			{
				typedef State<JSValue, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_await
			{
				typedef void type;
			};

			template <typename ... Types>
				struct calculate_state_for_await<JSValue, Types...>
			{
				typedef State<JSValue, Types...> type;
			};

			template <typename T, typename ... Types>
				struct calculate_state_for_if_range
			{
				typedef State<T, Types...> type;
			};

			template <typename ... Types>
				struct calculate_state_for_if_range<void, Types...>
			{
				typedef State<Types...> type;
			};
		}

		// information needed to generate a simple block
		class BlockRange
		{
		protected:
			bool completed = false;
			Instruction_SetupRange * range;
		public:
			BlockRange(Instruction_SetupRange * range) : range(range) { }
			~BlockRange()
			{
				if(!completed)
				{
					std::cerr << "Internal error: uncompleted block range" << std::endl;
				}
			}

			template <typename ... ResultTypes>
				State<ResultTypes...> EndBlockRange(State<ResultTypes...> state);
		};

		// information needed to generate a try/catch/finally block
		template <typename ... Types>
			class TryRange
		{
		protected:
			bool completed = false;
			Instruction_SetupRange * range;
#if ENABLE_ES4
			Instruction_AlterRange * alter = nullptr;
#endif
		public:
			TryRange(Instruction_SetupRange * range) : range(range) { }
			~TryRange();

			State<JSValue, Types...> BeginCatchRange(State<std::optional<JSValue>, Types...> state
#if ENABLE_ES4
				, bool final = true
#endif
			);
			State<JS::Runtime::CompletionRecord, Types...> BeginFinallyRange(State<std::optional<JSValue>, Types...> state);
			// after a catch clause, no finally clause
			State<std::optional<JSValue>, Types...> EndTryRange(State<std::optional<JSValue>, Types...> state);
			// after a finally clause
			State<std::optional<JSValue>, Types...> EndTryRange(State<JS::Runtime::CompletionRecord, Types...> state);
		};

		template <typename ... Types>
			class LoopRange
		{
		protected:
			bool completed = false;
			Instruction_SetupRange * range;
			size_t loop_start;
			Instruction_JumpIfNot * jump_if = nullptr;
		public:
			LoopRange(Instruction_SetupRange * range, size_t loop_start) : range(range), loop_start(loop_start) { }
			~LoopRange();

			// label to where continue jumps, start of the loop by default
			State<Types...> BeginContinueRange(State<Types...> state);
			// if value on top of stack is false, jump to end of range, there can be at most one of these
			State<Types...> AddLoopCheck(State<JSValue, Types...> state);
			State<Types...> EndLoopRange(State<Types...> state);
		};

		/* Represents an if range, used by if and switch statements, and other constructs implicitly
		 *
		 * Before the start of an if range, the execution stack is expected to contain a JSValue that is the condition
		 * which gets converted to a boolean via a ToBoolean call. This value is removed from the stack, and if it was true,
		 * execution continues on the current code path until BeginElseRange is called.
		 *
		 * Both the true and false branches can terminate with a value on the stack, whose type is represented by the
		 * template parameter T. Typical values would be JSValue for expressions, std::optional<JSValue> for statements
		 * and void for internal functions. A void value is not pushed onto the stack.
		 *
		 * When the else branch is started with a BeginElseRange call, the result is conceptually removed from the stack,
		 * and execution on true condition stops until EndIfRange is called. The execution path then continues for the
		 * false branch.
		 *
		 * Additional calls to BeginIfRange may be made, alternating with BeginElseRange calls, and behave similarly to
		 * the initial BeginIfRange call.
		 *
		 * The range has to be terminated by a call to EndIfRange. The stack is expected to have the same value as it had
		 * at the end of the true condition path, and it will be in the same state as before the call to EndIfRange.
		 * All execution paths will converge here, and the final value on the stack will reflect the result of whichever
		 * branch executed.
		 */
		template <typename T, typename ... Types>
			class IfRange
		{
		protected:
			class IfPart
			{
			protected:
				bool completed = false;
				Instruction_JumpIfNot * jump_if;
				Instruction_Jump * jump = nullptr;
			public:
				IfPart(Instruction_JumpIfNot * jump_if) : jump_if(jump_if) { }
				IfPart(IfPart&& other)
					: completed(other.completed), jump_if(other.jump_if), jump(other.jump)
				{
					other.completed = true; // avoid generating errors for missing closed blocks
				}
				~IfPart();

				State<Types...> BeginElseRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state);
				typename StateHelper::calculate_state_for_if_range<T, Types...>::type EndIfRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state);
			};
			bool completed = false;
			std::vector<IfPart> parts;
		public:
			IfRange(Instruction_JumpIfNot * jump_if)
			{
				parts.emplace_back(IfPart(jump_if));
			}
			~IfRange();

			State<Types...> BeginIfRange(State<JSValue, Types...> state);
			State<Types...> BeginElseRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state);
			typename StateHelper::calculate_state_for_if_range<T, Types...>::type EndIfRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state);
		};

		// information needed to generate a simple block
		template <typename ... Types>
			class FunctionRange
		{
		protected:
			bool completed = false;
			Instruction_SetupRange * range;
		public:
			bool is_arrow;
			bool is_generator;
			bool is_async;
			FunctionRange(Instruction_SetupRange * range, bool is_arrow, bool is_generator, bool is_async)
				: range(range), is_arrow(is_arrow), is_generator(is_generator), is_async(is_async)
			{
			}
			~FunctionRange();

			State<JSValue, Types...> EndFunctionRange(State<std::optional<JSValue>, Types...> state);
		};

		class Code
		{
		public:
			std::vector<Instruction *> instruction_sequence;

			// creates an initial state
			State<> Begin();

			// attaches the results of another state to the current one
			// note that the prior_state is assumed to be the state directly before next_state was generated, and next_state must be the result of the last changes applied
			template <typename ... NewTypes, typename ... Types>
				static State<NewTypes..., Types...> Then(State<Types...>& prior_state, State<NewTypes...> next_state);

			// initiates a block
			template <typename ... Types>
				static BlockRange BeginBlockRange(State<Types...>& state);

			// initiates a loop block
			template <typename ... Types>
				static LoopRange<Types...> BeginLoopRange(State<Types...>& state, std::optional<JSString> label = std::optional<JSString>{});

			// initiates a series of if-blocks, the parameter T describes the result left on the stack after each branch
			template <typename T, typename ... Types>
				static IfRange<T, Types...> BeginIfRange(State<JSValue, Types...>& state, State<Types...>& result_state);

			// initiates a try-catch-finally block
			template <typename ... Types>
				static TryRange<Types...> BeginTryRange(State<Types...>& state);

			// initiates a block
			template <typename ... Types>
				static FunctionRange<Types...> BeginFunctionRange(State<Types...>& state, bool is_arrow, bool is_generator, bool is_async);

			// transfer execution to the calling function
			static void Action_Yield(JSValue value);
			// transfer execution to the calling function, the second half of the await instruction
			static void Action_Await();
		};

		template <typename ... Types>
			class State
		{
		public:
			size_t initial_instruction_count;
			Code * self;
			size_t current_instruction_count;

			State() : initial_instruction_count(0), self(nullptr), current_instruction_count(0) { } // uninitialized, must be initialized using Begin

			State(const State& state) = default;

			template <typename ... OldTypes>
				explicit State(const State<OldTypes...>& previous_state)
				: initial_instruction_count(previous_state.initial_instruction_count), self(previous_state.self), current_instruction_count(previous_state.self->instruction_sequence.size()) { }

			template <typename ... OldTypes>
				explicit State(Code * self)
				: initial_instruction_count(self->instruction_sequence.size()), self(self), current_instruction_count(self->instruction_sequence.size()) { }

			explicit State(size_t initial_instruction_count, Code * self)
				: initial_instruction_count(initial_instruction_count), self(self), current_instruction_count(self->instruction_sequence.size()) { }

			// separates the current type stack into a new state and a remaining state
			// the argument is a newly initialized state capturing the current state
			// the returned state is one with the parameters conceptually removed (not in action though) to be processed through the new state
			// a Code::Then call should be issued to recombine the two states
			template <typename ... NewTypes>
				typename StateHelper::calculate_state_for_begin<void(NewTypes...), Types...>::type Begin(State<NewTypes...>& new_state)
			{
				new_state.initial_instruction_count = self->instruction_sequence.size();
				new_state.self = self;
				new_state.current_instruction_count = self->instruction_sequence.size();
				return typename StateHelper::calculate_state_for_begin<void(NewTypes...), Types...>::type(initial_instruction_count, self);
			}

			// removes the top elements of the stack and (optionally) replaces them with a result value
			template <typename FunctionType>
				typename StateHelper::calculate_state_for_apply<FunctionType, Types...>::type Apply(std::function<FunctionType> function)
			{
				self->instruction_sequence.push_back(new Instruction_Apply<FunctionType>(function));
				return typename StateHelper::calculate_state_for_apply<FunctionType, Types...>::type(initial_instruction_count, self);
			}

			// checks the top elements of the stack and (optionally) pushes a result value
			template <typename FunctionType>
				typename StateHelper::calculate_state_for_obtain<FunctionType, Types...>::type Obtain(std::function<FunctionType> function)
			{
				self->instruction_sequence.push_back(new Instruction_Obtain<FunctionType>(function));
				return typename StateHelper::calculate_state_for_obtain<FunctionType, Types...>::type(initial_instruction_count, self);
			}

			template <typename ... NewTypes>
				State<NewTypes..., Types...> Append(Instruction * instruction)
			{
				self->instruction_sequence.push_back(instruction);
				return State<NewTypes..., Types...>(initial_instruction_count, self);
			}

			template <typename ... NewTypes>
				State<NewTypes..., Types...> Append(Code& code)
			{
				self->instruction_sequence.insert(
					self->instruction_sequence.end(),
					code.instruction_sequence.begin(),
					code.instruction_sequence.end());
				return State<NewTypes..., Types...>(initial_instruction_count, self);
			}

			// pushes a new value on top of the stack
			template <typename T>
				State<T, Types...> Push(T&& value)
			{
				return Apply(std::function<T()>([value]()
				{
					return value;
				}));
			}

			// pushes a new value on top of the stack
			template <typename T>
				State<T, Types...> Push(const T& value)
			{
				return Apply(std::function<T()>([value]()
				{
					return value;
				}));
			}

			// removes element from top of the stack
			typename StateHelper::calculate_state_for_pop<Types...>::type Pop()
			{
				return Apply(std::function<void(typename StateHelper::calculate_state_for_pop<Types...>::top_type)>([]([[maybe_unused]] auto value)
				{
				}));
			}

			template <size_t N>
				typename StateHelper::calculate_state_for_replicate<N, Types...>::type Replicate()
			{
				return Obtain(std::function<typename StateHelper::calculate_state_for_replicate<N, Types...>::function_type>(
					StateHelper::calculate_state_for_replicate<N, Types...>::function_struct::action));
			}

			typename StateHelper::calculate_state_for_replicate<0, Types...>::type Dup()
			{
				return Replicate<0>();
			}

// TODO: create Replicate (generalized Dup), Exchange and Remove (generalized Pop) operations

			// converts element on top of stack into an optional type
			typename StateHelper::calculate_state_for_option<Types...>::type Option()
			{
				return Apply(std::function<typename StateHelper::calculate_state_for_option<Types...>::option_type(typename StateHelper::calculate_state_for_option<Types...>::top_type)>([](auto value)
				{
					return std::make_optional(value);
				}));
			}

			// unpacks an optional element on the top of the stack, this function assumes it is non-empty
			typename StateHelper::calculate_state_for_mandatory<Types...>::type Mandatory()
			{
				return Apply(std::function<typename StateHelper::calculate_state_for_mandatory<Types...>::mandatory_type(typename StateHelper::calculate_state_for_mandatory<Types...>::top_type)>([](auto value)
				{
					return value.value();
				}));
			}

			// unpacks an optional element on the top of the stack
			template <typename T>
				typename StateHelper::calculate_state_for_value_or<T, Types...>::type ValueOr(T default_value)
			{
				return Apply(std::function<T(typename StateHelper::calculate_state_for_value_or<T, Types...>::top_type)>([default_value](auto value)
				{
					return value.value_or(default_value);
				}));
			}

			// templates to simplify calling JS::Interpreter::Code::Then, remove superfluous types and reattach them after invocation
			template <typename F>
				typename StateHelper::calculate_state_for_then<F, Types...>::type Then(F * function)
			{
				typename StateHelper::calculate_state_for_then<F, Types...>::initial_state_type called_state;
				auto initial_state = Begin(called_state);
				return Code::Then(initial_state, function(called_state));
			}

			template <typename F>
				typename StateHelper::calculate_state_for_then<F, Types...>::type Then(std::function<F> function)
			{
				typename StateHelper::calculate_state_for_then<F, Types...>::initial_state_type called_state;
				auto initial_state = Begin(called_state);
				return Code::Then(initial_state, function(called_state));
			}

			template <typename C, typename R, typename T, typename ... Ts>
				typename StateHelper::calculate_state_for_then<R(T), Types...>::type Then(C * reference, R (C::* function)(T, Ts...), Ts ... args)
			{
				typename StateHelper::calculate_state_for_then<R(T), Types...>::initial_state_type called_state;
				auto initial_state = Begin(called_state);
				return Code::Then(initial_state, (reference->*function)(called_state, args...));
			}

			// takes the top element on the stack and throws it
			// it also takes an (optional) type parameter and pretends that that is the result of this instruction, needed for type checking only
			template <typename T>
				typename StateHelper::calculate_state_for_throw<T, Types...>::type Throw()
			{
				self->instruction_sequence.push_back(new Instruction_Throw());
				return typename StateHelper::calculate_state_for_throw<T, Types...>::type(initial_instruction_count, self);
			}

			// (optionally) takes the top element on the stack and returns
			template <typename T>
				typename StateHelper::calculate_state_for_return<T, Types...>::type Return()
			{
				self->instruction_sequence.push_back(new Instruction_Return<!std::is_void_v<T>>());
				return typename StateHelper::calculate_state_for_return<T, Types...>::type(initial_instruction_count, self);
			}

			template <typename T>
				typename StateHelper::calculate_state_for_break_and_continue<T, Types...>::type Break(std::optional<JSString> target)
			{
				self->instruction_sequence.push_back(new Instruction_Break(target));
				return typename StateHelper::calculate_state_for_break_and_continue<T, Types...>::type(initial_instruction_count, self);
			}

			template <typename T>
				typename StateHelper::calculate_state_for_break_and_continue<T, Types...>::type Continue(std::optional<JSString> target)
			{
				self->instruction_sequence.push_back(new Instruction_Continue(target));
				return typename StateHelper::calculate_state_for_break_and_continue<T, Types...>::type(initial_instruction_count, self);
			}

			// after an instruction that suspeds execution, this instruction would be used after returning
			// it assumes that a separate context has pushed a completion record onto the execution stack
			State<JSValue, Types...> WaitForReturn()
			{
				return State<JSValue, Types...>(initial_instruction_count, self);
			}

			// after an instruction that suspeds execution, this instruction would be used after resuming
			// it assumes that a separate context has pushed a completion record onto the execution stack
			State<JSValue, Types...> WaitForResume()
			{
				self->instruction_sequence.push_back(new Instruction_ActivateCompletion());
				return State<std::optional<JSValue>, Types...>(initial_instruction_count, self).Mandatory();
			}

			typename StateHelper::calculate_state_for_yield<Types...>::type Yield()
			{
				return Apply(std::function<void(JSValue)>(Code::Action_Yield)).WaitForResume();
			}

			typename StateHelper::calculate_state_for_await<Types...>::type Await()
			{
				return
					Apply(std::function<void(JSValue)>(JS::Await))
					.Apply(std::function<void()>(Code::Action_Await)).WaitForReturn();
			}
		};
	}

	namespace Runtime
	{
		class ExecutionContext : public gc_cleanup
		{
		public:
			static bool DebugStack;

			JSFunction Function;
			Runtime::Realm * Realm;
			JS::Runtime::ScriptOrModuleRecord * ScriptOrModule;

			std::shared_ptr<JS::Runtime::EnvironmentRecord>        LexicalEnvironment;
			std::shared_ptr<JS::Runtime::EnvironmentRecord>        VariableEnvironment;
			std::shared_ptr<JS::Runtime::PrivateEnvironmentRecord> PrivateEnvironment;

			gcn::gc_ptr<JS::Class::Generator> Generator;
			// captured by ECMAScript "AsyncBlockStart"
			std::optional<JS::Runtime::PromiseCapability<>> promiseCapability;

			// state
			std::vector<JS::Interpreter::Wrapper *> stack;
			JS::Interpreter::Code * code = nullptr;
			size_t code_pointer = 0;
			std::optional<JS::Parser::Location> location;

			enum class ExecutionState
			{
				Suspended,
				Executing,
				Completed,
			};
			ExecutionState state = ExecutionState::Suspended;

			// when set to normal, the value is stored on the stack
			CompletionRecord completion;
			std::vector<JS::Interpreter::Range> ranges;

			ExecutionContext() = default;
			ExecutionContext(const ExecutionContext& other)
				: Function(other.Function),
				Realm(other.Realm),
				ScriptOrModule(other.ScriptOrModule),
				LexicalEnvironment(other.LexicalEnvironment),
				VariableEnvironment(other.VariableEnvironment),
				PrivateEnvironment(other.PrivateEnvironment)
			{
			}

			/*void Action_Push(JSValue value);
			JSValue Action_Pop();
			void Action_Poke(size_t offset, JSValue value);
			JSValue Action_Peek(size_t offset);*/

			template <typename T>
				void Action_Push(T value);

			template <typename T>
				T Action_Pop();

			template <typename T>
				T Action_Peek(size_t offset = 0);

			template <typename T>
				void Action_Poke(T value, size_t offset = 0);

			void Action_Remove();
			void Action_JumpToHandler(CompletionRecord resulting_completion);

			// removes the top most range from the range stack, executing anything required for it
			void RemoveRange();

			// once the range is exited, this function sets up the completion record appropriately
			void FinalizeRange();

			static ExecutionContext * Execute();
			template <typename T>
				static T ExecuteAndReturn();

			template <typename T>
				static T ExecuteAndReturn(JS::Interpreter::Code * code);

			// must be called after Action_JumpToHandler, ExecuteContext() will call this automatically while running
			void ProcessHandlers();
			void ExecuteContext();

//			static std::vector<ExecutionContext *> ContextStack;
			static std::vector<ExecutionContext *>& GetContextStack();

			std::shared_ptr<JS::Runtime::EnvironmentRecord> GetThisEnvironment();
			static std::shared_ptr<JS::Runtime::EnvironmentRecord> GetThisEnvironment(std::shared_ptr<JS::Runtime::EnvironmentRecord> environment);
#if ENABLE_ES4
			std::shared_ptr<JS::Runtime::ClassEnvironmentRecord> GetClassEnvironment(); // only for ES4
			gcn::gc_ptr<JS::Class::Package> GetPackage(); // only for ES4
#endif
		};
	}
}


//// Instruction_Apply

template <typename RemainingFunction>
	struct pop_stack_and_apply;

template <>
	struct pop_stack_and_apply<void()>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		try
		{
			function(arguments...);
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(JS::Type::Value::WrapValue(thrown_value)));
		}
	}
};

template <typename ResultType>
	struct pop_stack_and_apply<ResultType()>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		ResultType result;
		try
		{
			result = function(arguments...);
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(JS::Type::Value::WrapValue(thrown_value)));
			return;
		}
		cxt->Action_Push<ResultType>(result);
	}
};

template <typename ResultType, typename FirstArgument, typename ... RemainingArguments>
	struct pop_stack_and_apply<ResultType(FirstArgument, RemainingArguments...)>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		auto value = cxt->Action_Pop<FirstArgument>();
		pop_stack_and_apply<ResultType(RemainingArguments...)>::execute(cxt, function, arguments..., value);
	}
};

template <typename FunctionType>
	void JS::Interpreter::Instruction_Apply<FunctionType>::Execute(JS::Runtime::ExecutionContext * cxt)
{
	pop_stack_and_apply<FunctionType>::execute(cxt, function);
}

//// Instruction_Obtain

template <typename RemainingFunction, size_t NextArgumentOffset = 0>
	struct peek_stack_and_obtain;

template <size_t NextArgumentOffset>
	struct peek_stack_and_obtain<void(), NextArgumentOffset>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		try
		{
			function(arguments...);
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(JS::Type::Value::WrapValue(thrown_value)));
		}
	}
};

template <typename ResultType, size_t NextArgumentOffset>
	struct peek_stack_and_obtain<ResultType(), NextArgumentOffset>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		ResultType result;
		try
		{
			result = function(arguments...);
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Throw(JS::Type::Value::WrapValue(thrown_value)));
			return;
		}
		cxt->Action_Push<ResultType>(result);
	}
};

template <typename ResultType, typename FirstArgument, typename ... RemainingArguments, size_t NextArgumentOffset>
	struct peek_stack_and_obtain<ResultType(FirstArgument, RemainingArguments...), NextArgumentOffset>
{
	template <typename FunctionType, typename ... ArgumentTypes>
		static inline void execute(JS::Runtime::ExecutionContext * cxt, std::function<FunctionType> function, ArgumentTypes ... arguments)
	{
		auto value = cxt->Action_Peek<FirstArgument>(NextArgumentOffset);
		peek_stack_and_obtain<ResultType(RemainingArguments...), NextArgumentOffset + 1>::execute(cxt, function, arguments..., value);
	}
};

template <typename FunctionType>
	void JS::Interpreter::Instruction_Obtain<FunctionType>::Execute(JS::Runtime::ExecutionContext * cxt)
{
	peek_stack_and_obtain<FunctionType>::execute(cxt, function);
}

//// Instruction_Return

template <bool HasReturnValue>
	void JS::Interpreter::Instruction_Return<HasReturnValue>::Execute(JS::Runtime::ExecutionContext * cxt)
{
	if constexpr(HasReturnValue)
	{
		JSValue value = cxt->Action_Pop<JSValue>();
		cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Return(value));
	}
	else
	{
		cxt->Action_JumpToHandler(JS::Runtime::CompletionRecord::Return());
	}
}

//// BlockRange

template <typename ... ResultTypes>
	JS::Interpreter::State<ResultTypes...> JS::Interpreter::BlockRange::EndBlockRange(State<ResultTypes...> state)
{
	assert(range->end == 0);
	range->end = state.self->instruction_sequence.size();
	completed = true;
	return JS::Interpreter::State<ResultTypes...>(state.initial_instruction_count, state.self);
}

//// LoopRange

template <typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::LoopRange<Types...>::BeginContinueRange(State<Types...> state)
{
	assert(range->handle == loop_start); // there can be only a single continue target
	range->handle = state.self->instruction_sequence.size();
	return JS::Interpreter::State<Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::LoopRange<Types...>::AddLoopCheck(State<JSValue, Types...> state)
{
	assert(jump_if == nullptr);
	jump_if = new Instruction_JumpIfNot(0);
	state.self->instruction_sequence.push_back(jump_if);
	return JS::Interpreter::State<Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::LoopRange<Types...>::EndLoopRange(State<Types...> state)
{
	state.self->instruction_sequence.push_back(new Instruction_Jump(loop_start));
	if(jump_if != nullptr)
	{
		jump_if->target = state.self->instruction_sequence.size();
	}
	range->end = state.self->instruction_sequence.size();
	completed = true;
	return JS::Interpreter::State<Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::LoopRange<Types...>::~LoopRange()
{
	if(!completed)
	{
		std::cerr << "Internal error: uncompleted loop range" << std::endl;
	}
}

//// TryRange

template <typename ... Types>
	JS::Interpreter::State<JS::Public::JSValue, Types...> JS::Interpreter::TryRange<Types...>::BeginCatchRange(State<std::optional<JSValue>, Types...> state
#if ENABLE_ES4
	, bool final
#endif
)
{
#if ENABLE_ES4
	if(alter != nullptr)
	{
		alter->new_handle = state.self->instruction_sequence.size();
	}
	else
#endif
	{
		assert(range->handle == 0);
		range->handle = state.self->instruction_sequence.size();
	}
#if ENABLE_ES4
	if(!final)
	{
		alter = new Instruction_AlterRange(0);
		state.self->instruction_sequence.push_back(alter);
	}
#endif
	return JS::Interpreter::State<JS::Public::JSValue, Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::State<JS::Runtime::CompletionRecord, Types...> JS::Interpreter::TryRange<Types...>::BeginFinallyRange(State<std::optional<JSValue>, Types...> state)
{
	assert(range->end == 0);
	if(range->handle == 0)
	{
		range->handle = state.self->instruction_sequence.size();
	}
	range->end = state.self->instruction_sequence.size();
	return JS::Interpreter::State<JS::Runtime::CompletionRecord, Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::State<std::optional<JS::Public::JSValue>, Types...> JS::Interpreter::TryRange<Types...>::EndTryRange(State<std::optional<JSValue>, Types...> state)
{
	assert(range->handle != 0);
	assert(range->end == 0);
	range->end = state.self->instruction_sequence.size();
	// when execution arrives at finally, it will insert an activation onto the stack
	state.self->instruction_sequence.push_back(new Instruction_ActivateCompletion());
	completed = true;
	return JS::Interpreter::State<std::optional<JS::Public::JSValue>, Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::State<std::optional<JS::Public::JSValue>, Types...> JS::Interpreter::TryRange<Types...>::EndTryRange(State<JS::Runtime::CompletionRecord, Types...> state)
{
	assert(range->handle != 0);
	assert(range->end != 0);
	state.self->instruction_sequence.push_back(new Instruction_ActivateCompletion());
	completed = true;
	return JS::Interpreter::State<std::optional<JS::Public::JSValue>, Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::TryRange<Types...>::~TryRange()
{
	if(!completed)
	{
		std::cerr << "Internal error: uncompleted try range" << std::endl;
	}
}

//// IfRange

template <typename T, typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::IfRange<T, Types...>::IfPart::BeginElseRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state)
{
	assert(jump == nullptr);
	jump = new Instruction_Jump(0);
	state.self->instruction_sequence.push_back(jump);
	jump_if->target = state.self->instruction_sequence.size();
	return JS::Interpreter::State<Types...>(state.initial_instruction_count, state.self);
}

template <typename T, typename ... Types>
	typename JS::Interpreter::StateHelper::calculate_state_for_if_range<T, Types...>::type JS::Interpreter::IfRange<T, Types...>::IfPart::EndIfRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state)
{
	if(jump == nullptr)
	{
		jump_if->target = state.self->instruction_sequence.size();
	}
	else
	{
		jump->target = state.self->instruction_sequence.size();
	}
	completed = true;
	return typename JS::Interpreter::StateHelper::calculate_state_for_if_range<T, Types...>::type(state.initial_instruction_count, state.self);
}

template <typename T, typename ... Types>
	JS::Interpreter::IfRange<T, Types...>::IfPart::~IfPart()
{
	if(!completed)
	{
		std::cerr << "Internal error: uncompleted if range part" << std::endl;
	}
}

template <typename T, typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::IfRange<T, Types...>::BeginIfRange(State<JSValue, Types...> state)
{
	auto jump_if = new Instruction_JumpIfNot(0);
	state.self->instruction_sequence.push_back(jump_if);
	parts.emplace_back(IfPart(jump_if));
	return JS::Interpreter::State<Types...>(state.initial_instruction_count, state.self);
}

template <typename T, typename ... Types>
	JS::Interpreter::State<Types...> JS::Interpreter::IfRange<T, Types...>::BeginElseRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state)
{
	return parts.back().BeginElseRange(state);
}

template <typename T, typename ... Types>
	typename JS::Interpreter::StateHelper::calculate_state_for_if_range<T, Types...>::type JS::Interpreter::IfRange<T, Types...>::EndIfRange(typename StateHelper::calculate_state_for_if_range<T, Types...>::type state)
{
	for(auto iter = parts.rbegin(); iter != parts.rend(); iter++)
	{
		state = iter->EndIfRange(state);
	}
	completed = true;
	return state;
}

template <typename T, typename ... Types>
	JS::Interpreter::IfRange<T, Types...>::~IfRange()
{
	if(!completed)
	{
		std::cerr << "Internal error: uncompleted if range" << std::endl;
	}
}

//// FunctionRange

template <typename ... Types>
	JS::Interpreter::State<JS::Public::JSValue, Types...> JS::Interpreter::FunctionRange<Types...>::EndFunctionRange(State<std::optional<JSValue>, Types...> state)
{
	assert(range->end == 0);
	range->end = state.self->instruction_sequence.size();
	completed = true;
	return JS::Interpreter::State<JSValue, Types...>(state.initial_instruction_count, state.self);
}

template <typename ... Types>
	JS::Interpreter::FunctionRange<Types...>::~FunctionRange()
{
	if(!completed)
	{
		std::cerr << "Internal error: uncompleted function range" << std::endl;
	}
}

//// Code

template <typename ... NewTypes, typename ... Types>
	JS::Interpreter::State<NewTypes..., Types...> JS::Interpreter::Code::Then(State<Types...>& prior_state, State<NewTypes...> next_state)
{
	assert(prior_state.self == next_state.self);
	assert(prior_state.current_instruction_count == next_state.initial_instruction_count);
	assert(next_state.current_instruction_count == next_state.self->instruction_sequence.size());
	return JS::Interpreter::State<NewTypes..., Types...>(prior_state.initial_instruction_count, prior_state.self);
}

template <typename ... Types>
	JS::Interpreter::BlockRange JS::Interpreter::Code::BeginBlockRange(State<Types...>& state)
{
	assert(state.self->instruction_sequence.size() == state.current_instruction_count);
	auto setup = new Instruction_SetupRange(JS::Interpreter::Range::RangeType::Block);
	state.self->instruction_sequence.push_back(setup);
	state.current_instruction_count++;
	return BlockRange(setup);
}

template <typename ... Types>
	JS::Interpreter::LoopRange<Types...> JS::Interpreter::Code::BeginLoopRange(State<Types...>& state, std::optional<JSString> label)
{
	assert(state.self->instruction_sequence.size() == state.current_instruction_count);
	auto setup = new Instruction_SetupRange(JS::Interpreter::Range::RangeType::Loop, label);
	state.self->instruction_sequence.push_back(setup);
	state.current_instruction_count++;
	setup->handle = state.self->instruction_sequence.size();
	return LoopRange<Types...>(setup, state.self->instruction_sequence.size());
}

template <typename T, typename ... Types>
	JS::Interpreter::IfRange<T, Types...> JS::Interpreter::Code::BeginIfRange(State<JSValue, Types...>& state, State<Types...>& result_state)
{
	assert(state.self->instruction_sequence.size() == state.current_instruction_count);
	auto jump_if = new Instruction_JumpIfNot(0);
	state.self->instruction_sequence.push_back(jump_if);
	state.current_instruction_count++;
	result_state = State<Types...>(state);
	return IfRange<T, Types...>(jump_if);
}

template <typename ... Types>
	JS::Interpreter::TryRange<Types...> JS::Interpreter::Code::BeginTryRange(State<Types...>& state)
{
	assert(state.self->instruction_sequence.size() == state.current_instruction_count);
	auto setup = new Instruction_SetupRange(JS::Interpreter::Range::RangeType::Try);
	state.self->instruction_sequence.push_back(setup);
	state.current_instruction_count++;
	return TryRange<Types...>(setup);
}

template <typename ... Types>
	JS::Interpreter::FunctionRange<Types...> JS::Interpreter::Code::BeginFunctionRange(State<Types...>& state, bool is_arrow, bool is_generator, bool is_async)
{
	assert(state.self->instruction_sequence.size() == state.current_instruction_count);
	auto setup = new Instruction_SetupRange(JS::Interpreter::Range::RangeType::Function);
	setup->is_arrow = is_arrow;
	setup->is_generator = is_generator;
	setup->is_async = is_async;
	state.self->instruction_sequence.push_back(setup);
	state.current_instruction_count++;
	return FunctionRange<Types...>(setup, is_arrow, is_generator, is_async);
}

//// ExecutionContext

template <typename T>
	void JS::Runtime::ExecutionContext::Action_Push(T value)
{
	stack.push_back(new JS::Interpreter::WrapperOf<T>(value));
}

template <typename T>
	T JS::Runtime::ExecutionContext::Action_Pop()
{
	if constexpr(std::is_same_v<T, void>)
	{
		// to make it possible to call Action_Pop<void>() in template contexts
		return;
	}
	else
	{
		if(stack.size() == 0)
		{
			JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"execution stack underflow"));
		}
		auto wrapper = dynamic_cast<JS::Interpreter::WrapperOf<T> *>(stack.back());
		if(wrapper == nullptr)
		{
			//std::cerr << "expected " << typeid(T).name() << " received " << typeid(*stack.back()).name() << std::endl;
			std::cerr << "expected ";
			inspect<T>::inspect_type(std::cerr);
			std::cerr << " received ";
			stack.back()->print_type(std::cerr);
			std::cerr << " (";
			stack.back()->print(std::cerr);
			std::cerr << ")" << std::endl;
			JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"execution stack type mismatch"));
		}
		stack.pop_back();
		auto value = wrapper->value;
		delete wrapper;
		return value;
	}
}

template <typename T>
	T JS::Runtime::ExecutionContext::Action_Peek(size_t offset)
{
	if(stack.size() <= offset)
	{
		JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"execution stack underflow"));
	}
	auto wrapper = dynamic_cast<JS::Interpreter::WrapperOf<T> *>(stack[stack.size() - 1 - offset]);
	if(wrapper == nullptr)
	{
		stack[stack.size() - 1 - offset]->print_type(std::cerr);
		std::cerr << ", " << typeid(*stack[stack.size() - 1 - offset]).name() << "?" << typeid(T).name() << std::endl;
		JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"execution stack type mismatch"));
	}
	return wrapper->value;
}

template <typename T>
	void JS::Runtime::ExecutionContext::Action_Poke(T value, size_t offset)
{
	if(stack.size() <= 0)
	{
		JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"execution stack underflow"));
	}
	auto wrapper = dynamic_cast<JS::Interpreter::WrapperOf<T> *>(stack[stack.size() - 1 - offset]);
	delete wrapper;
	stack[stack.size() - 1 - offset] = new JS::Interpreter::WrapperOf<T>(value);
}

//

template <typename T>
	T JS::Runtime::ExecutionContext::ExecuteAndReturn()
{
	auto final_context = Execute();
	// either completed, or in the case of a generator or async function, suspended */
	switch(final_context->completion.Type)
	{
	case JS::Runtime::CompletionRecord::CompletionType::Normal:
		return final_context->Action_Pop<T>();
	case JS::Runtime::CompletionRecord::CompletionType::Throw:
		if(auto object = final_context->completion.Value.value().get_if<JSObject>())
		{
			if(auto error = std::dynamic_pointer_cast<JS::Class::Error>(object.value()))
			{
				JS::Throw(error);
			}
		}
		JS::Throw(final_context->completion.Value.value());
	default:
		JS::Throw(JS::New<JS::Class::InternalError> __LOC(u"cannot leave via return/break/continue"));
	}
}

template <typename T>
	T JS::Runtime::ExecutionContext::ExecuteAndReturn(JS::Interpreter::Code * code)
{
	ExecutionContext * cxt = new (GC) ExecutionContext(*GetCurrentExecutionContext());
	GetContextStack().push_back(cxt);
	GetContextStack().back()->code = code;
	GetContextStack().back()->state = ExecutionState::Executing;

	struct reset_context
	{
		ExecutionContext * context;
		ExecutionState state;

		reset_context(ExecutionContext * context)
			: context(context), state(context->state)
		{
		}

		~reset_context()
		{
			context->state = state;
		}
	} reset { GetContextStack()[GetContextStack().size() - 2] };
	reset.context->state = ExecutionState::Suspended;
	return ExecuteAndReturn<T>();
}

#endif // _EXECUTE_H
