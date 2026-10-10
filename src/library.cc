
#ifdef _WIN32
# include <windows.h>
#else
# include <dlfcn.h>
#endif

#include "library.hh"

#ifdef _WIN32
namespace JS
{
	class WindowsLibrary : public Library
	{
	public:
		HMODULE handle = nullptr;
		bool main = false;

		WindowsLibrary(HMODULE handle, bool main)
			: handle(handle), main(main)
		{
		}

		void Close() override
		{
			if(main && handle)
			{
				FreeLibrary(handle);
				handle = nullptr;
			}
		}

		void * LoadAddress(std::string name) override
		{
			return reinterpret_cast<void *>(GetProcAddress(handle, name.c_str()));
		}
	};
}

JS::Library * JS::LibraryLoader::Open(std::string name)
{
	HMODULE handle = LoadLibrary(name.c_str());
	if(handle)
	{
		return new WindowsLibrary(handle, false);
	}
	else
	{
		return nullptr;
	}
}

JS::Library * JS::LibraryLoader::LoadMain()
{
	HMODULE handle = GetModuleHandle(NULL);
	if(handle)
	{
		return new WindowsLibrary(handle, true);
	}
	else
	{
		return nullptr;
	}
}

JS::LibraryLoader JS::LibraryLoader::Host;
#else
namespace JS
{
	class POSIXLibrary : public Library
	{
	public:
		void * handle = nullptr;
		bool main = false;

		POSIXLibrary(void * handle, bool main)
			: handle(handle), main(main)
		{
		}

		void Close() override
		{
			if(main && handle)
			{
				dlclose(handle);
				handle = nullptr;
			}
		}

		void * LoadAddress(std::string name) override
		{
			return reinterpret_cast<void *>(dlsym(handle, name.c_str()));
		}
	};
}

JS::Library * JS::LibraryLoader::Open(std::string name)
{
	void * handle = dlopen(name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
	if(handle)
	{
		return new POSIXLibrary(handle, false);
	}
	else
	{
		return nullptr;
	}
}

JS::Library * JS::LibraryLoader::LoadMain()
{
	void * handle = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
	if(handle)
	{
		return new POSIXLibrary(handle, true);
	}
	else
	{
		return nullptr;
	}
}

JS::LibraryLoader JS::LibraryLoader::Host;
#endif

