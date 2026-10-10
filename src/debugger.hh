#ifndef _DEBUGGER_HH
#define _DEBUGGER_HH

/* XML serialization of objects for debugging */

#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple> // TODO: might not be needed
#include <vector>
#include "common.hh"

namespace JS
{
	using namespace Public;

	namespace Runtime
	{
#if ENABLE_DEBUG
		class Debugger
		{
		public:
			static std::map<JSObject, std::string> object_labels;
			static void SetObjectLabel(JSObject object, std::string label);
			static void SetObjectLabel(JSFunction function, std::string label);
#if ENABLE_ES4
			static void SetObjectLabel(JSClass object, std::string label)
			{
				SetObjectLabel(JSObject(object), label);
			}
#endif
			static std::optional<std::string> GetObjectLabel(JSObject object);

			std::set<JSSymbol> used_symbols;
			std::vector<JSObject> serialized_objects;

			struct PropertyKeyLess
			{
				bool operator()(const JSPropertyKey& lhs, const JSPropertyKey& rhs) const;
			};

#if ENABLE_ES4
			std::set<JSNamespace> used_namespaces;

			struct NamespaceLess
			{
				bool operator()(const JSNamespace& lhs, const JSNamespace& rhs) const;
			};

			struct KeyData
			{
				JSPropertyKey key;
				JSNamespace namespace_;
				Access access;

				bool operator <(const KeyData& other) const
				{
					return PropertyKeyLess()(key, other.key)
						|| (key == other.key
							&& (NamespaceLess()(namespace_, other.namespace_)
								|| (namespace_ == other.namespace_ && access < other.access)));
				}
			};
#endif

			static Debugger * Current;

			class Element
			{
			public:
				Element * parent = nullptr;
				std::string tag_name;
				std::map<std::string, std::string> attributes;
				std::string content_text = "";
				std::vector<Element *> member_elements;
				std::string trailing_text = "";

				Element(Element * parent, std::string tag_name)
					: parent(parent), tag_name(tag_name)
				{
				}
			};
			Element * root_element = nullptr;
			Element * current_element = nullptr;

			Debugger * OpenXMLElement(std::string tag_name);
			Debugger * CloseXMLElement();
			Debugger * AppendText(std::string text);
			template <typename T>
				Debugger * AppendText(const T& value)
			{
				std::ostringstream oss;
				oss << value;
				return AppendText(oss.str());
			}
			Debugger * AppendText(JSString text);
			bool AlreadySerialized(JSObject reference, std::string& element_reference);
			Debugger * AddXMLAttribute(std::string attribute_name, std::string attribute_value);
			Debugger * AddXMLAttribute(std::string attribute_name, JSString attribute_value);
			static void PurifyString(std::ostream& out, JSString string); // replace characters such as ' ' and '#' that we intend to use for other purposes
			static std::string GetSymbolId(JSSymbol symbol);
			std::string LookupSymbol(JSSymbol symbol);
#if ENABLE_ES4
			static std::string GetNamespaceId(JSNamespace namespace_);
			std::string LookupNamespace(JSNamespace namespace_);
#endif

			Element * GetCurrentElement() { return current_element; }
			Debugger * GotoElement(Element * element) { current_element = element; return this; }

			void PrintXMLText(std::ostream& out, std::string text, char quote_char = '\0');
			void PrettyPrint(std::ostream& out, std::string indent, Element * element);
			void PrettyPrint(std::ostream& out, std::string indent = "");
		};

		extern Debugger::Element * Output_Element;
#endif
	}
}

#endif //  _DEBUGGER_HH
