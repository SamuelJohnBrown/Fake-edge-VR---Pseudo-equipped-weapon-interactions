#include "EquipManager.h"
#include "VRInputHandler.h"
#include "Engine.h"
#include "SkyrimVRESLAPI.h"
#include "skse64/GameData.h"
#include "skse64/GameForms.h"
#include "skse64/GameExtraData.h"
#include "skse64/GameReferences.h"
#include "skse64/PapyrusActor.h"
#include "skse64/PluginAPI.h"
#include <thread>
#include <chrono>

namespace FalseEdgeVR
{
    extern SKSETaskInterface* g_task;

  // Static member initialization
    bool EquipManager::s_suppressPickupSound = false;
    bool EquipManager::s_suppressDrawSound = false;

    // ============================================
    // Delayed Equip Weapon Task (runs on game thread)
    // ============================================
    class DelayedEquipWeaponTask : public TaskDelegate
    {
    public:
        UInt32 m_weaponFormId;
        bool m_equipToLeftHand;

        DelayedEquipWeaponTask(UInt32 weaponFormId, bool equipToLeftHand) 
            : m_weaponFormId(weaponFormId), m_equipToLeftHand(equipToLeftHand) {}

        virtual void Run() override
        {
            Actor* player = (*g_thePlayer);
            if (!player)
            {
                _MESSAGE("[DelayedEquipWeapon] Player not available");
                return;
            }

            TESForm* weaponForm = LookupFormByID(m_weaponFormId);
            if (!weaponForm)
            {
                _MESSAGE("[DelayedEquipWeapon] Weapon form %08X not found", m_weaponFormId);
                return;
            }

            ::EquipManager* equipMan = ::EquipManager::GetSingleton();
            if (!equipMan)
            {
                _MESSAGE("[DelayedEquipWeapon] EquipManager not available");
                return;
            }

            // Get the appropriate slot for left or right hand
            BGSEquipSlot* slot = m_equipToLeftHand ? GetLeftHandSlot() : GetRightHandSlot();

            // EquipItem params: actor, item, extraData, count, slot, withEquipSound, preventUnequip, showMsg, unk
            CALL_MEMBER_FN(equipMan, EquipItem)(player, weaponForm, nullptr,1, slot, false, true, false, nullptr);
            _MESSAGE("[DelayedEquipWeapon] Equipped weapon %08X to %s hand (silent)", m_weaponFormId, m_equipToLeftHand ? "LEFT" : "RIGHT");
        }

        virtual void Dispose() override
        {
            delete this;
        }
    };

    // ============================================
    // Thread function to delay then queue the equip task
    // ============================================
    static void DelayedEquipWeaponThread(UInt32 weaponFormId, bool equipToLeftHand, int delayMs)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
   
        if (g_task)
        {
            g_task->AddTask(new DelayedEquipWeaponTask(weaponFormId, equipToLeftHand));
            _MESSAGE("[EquipManager] Queued weapon equip task after %dms delay for weapon %08X to %s hand", 
        delayMs, weaponFormId, equipToLeftHand ? "LEFT" : "RIGHT");
        }
        else
        {
            _MESSAGE("[EquipManager] ERROR: g_task not available for delayed equip!");
        }
    }

    // ============================================
    // ContainerChangeEventHandler - Logs when weapons are added to player inventory
    // ============================================
    
    class ContainerChangeEventHandler : public BSTEventSink<TESContainerChangedEvent>
    {
    public:
   static ContainerChangeEventHandler* GetSingleton()
        {
     static ContainerChangeEventHandler instance;
            return &instance;
        }
        
        virtual EventResult ReceiveEvent(TESContainerChangedEvent* evn, EventDispatcher<TESContainerChangedEvent>* dispatcher) override;
      
    private:
        ContainerChangeEventHandler() = default;
      ~ContainerChangeEventHandler() = default;
        ContainerChangeEventHandler(const ContainerChangeEventHandler&) = delete;
 ContainerChangeEventHandler& operator=(const ContainerChangeEventHandler&) = delete;
    };

    // ============================================
    // EquipEventHandler Implementation
    // ============================================
    
    EquipEventHandler* EquipEventHandler::GetSingleton()
    {
   static EquipEventHandler instance;
        return &instance;
    }

    EventResult EquipEventHandler::ReceiveEvent(TESEquipEvent* evn, EventDispatcher<TESEquipEvent>* dispatcher)
{
        if (!evn)
     return kEvent_Continue;

    Actor* actor = DYNAMIC_CAST(evn->actor, TESObjectREFR, Actor);
        if (!actor)
     return kEvent_Continue;

        TESForm* item = LookupFormByID(evn->baseObject);
        if (!item)
 return kEvent_Continue;

        // Check if this is a one-handed weapon we track
        if (!EquipManager::IsWeapon(item))
  {
        // For player, also check shields
    if (actor == *g_thePlayer && EquipManager::IsShield(item))
            {
          // Continue to player handling below
}
            else
   {
    return kEvent_Continue;
            }
    }

        bool isEquipping = evn->equipped;
        
        // ============================================
        // NPC EQUIP TRACKING (within 1000 units of player)
        // ============================================
   if (actor != *g_thePlayer && isEquipping)
        {
     PlayerCharacter* player = *g_thePlayer;
  if (player)
    {
    // Calculate distance to player
       float dx = actor->pos.x - player->pos.x;
       float dy = actor->pos.y - player->pos.y;
      float dz = actor->pos.z - player->pos.z;
       float distance = sqrt(dx*dx + dy*dy + dz*dz);
          
      // Only log and play sound if within 1000 units
 if (distance <= 1000.0f)
      {
          WeaponType type = EquipManager::GetWeaponType(item);
    const char* npcName = CALL_MEMBER_FN(actor, GetReferenceName)();
           
    // Cache sound FormIDs from Fake Edge VR.esp (ESL-flagged)
             // Base FormIDs: Dagger=0x806, Sword=0x807, Axe=0x808, Mace=0x809
       static UInt32 cachedDaggerSound = 0;
static UInt32 cachedSwordSound = 0;
            static UInt32 cachedAxeSound = 0;
                static UInt32 cachedMaceSound = 0;
     static bool soundsCached = false;
                
     if (!soundsCached)
          {
          cachedDaggerSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x806);
        cachedSwordSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x807);
        cachedAxeSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x808);
          cachedMaceSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x809);
        soundsCached = true;
    _MESSAGE("EquipManager: Cached weapon draw sounds - Dagger:%08X, Sword:%08X, Axe:%08X, Mace:%08X",
         cachedDaggerSound, cachedSwordSound, cachedAxeSound, cachedMaceSound);
     }
           
  switch (type)
           {
    case WeaponType::Dagger:
    _MESSAGE(">>> NPC EQUIPPED: DAGGER - NPC: %s (RefID: %08X), Distance: %.1f units, WeaponID: %08X",
    npcName ? npcName : "Unknown", actor->formID, distance, item->formID);
        if (cachedDaggerSound != 0)
   PlaySoundAtActor(cachedDaggerSound, actor);
   break;
   case WeaponType::Sword:
    _MESSAGE(">>> NPC EQUIPPED: 1H SWORD - NPC: %s (RefID: %08X), Distance: %.1f units, WeaponID: %08X",
       npcName ? npcName : "Unknown", actor->formID, distance, item->formID);
        if (cachedSwordSound != 0)
       PlaySoundAtActor(cachedSwordSound, actor);
   break;
  case WeaponType::Mace:
    _MESSAGE(">>> NPC EQUIPPED: 1H MACE - NPC: %s (RefID: %08X), Distance: %.1f units, WeaponID: %08X",
  npcName ? npcName : "Unknown", actor->formID, distance, item->formID);
   if (cachedMaceSound != 0)
       PlaySoundAtActor(cachedMaceSound, actor);
         break;
case WeaponType::Axe:
     _MESSAGE(">>> NPC EQUIPPED: 1H AXE - NPC: %s (RefID: %08X), Distance: %.1f units, WeaponID: %08X",
        npcName ? npcName : "Unknown", actor->formID, distance, item->formID);
        if (cachedAxeSound != 0)
               PlaySoundAtActor(cachedAxeSound, actor);
      break;
 default:
      break;
     }
  }
 }
     return kEvent_Continue;
    }

        // ============================================
        // PLAYER EQUIP TRACKING (existing logic)
     // ============================================
        if (actor != *g_thePlayer)
        return kEvent_Continue;

     _MESSAGE("EquipEventHandler: Received equip event for FormID %08X, equipped=%d", evn->baseObject, evn->equipped);

     // Determine which hand based on the equipped flag
     
        // Get the actual hand from the currently equipped objects
     bool isLeftHand = false;
        TESForm* leftEquipped = actor->GetEquippedObject(true);
        TESForm* rightEquipped = actor->GetEquippedObject(false);
        
      if (isEquipping)
{
     if (leftEquipped && leftEquipped->formID == item->formID)
   isLeftHand = true;
   else if (rightEquipped && rightEquipped->formID == item->formID)
         isLeftHand = false;
            
EquipManager::GetSingleton()->OnEquip(item, actor, isLeftHand);
   }
        else
        {
 const PlayerEquipState& state = EquipManager::GetSingleton()->GetEquipState();
            if (state.leftHand.form && state.leftHand.form->formID == item->formID)
      isLeftHand = true;
        else if (state.rightHand.form && state.rightHand.form->formID == item->formID)
   isLeftHand = false;
  
            EquipManager::GetSingleton()->OnUnequip(item, actor, isLeftHand);
   }

      return kEvent_Continue;
    }

    // ============================================
    // EquipManager Implementation
    // ============================================

    EquipManager* EquipManager::GetSingleton()
    {
        static EquipManager instance;
 return &instance;
    }

    void EquipManager::Initialize()
 {
        if (m_initialized)
          return;

        _MESSAGE("EquipManager: Initializing...");
        
        m_equipState.leftHand.Clear();
      m_equipState.rightHand.Clear();
        
        m_initialized = true;
     _MESSAGE("EquipManager: Initialized successfully");
    }

    void EquipManager::UpdateEquipmentState()
{
        PlayerCharacter* player = *g_thePlayer;
        if (!player)
   {
   _MESSAGE("EquipManager::UpdateEquipmentState - No player!");
      return;
      }

  TESForm* leftItem = player->GetEquippedObject(true);
        TESForm* rightItem = player->GetEquippedObject(false);

 _MESSAGE("EquipManager::UpdateEquipmentState - Left: %08X, Right: %08X", 
            leftItem ? leftItem->formID : 0, 
      rightItem ? rightItem->formID : 0);

    // Update left hand
  if (leftItem && (IsWeapon(leftItem) || IsShield(leftItem)))
        {
    m_equipState.leftHand.form = leftItem;
  m_equipState.leftHand.type = GetWeaponType(leftItem);
            m_equipState.leftHand.isEquipped = true;
        }
     else
 {
   m_equipState.leftHand.Clear();
        }

        // Update right hand
        if (rightItem && (IsWeapon(rightItem) || IsShield(rightItem)))
        {
         m_equipState.rightHand.form = rightItem;
  m_equipState.rightHand.type = GetWeaponType(rightItem);
        m_equipState.rightHand.isEquipped = true;
        }
  else
        {
     m_equipState.rightHand.Clear();
   }

     LogEquipmentState();
    }

    void EquipManager::OnEquip(TESForm* item, Actor* actor, bool isLeftHand)
    {
      if (!item)
       return;

  WeaponType type = GetWeaponType(item);
        const char* typeName = GetWeaponTypeName(type);
   const char* handName = isLeftHand ? "Left" : "Right";

    EquippedWeapon& hand = isLeftHand ? m_equipState.leftHand : m_equipState.rightHand;
   hand.form = item;
        hand.type = type;
 hand.isEquipped = true;

  _MESSAGE("EquipManager: EQUIPPED %s in %s hand (FormID: %08X)", typeName, handName, item->formID);
        
// Cache sound FormIDs from Fake Edge VR.esp (ESL-flagged)
      // Base FormIDs: Dagger=0x806, Sword=0x807, Axe=0x808, Mace=0x809
  static UInt32 cachedDaggerSound = 0;
        static UInt32 cachedSwordSound = 0;
        static UInt32 cachedAxeSound = 0;
        static UInt32 cachedMaceSound = 0;
   static bool soundsCached = false;
        
        if (!soundsCached)
        {
    cachedDaggerSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x806);
            cachedSwordSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x807);
            cachedAxeSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x808);
cachedMaceSound = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x809);
     soundsCached = true;
        }
        
     // Log specific weapon types and play draw sounds (unless suppressed by collision logic)
        switch (type)
     {
        case WeaponType::Dagger:
      _MESSAGE(">>> PLAYER EQUIPPED: DAGGER (FormID: %08X) in %s hand", item->formID, handName);
   if (!s_suppressDrawSound && cachedDaggerSound != 0)
  PlaySoundAtPlayer(cachedDaggerSound);
    break;
     case WeaponType::Sword:
       _MESSAGE(">>> PLAYER EQUIPPED: 1H SWORD (FormID: %08X) in %s hand", item->formID, handName);
   if (!s_suppressDrawSound && cachedSwordSound != 0)
    PlaySoundAtPlayer(cachedSwordSound);
  break;
     case WeaponType::Mace:
    _MESSAGE(">>> PLAYER EQUIPPED: 1H MACE (FormID: %08X) in %s hand", item->formID, handName);
     if (!s_suppressDrawSound && cachedMaceSound != 0)
  PlaySoundAtPlayer(cachedMaceSound);
     break;
  case WeaponType::Axe:
     _MESSAGE(">>> PLAYER EQUIPPED: 1H AXE (FormID: %08X) in %s hand", item->formID, handName);
  if (!s_suppressDrawSound && cachedAxeSound != 0)
      PlaySoundAtPlayer(cachedAxeSound);
     break;
  case WeaponType::Shield:
 _MESSAGE(">>> PLAYER EQUIPPED: SHIELD (FormID: %08X) in %s hand", item->formID, handName);
         break;
            default:
     break;
  }
 
        if (s_suppressDrawSound)
        {
        _MESSAGE("EquipManager: Skipping draw sound (internal collision re-equip)");
        }
        
      LogEquipmentState();
   
      // Update VR input handler grab listening
     VRInputHandler::GetSingleton()->UpdateGrabListening();
    }

    void EquipManager::OnUnequip(TESForm* item, Actor* actor, bool isLeftHand)
    {
      if (!item)
            return;

     WeaponType type = GetWeaponType(item);
        const char* typeName = GetWeaponTypeName(type);
        const char* handName = isLeftHand ? "Left" : "Right";

 EquippedWeapon& hand = isLeftHand ? m_equipState.leftHand : m_equipState.rightHand;
        hand.Clear();

        _MESSAGE("EquipManager: UNEQUIPPED %s from %s hand (FormID: %08X)", typeName, handName, item->formID);
        
        if (m_equipState.HasOneWeaponEquipped())
      {
 const char* remainingHand = m_equipState.leftHand.isEquipped ? "Left" : "Right";
            WeaponType remainingType = m_equipState.leftHand.isEquipped 
     ? m_equipState.leftHand.type 
        : m_equipState.rightHand.type;
            
      _MESSAGE("EquipManager: Player now has SINGLE weapon equipped - %s in %s hand", 
        GetWeaponTypeName(remainingType), remainingHand);
        }
        
        LogEquipmentState();
        
        // Update VR input handler grab listening
   VRInputHandler::GetSingleton()->UpdateGrabListening();
    }

    void EquipManager::LogEquipmentState()
    {
        _MESSAGE("EquipManager: === Equipment State ===");
        _MESSAGE("  Left Hand:  %s (%s)", 
            m_equipState.leftHand.isEquipped ? GetWeaponTypeName(m_equipState.leftHand.type) : "Empty",
       m_equipState.leftHand.form ? std::to_string(m_equipState.leftHand.form->formID).c_str() : "None");
        _MESSAGE("  Right Hand: %s (%s)", 
      m_equipState.rightHand.isEquipped ? GetWeaponTypeName(m_equipState.rightHand.type) : "Empty",
            m_equipState.rightHand.form ? std::to_string(m_equipState.rightHand.form->formID).c_str() : "None");
    _MESSAGE("  Weapon Count: %d", m_equipState.GetEquippedWeaponCount());
        _MESSAGE("  Single Weapon: %s", m_equipState.HasOneWeaponEquipped() ? "YES" : "NO");
        _MESSAGE("==============================");
    }

    WeaponType EquipManager::GetWeaponType(TESForm* form)
    {
        if (!form)
  return WeaponType::None;

   if (IsShield(form))
     return WeaponType::Shield;

      TESObjectWEAP* weapon = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
        if (!weapon)
     return WeaponType::None;

     switch (weapon->gameData.type)
        {
 case TESObjectWEAP::GameData::kType_OneHandSword:
        case TESObjectWEAP::GameData::kType_1HS:
                return WeaponType::Sword;
     
   case TESObjectWEAP::GameData::kType_OneHandDagger:
      case TESObjectWEAP::GameData::kType_1HD:
       return WeaponType::Dagger;
  
    case TESObjectWEAP::GameData::kType_OneHandMace:
  case TESObjectWEAP::GameData::kType_1HM:
             return WeaponType::Mace;
            
 case TESObjectWEAP::GameData::kType_OneHandAxe:
            case TESObjectWEAP::GameData::kType_1HA:
      return WeaponType::Axe;

            // Two-handed weapons, bows, staffs - EXCLUDED from our tracking
            case TESObjectWEAP::GameData::kType_TwoHandSword:
            case TESObjectWEAP::GameData::kType_2HS:
            case TESObjectWEAP::GameData::kType_TwoHandAxe:
   case TESObjectWEAP::GameData::kType_2HA:
     case TESObjectWEAP::GameData::kType_Bow:
     case TESObjectWEAP::GameData::kType_Staff:
            case TESObjectWEAP::GameData::kType_CrossBow:
       return WeaponType::None;  // Treat as not a weapon for our purposes
            
            default:
   return WeaponType::None;
        }
    }

    const char* EquipManager::GetWeaponTypeName(WeaponType type)
    {
   switch (type)
        {
 case WeaponType::Sword:      return "Sword";
   case WeaponType::Dagger:   return "Dagger";
            case WeaponType::Mace:       return "Mace";
        case WeaponType::Axe:     return "Axe";
       case WeaponType::Shield:     return "Shield";
     case WeaponType::None:
       default: return "None";
        }
    }

    bool EquipManager::IsWeapon(TESForm* form)
    {
        if (!form)
  return false;

        if (form->formType != kFormType_Weapon)
       return false;
        
        // Check if it's a weapon type we actually track (one-handed only)
    // Exclude bows, staffs, crossbows, two-handed weapons, and bound weapons
  TESObjectWEAP* weapon = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
        if (!weapon)
   return false;
     
        // Check for bound weapon keyword - exclude bound weapons
        BGSKeywordForm* keywordForm = DYNAMIC_CAST(form, TESForm, BGSKeywordForm);
        if (keywordForm)
        {
            // WeapTypeBoundWeapon keyword FormID is 0x0010D501 in Skyrim.esm
        static const UInt32 kWeapTypeBoundWeapon = 0x0010D501;
            BGSKeyword* boundKeyword = DYNAMIC_CAST(LookupFormByID(kWeapTypeBoundWeapon), TESForm, BGSKeyword);
    if (boundKeyword && keywordForm->HasKeyword(boundKeyword))
        {
        return false;  // Bound weapon - don't track
        }
        }
 
        switch (weapon->gameData.type)
        {
   case TESObjectWEAP::GameData::kType_OneHandSword:
            case TESObjectWEAP::GameData::kType_1HS:
      case TESObjectWEAP::GameData::kType_OneHandDagger:
case TESObjectWEAP::GameData::kType_1HD:
    case TESObjectWEAP::GameData::kType_OneHandMace:
          case TESObjectWEAP::GameData::kType_1HM:
   case TESObjectWEAP::GameData::kType_OneHandAxe:
         case TESObjectWEAP::GameData::kType_1HA:
      return true;  // One-handed weapons - we track these
            
            default:
 return false;  // Bows, staffs, crossbows, two-handed - don't track
   }
    }

    bool EquipManager::IsShield(TESForm* form)
    {
      if (!form)
     return false;

 if (form->formType != kFormType_Armor)
      return false;

TESObjectARMO* armor = DYNAMIC_CAST(form, TESForm, TESObjectARMO);
        if (!armor)
   return false;

        return (armor->bipedObject.GetSlotMask() & BGSBipedObjectForm::kPart_Shield) != 0;
    }

    // ============================================
    // Forced Unequip Functions
    // ============================================

    void EquipManager::ForceUnequipHand(bool isLeftHand)
    {
   PlayerCharacter* player = *g_thePlayer;
        if (!player)
      {
      _MESSAGE("EquipManager::ForceUnequipHand - No player!");
     return;
        }

 EquippedWeapon& hand = isLeftHand ? m_equipState.leftHand : m_equipState.rightHand;
     if (!hand.isEquipped || !hand.form)
        {
 _MESSAGE("EquipManager::ForceUnequipHand - %s hand has no weapon to unequip", 
   isLeftHand ? "Left" : "Right");
       return;
        }

   TESForm* item = hand.form;
 
        // Store the weapon for later re-equip
        if (isLeftHand)
     {
     m_pendingReequipLeft = item;
        }
        else
{
         m_pendingReequipRight = item;
        }
    
        _MESSAGE("EquipManager: FORCE UNEQUIPPING %s from %s hand (FormID: %08X) - stored for re-equip", 
      GetWeaponTypeName(hand.type), 
        isLeftHand ? "Left" : "Right", 
     item->formID);

 // Get the EquipManager singleton from the game
    ::EquipManager* equipManager = ::EquipManager::GetSingleton();
        if (!equipManager)
        {
   _MESSAGE("EquipManager::ForceUnequipHand - Failed to get game EquipManager!");
  return;
        }

        // Get container changes to find the equipped item's extra data
        ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
   player->extraData.GetByType(kExtraData_ContainerChanges));
      
        if (!containerChanges || !containerChanges->data)
    {
      _MESSAGE("EquipManager::ForceUnequipHand - No container changes data!");
   return;
   }

     // Find the inventory entry for this item
 InventoryEntryData* entryData = containerChanges->data->FindItemEntry(item);
     if (!entryData)
   {
        _MESSAGE("EquipManager::ForceUnequipHand - Item not found in inventory!");
 return;
        }

      // Get the extra data lists for worn items
        BaseExtraList* rightEquipList = NULL;
      BaseExtraList* leftEquipList = NULL;
        entryData->GetExtraWornBaseLists(&rightEquipList, &leftEquipList);

    // Get the correct equip list and slot based on hand
        BaseExtraList* equipList = isLeftHand ? leftEquipList : rightEquipList;
     BGSEquipSlot* equipSlot = isLeftHand ? GetLeftHandSlot() : GetRightHandSlot();

        if (!equipList)
        {
 _MESSAGE("EquipManager::ForceUnequipHand - No equip list found for %s hand!", 
     isLeftHand ? "Left" : "Right");
            return;
        }

 // Remove CannotWear flag if present
        BSExtraData* xCannotWear = equipList->GetByType(kExtraData_CannotWear);
  if (xCannotWear)
        {
    equipList->Remove(kExtraData_CannotWear, xCannotWear);
        }

        // Unequip the item (silent - no sound, no message)
  CALL_MEMBER_FN(equipManager, UnequipItem)(player, item, equipList, 1, equipSlot, false, true, true, false, NULL);

    _MESSAGE("EquipManager: Force unequip command sent for %s hand (silent)", isLeftHand ? "Left" : "Right");
    }

    void EquipManager::ForceUnequipLeftHand()
    {
        ForceUnequipHand(true);
    }

    void EquipManager::ForceUnequipRightHand()
    {
        ForceUnequipHand(false);
    }

    void EquipManager::ForceReequipHand(bool isLeftHand)
    {
        PlayerCharacter* player = *g_thePlayer;
        if (!player)
        {
    _MESSAGE("EquipManager::ForceReequipHand - No player!");
            return;
        }

    // Use correct cached FormID for each hand
        UInt32 cachedFormID = isLeftHand ? m_cachedWeaponFormIDLeft : m_cachedWeaponFormIDRight;
      
        if (cachedFormID == 0)
        {
_MESSAGE("EquipManager::ForceReequipHand - No cached weapon FormID for %s hand!", 
     isLeftHand ? "Left" : "Right");
            return;
        }

        TESForm* weaponForm = LookupFormByID(cachedFormID);
        if (!weaponForm)
        {
            _MESSAGE("EquipManager::ForceReequipHand - Weapon form %08X not found!", cachedFormID);
 return;
        }
        
     ::EquipManager* equipMan = ::EquipManager::GetSingleton();
    if (!equipMan)
  {
            _MESSAGE("EquipManager::ForceReequipHand - EquipManager not available!");
            return;
        }
    
        // Get the appropriate slot for left or right hand
   BGSEquipSlot* slot = isLeftHand ? GetLeftHandSlot() : GetRightHandSlot();
        
        // Direct equip - same as auto-equip grabbed weapon
        CALL_MEMBER_FN(equipMan, EquipItem)(player, weaponForm, nullptr, 1, slot, false, true, false, nullptr);
     
        _MESSAGE("EquipManager: FORCE RE-EQUIPPED to %s hand (FormID: %08X) - direct call", 
            isLeftHand ? "Left" : "Right", cachedFormID);
        
    // Clear the pending re-equip and cached FormID for this hand
    ClearPendingReequip(isLeftHand);
        if (isLeftHand)
        {
  m_cachedWeaponFormIDLeft = 0;
  }
        else
        {
     m_cachedWeaponFormIDRight = 0;
    }
    }

    void EquipManager::ForceReequipLeftHand()
  {
        ForceReequipHand(true);
    }

    void EquipManager::ForceReequipRightHand()
    {
        ForceReequipHand(false);
    }

    bool EquipManager::HasPendingReequip(bool isLeftHand) const
    {
        return isLeftHand ? (m_pendingReequipLeft != nullptr) : (m_pendingReequipRight != nullptr);
    }

    void EquipManager::ClearPendingReequip(bool isLeftHand)
    {
    if (isLeftHand)
    {
      m_pendingReequipLeft = nullptr;
     }
        else
      {
         m_pendingReequipRight = nullptr;
        }
    }

    void EquipManager::ForceUnequipAndGrab(bool isLeftGameHand)
    {
    PlayerCharacter* player = *g_thePlayer;
        if (!player)
        {
            _MESSAGE("EquipManager::ForceUnequipAndGrab - No player!");
            return;
        }

    // ALWAYS use direct player check for what's equipped - our state might be stale
        TESForm* leftEquipped = player->GetEquippedObject(true);
   TESForm* rightEquipped = player->GetEquippedObject(false);
     
      TESForm* item = isLeftGameHand ? leftEquipped : rightEquipped;
        if (!item)
    {
  _MESSAGE("EquipManager::ForceUnequipAndGrab - %s GAME hand has no weapon (direct check)", 
        isLeftGameHand ? "Left" : "Right");
            return;
        }
   
        // Check if this is a weapon we should handle
   if (!IsWeapon(item))
  {
 _MESSAGE("EquipManager::ForceUnequipAndGrab - %s GAME hand item is not a weapon (FormID: %08X)", 
        isLeftGameHand ? "Left" : "Right", item->formID);
    return;
        }

        // Check if both hands have the SAME weapon (same FormID) - use DIRECT check
   bool bothHandsSameWeapon = leftEquipped && rightEquipped && 
  (leftEquipped->formID == rightEquipped->formID);
        
        if (bothHandsSameWeapon)
        {
   _MESSAGE("EquipManager::ForceUnequipAndGrab - SAME WEAPON in both hands (FormID: %08X)", item->formID);
        _MESSAGE("EquipManager::ForceUnequipAndGrab - Using special handling for duplicate weapons");
        }
        
        // Cache the FormID for later re-equip (use correct cache for each GAME hand)
        if (isLeftGameHand)
 {
        m_cachedWeaponFormIDLeft = item->formID;
      _MESSAGE("EquipManager: Cached LEFT GAME hand weapon FormID: %08X for re-equip", m_cachedWeaponFormIDLeft);
   }
 else
        {
   m_cachedWeaponFormIDRight = item->formID;
            _MESSAGE("EquipManager: Cached RIGHT GAME hand weapon FormID: %08X for re-equip", m_cachedWeaponFormIDRight);
  }
        
// Store for potential re-equip later
        if (isLeftGameHand)
        {
    m_pendingReequipLeft = item;
        }
        else
        {
  m_pendingReequipRight = item;
  }

        _MESSAGE("EquipManager: FORCE UNEQUIP AND GRAB - %s from %s GAME hand (FormID: %08X)", 
            GetWeaponTypeName(GetWeaponType(item)), 
  isLeftGameHand ? "Left" : "Right", 
  item->formID);

   // Step 1: Unequip the item first (uses GAME HAND)
        ::EquipManager* equipManager = ::EquipManager::GetSingleton();
        if (!equipManager)
        {
         _MESSAGE("EquipManager::ForceUnequipAndGrab - Failed to get game EquipManager!");
          return;
}

  ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(
    player->extraData.GetByType(kExtraData_ContainerChanges));
     
   if (!containerChanges || !containerChanges->data)
        {
 _MESSAGE("EquipManager::ForceUnequipAndGrab - No container changes data!");
          return;
}

        InventoryEntryData* entryData = containerChanges->data->FindItemEntry(item);
        if (!entryData)
        {
      _MESSAGE("EquipManager::ForceUnequipAndGrab - Item not found in inventory!");
    return;
        }

    BaseExtraList* rightEquipList = NULL;
        BaseExtraList* leftEquipList = NULL;
        entryData->GetExtraWornBaseLists(&rightEquipList, &leftEquipList);

        // Debug: Log what we got from GetExtraWornBaseLists
        _MESSAGE("EquipManager::ForceUnequipAndGrab - GetExtraWornBaseLists results:");
        _MESSAGE("  leftEquipList: %p, rightEquipList: %p", leftEquipList, rightEquipList);
        _MESSAGE("  Requested hand: %s GAME hand", isLeftGameHand ? "Left" : "Right");
        
        if (bothHandsSameWeapon)
   {
     _MESSAGE("  NOTE: Both hands have SAME weapon - entryData count: %d", entryData->countDelta);
      }

        // Note: These are GAME hand equip lists
        BaseExtraList* equipList = isLeftGameHand ? leftEquipList : rightEquipList;
 BGSEquipSlot* equipSlot = isLeftGameHand ? GetLeftHandSlot() : GetRightHandSlot();

        if (!equipList)
        {
          _MESSAGE("EquipManager::ForceUnequipAndGrab - No equip list found for %s hand!", 
isLeftGameHand ? "Left" : "Right");
          _MESSAGE("EquipManager::ForceUnequipAndGrab - leftEquipList: %p, rightEquipList: %p", 
             leftEquipList, rightEquipList);
  
            // If both hands have same weapon and we couldn't get the equip list,
      // try the other list as a fallback
            if (bothHandsSameWeapon)
            {
       BaseExtraList* fallbackList = isLeftGameHand ? rightEquipList : leftEquipList;
if (fallbackList)
          {
        _MESSAGE("EquipManager::ForceUnequipAndGrab - Using fallback equip list from other hand");
                    equipList = fallbackList;
             }
     else
        {
      _MESSAGE("EquipManager::ForceUnequipAndGrab - No fallback available, aborting");
    return;
         }
            }
    else
    {
          return;
 }
        }

BSExtraData* xCannotWear = equipList->GetByType(kExtraData_CannotWear);
   if (xCannotWear)
        {
 equipList->Remove(kExtraData_CannotWear, xCannotWear);
    }

   // Unequip the item (silent - no sound, no message)
        CALL_MEMBER_FN(equipManager, UnequipItem)(player, item, equipList, 1, equipSlot, false, true, true, false, NULL);

     _MESSAGE("EquipManager: Item unequipped (silent), now creating world object for HIGGS grab...");

        // Step 2: Get the hand position to spawn the weapon there
   NiNode* rootNode = player->GetNiRootNode(0);
    if (!rootNode)
        {
  rootNode = player->GetNiRootNode(1);
  }
      
        NiPoint3 spawnPos = player->pos;
    
      if (rootNode)
      {
            // Try to get the hand node position
  const char* handNodeName = isLeftGameHand ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]";
        BSFixedString handNodeStr(handNodeName);
      NiAVObject* handNode = rootNode->GetObjectByName(&handNodeStr.data);
      
    if (handNode)
     {
   spawnPos = handNode->m_worldTransform.pos;
          _MESSAGE("EquipManager: Spawning weapon at GAME %s hand position (%.2f, %.2f, %.2f)", 
    isLeftGameHand ? "Left" : "Right",
    spawnPos.x, spawnPos.y, spawnPos.z);
   }
else
  {
     _MESSAGE("EquipManager: Hand node not found, using player position");
   }
 }

      // Step 3: Create a world object using PlaceAtMe
     TESObjectREFR* droppedWeapon = PlaceAtMe_Native(nullptr, 0, player, item, 1, false, false);
      
   if (droppedWeapon)
     {
    _MESSAGE("EquipManager: Created world weapon reference (RefID: %08X)", droppedWeapon->formID);

 // Step 3.25: Set ownership to player to prevent "stolen" flag when picking up
        SetOwnerToPlayer(droppedWeapon);

  // Step 3.5: Remove the item from inventory to prevent duplication
  // PlaceAtMe creates a COPY, so we need to remove the original from inventory
        // EXCEPTION: If both hands have the same weapon, don't remove - we need it for the other hand!
        if (!bothHandsSameWeapon)
        {
    RemoveItemFromInventory(player, item, 1, true);
  _MESSAGE("EquipManager: Removed 1x item from inventory to prevent duplication");
        }
     else
   {
            _MESSAGE("EquipManager: SKIPPING inventory removal - same weapon in both hands, need it for other hand");
        }

 // Store the reference (by GAME hand)
 if (isLeftGameHand)
   {
      m_droppedWeaponLeft = droppedWeapon;
 }
     else
          {
   m_droppedWeaponRight = droppedWeapon;
       }

            // Step 4: Use HIGGS to grab the object
            // IMPORTANT: HIGGS uses VR CONTROLLER, not game hand!
   // We need to convert game hand to VR controller
    bool isLeftVRController = GameHandToVRController(isLeftGameHand);
        
     if (higgsInterface)
     {
 // Check if VR controller can grab
            if (higgsInterface->CanGrabObject(isLeftVRController))
    {
        _MESSAGE("EquipManager: HIGGS grabbing weapon with %s VR controller (game %s hand)!", 
      isLeftVRController ? "Left" : "Right",
   isLeftGameHand ? "Left" : "Right");
     higgsInterface->GrabObject(droppedWeapon, isLeftVRController);
   }
         else
       {
     _MESSAGE("EquipManager: HIGGS cannot grab with %s VR controller right now", 
       isLeftVRController ? "Left" : "Right");
    }
    }
  else
         {
         _MESSAGE("EquipManager: HIGGS interface not available!");
       }
     }
        else
  {
          _MESSAGE("EquipManager: Failed to create world weapon reference!");
 }
    }

    bool EquipManager::IsHiggsHoldingDroppedWeapon(bool isLeftHand) const
    {
        if (!higgsInterface)
            return false;
            
        TESObjectREFR* droppedRef = isLeftHand ? m_droppedWeaponLeft : m_droppedWeaponRight;
        if (!droppedRef)
      return false;
      
        TESObjectREFR* heldObject = higgsInterface->GetGrabbedObject(isLeftHand);
        return (heldObject == droppedRef);
    }

    TESObjectREFR* EquipManager::GetDroppedWeaponRef(bool isLeftHand) const
    {
      return isLeftHand ? m_droppedWeaponLeft : m_droppedWeaponRight;
    }

    void EquipManager::ClearDroppedWeaponRef(bool isLeftHand)
    {
        if (isLeftHand)
    {
          m_droppedWeaponLeft = nullptr;
     }
        else
   {
 m_droppedWeaponRight = nullptr;
        }
    }

    void EquipManager::ClearCachedWeaponFormID(bool isLeftHand)
    {
        if (isLeftHand)
        {
    m_cachedWeaponFormIDLeft = 0;
            _MESSAGE("EquipManager: Cleared cached weapon FormID for LEFT hand");
        }
     else
        {
        m_cachedWeaponFormIDRight = 0;
   _MESSAGE("EquipManager: Cleared cached weapon FormID for RIGHT hand");
        }
    }

    // ============================================
    // ContainerChangeEventHandler Implementation
    // ============================================
    
    EventResult ContainerChangeEventHandler::ReceiveEvent(TESContainerChangedEvent* evn, EventDispatcher<TESContainerChangedEvent>* dispatcher)
  {
        if (!evn)
  return kEvent_Continue;
            
     // Get the player's FormID
   PlayerCharacter* player = *g_thePlayer;
     if (!player)
       return kEvent_Continue;
        
        UInt32 playerFormID = player->formID;
        
        // Check if item is being added TO the player (player is the destination)
        if (evn->toFormId != playerFormID)
           return kEvent_Continue;
 
        // Look up the item form
      TESForm* itemForm = LookupFormByID(evn->itemFormId);
        if (!itemForm)
     return kEvent_Continue;
            
   // Check if this is a valid one-handed weapon we track
        if (!EquipManager::IsWeapon(itemForm))
           return kEvent_Continue;
        
      // Get weapon name for logging
        const char* weaponName = nullptr;
     TESFullName* fullName = DYNAMIC_CAST(itemForm, TESForm, TESFullName);
        if (fullName)
  {
 weaponName = fullName->GetName();
        }
        
    // Get weapon type
        WeaponType weaponType = EquipManager::GetWeaponType(itemForm);
        const char* typeName = EquipManager::GetWeaponTypeName(weaponType);
        
        // Log the weapon being added
 _MESSAGE("=== WEAPON ADDED TO PLAYER INVENTORY ===");
        _MESSAGE("  Name: %s", weaponName ? weaponName : "Unknown");
        _MESSAGE("  Type: %s", typeName);
  _MESSAGE("  FormID: %08X", evn->itemFormId);
        _MESSAGE("  Count: %d", evn->count);
     _MESSAGE("  From: %08X", evn->fromFormId);
    _MESSAGE("=========================================");
        
     // Play the weapon pickup sound from Fake Edge VR.esp (ESL-flagged)
     // BUT skip if this is from our internal re-equip logic (SafeActivate)
        if (EquipManager::s_suppressPickupSound)
     {
   _MESSAGE("EquipManager: Skipping pickup sound (internal re-equip)");
          return kEvent_Continue;
        }
        
  // Base FormID is 0x800, plugin name is "Fake Edge VR.esp"
   static UInt32 cachedSoundFormId = 0;
     if (cachedSoundFormId == 0)
     {
          cachedSoundFormId = GetFullFormIdFromEspAndFormId("Fake Edge VR.esp", 0x800);
   if (cachedSoundFormId != 0)
  {
  _MESSAGE("EquipManager: Cached weapon pickup sound FormID: %08X", cachedSoundFormId);
         }
     else
   {
    _MESSAGE("EquipManager: WARNING - Could not find weapon pickup sound in Fake Edge VR.esp");
   }
}

        PlaySoundAtPlayer(cachedSoundFormId);
        
 return kEvent_Continue;
    }

    // ============================================
    // Convenience Functions
    // ============================================

    void RegisterEquipEventHandler()
    {
        _MESSAGE("EquipManager: Registering event handlers...");
        
        auto* eventDispatcher = GetEventDispatcherList();
        if (eventDispatcher)
        {
            // Register equip event handler
          eventDispatcher->unk4D0.AddEventSink(EquipEventHandler::GetSingleton());
    _MESSAGE("EquipManager: Equip event handler registered successfully");
            
  // Register container change event handler
   eventDispatcher->unk370.AddEventSink(ContainerChangeEventHandler::GetSingleton());
            _MESSAGE("EquipManager: Container change event handler registered successfully");
        }
      else
        {
 _MESSAGE("EquipManager: ERROR - Failed to get event dispatcher list!");
        }
    }

    void UnregisterEquipEventHandler()
    {
        auto* eventDispatcher = GetEventDispatcherList();
   if (eventDispatcher)
        {
     eventDispatcher->unk4D0.RemoveEventSink(EquipEventHandler::GetSingleton());
            eventDispatcher->unk370.RemoveEventSink(ContainerChangeEventHandler::GetSingleton());
      _MESSAGE("EquipManager: Event handlers unregistered");
 }
    }
}
