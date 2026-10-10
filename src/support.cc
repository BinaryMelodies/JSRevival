
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
#include "value.hh"
#include "core.hh"
#include "utils.hh"
#include "debugger.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

#if ENABLE_DEBUG
JS::Runtime::Debugger * JS::Runtime::Debugger::Current = new JS::Runtime::Debugger();
JS::Runtime::Debugger::Element * JS::Runtime::Output_Element = nullptr;
#endif

#if ENABLE_DEBUG
bool JS::Enable_Debug = false;
#endif

//// Debugger
#if ENABLE_DEBUG
std::map<JSObject, std::string> JS::Runtime::Debugger::object_labels;

void JS::Runtime::Debugger::SetObjectLabel(JSObject object, std::string label)
{
	if(object_labels.find(object) == object_labels.end())
	{
		object_labels[object] = label;
	}
}

void JS::Runtime::Debugger::SetObjectLabel(JSFunction function, std::string label)
{
	auto object_option = GetNonNullObject(function);
	assert(object_option);
	SetObjectLabel(object_option.value(), label);
}

std::optional<std::string> JS::Runtime::Debugger::GetObjectLabel(JSObject object)
{
	auto object_label_iter = object_labels.find(object);
	if(object_label_iter != object_labels.end())
	{
		return object_label_iter->second;
	}
	else
	{
		return {};
	}
}

bool JS::Runtime::Debugger::PropertyKeyLess::operator()(const JSPropertyKey& lhs, const JSPropertyKey& rhs) const
{
	return std::visit([&rhs](auto&& lhs)
	{
		using T = std::decay_t<decltype(lhs)>;
		if constexpr(std::is_same_v<T, JSString>)
		{
			return std::visit([&lhs](auto&& rhs)
			{
				using T = std::decay_t<decltype(rhs)>;
				if constexpr(std::is_same_v<T, JSString>)
				{
					uint32_t lhs_index;
					if(is_array_index(lhs, lhs_index))
					{
						uint32_t rhs_index;
						if(is_array_index(rhs, rhs_index))
						{
							return lhs_index < rhs_index;
						}
						else
						{
							return true;
						}
					}
					else if(is_array_index(rhs, lhs_index))
					{
						return false;
					}
					else
					{
						return lhs < rhs;
					}
				}
				else if constexpr(std::is_same_v<T, JSSymbol>)
				{
					return false; // symbols come first
				}
				else
				{
					static_assert(false);
				}
			}, rhs);
		}
		else if constexpr(std::is_same_v<T, JSSymbol>)
		{
			return std::visit([&lhs](auto&& rhs)
			{
				using T = std::decay_t<decltype(rhs)>;
				if constexpr(std::is_same_v<T, JSString>)
				{
					return true; // symbols come first
				}
				else if constexpr(std::is_same_v<T, JSSymbol>)
				{
					return JS::Runtime::Debugger::GetSymbolId(lhs) < JS::Runtime::Debugger::GetSymbolId(rhs);
				}
				else
				{
					static_assert(false);
				}
			}, rhs);
		}
		else
		{
			static_assert(false);
		}
	}, lhs);
}

#if ENABLE_ES4
bool JS::Runtime::Debugger::NamespaceLess::operator()(const JSNamespace& lhs, const JSNamespace& rhs) const
{
#if 0
	if(lhs == JS_public && rhs != JS_public)
	{
		return true;
	}
	else if(lhs != JS_public && rhs == JS_public)
	{
		return false;
	}
	else if(lhs != JS_public && rhs != JS_public)
	{
	}
#endif

	auto& all_allocated_namespaces = JS::Class::Namespace::all_allocated_namespaces;
	auto lhs_namespace_iter = std::find(all_allocated_namespaces.begin(), all_allocated_namespaces.end(), lhs);
	auto rhs_namespace_iter = std::find(all_allocated_namespaces.begin(), all_allocated_namespaces.end(), rhs);
	size_t lhs_birth = std::distance(all_allocated_namespaces.begin(), lhs_namespace_iter);
	size_t rhs_birth = std::distance(all_allocated_namespaces.begin(), rhs_namespace_iter);

	if(lhs_birth < rhs_birth)
	{
		return true;
	}
	else
	{
		return false;
	}
}

#if 0
bool JS::Runtime::Debugger::NamespaceAccessLess::operator()(const type& lhs, const type& rhs) const
{
#if 0
	if(std::get<0>(lhs) == JS_public && std::get<0>(rhs) != JS_public)
	{
		return true;
	}
	else if(std::get<0>(lhs) != JS_public && std::get<0>(rhs) == JS_public)
	{
		return false;
	}
	else if(std::get<0>(lhs) != JS_public && std::get<0>(rhs) != JS_public)
	{
	}
#endif

	auto& all_allocated_namespaces = JS::Class::Namespace::all_allocated_namespaces;
	auto lhs_namespace_iter = std::find(all_allocated_namespaces.begin(), all_allocated_namespaces.end(), std::get<0>(lhs));
	auto rhs_namespace_iter = std::find(all_allocated_namespaces.begin(), all_allocated_namespaces.end(), std::get<0>(rhs));
	size_t lhs_birth = std::distance(all_allocated_namespaces.begin(), lhs_namespace_iter);
	size_t rhs_birth = std::distance(all_allocated_namespaces.begin(), rhs_namespace_iter);

	if(lhs_birth < rhs_birth)
	{
		return true;
	}
	else if(lhs_birth > rhs_birth)
	{
		return false;
	}

	return std::get<1>(lhs) == JS::Access::Read && std::get<1>(rhs) == JS::Access::Write;
}
#endif
#endif

JS::Runtime::Debugger * JS::Runtime::Debugger::OpenXMLElement(std::string tag_name)
{
	auto element = new Element(current_element, tag_name);
	if(current_element == nullptr)
	{
		root_element = element;
	}
	else
	{
		current_element->member_elements.push_back(element);
	}
	current_element = element;
	return this;
}

JS::Runtime::Debugger * JS::Runtime::Debugger::CloseXMLElement()
{
	if(current_element != nullptr)
	{
		current_element = current_element->parent;
		if(current_element == nullptr)
		{
			current_element = root_element;
		}
	}
	return this;
}

JS::Runtime::Debugger * JS::Runtime::Debugger::AppendText(std::string text)
{
	if(current_element->member_elements.size() == 0)
	{
		current_element->content_text += text;
	}
	else
	{
		current_element->member_elements.back()->trailing_text += text;
	}
	return this;
}

JS::Runtime::Debugger * JS::Runtime::Debugger::AppendText(JSString text)
{
	size_t index = 0;
	while(true)
	{
		std::ostringstream oss;
		char16_t surrogate = PrintString(oss, text, index);
		AppendText(oss.str());

		if(surrogate == 0)
		{
			break;
		}
		else
		{
			std::ostringstream oss;
			oss << std::hex << int(surrogate);
			OpenXMLElement("surrogate");
			AddXMLAttribute("value", oss.str());
			CloseXMLElement();
		}
	}
	return this;
}

bool JS::Runtime::Debugger::AlreadySerialized(JSObject reference, std::string& element_reference)
{
	auto iter = std::find(serialized_objects.begin(), serialized_objects.end(), reference);

	std::ostringstream oss;
	oss << std::distance(serialized_objects.begin(), iter);
	element_reference = oss.str();

	if(iter == serialized_objects.end())
	{
		serialized_objects.push_back(reference);
		return false;
	}
	else
	{
		return true;
	}
}

JS::Runtime::Debugger * JS::Runtime::Debugger::AddXMLAttribute(std::string attribute_name, std::string attribute_value)
{
	current_element->attributes[attribute_name] = attribute_value;
	return this;
}

JS::Runtime::Debugger * JS::Runtime::Debugger::AddXMLAttribute(std::string attribute_name, JSString attribute_value)
{
	std::ostringstream oss;
	PrintString(oss, attribute_value);
	return AddXMLAttribute(attribute_name, oss.str());
}

void JS::Runtime::Debugger::PurifyString(std::ostream& out, JSString string)
{
	std::ostringstream oss;
	PrintString(oss, string);
	for(char c : oss.str())
	{
		switch(c)
		{
		case ' ':
			out << "\\s";
			break;
		case '#':
			out << "\\h";
			break;
		default:
			out.put(c);
		}
	}
}

std::string JS::Runtime::Debugger::GetSymbolId(JSSymbol symbol)
{
	auto symbol_description = symbol->Description.value_or(u"");
	auto allocated_symbols_iter = JS::Runtime::SymbolInstance::allocated_symbols.find(symbol_description);
	if(allocated_symbols_iter ==  JS::Runtime::SymbolInstance::allocated_symbols.end())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto& allocated_symbol_vector = allocated_symbols_iter->second;
	auto symbol_iter = std::find(allocated_symbol_vector.begin(), allocated_symbol_vector.end(), symbol);

	if(symbol_iter == allocated_symbol_vector.end())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	std::ostringstream oss;
	PurifyString(oss, symbol_description);
	oss << "#" << std::distance(allocated_symbol_vector.begin(), symbol_iter);
	std::string symbol_id = oss.str();

	return symbol_id;
}

std::string JS::Runtime::Debugger::LookupSymbol(JSSymbol symbol)
{
	used_symbols.insert(symbol);
	return GetSymbolId(symbol);
}

#if ENABLE_ES4
std::string JS::Runtime::Debugger::GetNamespaceId(JSNamespace namespace_)
{
	if(namespace_ == JS_public)
	{
		return "public"; // avoid trying to look it up and making a public#0 reference
	}
	if(namespace_ == JS_meta)
	{
		return "meta"; // TODO: this would fail since it never gets stored in allocated_namespaces
	}

	auto allocated_namespaces_iter = JS::Class::Namespace::allocated_namespaces.find(namespace_->uri);
	if(allocated_namespaces_iter ==  JS::Class::Namespace::allocated_namespaces.end())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}
	auto& allocated_namespace_vector = allocated_namespaces_iter->second;
	auto namespace_iter = std::find(allocated_namespace_vector.begin(), allocated_namespace_vector.end(), namespace_);

	if(namespace_iter == allocated_namespace_vector.end())
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	std::ostringstream oss;
	PurifyString(oss, namespace_->uri);
	oss << "#" << std::distance(allocated_namespace_vector.begin(), namespace_iter);
	std::string namespace_id = oss.str();

	return namespace_id;
}

std::string JS::Runtime::Debugger::LookupNamespace(JSNamespace namespace_)
{
	used_namespaces.insert(namespace_);
	return GetNamespaceId(namespace_);
}
#endif

void JS::Runtime::Debugger::PrintXMLText(std::ostream& out, std::string text, char quote_char)
{
	for(auto c : text)
	{
		switch(c)
		{
		case '<':
			out << "&lt;";
			break;
		case '>':
			out << "&gt;";
			break;
		case '&':
			out << "&amp;";
			break;
		case '"':
			if(quote_char == c)
				out << "&quot;";
			else
				out.put(c);
			break;
		case '\'':
			if(quote_char == c)
				out << "&apos;";
			else
				out.put(c);
			break;
		default:
			out.put(c);
			break;
		}
	}
}

void JS::Runtime::Debugger::PrettyPrint(std::ostream& out, std::string indent, Element * element)
{
	out << indent << "<" << element->tag_name;
	for(auto attribute_pair : element->attributes)
	{
		out << " " << attribute_pair.first << "=\"";
		PrintXMLText(out, attribute_pair.second, '"');
		out << "\"";
	}

	if(element->member_elements.size() == 0)
	{
		if(element->content_text == "")
		{
			out << "/>" << std::endl;
		}
		else
		{
			out << ">";
			PrintXMLText(out, element->content_text);
			out << "</" << element->tag_name << ">" << std::endl;
		}
	}
	else
	{
		out << ">" << std::endl;
		if(element->content_text != "")
		{
			out << indent << " ";
			PrintXMLText(out, element->content_text);
			out << std::endl;
		}

		for(auto member_element : element->member_elements)
		{
			PrettyPrint(out, indent + " ", member_element);
			if(member_element->trailing_text != "")
			{
				out << indent << " ";
				PrintXMLText(out, member_element->trailing_text);
				out << std::endl;
			}
		}

		out << indent << "</" << element->tag_name << ">" << std::endl;
	}
}

void JS::Runtime::Debugger::PrettyPrint(std::ostream& out, std::string indent)
{
	PrettyPrint(out, indent, root_element);
}
#endif

