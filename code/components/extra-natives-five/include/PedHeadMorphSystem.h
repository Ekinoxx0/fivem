#pragma once

#include "EntityExtensions.h"

#define RAGE_FORMATS_GAME five
#define RAGE_FORMATS_GAME_FIVE

#define RAGE_FORMATS_IN_GAME

#include <gtaDrawable.h>

#define PED_HEAD_MORPH_DEBUG (_DEBUG)

using gtaDrawable = rage::five::gtaDrawable;

namespace rage
{
using grmGeometryQB = five::grmGeometryQB;
}


class HeadMorphInst
{
	const std::string m_modelName;
	rage::grmGeometryQB* m_geometry;
	short m_numRefs;

public:
	HeadMorphInst(const std::string_view& modelName)
		: m_modelName(modelName), m_geometry(nullptr), m_numRefs(0)
	{
	}

	~HeadMorphInst()
	{
#if _DEBUG
		// Instance must reset before destructing!
		assert(!GetGeometry());
#endif

		// But anyway releasing geometry, just to be sure
		if (GetGeometry())
		{
			ReleaseGeometry();
		}
	}

	short HeadMorphInst::GetRefCount()
	{
		return m_numRefs;
	}

	void AddRef();
	void RemoveRef();

	void SetGeometry(rage::grmGeometryQB* geometry)
	{
		m_geometry = geometry;
	}

	rage::grmGeometryQB* HeadMorphInst::GetGeometry()
	{
		return m_geometry;
	}

	const std::string& HeadMorphInst::GetModelName()
	{
		return m_modelName;
	}

	void UpdateGeometry();

private:
	void RequestGeometry();
	void ReleaseGeometry();
};

struct HeadMorphState
{
private:
	HeadMorphInst* m_inst;
	float m_value;

public:
	HeadMorphState(HeadMorphInst* data, float value)
		: m_inst(data), m_value(value)
	{
		assert(data);
	}

	void SetValue(float value)
	{
		m_value = value;
	}

	float GetValue()
	{
		return m_value;
	}

	HeadMorphInst* GetInst()
	{
		return m_inst;
	}
};

class HeadMorphManager
{
	using MorphsStore = std::map<std::tuple<uint32_t, uint32_t>, HeadMorphInst>;
	using PedsStore = std::map<uint32_t, CPed*>;

public:
	bool RegisterMorph(const std::string& morphName, const std::string& targetModel);
	void ReleaseMorph(const std::string& morphName);
	bool HasMorphInst(const std::string& morphName, const std::string& targetModel);
	bool HasMorphInst(uint32_t morphHash, uint32_t modelHash);
	HeadMorphInst& GetMorphInst(uint32_t morphHash, uint32_t modelHash);

	void RegisterPed(CPed* ped, uint32_t index);
	void UnregisterPed(uint32_t index);
	CPed* GetPedFromBlendIndex(uint32_t index);
	uint32_t GetBlendIndexFromPed(const CPed* ped);

	static gtaDrawable* LoadMorphDrawable(const std::string& modelName);
	static void UnloadMorphDrawable(const std::string& modelName);

	void Reset();

private:
	MorphsStore m_morphStore;
	PedsStore m_pedsStore;
};

inline HeadMorphManager TheHeadMorphManager;

class PedHeadMorphExtension final : public rage::fwExtension
{
public:
	PedHeadMorphExtension()
	{
		m_morphs = {};
	}

	virtual ~PedHeadMorphExtension()
	{
		ResetMorphs();
	}

	inline PedHeadMorphExtension* Clone() const
	{
		auto newExtension = new PedHeadMorphExtension(*this);
		return newExtension;
	}

	virtual int GetExtensionId() const override
	{
		return GetClassId();
	}

	static int GetClassId()
	{
		return (int)EntityExtensionClassId::PedHeadMorph;
	}

	HeadMorphState& GetMorph(uint32_t morphName);
	bool HasMorph(uint32_t morphName);
	void AddMorph(uint32_t morphName, HeadMorphState& morphState);
	void RemoveMorph(uint32_t morphName);

	void ResetMorphs();
	bool HasMorphs();
	std::map<uint32_t, HeadMorphState>& GetMorphs();

	static PedHeadMorphExtension* GetOrAdd(CPed* ped);

private:
	std::map<uint32_t, HeadMorphState> m_morphs;
};
