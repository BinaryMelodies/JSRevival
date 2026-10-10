#ifndef __MODULES_HH
#define __MODULES_HH

/* Types and routines needed to manage multiple modules */

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "common.hh"
#include "macros.hh"
#include "runtime.hh"
#include "promise.hh"

namespace JS
{
	namespace Runtime
	{
		/** Represents a single JavaScript module, usually corresponding to a source file
		 *
		 * ECMAScript 6 introduced the concept of (classic) scripts and module (scripts), and this abstract base class
		 * represents their common features. A typical classic script would be of type ScriptRecord and an ES6 module
		 * of type SourceTextModuleRecord.
		 *
		 * This project further generalizes the module concept to also incorporate ECMAScript 4 packages. For packages,
		 * the file-to-package correspondence is not one-to-one: a single package file might contain multiple
		 * SourceTextPackageRecords and a package might consist of multiple SourceTextPackageRecords.
		 */
		class ScriptOrModuleRecord : public gc_cleanup
		{
		public:
			std::optional<std::filesystem::path> sourcefilename;
			ScriptOrModuleRecord(std::optional<std::filesystem::path> filename = std::optional<std::filesystem::path>{})
				: sourcefilename(filename)
			{
			}

			virtual std::set<LoadedModuleRequestRecord> * GetLoadedModules();

			/** Completely initialize a module, including verifying, executing, and any other operations required
			 *
			 * This method is not described by the ECMAScript standards, but simplifies handling various types of
			 * scripts.
			 */
			virtual JSValue Perform() = 0;
		};

#if ENABLE_ES4
		class PackageRecord;
#endif

		class SyntheticModuleRecord;

		/** A record that represents a classic ECMAScript script (in particular, all pre-ECMAScript 6 files would use these) */
		class ScriptRecord : public ScriptOrModuleRecord
		{
		public:
			Runtime::Realm * Realm;
			JS::ParseNode::ParseNode * Code;
			std::set<LoadedModuleRequestRecord> LoadedModules;
#if ENABLE_ES4
			/** Package records corresponding to the package statements that appear inside the script, only used for ECMAScript 4 */
			std::vector<PackageRecord *> Packages;
#endif
			/** The module object of a CommonJS module */
			JSObject CommonJSModule = nullptr;
			/** Set when the module contains AMD 'define' calls */
			bool UsesAMD = false;

			/** Imports a CommonJS module using the import statement */
			SyntheticModuleRecord * SyntheticModule = nullptr;

			ScriptRecord(Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code)
				: Realm(Realm), Code(Code)
			{
			}

			ScriptRecord(std::optional<std::filesystem::path> filename, Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code)
				: ScriptOrModuleRecord(filename), Realm(Realm), Code(Code)
			{
			}

#if ENABLE_ES4
			ScriptRecord(Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code, std::vector<PackageRecord *> Packages)
				: Realm(Realm), Code(Code), Packages(Packages)
			{
			}

			ScriptRecord(std::optional<std::filesystem::path> filename, Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code, std::vector<PackageRecord *> Packages)
				: ScriptOrModuleRecord(filename), Realm(Realm), Code(Code), Packages(Packages)
			{
			}
#endif

			std::set<LoadedModuleRequestRecord> * GetLoadedModules() override;

			/** Evaluates the script (and for ECMAScript 4, all included packages) */
			JSValue Perform() override;

			SyntheticModuleRecord * GetSyntheticModule();
		};

		class CyclicModuleRecord;
		struct GraphLoadingState : public JS::Runtime::Host::Payload, public std::enable_shared_from_this<GraphLoadingState>
		{
			JS::Runtime::PromiseCapability<JS::Class::Promise> PromiseCapability;
			JSBoolean IsLoading = true;
			uint64_t PendingModulesCount = 1;
			std::set<CyclicModuleRecord *> Visited;

			GraphLoadingState();

			void NormalCompletion(ModuleRecord * moduleRecord) override;
			void ThrowCompletion(JSValue error_result) override;
		};

		class SourceTextModuleRecord;

		/** The abstract base class for an ECMAScript 6 module */
		class ModuleRecord : public ScriptOrModuleRecord
		{
		public:
			Runtime::Realm * Realm;
			std::shared_ptr<JS::Runtime::ModuleEnvironmentRecord> Environment = nullptr;
			JSObject Namespace = nullptr;

			ModuleRecord(Runtime::Realm * Realm, std::optional<std::filesystem::path> filename = std::optional<std::filesystem::path>{})
				: ScriptOrModuleRecord(filename), Realm(Realm)
			{
			}

			struct ResolvedBinding
			{
				ModuleRecord * Module;
				std::optional<JSString> BindingName;
			};

			struct ResolveRecord
			{
				ModuleRecord * Module;
				JSString ExportName;

				bool operator ==(const ResolveRecord& other) const
				{
					return Module == other.Module && ExportName == other.ExportName;
				}

				bool operator <(const ResolveRecord& other) const
				{
					return Module < other.Module || (Module == other.Module && ExportName < other.ExportName);
				}
			};

			struct Null { };
			struct Ambiguous { };

			/** Loads all dependencies of the module */
			virtual gcn::gc_ptr<JS::Class::Promise> LoadRequestedModules() = 0;

			/** Recursively loads the module and all its dependencies */
			virtual void InnerModuleLoading(std::shared_ptr<GraphLoadingState> state);

			/** Returns a collection of the string values of all exported names, including "default" for default exports */
			virtual std::set<JSString> GetExportedNames(std::set<SourceTextModuleRecord *>& exportStarSet) = 0;
			std::set<JSString> GetExportedNames()
			{
				std::set<SourceTextModuleRecord *> exportStarSet;
				return GetExportedNames(exportStarSet);
			}

			/** Tries to resolve a name that is exported by this module and return a corresponding binding */
			virtual std::variant<ResolvedBinding, Null, Ambiguous> ResolveExport(JSString exportName, std::set<ResolveRecord>& resolveSet) = 0;
			std::variant<ResolvedBinding, Null, Ambiguous> ResolveExport(JSString exportName)
			{
				std::set<ResolveRecord> resolveSet;
				return ResolveExport(exportName, resolveSet);
			}
			/** Resolves all imports and initializes exported function fields */
			virtual void Link() = 0;
			/** Recursively links the module and all its dependencies */
			virtual uint32_t InnerModuleLinking(std::vector<CyclicModuleRecord *>& stack, uint32_t index);
			/** Returns a promise for the evaluation of this module */
			virtual gcn::gc_ptr<JS::Class::Promise> Evaluate() = 0;
			/** Evaluates the module recursively and all its dependencies */
			virtual uint32_t InnerModuleEvaluation(std::vector<CyclicModuleRecord *>& stack, uint32_t index);
			/** Returns and possibly creates a module namespace exotic object */
			virtual JSObject GetModuleNamespace();

			/** Loads, initializes and executes the module fully, including all its dependencies
			 *
			 * This method performs LoadRequestedModules(), Link() and Evaluate() in a sequence.
			 *
			 * @return Returns undefined.
			 */
			JSValue Perform() override;
		};

		/** An abstract class to represent ECMAScript 6 modules that can participate in import cycles */
		class CyclicModuleRecord : public ModuleRecord
		{
		public:
			/** Represents the current status of the module within its loading process */
			enum class StatusType
			{
				/** The module is parsed, loaded into memory, waiting for its dependencies to also get loaded
				 *
				 * This is the initial state for a freshly loaded module.
				 * Once all its dependencies are successfully loaded, it will transition to state Unlinked.
				 */
				New,
				/** The module and all of its dependencies are loaded into memory, waiting for linking 
				 *
				 * When a linking module depends on this module, it will transition to the state Linking.
				 */
				Unlinked,
				/** The module is in the process of being linked, waiting for its dependencies
				 *
				 * On success, the module transitions to state Linked, on failure it goes back to Unlinked along
				 * with all modules depending on it.
				 */
				Linking,
				/** The module and all its dependencies have been linked, ready for evaluation
				 *
				 * Being linked means that imports are resolved and the exported names collected.
				 * Once a module depending on this begins evaluation, it will transition to state Evaluating.
				 */
				Linked,
				/** The module is in the process of being and having its dependencies evaluated
				 *
				 * If the evaluation is synchronous (i.e. no top-level await), it will eventually transition
				 * to state Evaluated, either successfully or through a failure. For asynchronous evaluation,
				 * it will transition to EvaluatingAsync.
				 */
				Evaluating,
				/** The module and its dependencies are being evaluated asynchronously with a promise available
				 *
				 * Once the evaluation resolves, it will transition to Evaluated, either successfully or
				 * through a failure.
				 */
				EvaluatingAsync,
				/** The module has finished evaluating, either successfully or with an error
				 *
				 * This is the final state of a module and it will not change once it transitions here.
				 */
				Evaluated,
			};
			StatusType Status = StatusType::New;
			std::optional<JSValue> EvaluationError;
			std::optional<uint32_t> DFSIndex;
			std::optional<uint32_t> DFSAncestorIndex;
			std::vector<ModuleRequestRecord> RequestedModules;
			std::set<LoadedModuleRequestRecord> LoadedModules;
			CyclicModuleRecord * CycleRoot = nullptr;
			bool HasTLA = false;

			struct Unset { };
			struct Done { };
			std::variant<Unset, uint32_t, Done> AsyncEvaluationOrder = Unset{};
			std::optional<JS::Runtime::PromiseCapability<JS::Class::Promise>> TopLevelCapability;
			std::vector<CyclicModuleRecord *> AsyncParentModules;
			std::optional<uint32_t> PendingAsyncDependencies;

			CyclicModuleRecord(Runtime::Realm * Realm, bool HasTLA, std::vector<ModuleRequestRecord> RequestedModules, std::optional<std::filesystem::path> filename = std::optional<std::filesystem::path>{})
				: ModuleRecord(Realm, filename), RequestedModules(RequestedModules), HasTLA(HasTLA)
			{
			}

			ModuleRecord * GetImportedModule(ModuleRequestRecord record);

			std::set<LoadedModuleRequestRecord> * GetLoadedModules() override;

			gcn::gc_ptr<JS::Class::Promise> LoadRequestedModules() override;
			void InnerModuleLoading(std::shared_ptr<GraphLoadingState> state) override;

			void Link() override;
			uint32_t InnerModuleLinking(std::vector<CyclicModuleRecord *>& stack, uint32_t index) override;

			virtual void InitializeEnvironment() = 0;

			gcn::gc_ptr<JS::Class::Promise> Evaluate() override;
			uint32_t InnerModuleEvaluation(std::vector<CyclicModuleRecord *>& stack, uint32_t index) override;
			void ExecuteAsyncModule();
			virtual void ExecuteModule(std::optional<JS::Runtime::PromiseCapability<JS::Class::Promise>> promise_capability = {}) = 0;

			struct AncestorAsyncOrder
			{
				bool operator()(CyclicModuleRecord * const& lhs, CyclicModuleRecord * const& rhs) const
				{
					return std::get<uint32_t>(lhs->AsyncEvaluationOrder) < std::get<uint32_t>(rhs->AsyncEvaluationOrder);
				}
			};

			std::set<CyclicModuleRecord *, AncestorAsyncOrder> GatherAvailableAncestors();
			void GatherAvailableAncestors(std::set<CyclicModuleRecord *, AncestorAsyncOrder>& exec_set);
			void AsyncModuleExecutionFulfilled();
			void AsyncModuleExecutionRejected(JSValue error);
		};

		/** An ECMAScript 6 module that is loaded from an ECMAScript source file */
		class SourceTextModuleRecord : public CyclicModuleRecord
		{
		public:
			JS::ParseNode::ParseNode * Code;
			ExecutionContext * Context;
			JSObject ImportMeta;
			std::vector<ImportEntry> ImportEntries;
			std::vector<ExportEntry> LocalExportEntries;
			std::vector<ExportEntry> IndirectExportEntries;
			std::vector<ExportEntry> StarExportEntries;

			SourceTextModuleRecord(
				Runtime::Realm * Realm,
				JS::ParseNode::ParseNode * Code,
				std::vector<ModuleRequestRecord> RequestedModules,
				std::vector<ImportEntry> ImportEntries,
				std::vector<ExportEntry> LocalExportEntries,
				std::vector<ExportEntry> IndirectExportEntries,
				std::vector<ExportEntry> StarExportEntries)
			:
				CyclicModuleRecord(Realm, /* HasTLA = */ false, RequestedModules), // HasTLA is initialized later
				Code(Code),
				ImportEntries(ImportEntries),
				LocalExportEntries(LocalExportEntries),
				IndirectExportEntries(IndirectExportEntries),
				StarExportEntries(StarExportEntries)
			{
			}

			SourceTextModuleRecord(
				std::optional<std::filesystem::path> filename,
				Runtime::Realm * Realm,
				JS::ParseNode::ParseNode * Code,
				std::vector<ModuleRequestRecord> RequestedModules,
				std::vector<ImportEntry> ImportEntries,
				std::vector<ExportEntry> LocalExportEntries,
				std::vector<ExportEntry> IndirectExportEntries,
				std::vector<ExportEntry> StarExportEntries)
			:
				CyclicModuleRecord(Realm, /* HasTLA = */ false, RequestedModules, filename), // HasTLA is initialized later
				Code(Code),
				ImportEntries(ImportEntries),
				LocalExportEntries(LocalExportEntries),
				IndirectExportEntries(IndirectExportEntries),
				StarExportEntries(StarExportEntries)
			{
			}

			std::set<JSString> GetExportedNames(std::set<SourceTextModuleRecord *>& exportStarSet) override;
			using ModuleRecord::ResolveExport;
			std::variant<ResolvedBinding, Null, Ambiguous> ResolveExport(JSString exportName, std::set<ResolveRecord>& resolveSet) override;
			void InitializeEnvironment() override;
			void ExecuteModule(std::optional<JS::Runtime::PromiseCapability<JS::Class::Promise>> promise_capability = {}) override;
		};

		class SyntheticModuleRecord : public ModuleRecord
		{
		public:
			std::set<JSString> ExportNames;
			std::function<void(SyntheticModuleRecord *)> EvaluationSteps;

			SyntheticModuleRecord(Runtime::Realm * Realm, std::set<JSString> ExportNames, std::function<void(SyntheticModuleRecord *)> EvaluationSteps, std::optional<std::filesystem::path> filename = std::optional<std::filesystem::path>{})
				: ModuleRecord(Realm, filename), ExportNames(ExportNames), EvaluationSteps(EvaluationSteps)
			{
			}

			void SetSyntheticModuleExport(JSString exportName, JSValue exportValue);

			gcn::gc_ptr<JS::Class::Promise> LoadRequestedModules() override;
			std::set<JSString> GetExportedNames(std::set<SourceTextModuleRecord *>& exportStarSet) override;
			std::variant<ResolvedBinding, Null, Ambiguous> ResolveExport(JSString exportName, std::set<ResolveRecord>& resolveSet) override;
			void Link() override;
			gcn::gc_ptr<JS::Class::Promise> Evaluate() override;
		};

#if ENABLE_ES4
		/** Abstract base class of ECMAScript 4 package modules.
		 *
		 * Note that a single file might contain multiple package records, and a package might be composed of multiple package records as well.
		 */
		class PackageRecord : public ScriptOrModuleRecord
		{
		public:
			enum class StateType
			{
				Unloaded,
				Loading,
				Loaded,
			};
			StateType State = StateType::Unloaded;

			// by analogy with ModuleRecord
			Runtime::Realm * Realm;
			// by analogy with ModuleRecord
			std::shared_ptr<JS::Runtime::GlobalEnvironmentRecord> Environment = nullptr;
			JS::Class::Package * Package = nullptr;
			ExecutionContext * Context = nullptr;

			PackageRecord(Runtime::Realm * Realm, std::optional<std::filesystem::path> filename = std::optional<std::filesystem::path>{})
				: ScriptOrModuleRecord(filename), Realm(Realm)
			{
			}
		};

		/** An ECMAScript 4 package module that was loaded from an ECMAScript 4 file */
		class SourceTextPackageRecord : public PackageRecord
		{
		public:
			JS::ParseNode::ParseNode * Code;

			SourceTextPackageRecord(Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code)
				: PackageRecord(Realm), Code(Code)
			{
			}

			SourceTextPackageRecord(std::optional<std::filesystem::path> filename, Runtime::Realm * Realm, JS::ParseNode::ParseNode * Code)
				: PackageRecord(Realm, filename), Code(Code)
			{
			}

			JSValue Perform() override;
		};

		class PackageElement;

		// information on accessing a module in the file
		struct PackageLookupPath
		{
		public:
			std::filesystem::path SystemPath = ".";
			// portion of the path afterwards
			std::vector<JSString> MemberPath;

			struct const_iterator
			{
				// path/m0/m1.../m2.m3...
				const PackageLookupPath& LookupPath;
				size_t slash_count;
				size_t dot_count;
				bool end;

				const_iterator(const PackageLookupPath& LookupPath, bool end = false)
					:
						LookupPath(LookupPath),
						slash_count(end || LookupPath.MemberPath.size() == 0 ? 0 : 1),
						dot_count(end || LookupPath.MemberPath.size() == 0 ? 0 : LookupPath.MemberPath.size() - 1),
						end(end)
				{
				}

				// prefix
				const_iterator& operator ++()
				{
					// first look up filenames of the form A.$ (where $ is only allowed to contain dots)
					// then look through filenames of the form A/$
					// finally, return A
					if(dot_count > 1)
					{
						dot_count --;
					}
					else if(slash_count < LookupPath.MemberPath.size())
					{
						slash_count ++;
						dot_count = LookupPath.MemberPath.size() - slash_count;
					}
					else if(slash_count != 0)
					{
						slash_count = 0;
						dot_count = 0;
					}
					else
					{
						end = true;
					}
					return *this;
				}

				bool operator ==(const const_iterator& other) const
				{
					// assumes they are pointing to the same PackageLookupPath object
					return
						end ? other.end :
							!other.end && slash_count == other.slash_count && dot_count == other.dot_count;
				}

				bool operator !=(const const_iterator& other) const
				{
					return !(*this == other);
				}

				std::filesystem::path operator *()
				{
					std::filesystem::path path = LookupPath.SystemPath;
					size_t index;
					for(index = 0; index < slash_count; index ++)
					{
						path /= LookupPath.MemberPath[index];
					}
					for(; index < slash_count + dot_count; index ++)
					{
						path += ".";
						path += LookupPath.MemberPath[index];
					}
					return path;
				}
			};

			const_iterator begin() const
			{
				return const_iterator(*this);
			}

			const_iterator end() const
			{
				return const_iterator(*this, true);
			}
		};

		/** Represents an entry in the ECMAScript 4 package namespace lookup hierarchy
		 *
		 * Packages form a hierarchy, starting from the root, with each package possibly containing subpackages
		 * This class encapsulates behavior required to look up members of an entry in the hierarcy
		 */
		// an entry in the package namespace lookup hierarchy
		class PackageContent
		{
		public:
			std::map<JSString, std::shared_ptr<PackageElement>> Members;

			enum class LookupMode
			{
				// only checks if it is already loaded or not
				Peek,
				// returns package object or member
				Transient,
				// returns closed package object or member
				Closed, // TODO: not used currently
				// returns open package object; if it does not exist, it is a open package (meaning its actual members are spread across several files)
				Open,
				// also loads from file
				Load,
			};

			std::optional<PackageElement *> GetMember(std::vector<JSString> PathName, LookupMode mode = LookupMode::Closed, size_t Level = 0);
			void SetMemberPath(std::vector<JSString> PathName, std::filesystem::path path, size_t Level = 0);
			void GetMemberLookupPath(std::vector<JSString> PathName, PackageLookupPath& LookupPath, size_t Level = 0);
		};

		/** Represents an ECMAScript 4 package or file (as part of a open package) */
		class PackageElement : public PackageContent, public PackageImportRecord
		{
		public:
			// a member can either be a package or a definition element (only as part of a open package)
			//
			std::optional<std::filesystem::path> SourceName;

			PackageElement(std::optional<std::filesystem::path> SourceName, PackageRecord * Access = nullptr)
				: PackageImportRecord(Access), SourceName(SourceName)
			{
			}

			PackageElement(PackageRecord * Access)
				: PackageImportRecord(Access), SourceName()
			{
			}

			PackageElement(JSPackage Access = nullptr)
				: PackageImportRecord(Access), SourceName()
			{
			}

			std::optional<PackageElement *> GetObject(std::vector<JSString> PathName, LookupMode mode = LookupMode::Closed);
			// returns true if lookup is finished at this stage, no further levels need to be read
			bool GetObjectLookupPath(std::vector<JSString> PathName, PackageLookupPath& LookupPath, size_t Level = 0);
		};
#endif

#if 0
		/** A class to represent a transport module: a module that is defined within some script but not directly associated with the script */
		class TransportModuleRecord : public ScriptOrModuleRecord
		{
		public:
			/* script this module originates from */
			ScriptRecord * Script;

			TransportModuleRecord(ScriptRecord * Script)
				: ScriptOrModuleRecord(Script ? std::optional<std::filesystem::path>{Script->sourcefilename} : std::optional<std::filesystem::path>{} ), Script(Script)
			{
			}

			std::set<LoadedModuleRequestRecord> * GetLoadedModules() override;
		};
#endif

		/** A class to represent an asynchronous transport module definition: a module that is defined within some script but not directly associated with the script */
		class AsynchronousModuleRecord : public ScriptOrModuleRecord
		{
		public:
			/* script this module originates from */
			ScriptRecord * Script;
			JSString Identifier;
			enum class LoadState
			{
				Unloaded,
				Fetched,
				Initialized,
			};
			LoadState IsInitialized = LoadState::Unloaded;
			std::vector<JSString> Dependencies;
			JSValue Factory = JS_undefined;
			enum class EnabledState
			{
				Disabled,
				Enabling,
				Enabled,
			};
			EnabledState IsEnabled = EnabledState::Disabled;
			enum class DefinedState
			{
				Undefined,
				Defining,
				Emitting,
				Complete,
			};
			DefinedState IsDefined = DefinedState::Undefined;
			uint32_t DependencyCount = 0;
			std::map<JSString, JSValue> DependencyExports;
			JSValue Exports = JS_undefined;
			JSValue Module = JS_undefined;
			bool UsingExports = false;

			struct OnDefinedCallback
			{
				AsynchronousModuleRecord * DependentModule;
				JSString DependencyName;
			};
			std::vector<OnDefinedCallback> OnDefinedCallbacks;

			AsynchronousModuleRecord(JSString Identifier)
//				: TransportModuleRecord(nullptr), Identifier(Identifier)
				: ScriptOrModuleRecord(), Script(nullptr), Identifier(Identifier)
			{
			}

			void Initialize(AsynchronousModuleDefinition definition);
			void Fetch();
			void Check();
			void Enable();
			void ExpectedDependencyBy(AsynchronousModuleRecord * DependentModule, JSString DependencyName);

			std::set<LoadedModuleRequestRecord> * GetLoadedModules() override;
			JSValue Perform() override;
		};

		/** Provides a default implementation for host behavior */
		class DefaultHost : public Host
		{
		public:
#if ENABLE_ES4
			// ECMAScript 4 packages
			PackageContent ES4Packages;
			PackageContent ES4StringPackages;

			virtual std::vector<JSString> GetES4FullPackageName(std::filesystem::path sourcefilename);
			virtual bool IsES4PackageOpen(std::filesystem::path sourcefilename, std::vector<JSString> PathName);

			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename);
			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename, JSString StringValue);
			virtual JSString GetES4PackageName(std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName);

			virtual void SetES4Package(std::filesystem::path sourcefilename, JSString StringValue);
			virtual void SetES4Package(std::filesystem::path sourcefilename, std::vector<JSString> PathName);

			// accesses (and possibly creates) a package import record
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, bool open = false);
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, JSString StringValue, bool open = false);
			virtual PackageImportRecord& GetES4Package(std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName, bool open = false);

			// accesses (and if needed, loads) a package
			virtual PackageImportRecord& LocateES4Package(JSString StringValue);
			virtual PackageImportRecord& LocateES4Package(std::vector<JSString> PathName);
#endif

			// unifies CommonJS modules and ECMAScript 6 modules for interchangeability
			// CommonJS expects a module identifier as a String and returns a ScriptRecord
			// the identifier can be converted to a ModuleRequestRecord with no attributes
			// ECMAScript 6 returns a ModuleRecord
			std::map<ModuleRequestRecord, JS::Runtime::ScriptOrModuleRecord *> LoadedModules;

			// import map for module names
			std::map<JSString, JSString> ImportMap;
			// import map for module prefixes
			std::map<JSString, JSString> ImportPrefixMap;

			JSString NormalizeImportPath(JSString path);
			JSString MapImportPath(JSString path);
			JSValue LoadRequiredModule(JSString path) override;
			void DeclareDefine(AsynchronousModuleDefinition definition) override;

			bool IsSupportedImportAttribute(JSString attribute) override;

			void LoadImportedModule(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload) override;

			// AMD specific
			// TODO: make it thread safe
			std::vector<AsynchronousModuleDefinition> DefinitionQueue;
			std::vector<JSString> RequiredQueue;

			void LoadDefinedModuleDependency(JSString identifier) override;
			void FinishLoadingDefinedModule() override;
			void EnableDefinedModule(JSString identifier) override;

		private:
			bool inCheckDefinedModulesLoaded = false;
			void BreakDefinedModuleCycles(AsynchronousModuleRecord * module, std::set<AsynchronousModuleRecord *>& traced, std::set<AsynchronousModuleRecord *>& processed);
			void BreakDefinedModuleCycles(AsynchronousModuleRecord * module, AsynchronousModuleRecord * dependency, std::set<AsynchronousModuleRecord *>& traced, std::set<AsynchronousModuleRecord *>& processed);
		public:
			/** Checks whether loading is blocked by something, corresponds to RequireJS checkLoaded() function */
			void CheckDefinedModulesLoaded();

			// https://html.spec.whatwg.org/multipage/webappapis.html#module-type-allowed
			enum class ModuleType
			{
				Default,
				Script,
				JSON,
				Text,
				Style, // CSS
			};

			enum class ScriptType
			{
				ECMAScript,
				NodeAPI,
				WebAssembly,
			};

			JS::Runtime::SyntheticModuleRecord * LoadJSONFileAsModule(std::filesystem::path host_path);
			JS::Runtime::SyntheticModuleRecord * LoadTextFileAsModule(std::filesystem::path host_path);

			/** @brief Loads the specified module, providing an interchange between CommonJS and ECMASCript 6 modules
			 *
			 * This is the main function that loads modules from the file system or internal store and completes any required
			 * initialization.
			 *
			 * CommonJS modules will be fully loaded and their script record returned.
			 *
			 * ECMAScript 6 modules will be loaded and if async is set to false, fully evaluated, then their module record returned.
			 * Note that this method should only be called with async set to false as part of the CommonJS loading process and it
			 * might break when loading asynchronous modules.
			 *
			 * @param moduleRequest The request that fully identifies the module.
			 * @param async Set to false to force the full initialization and evaluation of the loaded module, even for ECMAScript 6 modules.
			 */
			JS::Runtime::ScriptOrModuleRecord * LoadInterchangeModule(ModuleRequestRecord moduleRequest, bool async = true);
		};
	}
}

#endif // __MODULES_HH
