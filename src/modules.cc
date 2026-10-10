
#include <fstream>

#include "common.hh"
#include "macros.hh"
#include "execute.hh"
#include "environment.hh"
#include "modules.hh"
#include "parser.hh"
#include "napi.hh"

using namespace JS::Public;
using namespace JS::Public::ErrorClasses;

//// ScriptOrModuleRecord

std::set<JS::Runtime::LoadedModuleRequestRecord> * JS::Runtime::ScriptOrModuleRecord::GetLoadedModules()
{
	return nullptr;
}

//// ScriptRecord

std::set<JS::Runtime::LoadedModuleRequestRecord> * JS::Runtime::ScriptRecord::GetLoadedModules()
{
	return &LoadedModules;
}

JSValue JS::Runtime::ScriptRecord::Perform()
{
#if ENABLE_ES4
	if(IsVersionES4())
	{
		for(auto Package : Packages)
		{
			gcn::gc_ptr<JS::Class::Package> GlobalObject = JS::New<JS::Class::Package>();
			Package->Realm->InitializeGlobalObject(GlobalObject);
			auto GlobalEnv = std::make_shared<JS::Runtime::GlobalEnvironmentRecord>(GlobalObject, GlobalObject);

			Package->Context = new JS::Runtime::ExecutionContext();
			Package->Context->Function = nullptr;
			Package->Context->Realm = Package->Realm;
			Package->Context->ScriptOrModule = Package;
			Package->Context->VariableEnvironment = GlobalEnv;
			Package->Context->LexicalEnvironment = GlobalEnv;
			Package->Context->PrivateEnvironment = nullptr;
			JS::Runtime::ExecutionContext::GetContextStack().push_back(Package->Context);

			if(auto source = dynamic_cast<SourceTextPackageRecord *>(Package)) // TODO: otherwise?
			{
				JS::Runtime::ValidationContext cxt;
				//source->Code->Validate(cxt);

				JS::Interpreter::Code * code = new (GC) JS::Interpreter::Code;
				JS::Interpreter::State<> state = code->Begin();
				//Code->CompileStatement(state);

				source->Code->Process(cxt, state);

				Package->Context->code = code;
				Package->Context->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
			}

			JS::Runtime::ExecutionContext::GetContextStack().pop_back();
		}
	}
#endif

	JS::Runtime::ExecutionContext * scriptContext = new JS::Runtime::ExecutionContext();
	scriptContext->Function = nullptr;
	scriptContext->Realm = Realm;
	scriptContext->ScriptOrModule = this;
	scriptContext->VariableEnvironment = Realm->GlobalEnv;
	scriptContext->LexicalEnvironment = Realm->GlobalEnv;
	scriptContext->PrivateEnvironment = nullptr;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(scriptContext);

	JS::Host->SetupWrappers();

	JS::Runtime::ValidationContext cxt;
	//Code->Validate(cxt);

	JS::Interpreter::Code code;
	JS::Interpreter::State<> state = code.Begin();
	//Code->CompileStatement(state);

	Code->Process(cxt, state);

	scriptContext->code = &code;
	scriptContext->code_pointer = 0;
	scriptContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	std::optional<JSValue> value = JS::Runtime::ExecutionContext::ExecuteAndReturn<std::optional<JSValue>>();

	if(JS::Enable_AMD)
	{
		JS::Host->FinishLoadingDefinedModule();
	}

	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	return value.value_or(JS_undefined);
}

JS::Runtime::SyntheticModuleRecord * JS::Runtime::ScriptRecord::GetSyntheticModule()
{
	if(SyntheticModule == nullptr && CommonJSModule != nullptr)
	{
		SyntheticModule = new SyntheticModuleRecord(Realm, {u"default"}, std::function<void(SyntheticModuleRecord *)>(
			[this](SyntheticModuleRecord * module)
		{
			module->SetSyntheticModuleExport(u"default", JS::Get(CommonJSModule, u"exports"));
		}), sourcefilename);
	}
	return SyntheticModule;
}

//// GraphLoadingState

JS::Runtime::GraphLoadingState::GraphLoadingState()
	: PromiseCapability(JS::Runtime::PromiseCapability<JS::Class::Promise>(JS::Intrinsics()->Promise))
{
}

void JS::Runtime::GraphLoadingState::NormalCompletion(ModuleRecord * moduleRecord)
{
	moduleRecord->InnerModuleLoading(shared_from_this());
}

void JS::Runtime::GraphLoadingState::ThrowCompletion(JSValue error_result)
{
	if(IsLoading)
	{
		IsLoading = false;
		JS::Call(PromiseCapability.Reject, JS_undefined, { error_result } RUNTIME_PHASE);
	}
}

//// ModuleRecord

void JS::Runtime::ModuleRecord::InnerModuleLoading(std::shared_ptr<GraphLoadingState> state)
{
	if(state->PendingModulesCount < 1)
	{
		JS::Throw(JS::New<InternalError> __LOC());
	}

	state->PendingModulesCount --;

	if(state->PendingModulesCount == 0)
	{
		state->IsLoading = false;
		for(auto module : state->Visited)
		{
			if(auto cyclic_module = dynamic_cast<CyclicModuleRecord *>(module))
			{
				if(cyclic_module->Status == CyclicModuleRecord::StatusType::New)
				{
					cyclic_module->Status = CyclicModuleRecord::StatusType::Unlinked;
				}
			}
		}
		JS::Call(state->PromiseCapability.Resolve, JS_undefined, {JS_undefined} RUNTIME_PHASE);
	}
}

uint32_t JS::Runtime::ModuleRecord::InnerModuleLinking([[maybe_unused]] std::vector<CyclicModuleRecord *>& stack, uint32_t index)
{
	Link();
	return index;
}

uint32_t JS::Runtime::ModuleRecord::InnerModuleEvaluation([[maybe_unused]] std::vector<CyclicModuleRecord *>& stack, uint32_t index)
{
	auto promise = Evaluate();
	assert(promise->PromiseState != JS::Class::Promise::StateType::Pending);
	if(promise->PromiseState == JS::Class::Promise::StateType::Rejected)
	{
		JS::Throw(promise->PromiseResult.value());
	}
	return index;
}

JSObject JS::Runtime::ModuleRecord::GetModuleNamespace()
{
	if(Namespace == nullptr)
	{
		std::set<JSString> exportedNames = GetExportedNames();
		std::set<JSString> unambiguousNames;
		for(auto name : exportedNames)
		{
			if(std::holds_alternative<ResolvedBinding>(ResolveExport(name)))
			{
				unambiguousNames.insert(name);
			}
		}
		Namespace = JS::New<JS::Class::ModuleNamespace>(this, unambiguousNames);
	}
	return Namespace;
}

JSValue JS::Runtime::ModuleRecord::Perform()
{
	LoadRequestedModules();
	Link();
	Evaluate();
	return JS_undefined;
}

//// CyclicModuleRecord

std::set<JS::Runtime::LoadedModuleRequestRecord> * JS::Runtime::CyclicModuleRecord::GetLoadedModules()
{
	return &LoadedModules;
}

JS::Runtime::ModuleRecord * JS::Runtime::CyclicModuleRecord::GetImportedModule(ModuleRequestRecord record)
{
	auto loaded_module_iterator = LoadedModules.find(record);
	assert(loaded_module_iterator != LoadedModules.end());
	return loaded_module_iterator->Module;
}

gcn::gc_ptr<JS::Class::Promise> JS::Runtime::CyclicModuleRecord::LoadRequestedModules()
{
	std::shared_ptr<GraphLoadingState> state = std::make_shared<GraphLoadingState>();

	InnerModuleLoading(state);

	return state->PromiseCapability.Promise;
}

void JS::Runtime::CyclicModuleRecord::InnerModuleLoading(std::shared_ptr<GraphLoadingState> state)
{
	if(Status == StatusType::New && state->Visited.find(this) == state->Visited.end())
	{
		state->Visited.insert(this);
		state->PendingModulesCount += RequestedModules.size();
		for(auto request : RequestedModules)
		{
			if(!JS::Host->AllImportAttributesSupported(request.Attributes))
			{
				// ContinueModuleLoading
				if(state->IsLoading)
				{
					state->IsLoading = false;
					JS::Call(state->PromiseCapability.Reject, JS_undefined, {JS::New<SyntaxError> __LOC(u"unsupported import attribute by host")} RUNTIME_PHASE);
					return;
				}
			}

			auto request_iterator = LoadedModules.find(request);
			if(request_iterator != LoadedModules.end())
			{
				request_iterator->Module->InnerModuleLoading(state);
			}
			else
			{
				JS::Host->LoadImportedModule(&this->LoadedModules, request, state);
			}
		}

		if(!state->IsLoading)
		{
			return;
		}
	}
	ModuleRecord::InnerModuleLoading(state);
}

void JS::Runtime::CyclicModuleRecord::Link()
{
	std::vector<CyclicModuleRecord *> stack;

	assert(Status != StatusType::New);

	try
	{
		InnerModuleLinking(stack, 0);
	}
	catch(...)
	{
		for(auto module : stack)
		{
			assert(module->Status == StatusType::Linking);
			module->Status = StatusType::Unlinked;
		}
		assert(Status == StatusType::Unlinked);
		throw;
	}

	assert(Status == StatusType::Linked || Status == StatusType::EvaluatingAsync || Status == StatusType::Evaluated);
	assert(stack.size() == 0);
}

uint32_t JS::Runtime::CyclicModuleRecord::InnerModuleLinking(std::vector<CyclicModuleRecord *>& stack, uint32_t index)
{
	if(Status != StatusType::Unlinked)
	{
		return index;
	}

	Status = StatusType::Linking;
	DFSIndex = index;
	DFSAncestorIndex = index;
	index ++;
	stack.push_back(this);

	for(auto request : RequestedModules)
	{
		auto requiredModule = GetImportedModule(request);
		index = requiredModule->InnerModuleLinking(stack, index);
		if(auto cyclicModule = dynamic_cast<CyclicModuleRecord *>(requiredModule))
		{
			assert(cyclicModule->Status != StatusType::Unlinked);
			if(cyclicModule->Status == StatusType::Linking)
			{
				assert(std::find(stack.begin(), stack.end(), requiredModule) != stack.end());
				DFSAncestorIndex = std::min(DFSAncestorIndex, cyclicModule->DFSAncestorIndex);
			}
		}
	}

	InitializeEnvironment();

	if(DFSAncestorIndex == DFSIndex)
	{
		CyclicModuleRecord * requiredModule;
		do
		{
			requiredModule = stack.back();
			stack.pop_back();
			requiredModule->Status = StatusType::Linked;
		}
		while(requiredModule != this);
	}

	return index;
}

gcn::gc_ptr<JS::Class::Promise> JS::Runtime::CyclicModuleRecord::Evaluate()
{
	switch(Status)
	{
	default:
		assert(false);

	case StatusType::EvaluatingAsync:
	case StatusType::Evaluated:
		if(CycleRoot != this)
		{
			return CycleRoot->Evaluate();
		}
		[[fallthrough]];

	case StatusType::Linked:
		if(TopLevelCapability.has_value())
		{
			return (*TopLevelCapability).Promise;
		}

		std::vector<CyclicModuleRecord *> stack;
		TopLevelCapability = JS::Runtime::PromiseCapability<JS::Class::Promise>(JS::Intrinsics()->Promise);

		std::optional<JSValue> error_result;

		try
		{
			InnerModuleEvaluation(stack, 0);
		}
		catch(JS::Class::Internal::Value * thrown_value)
		{
			error_result = JSValue::WrapValue(thrown_value);
		}
		catch(...)
		{
			error_result = JS::New<InternalError> __LOC("unhandled native error");
		}

		if(error_result.has_value())
		{
			for(auto module : stack)
			{
				assert(module->Status == StatusType::Evaluating);
				assert(std::holds_alternative<Unset>(module->AsyncEvaluationOrder));
				module->Status = StatusType::Evaluated;
				module->EvaluationError = error_result.value();
			}

			assert(Status == StatusType::Evaluated);
			assert(EvaluationError.has_value());
			assert(EvaluationError.value() == error_result.value());
			JS::Call(TopLevelCapability.value().Reject, JS_undefined, { error_result.value() } RUNTIME_PHASE);
			return TopLevelCapability.value().Promise;
		}

		switch(Status)
		{
		default:
			assert(false);

		case StatusType::EvaluatingAsync:
			assert(!EvaluationError.has_value());
			break;

		case StatusType::Evaluated:
			assert(!EvaluationError.has_value());
			assert(std::holds_alternative<Unset>(AsyncEvaluationOrder));
			JS::Call(TopLevelCapability.value().Resolve, JS_undefined, {JS_undefined} RUNTIME_PHASE);
			assert(stack.empty());
			break;
		}

		return TopLevelCapability.value().Promise;
	}
}

uint32_t JS::Runtime::CyclicModuleRecord::InnerModuleEvaluation(std::vector<CyclicModuleRecord *>& stack, uint32_t index)
{
	switch(Status)
	{
	default:
		assert(false);
	case StatusType::EvaluatingAsync:
	case StatusType::Evaluated:
		if(EvaluationError.has_value())
		{
			JS::Throw(EvaluationError.value());
		}
		else
		{
			return index;
		}
	case StatusType::Evaluating:
		return index;
	case StatusType::Linked:
		Status = StatusType::Evaluating;
		DFSIndex = index;
		DFSAncestorIndex = index;
		PendingAsyncDependencies = 0;
		index ++;
		stack.push_back(this);

		for(auto request : RequestedModules)
		{
			auto requiredModule = GetImportedModule(request);
			index = requiredModule->InnerModuleEvaluation(stack, index);
			if(auto cyclicModule = dynamic_cast<CyclicModuleRecord *>(requiredModule))
			{
				if(cyclicModule->Status == StatusType::Evaluating)
				{
					assert(std::find(stack.begin(), stack.end(), requiredModule) != stack.end());
					DFSAncestorIndex = std::min(DFSAncestorIndex, cyclicModule->DFSAncestorIndex);
				}
				else
				{
					assert(cyclicModule->Status == StatusType::EvaluatingAsync || cyclicModule->Status == StatusType::Evaluated);
					cyclicModule = cyclicModule->CycleRoot;
					assert(cyclicModule->Status == StatusType::EvaluatingAsync || cyclicModule->Status == StatusType::Evaluated);
					if(cyclicModule->EvaluationError.has_value())
					{
						JS::Throw(cyclicModule->EvaluationError.value());
					}
				}

				if(std::get_if<uint32_t>(&cyclicModule->AsyncEvaluationOrder))
				{
					PendingAsyncDependencies.value() ++;
					cyclicModule->AsyncParentModules.push_back(this);
				}
			}
		}

		if(PendingAsyncDependencies > 0 || HasTLA)
		{
			assert(std::holds_alternative<Unset>(AsyncEvaluationOrder));
			AsyncEvaluationOrder = JS::GetSurroundingAgent()->IncrementModuleAsyncEvaluationCount();
			if(PendingAsyncDependencies == 0)
			{
				ExecuteAsyncModule();
			}
		}
		else
		{
			ExecuteModule();
		}
	}

	if(DFSAncestorIndex == DFSIndex)
	{
		CyclicModuleRecord * requiredModule;
		do
		{
			requiredModule = stack.back();
			stack.pop_back();
			assert(!std::holds_alternative<Done>(requiredModule->AsyncEvaluationOrder));
			if(std::holds_alternative<Unset>(requiredModule->AsyncEvaluationOrder))
			{
				Status = StatusType::Evaluated;
			}
			else
			{
				Status = StatusType::EvaluatingAsync;
			}
			requiredModule->CycleRoot = this;
		}
		while(requiredModule != this);
	}

	return index;
}

void JS::Runtime::CyclicModuleRecord::ExecuteAsyncModule()
{
	JS::Runtime::PromiseCapability<JS::Class::Promise> capability(JS::Intrinsics()->Promise);

	JSValue onFulfilled = JS::CreateBuiltinFunction(
		[this]([[maybe_unused]] std::optional<JSValue> this_argument, [[maybe_unused]] std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase)) -> JSValue
	{
		AsyncModuleExecutionFulfilled();
		return JS_undefined;
	}, 0, u"", {}, GetCurrentRealm());

	JSValue onRejected = JS::CreateBuiltinFunction(
		[this]([[maybe_unused]] std::optional<JSValue> this_argument, std::vector<JSValue> arguments, [[maybe_unused]] JSFunction new_target AND_PHASE(phase)) -> JSValue
	{
		JSValue error = arguments.size() > 0 ? arguments[0] : JS_undefined;
		AsyncModuleExecutionRejected(error);
		return JS_undefined;
	}, 0, u"", {}, GetCurrentRealm());

	capability.Promise->PerformPromiseThen(onFulfilled, onRejected);

	ExecuteModule(capability);
}

std::set<JS::Runtime::CyclicModuleRecord *, JS::Runtime::CyclicModuleRecord::AncestorAsyncOrder> JS::Runtime::CyclicModuleRecord::GatherAvailableAncestors()
{
	std::set<CyclicModuleRecord *, AncestorAsyncOrder> exec_set;
	GatherAvailableAncestors(exec_set);
	return exec_set;
}

void JS::Runtime::CyclicModuleRecord::GatherAvailableAncestors(std::set<CyclicModuleRecord *, AncestorAsyncOrder>& exec_set)
{
	for(auto module : AsyncParentModules)
	{
		if(exec_set.find(module) == exec_set.end()
		&& !module->CycleRoot->EvaluationError.has_value())
		{
			assert(module->Status == StatusType::EvaluatingAsync);
			assert(!module->EvaluationError.has_value());
			assert(std::holds_alternative<uint32_t>(module->AsyncEvaluationOrder));
			assert(module->PendingAsyncDependencies > 0);
			module->PendingAsyncDependencies.value() --;
			if(module->PendingAsyncDependencies == 0)
			{
				exec_set.insert(module);
				if(!module->HasTLA)
				{
					module->GatherAvailableAncestors(exec_set);
				}
			}
		}
	}
}

void JS::Runtime::CyclicModuleRecord::AsyncModuleExecutionFulfilled()
{
	switch(Status)
	{
	default:
		assert(false);

	case StatusType::Evaluated:
		assert(!EvaluationError.has_value());
		return;
	case StatusType::EvaluatingAsync:
		assert(std::holds_alternative<uint32_t>(AsyncEvaluationOrder));
		assert(!EvaluationError.has_value());

		AsyncEvaluationOrder = Done{};
		Status = StatusType::Evaluated;
		if(TopLevelCapability.has_value())
		{
			assert(CycleRoot == this);
			JS::Call((*TopLevelCapability).Resolve, JS_undefined, { JS_undefined } RUNTIME_PHASE);
		}
		std::set<CyclicModuleRecord *, AncestorAsyncOrder> exec_set = GatherAvailableAncestors();
		for(auto module : exec_set)
		{
			if(module->Status == StatusType::Evaluated)
			{
				assert(module->EvaluationError.has_value());
			}
			else if(module->HasTLA)
			{
				module->ExecuteAsyncModule();
			}
			else
			{
				try
				{
					module->ExecuteModule();
				}
				catch(JS::Class::Internal::Value * thrown_value)
				{
					module->AsyncModuleExecutionRejected(JSValue::WrapValue(thrown_value));
				}
				catch(...)
				{
					AsyncModuleExecutionRejected(JS::New<InternalError> __LOC("unhandled native error"));
				}

				module->AsyncEvaluationOrder = Done{};
				module->Status = StatusType::Evaluated;
				if(module->TopLevelCapability.has_value())
				{
					assert(module->CycleRoot == module);
					JS::Call((*module->TopLevelCapability).Resolve, JS_undefined, { JS_undefined } RUNTIME_PHASE);
				}
			}
		}
	}
}

void JS::Runtime::CyclicModuleRecord::AsyncModuleExecutionRejected(JSValue error)
{
	switch(Status)
	{
	default:
		assert(false);

	case StatusType::Evaluated:
		assert(EvaluationError.has_value());
		return;
	case StatusType::EvaluatingAsync:
		assert(std::holds_alternative<uint32_t>(AsyncEvaluationOrder));
		assert(EvaluationError.has_value());

		EvaluationError = error;
		AsyncEvaluationOrder = Done{};
		Status = StatusType::Evaluated;

		for(auto module : AsyncParentModules)
		{
			module->AsyncModuleExecutionRejected(error);
		}

		if(TopLevelCapability.has_value())
		{
			assert(CycleRoot == this);
			JS::Call((*TopLevelCapability).Reject, JS_undefined, { error } RUNTIME_PHASE);
		}
	}
}

//// SourceTextModuleRecord

std::set<JSString> JS::Runtime::SourceTextModuleRecord::GetExportedNames(std::set<SourceTextModuleRecord *>& exportStarSet)
{
	if(exportStarSet.find(this) != exportStarSet.end())
	{
		return {};
	}

	exportStarSet.insert(this);

	std::set<JSString> exportedNames;

	for(auto export_entry : LocalExportEntries)
	{
		assert(export_entry.ExportName.has_value());
		exportedNames.insert(export_entry.ExportName.value());
	}

	for(auto export_entry : IndirectExportEntries)
	{
		assert(export_entry.ExportName.has_value());
		exportedNames.insert(export_entry.ExportName.value());
	}

	for(auto export_entry : StarExportEntries)
	{
		assert(export_entry.ModuleRequest.has_value());
		auto requestedModule = GetImportedModule(export_entry.ModuleRequest.value());
		auto starNames = requestedModule->GetExportedNames(exportStarSet);
		starNames.erase(u"default");
		exportedNames.insert(starNames.begin(), starNames.end());
	}

	return exportedNames;
}

std::variant<JS::Runtime::ModuleRecord::ResolvedBinding, JS::Runtime::ModuleRecord::Null, JS::Runtime::ModuleRecord::Ambiguous>
	JS::Runtime::SourceTextModuleRecord::ResolveExport(JSString exportName, std::set<ResolveRecord>& resolveSet)
{
	assert(Status != StatusType::New);

	for(auto resolve : resolveSet)
	{
		if(resolve.Module == this)
		{
			return Null{};
		}
	}
	resolveSet.insert(ResolveRecord{ /* Module = */ this, /* ExportName = */ exportName });

	for(auto export_entry : LocalExportEntries)
	{
		if(export_entry.ExportName == exportName)
		{
			return ResolvedBinding{ /* Module = */ this, /* BindingName = */ export_entry.LocalName };
		}
	}

	for(auto export_entry : IndirectExportEntries)
	{
		if(export_entry.ExportName == exportName)
		{
			assert(export_entry.ModuleRequest.has_value());
			auto imported_module = GetImportedModule(export_entry.ModuleRequest.value());
			if(export_entry.ImportNameType == ExportEntry::Type::All)
			{
				return ResolvedBinding{ /* Module = */ imported_module, /* BindingName = */ { } };
			}
			else
			{
				return imported_module->ResolveExport(export_entry.ImportName.value(), resolveSet);
			}
		}
	}

	if(exportName == u"default")
	{
		return Null{};
	}

	std::optional<ResolvedBinding> starResolution;

	for(auto export_entry : StarExportEntries)
	{
		assert(export_entry.ModuleRequest.has_value());
		auto imported_module = GetImportedModule(export_entry.ModuleRequest.value());
		auto resolution = imported_module->ResolveExport(exportName, resolveSet);
		if(std::holds_alternative<Ambiguous>(resolution))
		{
			return resolution;
		}
		if(auto resolved_binding_pointer = std::get_if<ResolvedBinding>(&resolution))
		{
			auto resolved_binding = *resolved_binding_pointer;
			if(!starResolution.has_value())
			{
				starResolution = resolved_binding;
			}
			else
			{
				if(resolved_binding.Module != starResolution.value().Module
				|| (resolved_binding.BindingName.has_value()
					? !starResolution.value().BindingName.has_value() || resolved_binding.BindingName.value() != starResolution.value().BindingName.value()
					: starResolution.value().BindingName.has_value()))
				{
					return Ambiguous{};
				}
			}
		}
	}

	if(starResolution.has_value())
	{
		return starResolution.value();
	}
	else
	{
		return Null{};
	}
}

void JS::Runtime::SourceTextModuleRecord::InitializeEnvironment()
{
	for(auto export_entry : IndirectExportEntries)
	{
		assert(export_entry.ExportName.has_value());
		auto resolution = ResolveExport(export_entry.ExportName.value());
		if(std::holds_alternative<Null>(resolution))
		{
			JS::Throw(JS::New<SyntaxError> __LOC("indirect export cannot be resolved"));
		}
		else if(std::holds_alternative<Ambiguous>(resolution))
		{
			JS::Throw(JS::New<SyntaxError> __LOC("indirect export is ambiguous"));
		}
	}

	Environment = std::make_shared<JS::Runtime::ModuleEnvironmentRecord>(Realm->GlobalEnv);

	for(auto import_entry : ImportEntries)
	{
		auto importedModule = GetImportedModule(import_entry.ModuleRequest);
		if(import_entry.ImportName.has_value())
		{
			auto resolution = importedModule->ResolveExport(import_entry.ImportName.value());
			if(std::holds_alternative<Null>(resolution))
			{
				JS::Throw(JS::New<SyntaxError> __LOC("import is missing"));
			}
			else if(std::holds_alternative<Ambiguous>(resolution))
			{
				JS::Throw(JS::New<SyntaxError> __LOC("import is ambiguous"));
			}
			auto binding = std::get<ResolvedBinding>(resolution);
			if(binding.BindingName.has_value())
			{
				Environment->CreateImportBinding(import_entry.LocalName, binding.Module, binding.BindingName.value());
			}
			else
			{
				// namespace object
				auto moduleNamespace = binding.Module->GetModuleNamespace();
				Environment->CreateSingletonBinding(
					import_entry.LocalName
					PUBLIC_NAMESPACES
					READ_WRITE_ACCESS,
					JSBinding::MakeImmutable(moduleNamespace, /* Strict = */ true));
			}
		}
		else
		{
			// namespace object
			auto moduleNamespace = importedModule->GetModuleNamespace();
			Environment->CreateSingletonBinding(
				import_entry.LocalName
				PUBLIC_NAMESPACES
				READ_WRITE_ACCESS,
				JSBinding::MakeImmutable(moduleNamespace, /* Strict = */ true));
		}
	}

	Context = new JS::Runtime::ExecutionContext();
	Context->Function = nullptr;
	Context->Realm = Realm;
	Context->ScriptOrModule = this;
	Context->VariableEnvironment = Environment;
	Context->LexicalEnvironment = Environment;
	Context->PrivateEnvironment = nullptr;
	JS::Runtime::ExecutionContext::GetContextStack().push_back(Context);

	JS::Runtime::ValidationContext cxt;
	Context->code = new (GC) JS::Interpreter::Code;
	JS::Interpreter::State<> state = Context->code->Begin();
	cxt.scriptOrModuleRecord = this;
	JS::Interpreter::State<std::optional<JSValue>> final_state =
		Code->Process(cxt, state); // also sets up HasTLA

	if(HasTLA)
	{
		// await returns undefined, therefore we must also return an undefined
		final_state.Pop().Push<JSValue>(JS_undefined);
	}

	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
}

void JS::Runtime::SourceTextModuleRecord::ExecuteModule(std::optional<JS::Runtime::PromiseCapability<JS::Class::Promise>> promise_capability)
{
	auto moduleContext = new JS::Runtime::ExecutionContext();
	moduleContext->Function = nullptr;
	moduleContext->Realm = Realm;
	moduleContext->ScriptOrModule = this;
	moduleContext->VariableEnvironment = Environment;
	moduleContext->LexicalEnvironment = Environment;
	moduleContext->PrivateEnvironment = nullptr;

	if(HasTLA)
	{
		// AsyncBlockStart
		assert(promise_capability.has_value());

		CallContextCapture call_context_capture; // to roll back changes

		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
		JS::Runtime::ExecutionContext::GetContextStack().push_back(moduleContext);
		moduleContext->code = Context->code;
		moduleContext->code_pointer = 0;

		JS::Runtime::ExecutionContext::GetContextStack().back()->promiseCapability = *promise_capability;
		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

		JS::Runtime::ExecutionContext::ExecuteAndReturn<JSValue>(); // await will return undefined
		GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	}
	else
	{
		if(promise_capability.has_value())
		{
			JS::Throw(JS::New<InternalError> __LOC());
		}

		CallContextCapture call_context_capture; // to roll back changes

		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Suspended;
		JS::Runtime::ExecutionContext::GetContextStack().push_back(moduleContext);
		moduleContext->code = Context->code;
		moduleContext->code_pointer = 0;

		JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

		JS::Runtime::ExecutionContext::ExecuteAndReturn<std::optional<JSValue>>();
		GetCurrentExecutionContext()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;
	}
}

//// SyntheticModuleRecord

void JS::Runtime::SyntheticModuleRecord::SetSyntheticModuleExport(JSString exportName, JSValue exportValue)
{
	switch(Environment->SetMutableBinding(JSIdentifierReference(exportName), exportValue PUBLIC_NAMESPACES))
	{
	case JS::Runtime::Result::Success:
		break;
	case JS::Runtime::Result::ReferenceError:
		JS::Throw(JS::New<ReferenceError> __LOC());
	case JS::Runtime::Result::TypeError:
		JS::Throw(JS::New<TypeError> __LOC());
	}
}

gcn::gc_ptr<JS::Class::Promise> JS::Runtime::SyntheticModuleRecord::LoadRequestedModules()
{
	return JS::Class::Promise::Resolve(JS::Intrinsics()->Promise.cast_object<JS::Class::Object>(), JS_undefined);
}

std::set<JSString> JS::Runtime::SyntheticModuleRecord::GetExportedNames([[maybe_unused]] std::set<SourceTextModuleRecord *>& exportStarSet)
{
	return ExportNames;
}

std::variant<JS::Runtime::ModuleRecord::ResolvedBinding, JS::Runtime::ModuleRecord::Null, JS::Runtime::ModuleRecord::Ambiguous>
	JS::Runtime::SyntheticModuleRecord::ResolveExport(JSString exportName, [[maybe_unused]] std::set<ResolveRecord>& resolveSet)
{
	if(ExportNames.find(exportName) == ExportNames.end())
	{
		return Null{};
	}
	else
	{
		return ResolvedBinding{/* Module = */ this, /* BindingName = */ exportName};
	}
}

void JS::Runtime::SyntheticModuleRecord::Link()
{
	Environment = std::make_shared<JS::Runtime::ModuleEnvironmentRecord>(Realm->GlobalEnv);
	for(auto exportName : ExportNames)
	{
		Environment->CreateHoistedBinding(exportName, JS_undefined);
	}
}

gcn::gc_ptr<JS::Class::Promise> JS::Runtime::SyntheticModuleRecord::Evaluate()
{
	JS::Runtime::ExecutionContext * moduleContext = new JS::Runtime::ExecutionContext();
	moduleContext->Function = nullptr;
	moduleContext->Realm = Realm;
	moduleContext->ScriptOrModule = this;
	moduleContext->VariableEnvironment = Environment;
	moduleContext->LexicalEnvironment = Environment;
	moduleContext->PrivateEnvironment = nullptr;

	JS::Runtime::ExecutionContext::GetContextStack().push_back(moduleContext);
	moduleContext->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	std::optional<JSValue> error_result;
	try
	{
		EvaluationSteps(this);
	}
	catch(JS::Class::Internal::Value * thrown_value)
	{
		error_result = JSValue::WrapValue(thrown_value);
	}
	catch(...)
	{
		error_result = JS::New<InternalError> __LOC("unhandled native error");
	}

	JS::Runtime::ExecutionContext::GetContextStack().pop_back();
	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	JS::Runtime::PromiseCapability<JS::Class::Promise> promiseCapability(JS::Intrinsics()->Promise);

	if(error_result.has_value())
	{
		JS::Call(promiseCapability.Reject, JS_undefined, { error_result.value() } RUNTIME_PHASE);
	}
	else
	{
		JS::Call(promiseCapability.Resolve, JS_undefined, { JS_undefined } RUNTIME_PHASE);
	}

	return promiseCapability.Promise;
}

#if ENABLE_ES4
JSValue JS::Runtime::SourceTextPackageRecord::Perform()
{
	switch(State)
	{
	case StateType::Unloaded:
		State = StateType::Loading;
		break;
	case StateType::Loading:
		JS::Throw(JS::New<UninitializedError> __LOC("circular package dependency"));
	case StateType::Loaded:
		return JS_undefined;
	}

	JS::Runtime::ExecutionContext::GetContextStack().push_back(Context);

	Context->code_pointer = 0;
	Context->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	JS::Runtime::ExecutionContext::ExecuteAndReturn<std::optional<JSValue>>();

	JS::Runtime::ExecutionContext::GetContextStack().back()->state = JS::Runtime::ExecutionContext::ExecutionState::Executing;

	State = StateType::Loaded;

	return JS_undefined;
}
#endif

//// PackageContent
#if ENABLE_ES4
#if 0
std::optional<JS::Runtime::PackageElement> JS::Runtime::PackageContent::MaybeGetMember(std::vector<JSString> PathName, size_t Level)
{
	JSString PackageName = PathName[Level];
	std::vector<JSString> SubPathName(PathName.begin(), PathName.begin() + Level + 1);

	auto package_element = Members.find(PackageName);

	if(package_element != Members.end())
	{
		if(PathName.size() == Level + 1)
		{
			return *package_element->second;
		}
		else
		{
			return package_element->second->MaybeGetMember(PathName, Level + 1);
		}
	}
	else
	{
		return {};
	}
}
#endif

std::optional<JS::Runtime::PackageElement *> JS::Runtime::PackageContent::GetMember(std::vector<JSString> PathName, LookupMode mode, size_t Level)
{
	JSString PackageName = PathName[Level];
	std::vector<JSString> SubPathName(PathName.begin(), PathName.begin() + Level + 1);

	auto package_element = Members.find(PackageName);

	if(package_element != Members.end())
	{
		if(PathName.size() == Level + 1)
		{
			auto record_option = package_element->second->GetObject(SubPathName, mode);
			assert(record_option.has_value());
			auto record = record_option.value();
			auto package = record_option.has_value() ? record_option.value()->Package : nullptr;
			switch(mode)
			{
			case LookupMode::Peek:
				return package_element->second.get();
			case LookupMode::Open:
				assert(package != nullptr);
				switch(package->PackageType)
				{
				case JS::Class::Package::Type::Transient:
					package->PackageType = JS::Class::Package::Type::Open;
					break;
				case JS::Class::Package::Type::Open:
					break;
				case JS::Class::Package::Type::Closed:
					JS::Throw(JS::New<TypeError> __LOC("invalid package"));
				}
				break;
			case LookupMode::Closed:
			case LookupMode::Load:
				assert(record_option.has_value());
				if(package != nullptr && package->PackageType == JS::Class::Package::Type::Open)
				{
					JS::Throw(JS::New<TypeError> __LOC("invalid package"));
				}
				break;
			case LookupMode::Transient: // TODO: untested
				assert(record_option.has_value());
				break;
			default:
				assert(false);
			}

			return record;
		}
		else
		{
			return package_element->second->GetMember(PathName, mode, Level + 1);
		}
	}
	else
	{
		if(mode == LookupMode::Peek)
		{
			return {};
		}

		if(PathName.size() == Level + 1)
		{
			gcn::gc_ptr<JS::Class::Package> package;
			std::shared_ptr<PackageElement> element;
			switch(mode)
			{
			case LookupMode::Open:
				package = JS::New<JS::Class::Package>(
					PackageName,
					JS::Class::Package::Type::Open,
					SubPathName);
				element = std::make_shared<PackageElement>(package); // TODO
				Members[PackageName] = element;
				return element->GetObject(PathName, LookupMode::Closed);
			case LookupMode::Transient: // TODO: untested
			case LookupMode::Closed:
				package = JS::New<JS::Class::Package>(
					PackageName,
					JS::Class::Package::Type::Transient,
					SubPathName);
				element = std::make_shared<PackageElement>(package); // TODO
				Members[PackageName] = element;
				return element->GetObject(PathName, LookupMode::Closed);
			case LookupMode::Load:
				element = std::make_shared<PackageElement>(); // TODO
				Members[PackageName] = element;
				return element->GetObject(PathName, LookupMode::Load);
			default:
				assert(false);
			}
		}
		else
		{
			auto package = JS::New<JS::Class::Package>(
				PackageName,
				JS::Class::Package::Type::Transient,
				SubPathName);
			std::shared_ptr<PackageElement> element = std::make_shared<PackageElement>(package); // TODO
			Members[PackageName] = element;
			return element->GetMember(PathName, mode, Level + 1);
		}
	}
}

void JS::Runtime::PackageContent::SetMemberPath(std::vector<JSString> PathName, std::filesystem::path sourcefilename, size_t Level)
{
	JSString PackageName = PathName[Level];
	std::vector<JSString> SubPathName(PathName.begin(), PathName.begin() + Level + 1);

	auto package_element = Members.find(PackageName);

	if(package_element != Members.end())
	{
		if(PathName.size() == Level + 1)
		{
			auto element = package_element->second;
			if(element->ScriptOrModule != nullptr)
			{
				JS::Throw(JS::New<TypeError> __LOC("package already loaded"));
			}
			if(element->SourceName.has_value())
			{
				JS::Throw(JS::New<TypeError> __LOC("package already assigned to path"));
			}
			element->SourceName = sourcefilename;
		}
		else
		{
			return package_element->second->SetMemberPath(PathName, sourcefilename, Level + 1);
		}
	}
	else
	{
		if(PathName.size() == Level + 1)
		{
			std::shared_ptr<PackageElement> element = std::make_shared<PackageElement>(sourcefilename);
			Members[PackageName] = element;
		}
		else
		{
			std::shared_ptr<PackageElement> element = std::make_shared<PackageElement>();
			Members[PackageName] = element;
			element->SetMemberPath(PathName, sourcefilename, Level + 1);
		}
	}
}

void JS::Runtime::PackageContent::GetMemberLookupPath(std::vector<JSString> PathName, PackageLookupPath& LookupPath, size_t Level)
{
	JSString PackageName = PathName[Level];
	std::vector<JSString> SubPathName(PathName.begin(), PathName.begin() + Level + 1);

	auto package_element = Members.find(PackageName);

	if(package_element != Members.end())
	{
		if(!package_element->second->GetObjectLookupPath(PathName, LookupPath, Level))
		{
			package_element->second->GetMemberLookupPath(PathName, LookupPath, Level + 1);
		}
	}
	else
	{
		while(Level < PathName.size())
		{
			LookupPath.MemberPath.push_back(PathName[Level++]);
		}
	}
}
#endif

//// PackageElement
#if ENABLE_ES4
std::optional<JS::Runtime::PackageElement *> JS::Runtime::PackageElement::GetObject(std::vector<JSString> PathName, LookupMode mode)
{
	switch(mode)
	{
	case LookupMode::Peek:
		break;
	case LookupMode::Load:
		if(ScriptOrModule == nullptr && Package == nullptr)
		{
			PackageLookupPath LookupPath;
			dynamic_cast<JS::Runtime::DefaultHost *>(JS::Host)->ES4Packages.GetMemberLookupPath(PathName, LookupPath);

			for(auto path : LookupPath)
			{
				auto path1 = path;
				path1 += ".js";
				auto path2 = path;
				path2 += ".node";
				if(std::filesystem::exists(path1) && !std::filesystem::is_directory(path1))
				{
					SourceName = path1;
					break;
				}
				else if(std::filesystem::exists(path2) && !std::filesystem::is_directory(path2))
				{
					SourceName = path2;
					break;
				}
				else if(std::filesystem::exists(path))
				{
					SourceName = path;
					if(std::filesystem::is_directory(path))
					{
						Package = JS::New<JS::Class::Package>(
							PathName.back(),
							JS::Class::Package::Type::Open,
							PathName);
					}
					break;
				}
			}

			if(!SourceName.has_value())
			{
				JS::Throw(JS::New<ReferenceError> __LOC("unable to find package"));
			}
			else if(Package == nullptr)
			{
				std::ifstream input(SourceName.value());
				JS::Parser::UTF8StreamTokenizer tokenizer(input);
				tokenizer.last_location.filename = SourceName.value();
				JS::Parser::Parser parser(&tokenizer);
				auto record = parser.ParseScriptOrModule(JS::GetCurrentRealm(), JS::Parser::Parser::ExpectedScriptType::Module);

				record->Perform();

				if(ScriptOrModule == nullptr)
				{
					// check if this is a member of an open package
					std::vector<JSString> parent_path_name = PathName;
					parent_path_name.pop_back();
					auto package_record = dynamic_cast<JS::Runtime::DefaultHost *>(JS::Host)->GetES4Package(std::optional<std::filesystem::path>{}, parent_path_name, /* open = */ true);
					if(package_record.Package != nullptr && package_record.Package->PackageType == JS::Class::Package::Type::Open)
					{
						// for members of open packages, we still need to keep track of the source package statement so that the import statement can call it
						auto script = dynamic_cast<ScriptRecord *>(record);
						if(!script)
						{
							JS::Throw(JS::New<ReferenceError> __LOC("ECMAScript 6 module in ECMAScript 4 mode"));
						}
						if(script->Packages.size() == 0)
						{
							JS::Throw(JS::New<ReferenceError> __LOC("specified source file has no package declaration"));
						}
						if(script->Packages.size() > 1)
						{
							JS::Throw(JS::New<ReferenceError> __LOC("specified source file has multiple package declarations"));
						}
						ScriptOrModule = script->Packages[0];
					}
					else
					{
						JS::Throw(JS::New<ReferenceError> __LOC("specified source file has not contained expected declaration"));
					}
					break; // avoid making a new package for this node
				}
			}
		}
		// fallthrough
	default:
		if(Package == nullptr)
		{
			Package = JS::New<JS::Class::Package>(
				PathName.back(),
				JS::Class::Package::Type::Transient,
				PathName);
		}
		break;
	}

	return this;
}

bool JS::Runtime::PackageElement::GetObjectLookupPath(std::vector<JSString> PathName, PackageLookupPath& LookupPath, size_t Level)
{
	if(SourceName.has_value())
	{
		LookupPath.MemberPath.clear();
		if(SourceName.value().filename().empty())
		{
			// "pathname/", an open package
			LookupPath.SystemPath = SourceName.value().parent_path();
		}
		else
		{
			LookupPath.SystemPath = SourceName.value();
			return true;
		}
	}
	else
	{
		LookupPath.MemberPath.push_back(PathName[Level]);
	}
	return false;
}
#endif

#if 0
//// TransportModuleRecord

std::set<JS::Runtime::LoadedModuleRequestRecord> * JS::Runtime::TransportModuleRecord::GetLoadedModules()
{
	return Script->GetLoadedModules();
}
#endif

//// AsynchronousModuleRecord

std::set<JS::Runtime::LoadedModuleRequestRecord> * JS::Runtime::AsynchronousModuleRecord::GetLoadedModules()
{
	return Script->GetLoadedModules();
}

void JS::Runtime::AsynchronousModuleRecord::Initialize(AsynchronousModuleDefinition definition)
{
	// corresponds to Module.prototype.init in RequireJS sources
	if(IsInitialized == LoadState::Initialized)
	{
		return;
	}
	Script = definition.Script;
	if(Script != nullptr)
	{
		sourcefilename = Script->sourcefilename;
	}
	Factory = definition.Factory;
	if(definition.Dependencies.has_value())
	{
		Dependencies = definition.Dependencies.value();
	}
	else
	{
		if(GetFunction(Factory))
		{
			auto length = JS::Get(Factory, u"length");
			if(length == 1.0)
			{
				Dependencies = { u"require" };
			}
			else if(length == 2.0)
			{
				Dependencies = { u"require", u"exports" };
			}
			else if(length != 0.0)
			{
				Dependencies = { u"require", u"exports", u"module" };
			}
			// TODO: look for require calls
		}
	}
	IsInitialized = LoadState::Initialized;

	if(IsEnabled != EnabledState::Disabled)
	{
		Enable();
	}
	else
	{
		Check();
	}
}

void JS::Runtime::AsynchronousModuleRecord::Fetch()
{
	// corresponds to Module.prototype.init in RequireJS sources
	if(IsInitialized != LoadState::Unloaded)
	{
		return;
	}
	IsInitialized = LoadState::Fetched;
	JS::Host->LoadDefinedModuleDependency(Identifier);
}

void JS::Runtime::AsynchronousModuleRecord::Enable()
{
	// corresponds to Module.prototype.enable in RequireJS sources
	/*if(IsEnabled == EnabledState::Enabled)
	{
		return;
	}*/
	IsEnabled = EnabledState::Enabling;
	for(auto dependency : Dependencies)
	{
		if(dependency == u"require")
		{
			DependencyExports[dependency] = JS::Intrinsics()->require;
		}
		else if(dependency == u"exports")
		{
			UsingExports = true;
			if(IsUndefined(Exports))
			{
				Exports = JS::New<JS::Class::OrdinaryObject>();
			}

			DependencyExports[dependency] = Exports;
		}
		else if(dependency == u"module")
		{
			Module = JS::New<JS::Class::OrdinaryObject>();
			if(IsUndefined(Exports))
			{
				Exports = JS::New<JS::Class::OrdinaryObject>();
			}
			JS::Set(Module, u"exports", Exports);
			JS::Set(Module, u"id", Identifier);
			JS::Set(Module, u"uri", sourcefilename.has_value() ? sourcefilename.value().u16string() : Identifier);

			DependencyExports[dependency] = Module;
		}
		else
		{
			DependencyCount += 1;
			auto host = dynamic_cast<JS::Runtime::DefaultHost *>(JS::Host);
			AsynchronousModuleRecord * module = nullptr;
			auto module_iterator = host->LoadedModules.find(dependency);
			if(module_iterator != host->LoadedModules.end())
			{
				module = dynamic_cast<AsynchronousModuleRecord *>(module_iterator->second);
			}
			if(module == nullptr)
			{
				module = new AsynchronousModuleRecord(dependency);
				host->LoadedModules[ModuleRequestRecord(dependency)] = module;
			}
			module->ExpectedDependencyBy(this, dependency);
			if(module->IsEnabled == EnabledState::Disabled)
			{
				module->Enable();
			}
		}
	}
	IsEnabled = EnabledState::Enabled;
	Check();
}

void JS::Runtime::AsynchronousModuleRecord::Check()
{
	// corresponds to Module.prototype.check in RequireJS sources
	if(IsEnabled != EnabledState::Enabled)
	{
		return;
	}

	if(IsInitialized != LoadState::Initialized)
	{
		bool alreadyLoaded = false;
		for(auto moduleDefinition : dynamic_cast<DefaultHost *>(JS::Host)->DefinitionQueue)
		{
			if(moduleDefinition.Identifier == Identifier)
			{
				alreadyLoaded = true;
			}
		}

		if(!alreadyLoaded)
		{
			Fetch();
		}
	}
	// TODO: if error is set, call error callbacks
	else if(IsDefined != DefinedState::Defining)
	{
		if(DependencyCount == 0 && IsDefined == DefinedState::Undefined)
		{
			IsDefined = DefinedState::Defining;
			if(auto function = JS::GetFunction(Factory))
			{
				std::vector<JSValue> dependencies;
				for(auto dependency : Dependencies)
				{
					dependencies.push_back(DependencyExports[dependency]);
				}
				auto exports = JS::Call(function.value(), Exports, dependencies RUNTIME_PHASE);
				if(IsUndefined(exports))
				{
					if(!IsUndefined(Module))
					{
						Exports = JS::Get(Module, u"exports");
					}
					else if(!UsingExports)
					{
						Exports = exports;
					}
				}
				else
				{
					Exports = exports;
				}
			}
			else
			{
				Exports = Factory;
			}

			IsDefined = DefinedState::Emitting;
			for(auto callback : OnDefinedCallbacks)
			{
				if(callback.DependentModule->DependencyExports.find(callback.DependencyName)
				== callback.DependentModule->DependencyExports.end())
				{
					callback.DependentModule->DependencyExports[callback.DependencyName] = Exports;
					callback.DependentModule->DependencyCount -= 1;
					callback.DependentModule->Check();
				}
			}
			IsDefined = DefinedState::Complete;
		}
	}
}

void JS::Runtime::AsynchronousModuleRecord::ExpectedDependencyBy(AsynchronousModuleRecord * DependentModule, JSString DependencyName)
{
	if(ToBoolean(Exports) && IsDefined == DefinedState::Complete)
	{
		DependentModule->DependencyExports[DependencyName] = Exports;
		DependentModule->DependencyCount -= 1;
		DependentModule->Check();
	}
	else
	{
		OnDefinedCallbacks.push_back(OnDefinedCallback{DependentModule, DependencyName});
	}
}

JSValue JS::Runtime::AsynchronousModuleRecord::Perform()
{
	// TODO???
	return JS_undefined;
}

//// DefaultHost

#if ENABLE_ES4
std::vector<JSString> JS::Runtime::DefaultHost::GetES4FullPackageName(std::filesystem::path sourcefilename)
{
	std::vector<JSString> result;
	for(auto element : sourcefilename)
	{
		result.push_back(UTF8toUTF16(element.stem()));
	}
	return result;
}

bool JS::Runtime::DefaultHost::IsES4PackageOpen(std::filesystem::path sourcefilename, std::vector<JSString> PathName)
{
	// an open package is one that is not contained in a single source file but is a directory containing its sources

	// first we check if an import map has already assigned it to be open
	if(auto element = ES4Packages.GetMember(PathName, PackageElement::LookupMode::Peek))
	{
		if((*element)->Package != nullptr && (*element)->Package->PackageType == JS::Class::Package::Type::Open)
		{
			// it is an open package, but we need to verify this is due to an import map directive
			if((*element)->SourceName.has_value() && (*element)->SourceName.value().filename().empty())
			{
				// the package explicitly refers to a directory
				return true;
			}

			for(auto member : (*element)->Members)
			{
				if(member.second->SourceName.has_value() && sourcefilename == member.second->SourceName.value())
				{
					// the file is explicitly loaded for a member
					return true;
				}
			}
		}
	}

	// to see this, we will check if the source file name contains the path name plus a single file under it
	std::vector<JSString> FullName = GetES4FullPackageName(sourcefilename);
	if(FullName.size() < PathName.size() + 1)
	{
		return false;
	}

	for(size_t offset = 0; offset < PathName.size(); offset ++)
	{
		JSString ComponentName = PathName[PathName.size() - offset - 1];
		if(ComponentName != FullName[FullName.size() - offset - 2]) // skip the final part
		{
			return false;
		}
	}

	return true;
}

JSString JS::Runtime::DefaultHost::GetES4PackageName(std::optional<std::filesystem::path> sourcefilename)
{
	if(sourcefilename.has_value())
	{
		return UTF8toUTF16(sourcefilename.value().stem());
	}
	else
	{
		return u"main";
	}
}

JSString JS::Runtime::DefaultHost::GetES4PackageName([[maybe_unused]] std::optional<std::filesystem::path> sourcefilename, JSString StringValue)
{
	return StringValue;
}

JSString JS::Runtime::DefaultHost::GetES4PackageName([[maybe_unused]] std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName)
{
	JSString result;
	for(auto part : PathName)
	{
		if(result.empty())
		{
			result += part;
		}
		else
		{
			result += u".";
			result += part;
		}
	}
	return result;
}

void JS::Runtime::DefaultHost::SetES4Package(std::filesystem::path sourcefilename, JSString StringValue)
{
	ES4StringPackages.SetMemberPath({StringValue}, sourcefilename);
}

void JS::Runtime::DefaultHost::SetES4Package(std::filesystem::path sourcefilename, std::vector<JSString> PathName)
{
	ES4Packages.SetMemberPath(PathName, sourcefilename);
}

JS::Runtime::PackageImportRecord& JS::Runtime::DefaultHost::GetES4Package(std::optional<std::filesystem::path> sourcefilename, bool open)
{
	JSString name = GetES4PackageName(sourcefilename);
	return GetES4Package(sourcefilename, std::vector<JSString>{name}, open);
}

JS::Runtime::PackageImportRecord& JS::Runtime::DefaultHost::GetES4Package([[maybe_unused]] std::optional<std::filesystem::path> sourcefilename, JSString StringValue, bool open)
{
	auto result = ES4StringPackages.GetMember({StringValue}, open ? PackageContent::LookupMode::Open : PackageContent::LookupMode::Closed);
	assert(result.has_value());
	return *result.value();
}

JS::Runtime::PackageImportRecord& JS::Runtime::DefaultHost::GetES4Package([[maybe_unused]] std::optional<std::filesystem::path> sourcefilename, std::vector<JSString> PathName, bool open)
{
	auto result = ES4Packages.GetMember(PathName, open ? PackageContent::LookupMode::Open : PackageContent::LookupMode::Closed);
	assert(result.has_value());
	return *result.value();
}

JS::Runtime::PackageImportRecord& JS::Runtime::DefaultHost::LocateES4Package(JSString StringValue)
{
	auto result = ES4Packages.GetMember({StringValue}, PackageContent::LookupMode::Load);
	assert(result.has_value());
	return *result.value();
}

JS::Runtime::PackageImportRecord& JS::Runtime::DefaultHost::LocateES4Package(std::vector<JSString> PathName)
{
	auto result = ES4Packages.GetMember(PathName, PackageContent::LookupMode::Load);
	assert(result.has_value());
	return *result.value();
}
#endif

JSString JS::Runtime::DefaultHost::NormalizeImportPath(JSString path)
{
	size_t term_start = 0;
	for(size_t slash_index = path.find(u'/');
		slash_index != std::u16string::npos;
		slash_index = path.find(u'/', term_start))
	{
		if(term_start != 0 && term_start + 1 == slash_index && path[term_start] == u'.')
		{
			// "/./"
			path.erase(term_start, slash_index + 1);
			// term_start does not change
		}
		else if(term_start >= 2 && term_start + 2 == slash_index && path[term_start] == u'.' && path[term_start] == u'.')
		{
			// "/../"
			term_start = path.rfind(u'/', term_start - 2);
			if(term_start == std::u16string::npos)
			{
				path.erase(0, slash_index + 1);
				term_start = 0;
			}
			else
			{
				term_start ++;
				path.erase(term_start, slash_index + 1);
			}
		}
		else
		{
			term_start = slash_index + 1;
		}
	}
	return path;
}

JSString JS::Runtime::DefaultHost::MapImportPath(JSString path)
{
	path = NormalizeImportPath(path);

	auto mapped_path = ImportMap.find(path);
	if(mapped_path != ImportMap.end())
	{
		return mapped_path->second;
	}

	// start with longest prefix, work backwards
	for(size_t slash_index = path.rfind(u'/');
		slash_index != std::u16string::npos;
		slash_index = path.rfind(u'/', slash_index - 1))
	{
		if(slash_index == 0)
		{
			// do not interpret this part (initial "/")
			break;
		}

		auto mapped_path = ImportPrefixMap.find(path.substr(0, slash_index + 1));
		if(mapped_path != ImportPrefixMap.end())
		{
			return mapped_path->second + path.substr(slash_index + 1);
		}
	}

	return path;
}

JSValue JS::Runtime::DefaultHost::LoadRequiredModule(JSString path)
{
	// attempts to load a module via the CommonJS built-in 'require' function
	JS::Runtime::ScriptOrModuleRecord * record = LoadInterchangeModule(ModuleRequestRecord(path), /* async = */ false);

	if(auto cjs_record = dynamic_cast<JS::Runtime::ScriptRecord *>(record))
	{
		// return exports object
		return JS::Get(cjs_record->CommonJSModule, u"exports");
	}
	else if(auto amd_record = dynamic_cast<JS::Runtime::AsynchronousModuleRecord *>(record))
	{
		amd_record->Enable();
		RequiredQueue.push_back(amd_record->Identifier);
		CheckDefinedModulesLoaded();
		// return exports object
		return amd_record->Exports;
	}
	else if(auto esm_record = dynamic_cast<JS::Runtime::ModuleRecord *>(record))
	{
		// return the module namespace
		return esm_record->GetModuleNamespace();
	}
	else
	{
		JS::Throw(JS::New<InternalError> __LOC("unexpected module type"));
	}
}

void JS::Runtime::DefaultHost::LoadDefinedModuleDependency(JSString path)
{
	// attempts to load a module via the AMD dependency list of the built-in 'define' function
	JS::Runtime::ScriptOrModuleRecord * record = LoadInterchangeModule(ModuleRequestRecord(path), /* async = */ false); // TODO: async should be true

	if([[maybe_unused]] auto amd_record = dynamic_cast<JS::Runtime::AsynchronousModuleRecord *>(record))
	{
	}
	else
	{
		JS::Throw(JS::New<Error> __LOC("unexpected module type"));
	}
}

void JS::Runtime::DefaultHost::DeclareDefine(AsynchronousModuleDefinition definition)
{
	if(definition.Script != nullptr)
	{
		definition.Script->UsesAMD = true;
	}
	DefinitionQueue.push_back(definition);
}

bool JS::Runtime::DefaultHost::IsSupportedImportAttribute(JSString attribute)
{
	return attribute == u"type";
}

void JS::Runtime::DefaultHost::LoadImportedModule(std::set<LoadedModuleRequestRecord> * referrer_loaded_modules, ModuleRequestRecord moduleRequest, std::shared_ptr<Payload> payload)
{
	// attempts to load a module via the ECMAScript 6 'import' statement or dynamic 'import()'
	// referrer_loaded_modules is the set of loaded modules of either the caller script/module or the realm if no script record is available
	JS::Runtime::ModuleRecord * record = nullptr;
	JSValue error_result = JS_undefined;

	try
	{
		auto script_or_module_record = LoadInterchangeModule(moduleRequest, /* async = */ true);

		if(auto cjs_record = dynamic_cast<JS::Runtime::ScriptRecord *>(script_or_module_record))
		{
			// create a synthetic module record from the CommonJS export
			record = cjs_record->GetSyntheticModule();
		}
		else
		{
			record = dynamic_cast<JS::Runtime::ModuleRecord *>(script_or_module_record);
		}

		if(record == nullptr)
		{
			JS::Throw(JS::New<InternalError> __LOC("unexpected module type"));
		}
	}
	catch(JS::Class::Internal::Value * thrown_value)
	{
		error_result = JSValue::WrapValue(thrown_value);
	}
	catch(...)
	{
		error_result = JS::New<InternalError> __LOC("unhandled native error");
	}

	if(record != nullptr)
	{
		// normal completion
		FinishLoadingImportedModule_NormalCompletion(referrer_loaded_modules, moduleRequest, payload, record);
	}
	else
	{
		// throw completion
		FinishLoadingImportedModule_ThrowCompletion(referrer_loaded_modules, moduleRequest, payload, error_result);
	}
}

void JS::Runtime::DefaultHost::FinishLoadingDefinedModule()
{
	std::vector<AsynchronousModuleDefinition> Queue = DefinitionQueue;
	DefinitionQueue.erase(DefinitionQueue.begin(), DefinitionQueue.begin() + Queue.size());

	// corresponds to onScriptLoad in RequireJS sources

	for(auto moduleDefinition : Queue)
	{
		ModuleRequestRecord moduleRequest(moduleDefinition.Identifier);
		AsynchronousModuleRecord * amd = nullptr;
		auto imported_module = LoadedModules.find(moduleRequest);
		if(imported_module != LoadedModules.end())
		{
			amd = dynamic_cast<AsynchronousModuleRecord *>(imported_module->second);
		}
		if(amd == nullptr)
		{
			amd = new AsynchronousModuleRecord(moduleDefinition.Identifier);
			LoadedModules[moduleRequest] = amd;
		}
		amd->Initialize(moduleDefinition);
	}
	CheckDefinedModulesLoaded();
}

void JS::Runtime::DefaultHost::EnableDefinedModule(JSString identifier)
{
	RequiredQueue.push_back(identifier);

	ModuleRequestRecord moduleRequest(identifier);
	auto main_module_iterator = LoadedModules.find(moduleRequest);
	if(main_module_iterator != LoadedModules.end())
	{
		if(auto main_module = dynamic_cast<AsynchronousModuleRecord *>(main_module_iterator->second))
		{
			main_module->Enable();
			CheckDefinedModulesLoaded();
		}
	}
}

void JS::Runtime::DefaultHost::BreakDefinedModuleCycles(AsynchronousModuleRecord * module, std::set<AsynchronousModuleRecord *>& traced, std::set<AsynchronousModuleRecord *>& processed)
{
	// corresponds to breakCycle() in RequireJS
	traced.insert(module);
	for(auto dependency : module->Dependencies)
	{
		auto loaded_module_iterator = LoadedModules.find(dependency);
		if(loaded_module_iterator != LoadedModules.end())
		{
			if(auto loaded_module = dynamic_cast<AsynchronousModuleRecord *>(loaded_module_iterator->second))
			{
				if(module->DependencyExports.find(loaded_module->Identifier) == module->DependencyExports.end())
				{
					BreakDefinedModuleCycles(module, loaded_module, traced, processed);
				}
			}
			else
			{
				JS::Throw(JS::New<Error> __LOC("invalid module type"));
			}
		}
	}
	processed.insert(module);
}

void JS::Runtime::DefaultHost::BreakDefinedModuleCycles(AsynchronousModuleRecord * module, AsynchronousModuleRecord * dependency, std::set<AsynchronousModuleRecord *>& traced, std::set<AsynchronousModuleRecord *>& processed)
{
	if(processed.find(dependency) == processed.end())
	{
		if(traced.find(dependency) == traced.end())
		{
			BreakDefinedModuleCycles(dependency, traced, processed);
		}
		else if(module != nullptr)
		{
			module->DependencyExports[dependency->Identifier] = dependency->Exports;
			module->DependencyCount -= 1;
			module->Check();
		}
	}
}

void JS::Runtime::DefaultHost::CheckDefinedModulesLoaded()
{
	// corresponds to checkLoaded() in RequireJS
	bool needsCycleBreak = true;
	inCheckDefinedModulesLoaded = true;
	for(auto module_iterator : LoadedModules)
	{
		if(auto module = dynamic_cast<AsynchronousModuleRecord *>(module_iterator.second))
		{
			if(module->IsEnabled == AsynchronousModuleRecord::EnabledState::Disabled)
			{
				continue;
			}
			if(module->IsInitialized == AsynchronousModuleRecord::LoadState::Fetched)
			{
				needsCycleBreak = false;
				break;
			}
		}
	}
	// break cycles
	if(needsCycleBreak)
	{
		std::set<AsynchronousModuleRecord *> traced;
		std::set<AsynchronousModuleRecord *> processed;

		for(size_t module_index = 0; module_index < RequiredQueue.size(); module_index++)
		{
			auto required_module = RequiredQueue[module_index];
			auto loaded_module_iterator = LoadedModules.find(required_module);
			if(loaded_module_iterator != LoadedModules.end())
			{
				if(auto loaded_module = dynamic_cast<AsynchronousModuleRecord *>(loaded_module_iterator->second))
				{
					BreakDefinedModuleCycles(nullptr, loaded_module, traced, processed);
				}
				else
				{
					JS::Throw(JS::New<Error> __LOC("invalid module type"));
				}
			}
		}
	}
	inCheckDefinedModulesLoaded = false;
}

JS::Runtime::SyntheticModuleRecord * JS::Runtime::DefaultHost::LoadJSONFileAsModule(std::filesystem::path host_path)
{
	JSValue json = LoadJSONFile(host_path);
	SyntheticModuleRecord * SyntheticModule = new SyntheticModuleRecord(GetCurrentRealm(), {u"default"}, std::function<void(SyntheticModuleRecord *)>(
		[json](SyntheticModuleRecord * module)
	{
		module->SetSyntheticModuleExport(u"default", json);
	}), host_path);
	return SyntheticModule;
}

JS::Runtime::SyntheticModuleRecord * JS::Runtime::DefaultHost::LoadTextFileAsModule(std::filesystem::path host_path)
{
	JSString string = LoadTextFile(host_path);
	SyntheticModuleRecord * SyntheticModule = new SyntheticModuleRecord(GetCurrentRealm(), {u"default"}, std::function<void(SyntheticModuleRecord *)>(
		[string](SyntheticModuleRecord * module)
	{
		module->SetSyntheticModuleExport(u"default", string);
	}), host_path);
	return SyntheticModule;
}

JS::Runtime::ScriptOrModuleRecord * JS::Runtime::DefaultHost::LoadInterchangeModule(ModuleRequestRecord moduleRequest, bool async)
{
	auto imported_module = LoadedModules.find(moduleRequest);
	if(imported_module != LoadedModules.end())
	{
		if(auto amd = dynamic_cast<AsynchronousModuleRecord *>(imported_module->second))
		{
			if(amd->IsInitialized == AsynchronousModuleRecord::LoadState::Initialized)
			{
				return imported_module->second;
			}
		}
		else
		{
			return imported_module->second;
		}
	}

	JS::Parser::Parser::ExpectedScriptType scriptType = JS::Parser::Parser::ExpectedScriptType::Either;

	ModuleType moduleType = ModuleType::Default;
	ScriptType moduleScriptType = ScriptType::ECMAScript;

	if(moduleRequest.Attributes.find(u"type") != moduleRequest.Attributes.end())
	{
		JSString typeAttribute = moduleRequest.Attributes[u"type"];
		if(typeAttribute == u"json")
		{
			moduleType = ModuleType::JSON;
		}
		else if(typeAttribute == u"css")
		{
			moduleType = ModuleType::Style;
		}
		else if(typeAttribute == u"text")
		{
			moduleType = ModuleType::Text;
		}
		// TODO: unrecognized
	}

	// TODO: attributes
	// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import/with
	// "type": "json" (application/json) also "destination": "json"
	// "type": "css" (text/css) also "destination": "style"
	// "type": "text" also "destination": "text"
	// "resolution-mode": "require"
	// "resolution-mode": "import"

	std::filesystem::path original_host_path = MapImportPath(moduleRequest.Specifier);
	std::filesystem::path host_path = original_host_path;
	if(!std::filesystem::exists(host_path))
	{
		host_path += ".js";
		if(!std::filesystem::exists(host_path))
		{
			host_path = original_host_path;
			host_path += ".json";
			if(!std::filesystem::exists(host_path))
			{
				host_path = original_host_path;
				host_path += ".node";
			}
		}
	}

	if(moduleType == ModuleType::Default)
	{
		if(host_path.extension() == ".node")
		{
			moduleType = ModuleType::Script;
			moduleScriptType = ScriptType::NodeAPI;
		}
		else if(host_path.extension() == ".mjs")
		{
			moduleType = ModuleType::Script;
			moduleScriptType = ScriptType::ECMAScript;
		}
		else if(host_path.extension() == ".cjs")
		{
			moduleType = ModuleType::Script;
			moduleScriptType = ScriptType::ECMAScript;
		}
		else if(host_path.extension() == ".json")
		{
			moduleType = ModuleType::JSON;
		}
		else if(host_path.extension() == ".wasm")
		{
			moduleType = ModuleType::Script;
			moduleScriptType = ScriptType::WebAssembly;
		}
		else
		{
			moduleType = ModuleType::Script;
		}
	}

	switch(moduleType)
	{
	case ModuleType::Default: // should not happen
	case ModuleType::Script:
		switch(moduleScriptType)
		{
		case ScriptType::ECMAScript:
			// handle this outside the switch branches
			break;
		case ScriptType::NodeAPI:
			{
				auto record = new JS::Napi::NativeModuleRecord(host_path.string(), JS::GetCurrentRealm());
				LoadedModules[moduleRequest] = record; // make sure we do not get into an infinite recursion with loading
				record->CommonJSModule = JS::New<JS::Class::OrdinaryObject>();
				record->Perform();
				return record;
			}
		case ScriptType::WebAssembly:
			{
				// TODO
				return nullptr;
			}
		default:
			assert(false);
		}
		break;
	case ModuleType::JSON:
		{
			auto record = LoadJSONFileAsModule(host_path);
			LoadedModules[moduleRequest] = record;
			return record;
		}
	case ModuleType::Text:
		{
			auto record = LoadTextFileAsModule(host_path);
			LoadedModules[moduleRequest] = record;
			return record;
		}
	case ModuleType::Style:
		{
			// TODO
			return nullptr;
		}
	default:
		assert(false);
	}

	if(host_path.extension() == ".mjs")
	{
		scriptType = JS::Parser::Parser::ExpectedScriptType::Module;
	}
	else if(host_path.extension() == ".cjs")
	{
		if(!JS::Enable_CommonJS)
		{
			JS::Throw(JS::New<Error> __LOC("CommonJS not enabled"));
		}
		scriptType = JS::Parser::Parser::ExpectedScriptType::Script;
	}
	else
	{
		if(!JS::Enable_CommonJS && !JS::Enable_AMD)
		{
			scriptType = JS::Parser::Parser::ExpectedScriptType::Module;
		}
	}

	std::ifstream input(host_path);
	JS::Parser::UTF8StreamTokenizer tokenizer(input);
	//tokenizer.last_location.filename = host_path;
	tokenizer.last_location.filename = std::filesystem::path(moduleRequest.Specifier);
	JS::Parser::Parser parser(&tokenizer);

	// TODO: store the host_path as well

	JS::Runtime::ScriptOrModuleRecord * record = parser.ParseScriptOrModule(JS::GetCurrentRealm(), scriptType);
	if(imported_module == LoadedModules.end())
	{
		LoadedModules[moduleRequest] = record; // for CommonJS, this makes sure we do not get into an infinite recursion with loading
	}
	// for AMD, initialize the record afterwards (TODO: how does this interoperate with other module systems?)

	if(auto cjs_record = dynamic_cast<JS::Runtime::ScriptRecord *>(record))
	{
		SetupWrappers();
		record->Perform(); // evaluate synchronously
		if(cjs_record->CommonJSModule != nullptr)
		{
			JS::Set(cjs_record->CommonJSModule, u"loaded", true);
		}

		// check if an AMD module has appeared for this request, replace the script record with it
		imported_module = LoadedModules.find(moduleRequest);
		if(imported_module != LoadedModules.end() && dynamic_cast<JS::Runtime::AsynchronousModuleRecord *>(imported_module->second))
		{
			record = imported_module->second;
		}
	}
	// for ECMAScript 6, modules will be processed incrementally along the other modules
	else if([[maybe_unused]] auto esm_record = dynamic_cast<JS::Runtime::ModuleRecord *>(record))
	{
		if(!async)
		{
			record->Perform(); // evaluate immediately
		}
		// it will be processed incrementally along the other modules
	}

	return record;
}

