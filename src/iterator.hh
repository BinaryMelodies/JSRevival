#ifndef _ITERATOR_HH
#define _ITERATOR_HH

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"

/* Structures related to handling iterators and generators */

namespace JS
{
	namespace Runtime
	{
		enum class EnumerationKind
		{
			Key,
			Value,
			KeyValue,
		};

		/** Contains the information necessary to execute the ECMAScript IteratorBindingInitialization operation */
		class IteratorBindingData
		{
		public:
			virtual ~IteratorBindingData() = default;
			virtual bool IsDone() = 0;
			virtual std::optional<JSValue> StepValue() = 0;
			virtual void Close() = 0;
		};

		/** Specialization that implements iterating through a list of arguments, represented as a C++ std::vector */
		class ListIteratorBindingData : public IteratorBindingData
		{
		public:
			std::vector<JSValue>::iterator argument_iterator;
			std::vector<JSValue>::iterator argument_end;

			bool IsDone() override;
			std::optional<JSValue> StepValue() override;
			void Close() override;
		};

		/** Contains the information necessary to execute the ECMAScript EnumerateObjectProperties operation */
		class ForInIterator : public IteratorBindingData
		{
		public:
			JSObject StartingObject;
			JSObject CurrentObject;
			EnumerationKind Kind;
			JSBoolean ObjectWasVisited = false;
#if ENABLE_ES4
			JSClass CurrentClass = nullptr;
			JSBoolean ClassWasVisited = false;
#endif
			std::set<JSString> VisitedKeys;
			std::queue<JSString> RemainingKeys;

			ForInIterator(JSObject Object, EnumerationKind Kind = EnumerationKind::Key)
				: StartingObject(Object), CurrentObject(Object), Kind(Kind)
			{
			}

			bool IsDone() override;
			std::optional<JSValue> StepValue() override;
			void Close() override;
		};

		/** Specialization that implements an ECMAScript iterator record */
		class IteratorRecord : public IteratorBindingData
		{
		public:
			JSObject Iterator = nullptr;
			JSValue NextMethod = JS_undefined;
			JSBoolean Done = false;

			IteratorRecord()
				: Iterator(nullptr), NextMethod(JS_undefined), Done(false)
			{
			}

			IteratorRecord(JSObject Iterator, JSValue NextMethod, JSBoolean Done)
				: Iterator(Iterator), NextMethod(NextMethod), Done(Done)
			{
			}

			IteratorRecord(const IteratorRecord& other)
				: Iterator(other.Iterator), NextMethod(other.NextMethod), Done(other.Done)
			{
			}

			~IteratorRecord()
			{
				// TODO: is this a good idea?
				//Close();
			}

			bool IsDone() override;

			static IteratorRecord GetIteratorDirect(JSObject obj);
			static IteratorRecord GetIteratorFromMethod(JSValue obj, JSFunction method);
			static IteratorRecord GetIterator(JSValue obj, bool async = false);
			static IteratorRecord CreateAsyncFromSyncIterator(IteratorRecord sync_iterator_record);

			JSObject Next(std::optional<JSValue> value = std::optional<JSValue>());

			static JSBoolean IteratorComplete(JSObject iterator_result);
			static JSValue IteratorValue(JSObject iterator_result);

			std::optional<JSObject> Step();
			std::optional<JSValue> StepValue() override;
			void Close() override;
		};
	}

	namespace Class
	{
		// ES6
		class Generator : public JS::Class::OrdinaryObject
		{
		public:
			enum class StateType
			{
				SuspendedStart,
				SuspendedYield,
				Executing,
				Completed,
			};
			StateType GeneratorState;
			JS::Runtime::ExecutionContext * GeneratorContext;
			// ESNext
			std::optional<JSString> GeneratorBrand;

			Generator(StateType GeneratorState, JS::Runtime::ExecutionContext * GeneratorContext, std::optional<JSString> GeneratorBrand = std::optional<JSString>{})
				: GeneratorState(GeneratorState), GeneratorContext(GeneratorContext), GeneratorBrand(GeneratorBrand)
			{
			}

			std::map<JSString, JSValue> GetInternalSlots(bool unified = false) override;

			JSValue Resume(JS::Runtime::CompletionRecord completion, std::optional<JSString> brand = std::optional<JSString>());
		};

		// ES6
		class ArrayIterator : public JS::Class::OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			enum class Kind
			{
				Key,
				Value,
				KeyValue,
			};

			std::optional<JSObject> IteratedArrayLike;
			uintptr_t ArrayLikeNextIndex;
			Kind ArrayLikeIterationKind;

			ArrayIterator(JSObject IteratedArrayLike, uintptr_t ArrayLikeNextIndex, Kind ArrayLikeIterationKind)
				: IteratedArrayLike(IteratedArrayLike), ArrayLikeNextIndex(ArrayLikeNextIndex), ArrayLikeIterationKind(ArrayLikeIterationKind)
			{
			}

			ArrayIterator(JSObject IteratedArrayLike, Kind ArrayLikeIterationKind)
				: ArrayIterator(IteratedArrayLike, 0, ArrayLikeIterationKind)
			{
			}
		};

		// ESNext
		class AsyncFromSyncIterator : public JS::Class::OrdinaryObject
		{
		public:
			static JSObject JS::Runtime::Intrinsics::* IntrinsicPrototype;

			JS::Runtime::IteratorRecord * SyncIteratorRecord;
			AsyncFromSyncIterator(JS::Runtime::IteratorRecord * SyncIteratorRecord) : SyncIteratorRecord(SyncIteratorRecord)
			{
			}
		};
	}
}

#endif // _ITERATOR_HH
