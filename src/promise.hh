#ifndef _PROMISE_HH
#define _PROMISE_HH

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "builtins.hh"
#include "runtime.hh"

namespace JS
{
	namespace Runtime
	{
		class PromiseCapabilityBase
		{
		protected:
			void InitializePromise(JSValue constructor_value, JSObject& promise);

		public:
			JSFunction Resolve = nullptr;
			JSFunction Reject = nullptr;

			static void ContinueDynamicImport(PromiseCapability<> promiseCapability, JS::Runtime::ModuleRecord * moduleRecord);
		};

		template <class C>
			class PromiseCapability : public PromiseCapabilityBase
		{
		public:
			gcn::gc_ptr<C> Promise = nullptr;

			PromiseCapability(JSValue constructor_value)
			{
				JSObject promise;
				InitializePromise(constructor_value, promise);
				Promise = std::dynamic_pointer_cast<C>(promise);
			}

			PromiseCapability(const PromiseCapability&) = default;

			template <class D>
				PromiseCapability(const PromiseCapability<D>& other)
					: PromiseCapabilityBase(other), Promise(std::dynamic_pointer_cast<D>(other.Promise))
			{
			}

			void ContinueDynamicImport(JS::Runtime::ModuleRecord * moduleRecord)
			{
				PromiseCapabilityBase::ContinueDynamicImport(*this, moduleRecord);
			}
		};

		class DynamicPayload : public JS::Runtime::Host::Payload
		{
		public:
			JS::Runtime::PromiseCapability<JS::Class::Promise> PromiseCapability;
			DynamicPayload(const JS::Runtime::PromiseCapability<JS::Class::Promise>& PromiseCapability)
				: PromiseCapability(PromiseCapability)
			{
			}

			void NormalCompletion(ModuleRecord * moduleRecord) override;
			void ThrowCompletion(JSValue error_result) override;
		};

		class PromiseReaction
		{
		public:
			std::optional<PromiseCapability<>> Capability;
			enum class ReactionType
			{
				Fulfill,
				Reject,
			};
			ReactionType Type;
			JobCallback * Handler;
		};
	}

	namespace Class
	{
		// ES6
		class Promise : public JS::Class::OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			enum class StateType
			{
				Pending,
				Fulfilled,
				Rejected,
			};
			StateType PromiseState = StateType::Pending;
			std::optional<JSValue> PromiseResult;
			std::vector<JS::Runtime::PromiseReaction> PromiseFulfillReactions;
			std::vector<JS::Runtime::PromiseReaction> PromiseRejectReactions;
			JSBoolean PromiseIsHandled = false;

			void Reject(JSValue reason);
			void Fulfill(JSValue reason);
			void TriggerPromiseReactions(std::vector<JS::Runtime::PromiseReaction>& reactions, JSValue argument);
			JSValue PerformPromiseThen(JSValue onFulfilled, JSValue onRejected, std::optional<JS::Runtime::PromiseCapability<>> resultCapability = {});
			static gcn::gc_ptr<Promise> Resolve(JSObject constructor, JSValue value);

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;
		};
	}

	namespace Runtime
	{
		class ResolutionRecord
		{
		public:
			JSBoolean Value = false;
		};

		class PromiseResolveThenableJob : public JS::Runtime::Job
		{
		public:
			gcn::gc_ptr<JS::Class::Promise> promiseToResolve;
			JSObject thenable;
			JS::Runtime::JobCallback * then;

			PromiseResolveThenableJob(gcn::gc_ptr<JS::Class::Promise> promiseToResolve, JSObject thenable, JS::Runtime::JobCallback * then)
				: promiseToResolve(promiseToResolve), thenable(thenable), then(then)
			{
			}

			void Execute() override;

			static PromiseResolveThenableJob * New(gcn::gc_ptr<JS::Class::Promise> promiseToResolve, JSObject thenable, JS::Runtime::JobCallback * then, JS::Runtime::Realm *& realm);
		};

		class PromiseReactionJob : public JS::Runtime::Job
		{
		public:
			JS::Runtime::PromiseReaction reaction;
			JSValue argument;

			PromiseReactionJob(JS::Runtime::PromiseReaction reaction, JSValue argument)
				: reaction(reaction), argument(argument)
			{
			}

			void Execute() override;

			static PromiseReactionJob * New(JS::Runtime::PromiseReaction reaction, JSValue argument, JS::Runtime::Realm *& realm);
		};

		namespace Builtin
		{
			class PromiseResolveFunction : public BuiltinFunction
			{
			public:
				gcn::gc_ptr<JS::Class::Promise> Promise;
				std::shared_ptr<ResolutionRecord> AlreadyResolved;

				PromiseResolveFunction(JS::Runtime::Realm * Realm, gcn::gc_ptr<JS::Class::Promise> Promise, std::shared_ptr<ResolutionRecord> AlreadyResolved)
					: BuiltinFunction(Realm, u"", 1),
					Promise(Promise),
					AlreadyResolved(AlreadyResolved)
				{
				}

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			};

			class PromiseRejectFunction : public BuiltinFunction
			{
			public:
				gcn::gc_ptr<JS::Class::Promise> Promise;
				std::shared_ptr<ResolutionRecord> AlreadyResolved;

				PromiseRejectFunction(JS::Runtime::Realm * Realm, gcn::gc_ptr<JS::Class::Promise> Promise, std::shared_ptr<ResolutionRecord> AlreadyResolved)
					: BuiltinFunction(Realm, u"", 1),
					Promise(Promise),
					AlreadyResolved(AlreadyResolved)
				{
				}

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			};

			class PromiseCapabilityExecutor : public BuiltinFunction
			{
			public:
				JSValue Resolve;
				JSValue Reject;

				PromiseCapabilityExecutor(JS::Runtime::Realm * Realm)
					: BuiltinFunction(Realm, u"", 2)
				{
				}

				JSValue Call(JSValue this_argument, std::vector<JSValue> arguments AND_PHASE(phase)) override;
			};

			void CreateResolvingFunctions(gcn::gc_ptr<JS::Class::Promise> promise, gcn::gc_ptr<PromiseResolveFunction>& resolve, gcn::gc_ptr<PromiseRejectFunction>& reject);
		}
	}
}

#endif // _PROMISE_HH
