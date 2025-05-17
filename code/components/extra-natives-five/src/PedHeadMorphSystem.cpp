#include <StdInc.h>

#include "Hooking.Patterns.h"
#include "Hooking.Stubs.h"
#include "ScriptEngine.h"
#include "Streaming.h"
#include "jitasm.h"
#include "GameInit.h"
#include "PedHeadMorphSystem.h"

#define DEBUG_TRACE(...) if (PED_HEAD_MORPH_DEBUG) trace(__VA_ARGS__);

static constexpr float kMicroMorphMinValue = 0.001f;

#pragma region morph structs
void HeadMorphInst::AddRef()
{
	++m_numRefs;

#if _DEBUG
	// shouldn't happen during basic tests, remove before releasing
	assert(m_numRefs < 32);
#endif

	DEBUG_TRACE("%s: Incrementing refs count to %d (%s)\n", __FUNCTION__, GetRefCount(), GetModelName());

	UpdateGeometry();
}

void HeadMorphInst::RemoveRef()
{
	--m_numRefs;

	assert(m_numRefs >= 0);

	DEBUG_TRACE("%s: Decrementing refs count to %d (%s)\n", __FUNCTION__, GetRefCount(), GetModelName());

	UpdateGeometry();
}

void HeadMorphInst::UpdateGeometry()
{
	if (GetRefCount() == 0 && GetGeometry())
	{
		ReleaseGeometry();
	}
	else if (GetRefCount() > 0 && !GetGeometry())
	{
		RequestGeometry();
	}
}

void HeadMorphInst::RequestGeometry()
{
	DEBUG_TRACE("%s: Requesting morph geometry (current: %p)\n", __FUNCTION__, (void*)GetGeometry());
	assert(GetGeometry() == nullptr);

	if (auto drawable = HeadMorphManager::LoadMorphDrawable(GetModelName()))
	{
		if (auto model = drawable->GetLodGroup().GetPrimaryModel()->Get(0))
		{
			if (auto geometry = model->GetGeometries().Get(0))
			{
				SetGeometry(geometry);
			}
		}
	}
}

void HeadMorphInst::ReleaseGeometry()
{
	DEBUG_TRACE("%s: Releasing morph geometry (current: %p)\n", __FUNCTION__, (void*)GetGeometry());
	assert(GetGeometry() != nullptr);

	HeadMorphManager::UnloadMorphDrawable(GetModelName());
	SetGeometry(nullptr);
}
#pragma endregion

#pragma region head morph manager
void HeadMorphManager::RegisterPed(CPed* ped, uint32_t index)
{
	m_pedsStore[index] = ped;
	DEBUG_TRACE("HeadBlendStorage: Added blend index %d of ped %x\n", index, (uint64_t)ped);
}

void HeadMorphManager::UnregisterPed(uint32_t index)
{
	if (const auto it = m_pedsStore.find(index); it != m_pedsStore.end())
	{
		DEBUG_TRACE("HeadBlendStorage: Removed blend index %d of ped %x\n", index, (uint64_t)m_pedsStore[index]);
		m_pedsStore.erase(index);
	}
}

CPed* HeadMorphManager::GetPedFromBlendIndex(uint32_t index)
{
	if (const auto it = m_pedsStore.find(index); it != m_pedsStore.end())
	{
		return m_pedsStore[index];
	}

	return nullptr;
}

uint32_t HeadMorphManager::GetBlendIndexFromPed(const CPed* ped)
{
	for (auto& entry : m_pedsStore)
	{
		if (entry.second == ped)
		{
			return entry.first;
		}
	}

	return -1;
}

bool HeadMorphManager::RegisterMorph(const std::string& morphName, const std::string& targetModel)
{
	const char* modelName = va("%s/%s", targetModel, morphName);

	if (HasMorphInst(morphName, targetModel))
	{
		DEBUG_TRACE("%s: Morph \"%s\" is already registered!\n", __FUNCTION__, modelName);
		return false;
	}

	HeadMorphInst microMorph(modelName);
	m_morphStore.insert({ { HashString(morphName), HashString(targetModel) }, microMorph });

	DEBUG_TRACE("%s: Morph \"%s\" was successfully registered!\n", __FUNCTION__, modelName);

	return true;
}

bool HeadMorphManager::HasMorphInst(const std::string& morphName, const std::string& targetModel)
{
	return HasMorphInst(HashString(morphName), HashString(targetModel));
}

bool HeadMorphManager::HasMorphInst(uint32_t morphHash, uint32_t modelHash)
{
	return m_morphStore.find({ morphHash, modelHash }) != m_morphStore.end();
}

HeadMorphInst& HeadMorphManager::GetMorphInst(uint32_t morphHash, uint32_t modelHash)
{
	if (const auto it = m_morphStore.find({ morphHash, modelHash }); it != m_morphStore.end())
	{
		return it->second;
	}

	assert("HeadMorphManager::GetMorphInst returns null" && false);
}

gtaDrawable* HeadMorphManager::LoadMorphDrawable(const std::string& modelName)
{
	DEBUG_TRACE("%s: Requesting morph drawable \"%s\"\n", __FUNCTION__, modelName);

	streaming::Manager* streaming = streaming::Manager::GetInstance();
	auto ydrStore = streaming->moduleMgr.GetStreamingModule("ydr");

	uint32_t slotIndex = -1;

	if (ydrStore->FindSlotFromHashKey(&slotIndex, modelName.c_str()))
	{
		streaming->RequestObject(slotIndex + ydrStore->baseIdx, 6);
		streaming::LoadObjectsNow(0);

		return (gtaDrawable*)ydrStore->GetPtr(slotIndex);
	}

	return nullptr;
}

void HeadMorphManager::UnloadMorphDrawable(const std::string& modelName)
{
	DEBUG_TRACE("%s: Releasing morph drawable \"%s\"\n", __FUNCTION__, modelName);

	streaming::Manager* streaming = streaming::Manager::GetInstance();
	auto yddStore = streaming->moduleMgr.GetStreamingModule("ydd");

	uint32_t slotIndex = -1;

	if (yddStore->FindSlotFromHashKey(&slotIndex, modelName.c_str()))
	{
		streaming->ReleaseObject(slotIndex);
	}
}

void HeadMorphManager::Reset()
{
	for (auto& [index, ped] : m_pedsStore)
	{
		if (ped && ped->IsOfType<CPed>())
		{
			if (const auto extension = ped->GetExtension<PedHeadMorphExtension>())
			{
				DEBUG_TRACE("%s: Resetting ped %p morphs\n", __FUNCTION__, (void*)ped);
				extension->ResetMorphs();
			}
		}
	}

	DEBUG_TRACE("%s: Peds and morphs stores had %d and %d entries\n", __FUNCTION__, m_pedsStore.size(), m_morphStore.size());

	m_pedsStore.clear();
	m_morphStore.clear();
}
#pragma endregion

#pragma region ped extension
PedHeadMorphExtension* PedHeadMorphExtension::GetOrAdd(CPed* ped)
{
	auto extension = ped->GetExtension<PedHeadMorphExtension>();

	if (!extension)
	{
		extension = new PedHeadMorphExtension();
		ped->AddExtension(extension);
	}

	return extension;
}

HeadMorphState& PedHeadMorphExtension::GetMorph(uint32_t morphName)
{
	if (const auto it = m_morphs.find(morphName); it != m_morphs.end())
	{
		return it->second;
	}

	assert("PedHeadMorphExtension::GetMorph returns null" && false);
}

bool PedHeadMorphExtension::HasMorph(uint32_t morphName)
{
	return m_morphs.find(morphName) != m_morphs.end();
}

void PedHeadMorphExtension::AddMorph(uint32_t morphName, HeadMorphState& morphState)
{
	DEBUG_TRACE("%s: Add morph %x\n", __FUNCTION__, morphName);

	if (const auto it = m_morphs.find(morphName); it != m_morphs.end())
	{
		DEBUG_TRACE("%s: Removing previous morph of %x\n", __FUNCTION__, morphName);

		it->second.GetInst()->RemoveRef();
		m_morphs.erase(morphName);
	}

	m_morphs.insert({ morphName, morphState });
	morphState.GetInst()->AddRef();
}

void PedHeadMorphExtension::RemoveMorph(uint32_t morphName)
{
	DEBUG_TRACE("%s: Remove morph %x\n", __FUNCTION__, morphName);

	if (const auto it = m_morphs.find(morphName); it != m_morphs.end())
	{
		it->second.GetInst()->RemoveRef();
		m_morphs.erase(it);
	}
}

void PedHeadMorphExtension::ResetMorphs()
{
	DEBUG_TRACE("%s: Reset morphs\n", __FUNCTION__);

	for (auto& it : m_morphs)
	{
		it.second.GetInst()->RemoveRef();
	}

	m_morphs.clear();
}

bool PedHeadMorphExtension::HasMorphs()
{
	return m_morphs.size() > 0;
}

std::map<uint32_t, HeadMorphState>& PedHeadMorphExtension::GetMorphs()
{
	return m_morphs;
}
#pragma endregion

#pragma region game code
static inline uint32_t GetEntityModelNameHash(fwEntity* entity)
{
	if (const auto modelInfo = *(uint64_t*)((char*)entity + 0x20))
	{
		return *(uint32_t*)(modelInfo + 0x18);
	}

	return 0;
}

static inline bool DoesPedHaveHeadBlendData(fwEntity* entity)
{
	return (*(BYTE*)(*(uint64_t*)((char*)entity + 32) + 646) & 2);
}

static hook::cdecl_stub<void(rage::grmGeometryQB*, rage::grmGeometryQB*, float)> rage__grmGeometryQB__applyMorph([]()
{
	return hook::get_pattern("74 17 8D 57 03", -0x2D);
});

static CPed* g_currentBlendingPed = nullptr;

static void* g_meshBlendManager = nullptr;

static uint32_t (*g_origMeshBlendManagerCreateBlend)(void*, int, CPed*, int, int, int, int, int, int, float, float, float, bool, bool, bool);

static uint32_t MeshBlendManagerCreateBlend(void* mgr, int unk1, CPed* ped, int geometry1, int texture1, int geometry2, int texture2, int geometry3, int texture3, float geometryBlend, float textureBlend, float unkBlend, bool unk2, bool unk3, bool unk4)
{
	auto blendIndex = g_origMeshBlendManagerCreateBlend(mgr, unk1, ped, geometry1, texture1, geometry2, texture2, geometry3, texture3, geometryBlend, textureBlend, unkBlend, unk2, unk3, unk4);

	TheHeadMorphManager.RegisterPed(ped, blendIndex);

	return blendIndex;
}

static void (*g_origMeshBlendManagerDeleteBlend)(void*, uint32_t, bool);

static void MeshBlendManagerDeleteBlend(void* mgr, uint32_t blendIndex, bool unk)
{
	TheHeadMorphManager.UnregisterPed(blendIndex);

	g_origMeshBlendManagerDeleteBlend(mgr, blendIndex, unk);
}

static void (*g_origMeshBlendManagerHandleBlend)(void*, uint32_t);

static void MeshBlendManagerHandleBlend(void* mgr, uint32_t blendIndex)
{
	// Save mesh blend manager pointer
	// TODO: replace with a proper hook::get_address
	g_meshBlendManager = mgr;

	g_currentBlendingPed = TheHeadMorphManager.GetPedFromBlendIndex(blendIndex);

	g_origMeshBlendManagerHandleBlend(mgr, blendIndex);

	g_currentBlendingPed = nullptr;
}

static void HandlePedCustomMicroMorph(rage::grmGeometryQB* headGeometry)
{
	// We're blending something but not aware of a target? Weird!
	if (!g_currentBlendingPed)
	{
		//DEBUG_TRACE("%s: missing target ped for %p!!!\n", (void*)headGeometry);
		//return;
		assert("Missing target ped for blending" && false);
	}

	// Should never happen, remove after testing.
	if (!g_currentBlendingPed->IsOfType<CPed>())
	{
		assert("Currently blending ped is not a ped" && false);
	}

	// No required extension on ped? Skip!
	const auto extension = g_currentBlendingPed->GetExtension<PedHeadMorphExtension>();
	if (!extension)
	{
		return;
	}

	for (auto& [hash, state] : extension->GetMorphs())
	{
		if (auto inst = state.GetInst())
		{
			if (auto morphGeometry = inst->GetGeometry())
			{
				rage__grmGeometryQB__applyMorph(headGeometry, morphGeometry, state.GetValue());
				DEBUG_TRACE("%s: Handled morph %s (%x) to %f\n", __FUNCTION__, inst->GetModelName(), hash, state.GetValue());
			}
			else
			{
				inst->UpdateGeometry();
				DEBUG_TRACE("%s: Failed to handle morph - no geometry for %s (%x)\n", __FUNCTION__, inst->GetModelName(), hash);
			}
		}
		else
		{
			DEBUG_TRACE("%s: Failed to handle morph - no morph inst for %x\n", __FUNCTION__, hash);
		}
	}
}
#pragma endregion

static HookFunction hookFunction([]
{
	{
		auto location = hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8B 9C 24 ? ? ? ? 45 33 E4"));
		g_origMeshBlendManagerCreateBlend = hook::trampoline(location, &MeshBlendManagerCreateBlend);
	}

	{
		auto location = hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8D 7E 18 48 8B 0F"));
		g_origMeshBlendManagerDeleteBlend = hook::trampoline(location, &MeshBlendManagerDeleteBlend);
	}

	{
		auto location = hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 1D ? ? ? ? FF C6"));
		g_origMeshBlendManagerHandleBlend = hook::trampoline(location, &MeshBlendManagerHandleBlend);
	}

	{
		static struct : jitasm::Frontend
		{
			int* flags;

			virtual void InternalMain() override
			{
				// original code
				mov(r9d, *flags);

				// geometryQB pointer as first argument
				mov(rcx, r13);

				sub(rsp, 0x28);

				mov(rax, (uintptr_t)HandlePedCustomMicroMorph);
				call(rax);

				add(rsp, 0x28);

				ret();
			}

		} microMorphStub;


		auto location = hook::get_pattern<char>("48 83 C3 04 48 FF CF 75 82", 9);
		microMorphStub.flags = hook::get_address<int*>(location + 3);

		hook::nop(location, 7);
		hook::call(location, microMorphStub.GetCode());
	}
});

static InitFunction initFunction([]()
{
	OnKillNetworkDone.Connect([=]()
	{
		TheHeadMorphManager.Reset();
	});

	// crun Citizen.InvokeNative(`REGISTER_CUSTOM_HEAD_MICRO_MORPH` & 0xFFFFFFFF, "mp_m_freemode_01", "micro_cheek_up")
	fx::ScriptEngine::RegisterNativeHandler("REGISTER_CUSTOM_HEAD_MICRO_MORPH", [](fx::ScriptContext& context)
	{
		const auto targetModel = std::string(context.CheckArgument<char*>(0));
		const auto morphModel = std::string(context.CheckArgument<char*>(1));

		const char* modelName = va("%s/%s", targetModel, morphModel);

		if (TheHeadMorphManager.HasMorphInst(morphModel, targetModel))
		{
			DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Morph \"%s\" is already registered\n", modelName);
			return false;
		}

		// Testing morph drawable
		if (auto drawable = TheHeadMorphManager.LoadMorphDrawable(modelName))
		{
			DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Morph \"%s\" drawable address %p (vtable %x)\n", modelName, (void*)drawable, *(uint64_t*)drawable);

			if (auto model = drawable->GetLodGroup().GetPrimaryModel()->Get(0))
			{
				if (auto geometry = model->GetGeometries().Get(0))
				{
					auto success = TheHeadMorphManager.RegisterMorph(morphModel, targetModel);

					TheHeadMorphManager.UnloadMorphDrawable(modelName);

					DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Status of \"%s\" - %s\n", modelName, success ? "successfully registered" : "failed to register");

					return success;
				}
				else
				{
					DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Morph primary model has no geometry! (%s)\n", modelName);
				}
			}
			else
			{
				DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Morph drawable has no primary model! (%s)\n", modelName);
			}
		}
		else
		{
			DEBUG_TRACE("REGISTER_CUSTOM_HEAD_MICRO_MORPH: Failed to find morph drawable! (%s/%s)\n", morphModel, targetModel);
		}

		return false;
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_PED_CUSTOM_HEAD_MICRO_MORPH", [](fx::ScriptContext& context)
	{
		auto handle = context.GetArgument<uint32_t>(0);
		auto ped = rage::fwScriptGuid::GetBaseFromGuid(handle);

		if (ped && ped->IsOfType<CPed>() && DoesPedHaveHeadBlendData(ped))
		{
			auto morphName = context.CheckArgument<char*>(1);
			auto morphHash = HashString(morphName);
			auto modelHash = GetEntityModelNameHash(ped);

			if (TheHeadMorphManager.HasMorphInst(morphHash, modelHash))
			{
				auto value = context.GetArgument<float>(2);
				bool shouldDelete = (abs(value) < kMicroMorphMinValue);

				auto extension = ped->GetExtension<PedHeadMorphExtension>();

				if (extension && extension->HasMorph(morphHash))
				{
					auto& oldState = extension->GetMorph(morphHash);

					if (shouldDelete)
					{
						extension->RemoveMorph(morphHash);
						DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Removed micro morph \"%s\" due to value (was %f)\n", morphName, oldState.GetValue());

						if (!extension->HasMorphs())
						{
							ped->DestroyExtension(extension);
							DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Destroyed ped extension, no morphs left\n", morphName);
						}
					}
					else
					{
						DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Set micro morph \"%s\" from %f to %f value\n", morphName, oldState.GetValue(), value);
						oldState.SetValue(value);
					}
				}
				else if (!shouldDelete)
				{
					if (TheHeadMorphManager.HasMorphInst(morphHash, modelHash))
					{
						// Now ensure that we have an extension
						if (!extension)
						{
							extension = PedHeadMorphExtension::GetOrAdd(reinterpret_cast<CPed*>(ped));
							DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Added missing extension to ped %p\n", (void*)ped);
						}

						auto& morphInst = TheHeadMorphManager.GetMorphInst(morphHash, modelHash);

						// Finally we can add new morph state
						HeadMorphState morphState(&morphInst, value);
						extension->AddMorph(morphHash, morphState);

						DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Added new micro morph \"%s\" with value %f\n", morphName, value);
					}
					else
					{
						DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Attempt to add micro morph \"%s\" with no inst!\n", morphName);
					}
				}

				// Force the game to update head blending so we can apply our custom stuff immediately.
				if (g_meshBlendManager)
				{
					auto blendIndex = TheHeadMorphManager.GetBlendIndexFromPed(reinterpret_cast<CPed*>(ped));
					if (blendIndex != -1)
					{
						MeshBlendManagerHandleBlend(g_meshBlendManager, blendIndex);
					}
				}
			}
			else
			{
				DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph %x\n", handle, morphHash);
			}
		}
		else
		{
			DEBUG_TRACE("SET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) is not a ped or have no head blend data\n", handle);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("RESET_PED_CUSTOM_HEAD_MICRO_MORPH", [](fx::ScriptContext& context)
	{
		auto handle = context.GetArgument<uint32_t>(0);
		auto ped = rage::fwScriptGuid::GetBaseFromGuid(handle);

		if (ped && ped->IsOfType<CPed>() && DoesPedHaveHeadBlendData(ped))
		{
			auto morphName = context.CheckArgument<char*>(1);
			auto morphHash = HashString(morphName);
			auto modelHash = GetEntityModelNameHash(ped);

			// TODO: maybe don't even try to search for this in a global store as
			// if ped doesn't have morph data with such name, it means it shouldn't exist
			if (TheHeadMorphManager.HasMorphInst(morphHash, modelHash))
			{
				auto extension = ped->GetExtension<PedHeadMorphExtension>();

				if (extension && extension->HasMorph(morphHash))
				{
					DEBUG_TRACE("RESET_PED_CUSTOM_HEAD_MICRO_MORPH: Removed micro morph \"%s\" as requested\n", morphName);
					extension->RemoveMorph(morphHash);

					if (!extension->HasMorphs())
					{
						ped->DestroyExtension(extension);
						DEBUG_TRACE("RESET_PED_CUSTOM_HEAD_MICRO_MORPH: Destroyed ped extension, no morphs left\n", morphName);
					}
				}
			}
			else
			{
				DEBUG_TRACE("RESET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph %x\n", handle, morphHash);
			}
		}
		else
		{
			DEBUG_TRACE("RESET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) is not a ped or have no head blend data\n", handle);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_CUSTOM_HEAD_MICRO_MORPH", [](fx::ScriptContext& context)
	{
		float result = 0.0f;

		auto handle = context.GetArgument<uint32_t>(0);
		auto ped = rage::fwScriptGuid::GetBaseFromGuid(handle);

		if (ped && ped->IsOfType<CPed>() && DoesPedHaveHeadBlendData(ped))
		{
			auto morphHash = HashString(context.CheckArgument<char*>(1));
			auto modelHash = GetEntityModelNameHash(ped);

			// TODO: maybe don't even try to search for this in a global store as if ped doesn't have morph data with such name, it means it shouldn't exist
			if (TheHeadMorphManager.HasMorphInst(morphHash, modelHash))
			{
				if (const auto extension = ped->GetExtension<PedHeadMorphExtension>())
				{
					if (extension->HasMorph(morphHash))
					{
						result = extension->GetMorph(morphHash).GetValue();
					}
				}
				else
				{
					DEBUG_TRACE("GET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph extension\n", handle);
				}
			}
			else
			{
				DEBUG_TRACE("GET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph %x\n", handle, morphHash);
			}
		}
		else
		{
			DEBUG_TRACE("GET_PED_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) is not a ped or have no head blend data\n", handle);
		}

		context.SetResult<float>(result);
	});

	fx::ScriptEngine::RegisterNativeHandler("DOES_PED_USE_CUSTOM_HEAD_MICRO_MORPH", [](fx::ScriptContext& context)
	{
		bool result = false;

		auto handle = context.GetArgument<uint32_t>(0);
		auto ped = rage::fwScriptGuid::GetBaseFromGuid(handle);

		if (ped && ped->IsOfType<CPed>() && DoesPedHaveHeadBlendData(ped))
		{
			auto morphHash = HashString(context.CheckArgument<char*>(1));
			auto modelHash = GetEntityModelNameHash(ped);

			// TODO: maybe don't even try to search for this in a global store as if ped doesn't have morph data with such name, it means it shouldn't exist
			if (TheHeadMorphManager.HasMorphInst(morphHash, modelHash))
			{
				if (const auto extension = ped->GetExtension<PedHeadMorphExtension>())
				{
					result = extension->HasMorph(morphHash);
				}
				else
				{
					DEBUG_TRACE("DOES_PED_USE_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph extension\n", handle);
				}
			}
			else
			{
				DEBUG_TRACE("DOES_PED_USE_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) have no micro morph %x\n", handle, morphHash);
			}
		}
		else
		{
			DEBUG_TRACE("DOES_PED_USE_CUSTOM_HEAD_MICRO_MORPH: Passed entity (%d) is not a ped or have no head blend data\n", handle);
		}

		context.SetResult<bool>(result);
	});
});
