#ifndef _MACROS_HH
#define _MACROS_HH

/* Common macros to simplify writing code */

/* ES4 permits accessing elements through namespaces and access types (read/write) */
#if ENABLE_ES4
// nomenclature: macros FOR_* and AND_* are optional arguments, VIA_* and WITH_* are for passing arguments
# define __ES4_ONLY(...) __VA_ARGS__
# define AND_ACCESS_SET(__accesses) , [[maybe_unused]] ::JS::AccessSet __accesses
# define WITH_ACCESS_SET(__accesses) , __accesses
# define AND_NAMESPACE(__namespace) , [[maybe_unused]] gcn::gc_ptr<::JS::Class::Namespace> __namespace
# define WITH_NAMESPACE(__namespace) , __namespace
# define AND_NAMESPACES(__namespaces) , [[maybe_unused]] std::set<gcn::gc_ptr<::JS::Class::Namespace>> __namespaces
# define WITH_NAMESPACES(__namespaces) , __namespaces
# define AND_CLASS(__limit) , [[maybe_unused]] JSClass __limit
# define WITH_CLASS(__limit) , __limit
# define FOR_CLASS(__limit) [[maybe_unused]] JSClass __limit
# define VIA_CLASS(__limit) __limit
# define FOR_PHASE(__phase) [[maybe_unused]] ::JS::Runtime::ExecutionPhase __phase
# define VIA_PHASE(__phase) __phase
# define AND_PHASE(__phase) , [[maybe_unused]] ::JS::Runtime::ExecutionPhase __phase
# define WITH_PHASE(__phase) , __phase
# define AND_ENVIRONMENT(__env) , [[maybe_unused]] std::shared_ptr<::JS::Runtime::EnvironmentRecord> __env
# define WITH_ENVIRONMENT(__env) , __env
# define INDEXES(__indexes) __indexes
# define WITH_TYPE(__type) , __type
# define AND_FLAG(__flag) , [[maybe_unused]] bool __flag
# define WITH_FLAG(__flag) , __flag
#else
# define __ES4_ONLY(...)
# define AND_ACCESS_SET(__accesses)
# define WITH_ACCESS_SET(__accesses)
# define AND_NAMESPACE(__namespace)
# define WITH_NAMESPACE(__namespace)
# define AND_NAMESPACES(__namespaces)
# define WITH_NAMESPACES(__namespaces)
# define AND_CLASS(__limit)
# define WITH_CLASS(__limit)
# define FOR_CLASS(__limit)
# define VIA_CLASS(__limit)
# define FOR_PHASE(__phase)
# define VIA_PHASE(__phase)
# define AND_PHASE(__phase)
# define WITH_PHASE(__phase)
# define AND_ENVIRONMENT(__env)
# define WITH_ENVIRONMENT(__env)
# define INDEXES(__indexes) (__indexes)[0]
# define WITH_TYPE(__type)
# define AND_FLAG(__flag)
# define WITH_FLAG(__flag)
#endif

//#define __ES4_ONLY_ARG(...) __ES4_ONLY(, __VA_ARGS__)

#define PUBLIC_NAMESPACE WITH_NAMESPACE(::JS::Public::JS_public)
#define PUBLIC_NAMESPACES WITH_NAMESPACES(std::set<gcn::gc_ptr<::JS::Class::Namespace>>{::JS::Public::JS_public})
#define READ_ACCESS WITH_ACCESS_SET(::JS::AccessSet::Read)
#define WRITE_ACCESS WITH_ACCESS_SET(::JS::AccessSet::Write)
#define READ_WRITE_ACCESS WITH_ACCESS_SET(::JS::AccessSet::ReadWrite)
#define RUNTIME_PHASE WITH_PHASE(::JS::Runtime::ExecutionPhase::Run)
#define VIA_RUNTIME_PHASE __ES4_ONLY(::JS::Runtime::ExecutionPhase::Run)
#define COMPILE_TIME_PHASE WITH_PHASE(::JS::Runtime::ExecutionPhase::Compile)
#define VIA_COMPILE_TIME_PHASE __ES4_ONLY(::JS::Runtime::ExecutionPhase::Compile)
#define NO_ENVIRONMENT WITH_ENVIRONMENT(nullptr)
//#define RESOLVE_PROPERTY __ES4_ONLY(.ResolveProperty())
//#define RESOLVE_PROPERTY_ACCESS __ES4_ONLY(.ResolvePropertyAccess())

#define __STR2(__arg) #__arg
#define __STR(__arg) __STR2(__arg)
#define __LOC(...) (__VA_ARGS__ u" {" __FILE__ ":" __STR(__LINE__) "} ")

#endif // _MACROS_HH
