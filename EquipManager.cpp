#include "EquipManager.h"
#include "VRInputHandler.h"
#include "Engine.h"
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

    // Only track player equipment
        Actor* actor = DYNAMIC_CAST(evn->actor, TESObjectREFR, Actor);
        if (!actor || actor != *g_thePlayer)
            return kEvent_Continue;

        TESForm* item = LookupFormByID(evn->baseObject);
  if (!item)
            return kEvent_Continue;

        // Check if this is a weapon or shield
      if (!EquipManager::IsWeapon(item) && !EquipManager::IsShield(item))
   return kEvent_Continue;

        _MESSAGE("EquipEventHandler: Received equip event for FormID %08X, equipped=%d", evn->baseObject, evn->equipped);

     // Determine which hand based on the equipped flag
        bool isEquipping = evn->equipped;
        
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

        case TESObjectWEAP::GameData::kType_TwoHandSword:
     case TESObjectWEAP::GameData::kType_2HS:
         return WeaponType::Greatsword;
          
            case TESObjectWEAP::GameData::kType_TwoHandAxe:
     case TESObjectWEAP::GameData::kType_2HA:
   return WeaponType::Battleaxe;
     
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
        case WeaponType::Axe:        return "Axe";
          case WeaponType::Greatsword: return "Greatsword";
        case WeaponType::Battleaxe:  return "Battleaxe";
       case WeaponType::Shield:   return "Shield";
 case WeaponType::None:
     default:        return "None";
        }
    }

    bool EquipManager::IsWeapon(TESForm* form)
    {
        if (!form)
            return false;

     return form->formType == kFormType_Weapon;
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
  CALL_MEMBER_FN(equipManager, UnequipItem)(player, item, equipList, 1, equipSlot, false, true, false, false, NULL);

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
        CALL_MEMBER_FN(equipManager, UnequipItem)(player, item, equipList, 1, equipSlot, false, true, false, false, NULL);

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
    // Convenience Functions
    // ============================================

    void RegisterEquipEventHandler()
    {
        _MESSAGE("EquipManager: Registering equip event handler...");
        
        auto* eventDispatcher = GetEventDispatcherList();
        if (eventDispatcher)
        {
            eventDispatcher->unk4D0.AddEventSink(EquipEventHandler::GetSingleton());
    _MESSAGE("EquipManager: Equip event handler registered successfully");
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
            _MESSAGE("EquipManager: Equip event handler unregistered");
 }
    }
}
