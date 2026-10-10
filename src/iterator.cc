
#include "common.hh"
#include "macros.hh"
#include "value.hh"
#include "core.hh"
#include "iterator.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// ListIteratorBindingData

bool JS::Runtime::ListIteratorBindingData::IsDone()
{
	return argument_iterator == argument_end;
}

std::optional<JSValue> JS::Runtime::ListIteratorBindingData::StepValue()
{
	if(argument_iterator < argument_end)
	{
		return *argument_iterator++;
	}
	else
	{
		return {};
	}
}

void JS::Runtime::ListIteratorBindingData::Close()
{
}

//// ForInIterator

bool JS::Runtime::ForInIterator::IsDone()
{
	return CurrentObject == nullptr;
}

std::optional<JSValue> JS::Runtime::ForInIterator::StepValue()
{
	while(CurrentObject != nullptr)
	{
		if(!ObjectWasVisited)
		{
			for(auto property_key : CurrentObject->OwnPropertyKeys(VIA_CLASS(CurrentObject == StartingObject ? CurrentObject->GetClass() : nullptr)))
			{
				if(auto string_pointer = std::get_if<JSString>(&property_key))
				{
					RemainingKeys.push(*string_pointer);
				}
			}
			ObjectWasVisited = true;
#if ENABLE_ES4
			if(auto class_ = std::dynamic_pointer_cast<JS::Class::Class>(CurrentObject))
			{
				if(class_->Super.has_value())
				{
					CurrentClass = GetClassForType(class_->Super.value());
				}
				ClassWasVisited = false;
			}
#endif
		}

		while(!RemainingKeys.empty())
		{
			auto property_key = RemainingKeys.front();
			RemainingKeys.pop();
			if(VisitedKeys.find(property_key) == VisitedKeys.end())
			{
				// TODO: ES4 might have instance elements
				auto property_option = CurrentObject->GetOwnProperty(property_key PUBLIC_NAMESPACES READ_ACCESS);
				if(property_option.has_value())
				{
					auto property = property_option.value();
					VisitedKeys.insert(property_key);
					if(!property.Enumerable)
					{
						continue;
					}
					switch(Kind)
					{
					case EnumerationKind::Key:
						// for(in) statement
						return property_key;
					case EnumerationKind::KeyValue:
						// not implemented, never used
						assert(false);
					case EnumerationKind::Value:
						// for each(in) statement

						// TODO: duplicate of Object::Get
						switch(property.PropertyType)
						{
						case JSProperty::Type::Data:
							if(property.IsInitialized())
							{
								return property.GetValue();
							}
							else
							{
								JS::Throw(
#if ENABLE_ES4
									IsVersionES4() ? JS::New<InternalError> __LOC(u"uninitialized dynamic variable") :
#endif
										JS::New<InternalError> __LOC(u"uninitialized data property"));
							}
							break;
#if ENABLE_ES4
						case JSProperty::Type::Variable:
							// variable properties need to be accessed and modified by address
							if(property_option.value().direct_access == nullptr)
							{
								JS::Throw(JS::New<UninitializedError> __LOC());
							}
							if(!property_option.value().direct_access->IsInitialized())
							{
								property_option.value().direct_access->Initialize(JS::Runtime::ExecutionPhase::Run);
							}
							if(!property_option.value().direct_access->IsInitialized())
							{
								JS::Throw(JS::New<UninitializedError> __LOC());
							}
							return property_option.value().direct_access->GetValue();
#endif
						case JSProperty::Type::Accessor:
							if(CurrentVersion >= Version::ES5)
							{
								if(property.Get != nullptr)
								{
									// ES5 accessor properties get the receiver object as their "this" value
									return property.Get.Call(StartingObject, std::vector<JSValue>{} RUNTIME_PHASE);
								}
								else
								{
									return JS_undefined;
								}
							}
							// ignore accessors in these versions
							break;
#if ENABLE_ES4
						case JSProperty::Type::Getter:
							if(IsVersionES4())
							{
								// ES4 static getter methods do not get a "this" operand
								return property.Get.Call(JS_undefined, std::vector<JSValue>{} RUNTIME_PHASE);
							}
							// ignore getters in these versions
							break;
						case JSProperty::Type::Setter:
							if(IsVersionES4())
							{
								// ES4 static setter methods cannot occur for a JS::Access::Read lookup
								JS::Throw(JS::New<InternalError> __LOC(u"setter method on read lookup"));
							}
							// ignore setters in these versions
							break;
						case JSProperty::Type::Forbidden:
							// this should have not been returned
							if(IsVersionES4())
							{
								JS::Throw(JS::New<InternalError> __LOC(u"property outside region shadowed by inner block"));
							}
							// ignore
							break;
#endif
						}
					}
				}
			}
		}

#if ENABLE_ES4
		if(CurrentClass != nullptr)
		{
			if(!ClassWasVisited)
			{
				for(auto property_key : CurrentClass->OwnPropertyKeys(VIA_CLASS(CurrentObject == StartingObject ? CurrentObject->GetClass() : nullptr)))
				{
					if(auto string_pointer = std::get_if<JSString>(&property_key))
					{
						RemainingKeys.push(*string_pointer);
					}
				}
				ClassWasVisited = true;
				continue;
			}

			if(CurrentClass->Super.has_value())
			{
				CurrentClass = GetClassForType(CurrentClass->Super.value());
			}
			continue;
		}

		if(!RemainingKeys.empty())
		{
			continue;
		}
#endif

		CurrentObject = GetNonNullObject(CurrentObject->GetPrototypeOf().value_or(JS_undefined)).value_or(nullptr); // TODO: primitive types
		ObjectWasVisited = false;
	}

	return {};
}

void JS::Runtime::ForInIterator::Close()
{
}

//// IteratorRecord

bool JS::Runtime::IteratorRecord::IsDone()
{
	return Done;
}

JS::Runtime::IteratorRecord JS::Runtime::IteratorRecord::GetIteratorDirect(JSObject obj)
{
	auto next_method = JS::Get(obj, u"next");
	return IteratorRecord(obj, next_method, false);
}

JS::Runtime::IteratorRecord JS::Runtime::IteratorRecord::GetIteratorFromMethod(JSValue obj, JSFunction method)
{
	JSValue iterator = JS::Call(method, obj, {} RUNTIME_PHASE);
	if(auto iterator_object = GetNonNullObject(iterator))
	{
		return GetIteratorDirect(*iterator_object);
	}
	else
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

JS::Runtime::IteratorRecord JS::Runtime::IteratorRecord::GetIterator(JSValue obj, bool async)
{
	std::optional<JSFunction> method;
	if(async)
	{
		method = GetMethod(obj, JS::Symbols::asyncIterator);
		if(!method.has_value())
		{
			method = GetMethod(obj, JS::Symbols::iterator);
			if(!method.has_value())
			{
				JS::Throw(JS::New<TypeError> __LOC());
			}
			auto sync_iterator_record = GetIteratorFromMethod(obj, method.value());
			return CreateAsyncFromSyncIterator(sync_iterator_record);
		}
	}
	else
	{
		method = GetMethod(obj, JS::Symbols::iterator);
		if(!method.has_value())
		{
			JS::Throw(JS::New<TypeError> __LOC());
		}
	}
	return GetIteratorFromMethod(obj, method.value());
}

JS::Runtime::IteratorRecord JS::Runtime::IteratorRecord::CreateAsyncFromSyncIterator(IteratorRecord sync_iterator_record)
{
	auto async_iterator = JS::New<JS::Class::AsyncFromSyncIterator>(new IteratorRecord(sync_iterator_record));
	auto next_method = JS::Get(async_iterator, u"next");
	return IteratorRecord{async_iterator, next_method, false};
}

JSObject JS::Runtime::IteratorRecord::Next(std::optional<JSValue> value)
{
	try
	{
		if(auto next_method = GetFunction(NextMethod))
		{
			JSValue result;
			if(value.has_value())
			{
				result = JS::Call(next_method.value(), Iterator, {value.value()} RUNTIME_PHASE);
			}
			else
			{
				result = JS::Call(next_method.value(), Iterator, {} RUNTIME_PHASE);
			}

			if(auto result_object = GetNonNullObject(result))
			{
				return result_object.value();
			}
			else
			{
				JS::Throw(JS::New<TypeError> __LOC("next returned non-object"));
			}
		}
		else
		{
			JS::Throw(JS::New<TypeError> __LOC("next is not callable"));
		}
	}
	catch(...)
	{
		Done = true;
		throw;
	}
}

JSBoolean JS::Runtime::IteratorRecord::IteratorComplete(JSObject iterator_result)
{
	return JS::ToBoolean(JS::Get(iterator_result, u"done"));
}

JSValue JS::Runtime::IteratorRecord::IteratorValue(JSObject iterator_result)
{
	return JS::Get(iterator_result, u"value");
}

std::optional<JSObject> JS::Runtime::IteratorRecord::Step()
{
	JSObject result = Next();
	try
	{
		if(IteratorComplete(result))
		{
			Done = true;
			return {};
		}
		else
		{
			return result;
		}
	}
	catch(...)
	{
		Done = true;
		throw;
	}
}

std::optional<JSValue> JS::Runtime::IteratorRecord::StepValue()
{
	std::optional<JSObject> result = Step();
	if(result.has_value())
	{
		try
		{
			return IteratorValue(result.value());
		}
		catch(...)
		{
			Done = true;
			return {};
		}
	}
	else
	{
		return {};
	}
}

void JS::Runtime::IteratorRecord::Close()
{
	auto return_method = JS::GetMethod(Iterator, u"return");
	if(!return_method.has_value())
	{
		return;
	}
	auto inner_result = JS::Call(return_method.value(), Iterator, {} RUNTIME_PHASE);
	if(!GetNonNullObject(inner_result))
	{
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

