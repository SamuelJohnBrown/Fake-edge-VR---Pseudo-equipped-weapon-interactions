#pragma once

#include "skse64/GameReferences.h"
#include "skse64/GameObjects.h"
#include "skse64/GameEvents.h"
#include "skse64/GameRTTI.h"
#include "config.h"

namespace FalseEdgeVR
{
    // Weapon type classification
    enum class WeaponType
    {
        None = 0,
        Sword,
        Dagger,
        Mace,
        Axe,
        Shield
        // Note: Two-handed weapons, bows, staffs are NOT tracked - they return None
    };

    // Represents an equipped item in a hand slot
    struct EquippedWeapon
    {
        TESForm* form = nullptr;
        WeaponType type = WeaponType::None;
        bool isEquipped = false;
        
        void Clear()
        {
            form = nullptr;
            type = WeaponType::None;
            isEquipped = false;
        }
    };

    // Equipment state for tracking both hands
    struct PlayerEquipState
    {
        EquippedWeapon leftHand;
        EquippedWeapon rightHand;
  
        bool HasOneWeaponEquipped() const
        {
         return (leftHand.isEquipped && !rightHand.isEquipped) ||
              (!leftHand.isEquipped && rightHand.isEquipped);
        }
        
        bool HasBothWeaponsEquipped() const
  {
            return leftHand.isEquipped && rightHand.isEquipped;
    }
        
        bool HasNoWeaponsEquipped() const
        {
    return !leftHand.isEquipped && !rightHand.isEquipped;
     }
     
 int GetEquippedWeaponCount() const
        {
 int count = 0;
            if (leftHand.isEquipped) count++;
            if (rightHand.isEquipped) count++;
            return count;
        }
    };

 // Equipment event handler - listens for equip/unequip events
    class EquipEventHandler : public BSTEventSink<TESEquipEvent>
    {
    public:
  virtual EventResult ReceiveEvent(TESEquipEvent* evn, EventDispatcher<TESEquipEvent>* dispatcher) override;
        
        static EquipEventHandler* GetSingleton();
        
    private:
        EquipEventHandler() = default;
 ~EquipEventHandler() = default;
    EquipEventHandler(const EquipEventHandler&) = delete;
        EquipEventHandler& operator=(const EquipEventHandler&) = delete;
    };

    // Main equip manager class
    class EquipManager
    {
    public:
        static EquipManager* GetSingleton();
        
 // Initialize the equip manager and register event handlers
      void Initialize();
        
        // Flag to suppress weapon pickup sound during internal re-equip
        static bool s_suppressPickupSound;
        
     // Flag to suppress weapon draw sound during internal collision re-equip
        static bool s_suppressDrawSound;
   
     // Update equipment state from current player equipped items
        void UpdateEquipmentState();
        
        // Handle equip event
      void OnEquip(TESForm* item, Actor* actor, bool isLeftHand);
   
   // Handle unequip event
        void OnUnequip(TESForm* item, Actor* actor, bool isLeftHand);
        
   // Get current equipment state
        const PlayerEquipState& GetEquipState() const { return m_equipState; }
 
        // Check if player has only one weapon equipped
   bool HasSingleWeaponEquipped() const { return m_equipState.HasOneWeaponEquipped(); }
        
        // Get weapon type from a form
  static WeaponType GetWeaponType(TESForm* form);
  
     // Get weapon type name as string
 static const char* GetWeaponTypeName(WeaponType type);
        
        // Check if form is a weapon (sword, mace, axe, dagger, etc.)
        static bool IsWeapon(TESForm* form);
        
        // Check if form is a shield
        static bool IsShield(TESForm* form);
        
        // ============================================
        // Forced Equip/Unequip Functions
        // ============================================
  
        // Unequip weapon and drop it for HIGGS to grab
        void ForceUnequipAndGrab(bool isLeftHand);

      // Unequip the weapon from the specified hand (stores for re-equip)
  void ForceUnequipHand(bool isLeftHand);
        
 // Unequip the left hand weapon
      void ForceUnequipLeftHand();
        
        // Unequip the right hand weapon
        void ForceUnequipRightHand();
        
  // Re-equip a previously unequipped weapon to specified hand
        void ForceReequipHand(bool isLeftHand);
        
        // Re-equip the left hand weapon
    void ForceReequipLeftHand();
        
    // Re-equip the right hand weapon
        void ForceReequipRightHand();
        
        // Check if there's a pending re-equip for a hand
        bool HasPendingReequip(bool isLeftHand) const;
 
        // Clear pending re-equip for a hand
     void ClearPendingReequip(bool isLeftHand);
        
  // Check if HIGGS is currently holding the dropped weapon
        bool IsHiggsHoldingDroppedWeapon(bool isLeftHand) const;
   
        // Get the dropped weapon reference (for HIGGS grab)
        TESObjectREFR* GetDroppedWeaponRef(bool isLeftHand) const;
        
  // Clear dropped weapon reference
        void ClearDroppedWeaponRef(bool isLeftHand);
        
        // Clear cached weapon FormID
        void ClearCachedWeaponFormID(bool isLeftHand);
        
        // Track if we're in dual-wield same weapon mode (for cleanup after re-equip)
        bool WasDualWieldingSameWeapon(bool isLeftHand) const;

    private:
 EquipManager() = default;
        ~EquipManager() = default;
 EquipManager(const EquipManager&) = delete;
  EquipManager& operator=(const EquipManager&) = delete;
        
      void LogEquipmentState();

        PlayerEquipState m_equipState;
        
        // Pending re-equip tracking - store FormID instead of pointer
        TESForm* m_pendingReequipLeft = nullptr;
        TESForm* m_pendingReequipRight = nullptr;
        UInt32 m_cachedWeaponFormIDLeft = 0;   // Cache the weapon FormID for left hand re-equip
        UInt32 m_cachedWeaponFormIDRight = 0;  // Cache the weapon FormID for right hand re-equip
        
     // Dropped weapon world references
      TESObjectREFR* m_droppedWeaponLeft = nullptr;
        TESObjectREFR* m_droppedWeaponRight = nullptr;
        
        // Track if we were dual-wielding same weapon when collision was triggered
        // This is needed to know if we should clean up the duplicate after re-equip
        bool m_wasDualWieldingSameWeaponLeft = false;
   bool m_wasDualWieldingSameWeaponRight = false;
        
  // Track which hand we're currently force-unequipping (for same-weapon detection)
        // -1 = none, 0 = right hand, 1 = left hand
    int m_forceUnequipHand = -1;
        
      bool m_initialized = false;
    };

    // Convenience functions
    void RegisterEquipEventHandler();
    void UnregisterEquipEventHandler();
}
