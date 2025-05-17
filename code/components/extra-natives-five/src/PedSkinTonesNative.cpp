#include <StdInc.h>
#include "Hooking.h"
#include "atArray.h"
#include "ScriptEngine.h"

struct CPedSkinTones__sSkinComp
{
	atArray<uint32_t> skin;
};

struct CPedSkinTones
{
	CPedSkinTones__sSkinComp comps[3];
	atArray<uint8_t> males;
	atArray<uint8_t> females;
	atArray<uint8_t> uniqueMales;
	atArray<uint8_t> uniqueFemales;
};

static CPedSkinTones* g_pedSkinTones;

enum PedSkinToneType : uint8_t
{
	PED_SKIN_TONE_MALES,
	PED_SKIN_TONE_FEMALES,
	PED_SKIN_TONE_UNIQUE_MALES,
	PED_SKIN_TONE_UNIQUE_FEMALES,
	MAX_SKIN_TONES,
};

static HookFunction hookFunction([]()
{
	g_pedSkinTones = hook::get_address<decltype(g_pedSkinTones)>(hook::get_pattern("49 8D 53 30 48 8D 0D", 7));

	// Citizen.InvokeNative(`ADD_PED_SKIN_TONES_HEAD` & 0xFFFFFFFF, 0, 3)
	fx::ScriptEngine::RegisterNativeHandler("ADD_PED_SKIN_TONES_HEAD", [](fx::ScriptContext& context)
	{
		auto index = (PedSkinToneType)context.GetArgument<uint8_t>(0);

		if (index >= MAX_SKIN_TONES)
		{
			trace("Invalid ped skin tone type %d\n", index);
			return;
		}

		atArray<uint8_t>* targetArray;

		switch (index)
		{
		case PED_SKIN_TONE_MALES:
			targetArray = &g_pedSkinTones->males;
			break;
		case PED_SKIN_TONE_FEMALES:
			targetArray = &g_pedSkinTones->females;
			break;
		case PED_SKIN_TONE_UNIQUE_MALES:
			targetArray = &g_pedSkinTones->uniqueMales;
			break;
		case PED_SKIN_TONE_UNIQUE_FEMALES:
			targetArray = &g_pedSkinTones->uniqueFemales;
			break;
		default:
			return;
		}

		auto textureIdx = context.GetArgument<uint8_t>(1);
		auto maxTextures = g_pedSkinTones->comps[0].skin.GetCount(); // head? component

		if (textureIdx >= maxTextures)
		{
			trace("Invalid amount of textures %d of %d\n", textureIdx, maxTextures);
			return;
		}

		auto numEntries = targetArray->GetSize();

		if (targetArray->GetCount() >= targetArray->GetSize())
		{
			targetArray->Expand(targetArray->GetSize() + 1);
		}

		targetArray->Set(targetArray->GetCount(), textureIdx);

		context.SetResult<int>(numEntries);
	});
});
