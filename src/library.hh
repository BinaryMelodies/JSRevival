#ifndef _LIBRARY_HH
#define _LIBRARY_HH

#include <string>

namespace JS
{
	class Library
	{
	public:
		virtual ~Library() = default;

		virtual void Close() = 0;
		virtual void * LoadAddress(std::string name) = 0;
		template <typename T>
			T * Load(std::string name)
		{
			return reinterpret_cast<T *>(LoadAddress(name));
		}
	};

	class LibraryLoader
	{
	public:
		static LibraryLoader Host;

		virtual Library * Open(std::string name);
		virtual Library * LoadMain();
	};
}

#endif // _LIBRARY_HH
