#include "Engine.h"
#include "EquipManager.h"
#include "VRInputHandler.h"
#include "WeaponGeometry.h"
#include "ShieldCollision.h"

#include <skse64/PapyrusActor.cpp>
#include "skse64/GameRTTI.h"
#include "skse64/PapyrusVM.h"
#include "skse64/GameExtraData.h"

namespace FalseEdgeVR
{
	SKSETrampolineInterface* g_trampolineInterface = nullptr;

	HiggsPluginAPI::IHiggsInterface001* higgsInterface;
	vrikPluginApi::IVrikInterface001* vrikInterface;

	SkyrimVRESLPluginAPI::ISkyrimVRESLInterface001* skyrimVRESLInterface;

	// ============================================
	// Spell Casting (from SpellWheelVR - Papyrus Spell.Cast)
	// ============================================
	typedef bool(*_CastSpell)(VMClassRegistry* registry, UInt32 stackId, SpellItem* spell, TESObjectREFR* akSource, TESObjectREFR* akTarget);
	RelocAddr<_CastSpell> CastSpell_Native(0x009BB6B0);

	// Task to cast spell on main game thread
	class CastSpellOnPlayerTask : public TaskDelegate
	{
	public:
		UInt32 m_formId;

		CastSpellOnPlayerTask(UInt32 formId) : m_formId(formId) {}

		virtual void Run() override
		{
			Actor* player = *g_thePlayer;
			if (!player)
			{
				_MESSAGE("[CastSpell] ERROR: Player not available");
				return;
			}

			TESForm* form = LookupFormByID(m_formId);
			if (!form)
			{
				_MESSAGE("[CastSpell] ERROR: Spell form %08X not found", m_formId);
				return;
			}

			SpellItem* spell = DYNAMIC_CAST(form, TESForm, SpellItem);
			if (!spell)
			{
				_MESSAGE("[CastSpell] ERROR: Form %08X is not a SpellItem", m_formId);
				return;
			}

			// Cast the spell on the player (source = player, target = player for self-cast spells)
			bool result = CastSpell_Native((*g_skyrimVM)->GetClassRegistry(), 0, spell, player, player);
			_MESSAGE("[CastSpell] Cast spell %08X on player, result: %s", m_formId, result ? "success" : "failed");
		}

		virtual void Dispose() override
		{
			delete this;
		}
	};

	void CastSpellOnPlayer(UInt32 formId)
	{
		if (formId == 0)
		{
			_MESSAGE("[CastSpell] ERROR: Invalid formId 0");
			return;
		}

		extern SKSETaskInterface* g_task;
		if (g_task)
		{
			g_task->AddTask(new CastSpellOnPlayerTask(formId));
			_MESSAGE("[CastSpell] Queued spell cast %08X on player", formId);
		}
		else
		{
			_MESSAGE("[CastSpell] ERROR: g_task not available!");
		}
	}

	// ============================================
	// Sound Playing
	// ============================================

	// PlaySoundEffect function signature - for playing TESSound records
	typedef void(*_PlaySoundEffect)(VMClassRegistry* VMinternal, UInt32 stackId, TESSound* sound, TESObjectREFR* source);
	static RelocAddr<_PlaySoundEffect> PlaySoundEffect(0x009EF150);

	// Play a sound at the player's location
	// soundFormId is the full FormID of the SOUN record
	void PlaySoundAtPlayer(UInt32 soundFormId)
	{
		PlayerCharacter* player = *g_thePlayer;
		if (!player)
		{
			_MESSAGE("[PlaySound] ERROR: Player not available");
			return;
		}

		// Look up the sound form (SOUN type)
		TESForm* form = LookupFormByID(soundFormId);
		if (!form)
		{
			_MESSAGE("[PlaySound] ERROR: Failed to find sound form %08X", soundFormId);
			return;
		}

		// Cast to TESSound (SOUN record)
		TESSound* sound = DYNAMIC_CAST(form, TESForm, TESSound);
		if (!sound)
		{
			_MESSAGE("[PlaySound] ERROR: Form %08X is not a TESSound (type=%d, expected=%d)",
				soundFormId, form->formType, kFormType_Sound);
			return;
		}

		// Play the sound using the Papyrus native function
		PlaySoundEffect((*g_skyrimVM)->GetClassRegistry(), 0, sound, player);
		_MESSAGE("[PlaySound] Played sound %08X at player", soundFormId);
	}

	// Play a sound at any actor's location (NPC or player)
	// soundFormId is the full FormID of the SOUN record
	void PlaySoundAtActor(UInt32 soundFormId, Actor* actor)
	{
		if (!actor)
		{
			_MESSAGE("[PlaySound] ERROR: Actor not available");
			return;
		}

		// Look up the sound form (SOUN type)
		TESForm* form = LookupFormByID(soundFormId);
		if (!form)
		{
			_MESSAGE("[PlaySound] ERROR: Failed to find sound form %08X", soundFormId);
			return;
		}

		// Cast to TESSound (SOUN record)
		TESSound* sound = DYNAMIC_CAST(form, TESForm, TESSound);
		if (!sound)
		{
			_MESSAGE("[PlaySound] ERROR: Form %08X is not a TESSound (type=%d, expected=%d)",
				soundFormId, form->formType, kFormType_Sound);
			return;
		}

		// Play the sound using the Papyrus native function
		PlaySoundEffect((*g_skyrimVM)->GetClassRegistry(), 0, sound, actor);
	}

	// Set the ownership of an object reference to the player
	// This prevents the item from being flagged as stolen when picked up
	void SetOwnerToPlayer(TESObjectREFR* objRef)
	{
		if (!objRef)
		{
			_MESSAGE("[SetOwner] ERROR: Object reference is null");
			return;
		}

		PlayerCharacter* player = *g_thePlayer;
		if (!player)
		{
			_MESSAGE("[SetOwner] ERROR: Player not available");
			return;
		}

		// Get or create the extra data list
		BaseExtraList* extraList = &objRef->extraData;
		
		// Check if ownership already exists
		if (extraList->HasType(kExtraData_Ownership))
		{
			// Get existing ownership and update it
			BSExtraData* existing = extraList->m_data;
			while (existing)
			{
				if (existing->GetType() == kExtraData_Ownership)
				{
					ExtraOwnership* ownership = static_cast<ExtraOwnership*>(existing);
					ownership->owner = player;
					_MESSAGE("[SetOwner] Updated existing ownership to player (RefID: %08X)", objRef->formID);
					return;
				}
				existing = existing->next;
			}
		}
		
		// Create new ExtraOwnership
		// We use BSExtraData::Create with the vtable for ExtraOwnership
		static RelocPtr<uintptr_t> s_ExtraOwnershipVtbl(0x015A32D0);
		ExtraOwnership* xOwnership = (ExtraOwnership*)BSExtraData::Create(sizeof(ExtraOwnership), s_ExtraOwnershipVtbl.GetUIntPtr());
		if (xOwnership)
		{
			xOwnership->owner = player;
			extraList->Add(kExtraData_Ownership, xOwnership);
			_MESSAGE("[SetOwner] Set ownership to player for RefID: %08X", objRef->formID);
		}
		else
		{
			_MESSAGE("[SetOwner] ERROR: Failed to create ExtraOwnership");
		}
	}

	// ============================================
	// Blocking (X-Pose) - from dual_wield_block_vr
	// ============================================
	
	// Animation graph function typedefs
	typedef bool(*_IAnimationGraphManagerHolder_NotifyAnimationGraph)(IAnimationGraphManagerHolder* _this, const BSFixedString& a_eventName);
	typedef bool(*_IAnimationGraphManagerHolder_GetAnimationVariableBool)(IAnimationGraphManagerHolder* _this, const BSFixedString& a_variableName, bool& a_out);
	
	inline UInt64* get_vtbl(void* object) { return *((UInt64**)object); }
	
	template<typename T>
	inline T get_vfunc(void* object, UInt64 index) {
		UInt64* vtbl = get_vtbl(object);
		return (T)(vtbl[index]);
	}
	
	void StartBlocking()
	{
		Actor* player = *g_thePlayer;
		if (!player)
		{
			_MESSAGE("[Blocking] ERROR: Player not available");
			return;
		}
		
		static BSFixedString s_blockStart("blockStart");
		get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&player->animGraphHolder, 0x1)(&player->animGraphHolder, s_blockStart);
		_MESSAGE("[Blocking] Started blocking (X-Pose)");
	}
	
	void StopBlocking()
	{
		Actor* player = *g_thePlayer;
		if (!player)
		{
			_MESSAGE("[Blocking] ERROR: Player not available");
			return;
		}
		
		static BSFixedString s_blockStop("blockStop");
		get_vfunc<_IAnimationGraphManagerHolder_NotifyAnimationGraph>(&player->animGraphHolder, 0x1)(&player->animGraphHolder, s_blockStop);
		_MESSAGE("[Blocking] Stopped blocking (X-Pose ended)");
	}
	
	bool IsBlocking()
	{
		Actor* player = *g_thePlayer;
		if (!player)
			return false;
		
		static BSFixedString s_IsBlocking("IsBlocking");
		bool isBlocking = false;
		get_vfunc<_IAnimationGraphManagerHolder_GetAnimationVariableBool>(&player->animGraphHolder, 0x12)(&player->animGraphHolder, s_IsBlocking, isBlocking);
		return isBlocking;
	}

	// ============================================
	// Left-Handed Mode Support
	// ============================================
	
	// Local RelocPtr for left-handed mode - the address 0x01E71778 is from GameInput.cpp
	static RelocPtr<bool> s_leftHandedMode(0x01E71778);
	
	bool IsLeftHandedMode()
	{
		// Access the left-handed mode flag from the game
		return *s_leftHandedMode;
	}
	
	bool VRControllerToGameHand(bool isLeftVRController)
	{
		// In left-handed mode, VR controllers are inverted:
		//   - Left VR controller = Right game hand (returns false)
		//   - Right VR controller = Left game hand (returns true)
		// In right-handed (default) mode:
		//   - Left VR controller = Left game hand (returns true)
		//   - Right VR controller = Right game hand (returns false)
		
		if (IsLeftHandedMode())
		{
			return !isLeftVRController;  // Invert
		}
		else
		{
			return isLeftVRController;   // No change
		}
	}
	
	bool GameHandToVRController(bool isLeftGameHand)
	{
		// Reverse of VRControllerToGameHand
		// In left-handed mode:
		//   - Left game hand = Right VR controller (returns false)
		//   - Right game hand = Left VR controller (returns true)
		// In right-handed (default) mode:
		//   - Left game hand = Left VR controller (returns true)
		//   - Right game hand = Right VR controller (returns false)
		
		if (IsLeftHandedMode())
		{
			return !isLeftGameHand;  // Invert
		}
		else
		{
			return isLeftGameHand;   // No change
		}
	}

	void StartMod()
	{
		// Note: Most initialization now happens in main.cpp's InitializeVRSystems()
		// which is called after HIGGS interface is available (PostPostLoad)
		
		// This function is called during DataLoaded, before HIGGS is ready
		// Only do non-HIGGS dependent initialization here
		
		LOG("StartMod: FalseEdgeVR starting...");
		
		// Log initial left-handed mode
		_MESSAGE("==============================================");
		_MESSAGE("[LeftHandedMode] VR Controller Mode: %s", IsLeftHandedMode() ? "LEFT-HANDED" : "RIGHT-HANDED (default)");
		if (IsLeftHandedMode())
		{
			_MESSAGE("[LeftHandedMode] NOTE: In left-handed mode, VR controllers are inverted!");
			_MESSAGE("[LeftHandedMode]   Left VR controller  -> Right game hand");
			_MESSAGE("[LeftHandedMode]   Right VR controller -> Left game hand");
		}
		_MESSAGE("==============================================");
	}
}
