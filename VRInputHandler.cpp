#include "VRInputHandler.h"
#include "Engine.h"
#include "WeaponGeometry.h"
#include "ShieldCollision.h"
#include "ActivateHook.h"
#include "skse64/GameReferences.h"
#include <chrono>

namespace FalseEdgeVR
{
    // ============================================
    // VRInputHandler Implementation
    // ============================================

    VRInputHandler* VRInputHandler::GetSingleton()
    {
        static VRInputHandler instance;
        return &instance;
    }

    void VRInputHandler::Initialize()
    {
        if (m_initialized)
            return;

        _MESSAGE("VRInputHandler: Initializing...");

        // Register HIGGS callbacks if HIGGS is available
        RegisterHiggsCallbacks();

        m_initialized = true;
        _MESSAGE("VRInputHandler: Initialized successfully");
    }

    void VRInputHandler::RegisterHiggsCallbacks()
    {
        if (m_callbacksRegistered)
            return;

        if (!higgsInterface)
        {
            _MESSAGE("VRInputHandler: HIGGS interface not available, skipping callback registration");
            return;
        }

        _MESSAGE("VRInputHandler: Registering HIGGS callbacks...");

        // Register grab/drop callbacks
        higgsInterface->AddGrabbedCallback(OnGrabbed);
        higgsInterface->AddDroppedCallback(OnDropped);
        higgsInterface->AddPulledCallback(OnPulled);

        // Register collision callback for weapon impacts
        higgsInterface->AddCollisionCallback(OnCollision);

        // Register two-handing callbacks
        higgsInterface->AddStartTwoHandingCallback(OnStartTwoHanding);
        higgsInterface->AddStopTwoHandingCallback(OnStopTwoHanding);
        
        // Register pre-physics step callback for per-frame updates
        higgsInterface->AddPrePhysicsStepCallback(OnPrePhysicsStep);

        m_callbacksRegistered = true;
        _MESSAGE("VRInputHandler: HIGGS callbacks registered successfully");
    }

    void VRInputHandler::UpdateGrabListening()
    {
        bool shouldListen = ShouldListenForGrabs();

        if (shouldListen && !m_isListening)
        {
            m_isListening = true;
            _MESSAGE("VRInputHandler: Started listening for grab events (weapon equipped)");
        }
        else if (!shouldListen && m_isListening)
        {
            m_isListening = false;
            _MESSAGE("VRInputHandler: Stopped listening for grab events (no weapons equipped)");
        }
    }

    bool VRInputHandler::ShouldListenForGrabs() const
    {
        // Listen for grabs when either hand has a weapon equipped
        const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
        return equipState.leftHand.isEquipped || equipState.rightHand.isEquipped;
    }

    bool VRInputHandler::IsTwoHanding() const
    {
        if (!higgsInterface)
            return false;

        return higgsInterface->IsTwoHanding();
    }

    // ============================================
    // HIGGS Callback Handlers
    // ============================================
    
    void VRInputHandler::OnPrePhysicsStep(void* world)
    {
        static int frameCount = 0;
        static bool loggedOnce = false;
     
        VRInputHandler* handler = GetSingleton();
  
        // Calculate delta time
        static auto lastTime = std::chrono::high_resolution_clock::now();
  auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
      lastTime = currentTime;
        
        // Clamp delta time to reasonable values
  if (deltaTime > 0.1f) deltaTime = 0.1f;
        if (deltaTime < 0.0001f) deltaTime = 0.0001f;
        
        frameCount++;
        
        // Log once to confirm callback is working
     if (!loggedOnce)
        {
    _MESSAGE("VRInputHandler::OnPrePhysicsStep - Callback is firing! Frame: %d", frameCount);
            loggedOnce = true;
        }
    
  // Log every 500 frames to confirm still running
        if (frameCount % 500 == 0)
        {
   _MESSAGE("VRInputHandler::OnPrePhysicsStep - Frame %d, IsListening: %s", 
            frameCount, handler->IsListening() ? "YES" : "NO");
        }
     
  // Update cooldown timers
        if (handler->m_leftHandOnCooldown)
      {
 handler->m_leftHandCooldownTimer += deltaTime;
          if (handler->m_leftHandCooldownTimer >= bladeReequipCooldown)
     {
                handler->m_leftHandOnCooldown = false;
       handler->m_leftHandCooldownTimer = 0.0f;
        _MESSAGE("VRInputHandler: Left hand cooldown expired, can trigger again");
    }
  }
 if (handler->m_rightHandOnCooldown)
    {
       handler->m_rightHandCooldownTimer += deltaTime;
if (handler->m_rightHandCooldownTimer >= shieldReequipCooldown)
      {
        handler->m_rightHandOnCooldown = false;
        handler->m_rightHandCooldownTimer = 0.0f;
      _MESSAGE("VRInputHandler: Right hand cooldown expired, can trigger again");
      }
      }

        // Check for HIGGS collision timeout (grabbed LEFT weapon separating from equipped RIGHT weapon)
  handler->CheckCollisionTimeout(deltaTime);
      
     // Check for shield collision timeout (grabbed RIGHT weapon separating from equipped shield)
    handler->CheckShieldCollisionTimeout(deltaTime);
     
        // Check for pending re-equip after activation (both hands)
        handler->CheckPendingReequip(deltaTime);
      
        // Check for auto-equip of grabbed weapons (player grabbed weapon while other hand has weapon)
        handler->CheckAutoEquipGrabbedWeapon(deltaTime);
     
    // Update combat tracking (in combat, closest target distance)
   handler->UpdateCombatTracking();
   
        // Update shield bash tracking
      handler->UpdateShieldBashTracking(deltaTime);
  
     // ALWAYS update weapon geometry and shield collision tracking
        // These trackers handle their own equipment checks internally
    UpdateWeaponGeometry(deltaTime);
        UpdateShieldCollision(deltaTime);
    }
    

    void VRInputHandler::PauseTracking(bool pause)
    {
    // Menu pause tracking removed - trackers now run continuously
      // This function is kept for compatibility but does nothing
        if (pause)
        {
            _MESSAGE("VRInputHandler: Menu opened (tracking continues)");
      }
        else
     {
     _MESSAGE("VRInputHandler: Menu closed");
       // Force equipment state refresh when menu closes
 EquipManager::GetSingleton()->UpdateEquipmentState();
            UpdateGrabListening();
  }
    }

    void VRInputHandler::UpdateCombatTracking()
    {
        PlayerCharacter* player = *g_thePlayer;
        if (!player)
        {
            m_isInCombat = false;
  m_closestTargetDistance = 9999.0f;
       m_closestTargetHandle = 0;
            m_closeCombatMode = false;
            return;
  }
   
        // Check if player is in combat
    bool wasInCombat = m_isInCombat;
        m_isInCombat = player->IsInCombat();
    
     // Log combat state changes
        if (m_isInCombat && !wasInCombat)
      {
     _MESSAGE("VRInputHandler: === PLAYER ENTERED COMBAT ===");
          _MESSAGE("VRInputHandler:   currentCombatTarget handle: %08X", player->currentCombatTarget);
        }
   else if (!m_isInCombat && wasInCombat)
  {
            _MESSAGE("VRInputHandler: === PLAYER LEFT COMBAT ===");
    m_closestTargetDistance = 9999.0f;
          m_closestTargetHandle = 0;
       
  // Exit close combat mode when leaving combat
  if (m_closeCombatMode)
            {
   m_closeCombatMode = false;
     _MESSAGE("VRInputHandler: Exited CLOSE COMBAT MODE (left combat)");
            }
        }
   
     // If in combat, get current combat target distance
     if (m_isInCombat)
        {
            m_closestTargetDistance = 9999.0f;
   m_closestTargetHandle = 0;

   NiPoint3 playerPos = player->pos;
 
        // Try to get the current combat target from the Actor's currentCombatTarget handle
       UInt32 combatTargetHandle = player->currentCombatTarget;
         if (combatTargetHandle != 0 && combatTargetHandle != *g_invalidRefHandle)
            {
          NiPointer<TESObjectREFR> targetRefr;
  if (LookupREFRByHandle(combatTargetHandle, targetRefr) && targetRefr)
    {
  Actor* targetActor = DYNAMIC_CAST(targetRefr.get(), TESObjectREFR, Actor);
         if (targetActor && !targetActor->IsDead(1))
       {
    NiPoint3 targetPos = targetActor->pos;
       float dx = playerPos.x - targetPos.x;
 float dy = playerPos.y - targetPos.y;
       float dz = playerPos.z - targetPos.z;
   m_closestTargetDistance = sqrt(dx*dx + dy*dy + dz*dz);
        m_closestTargetHandle = combatTargetHandle;
         }
     }
      }
        else
            {
          // No combat target - log this periodically for debugging
         static int noTargetLogCounter = 0;
  noTargetLogCounter++;
          if (noTargetLogCounter % 200 == 1)
        {
      _MESSAGE("VRInputHandler: In combat but NO COMBAT TARGET! Handle: %08X, InvalidHandle: %08X",
    combatTargetHandle, *g_invalidRefHandle);
    }
            }
    
   // ============================================
     // Close Combat Mode Logic
            // ============================================
     bool wasInCloseCombatMode = m_closeCombatMode;
 
         if (!m_closeCombatMode)
     {
         // Enter close combat mode if within enter threshold
            if (m_closestTargetDistance <= closeCombatEnterDistance)
        {
         m_closeCombatMode = true;
    _MESSAGE("VRInputHandler: === ENTERED CLOSE COMBAT MODE ===");
         _MESSAGE("VRInputHandler:   Target distance: %.1f units (threshold: %.1f)", 
    m_closestTargetDistance, closeCombatEnterDistance);
 _MESSAGE("VRInputHandler:   Collision avoidance DISABLED - auto-equipping any grabbed weapons");
          
   // Auto-equip any grabbed weapons immediately
    ForceEquipGrabbedWeapons();
          }
     }
    else
            {
   // Exit close combat mode if past exit threshold (with buffer)
 if (m_closestTargetDistance > closeCombatExitDistance)
   {
       m_closeCombatMode = false;
        _MESSAGE("VRInputHandler: === EXITED CLOSE COMBAT MODE ===");
             _MESSAGE("VRInputHandler:   Target distance: %.1f units (threshold: %.1f)", 
         m_closestTargetDistance, closeCombatExitDistance);
      _MESSAGE("VRInputHandler:   Collision avoidance RE-ENABLED");
   }
            }
       
       // Log combat status periodically (every 2 seconds)
   m_combatLogTimer += 0.011f; // Approximate frame time at 90fps
  if (m_combatLogTimer >= 2.0f)
            {
         m_combatLogTimer = 0.0f;
    
           if (m_closestTargetHandle != 0)
             {
   NiPointer<TESObjectREFR> targetRefr;
 if (LookupREFRByHandle(m_closestTargetHandle, targetRefr) && targetRefr)
        {
       const char* targetName = CALL_MEMBER_FN(targetRefr.get(), GetReferenceName)();
          _MESSAGE("VRInputHandler: COMBAT STATUS - Target: %s, Distance: %.1f units (%.1f m), CloseCombat: %s", 
 targetName ? targetName : "Unknown",
           m_closestTargetDistance,
      m_closestTargetDistance / 70.0f,
     m_closeCombatMode ? "YES" : "NO");
       }
   }
       else
                {
         _MESSAGE("VRInputHandler: COMBAT STATUS - In combat but no specific target, CloseCombat: %s",
        m_closeCombatMode ? "YES" : "NO");
      }
            }
        }
        else
        {
     // Not in combat - ensure close combat mode is off
      if (m_closeCombatMode)
            {
    m_closeCombatMode = false;
      _MESSAGE("VRInputHandler: Exited CLOSE COMBAT MODE (not in combat)");
      }
    }
    }

    void VRInputHandler::ForceEquipGrabbedWeapons()
    {
        // Check if either hand has a grabbed weapon (from auto-equip pending or collision avoidance)
  // and immediately equip it
        
        PlayerCharacter* player = *g_thePlayer;
        if (!player)
    return;

        // Check left VR controller for grabbed weapon
        if (m_autoEquipWeaponLeft && higgsInterface)
        {
       TESObjectREFR* grabbed = higgsInterface->GetGrabbedObject(true);
            if (grabbed == m_autoEquipWeaponLeft && grabbed->baseForm)
            {
                _MESSAGE("VRInputHandler: Close combat - force equipping LEFT grabbed weapon");
       
     // Suppress pickup sound during internal re-equip
     EquipManager::s_suppressPickupSound = true;
                bool activated = SafeActivate(grabbed, player, 0, 0, 1, true);
                EquipManager::s_suppressPickupSound = false;
     if (activated)
    {
         bool isLeftGameHand = VRControllerToGameHand(true);
           ::EquipManager* equipMan = ::EquipManager::GetSingleton();
       if (equipMan)
            {
       BGSEquipSlot* slot = isLeftGameHand ? GetLeftHandSlot() : GetRightHandSlot();
    // Suppress draw sound during collision re-equip
         EquipManager::s_suppressDrawSound = true;
      CALL_MEMBER_FN(equipMan, EquipItem)(player, grabbed->baseForm, nullptr, 1, slot, false, true, false, nullptr);
       EquipManager::s_suppressDrawSound = false;
       _MESSAGE("VRInputHandler: Force equipped weapon to %s game hand", isLeftGameHand ? "LEFT" : "RIGHT");
          }
 }
         
              m_autoEquipPendingLeft = false;
  m_autoEquipTimerLeft = 0.0f;
          m_autoEquipWeaponLeft = nullptr;
         }
 }
        
        // Check right VR controller for grabbed weapon
        if (m_autoEquipWeaponRight && higgsInterface)
        {
            TESObjectREFR* grabbed = higgsInterface->GetGrabbedObject(false);
            if (grabbed == m_autoEquipWeaponRight && grabbed->baseForm)
       {
                _MESSAGE("VRInputHandler: Close combat - force equipping RIGHT grabbed weapon");
          
  // Suppress pickup sound during internal re-equip
     EquipManager::s_suppressPickupSound = true;
   bool activated = SafeActivate(grabbed, player, 0, 0, 1, false);
     EquipManager::s_suppressPickupSound = false;
        if (activated)
   {
 bool isLeftGameHand = VRControllerToGameHand(false);
            ::EquipManager* equipMan = ::EquipManager::GetSingleton();
    if (equipMan)
  {
      BGSEquipSlot* slot = isLeftGameHand ? GetLeftHandSlot() : GetRightHandSlot();
      // Suppress draw sound during auto-equip
   EquipManager::s_suppressDrawSound = true;
      CALL_MEMBER_FN(equipMan, EquipItem)(player, grabbed->baseForm, nullptr, 1, slot, false, true, false, nullptr);
       EquipManager::s_suppressDrawSound = false;
                 _MESSAGE("VRInputHandler: Force equipped weapon to %s game hand", isLeftGameHand ? "LEFT" : "RIGHT");
          }
}
         
         m_autoEquipPendingRight = false;
       m_autoEquipTimerRight = 0.0f;
        m_autoEquipWeaponRight = nullptr;
      }
 }
        
        // Also check collision avoidance grabbed weapons
        TESObjectREFR* leftDropped = EquipManager::GetSingleton()->GetDroppedWeaponRef(true);
if (leftDropped && higgsInterface)
        {
  TESObjectREFR* grabbed = higgsInterface->GetGrabbedObject(true);
            if (grabbed == leftDropped && grabbed->baseForm)
 {
         _MESSAGE("VRInputHandler: Close combat - force equipping LEFT collision-avoidance weapon");
    
                // Suppress pickup sound during internal re-equip
     EquipManager::s_suppressPickupSound = true;
    bool activated = SafeActivate(grabbed, player, 0, 0, 1, false);
    EquipManager::s_suppressPickupSound = false;
      if (activated)
           {
    // Re-equip using cached FormID (suppress draw sound)
         EquipManager::s_suppressDrawSound = true;
     EquipManager::GetSingleton()->ForceReequipLeftHand();
       EquipManager::s_suppressDrawSound = false;
     }
    
 // Clear collision avoidance state
       EquipManager::GetSingleton()->ClearDroppedWeaponRef(true);
        EquipManager::GetSingleton()->ClearPendingReequip(true);
     m_higgsCollisionActive = false;
        m_pendingReequip = false;
     }
        }
        
 TESObjectREFR* rightDropped = EquipManager::GetSingleton()->GetDroppedWeaponRef(false);
      if (rightDropped && higgsInterface)
      {
        TESObjectREFR* grabbed = higgsInterface->GetGrabbedObject(false);
            if (grabbed == rightDropped && grabbed->baseForm)
            {
    _MESSAGE("VRInputHandler: Close combat - force equipping RIGHT collision-avoidance weapon");
 
     // Suppress pickup sound during internal re-equip
     EquipManager::s_suppressPickupSound = true;
   bool activated = SafeActivate(grabbed, player, 0, 0, 1, false);
     EquipManager::s_suppressPickupSound = false;
          if (activated)
    {
    // Re-equip using cached FormID (suppress draw sound)
    EquipManager::s_suppressDrawSound = true;
  EquipManager::GetSingleton()->ForceReequipRightHand();
        EquipManager::s_suppressDrawSound = false;
      }
         
                // Clear collision avoidance state
        EquipManager::GetSingleton()->ClearDroppedWeaponRef(false);
              EquipManager::GetSingleton()->ClearPendingReequip(false);
     m_shieldCollisionActive = false;
     m_pendingReequipRight = false;
  }
     }
    }

    void VRInputHandler::OnShieldBash()
    {
        // Ignore if lockout is active
     if (m_shieldBashLockoutActive)
        {
       _MESSAGE("VRInputHandler: Shield bash detected but LOCKOUT is active (%.0f sec remaining)",
         shieldBashLockoutDuration - m_shieldBashLockoutTimer);
  return;
        }
  
        // If this is the first bash, start the window timer
        if (m_shieldBashCount == 0)
     {
            m_shieldBashWindowTimer = 0.0f;
        }
  
        m_shieldBashCount++;
        _MESSAGE("VRInputHandler: === SHIELD BASH DETECTED === Count: %d/%d (Window: %.1f/%.1f sec)",
            m_shieldBashCount, shieldBashThreshold, m_shieldBashWindowTimer, shieldBashWindow);
   
     // Check if threshold reached
        if (m_shieldBashCount >= shieldBashThreshold)
 {
            _MESSAGE("VRInputHandler: *** SHIELD BASH THRESHOLD REACHED *** %d bashes in %.1f seconds!",
         shieldBashThreshold, m_shieldBashWindowTimer);
      _MESSAGE("VRInputHandler: *** LOCKOUT ACTIVATED *** Duration: %.0f seconds",
      shieldBashLockoutDuration);
     
            // Cast spell on player (Skyrim.esm 0x000AA026)
          const UInt32 SHIELD_BASH_SPELL_FORM_ID = 0x000AA026;
   _MESSAGE("VRInputHandler: Casting shield bash spell %08X on player", SHIELD_BASH_SPELL_FORM_ID);
  CastSpellOnPlayer(SHIELD_BASH_SPELL_FORM_ID);

      // Activate lockout
            m_shieldBashLockoutActive = true;
         m_shieldBashLockoutTimer = 0.0f;
            m_shieldBashCount = 0;
     m_shieldBashWindowTimer = 0.0f;
        }
    }

    void VRInputHandler::OnWeaponSwing(bool isLeftHand, TESForm* weapon)
    {
     // Stub implementation - can be expanded later for swing detection
        // Currently not used
    }

    void VRInputHandler::UpdateShieldBashTracking(float deltaTime)
    {
        // Update lockout timer if active
        if (m_shieldBashLockoutActive)
      {
     m_shieldBashLockoutTimer += deltaTime;
  
        // Log progress every 30 seconds
            static float lockoutLogTimer = 0.0f;
   lockoutLogTimer += deltaTime;
     if (lockoutLogTimer >= 30.0f)
         {
         lockoutLogTimer = 0.0f;
      float remaining = shieldBashLockoutDuration - m_shieldBashLockoutTimer;
       _MESSAGE("VRInputHandler: Shield bash lockout: %.0f sec remaining (%.0f sec elapsed)",
   remaining, m_shieldBashLockoutTimer);
  }
            
  if (m_shieldBashLockoutTimer >= shieldBashLockoutDuration)
  {
   m_shieldBashLockoutActive = false;
        m_shieldBashLockoutTimer = 0.0f;
      lockoutLogTimer = 0.0f;
   _MESSAGE("VRInputHandler: *** SHIELD BASH LOCKOUT EXPIRED *** Bash tracking resumed");
 }
       return;
        }
     
   // Update window timer if we have bashes counted
        if (m_shieldBashCount > 0)
   {
     m_shieldBashWindowTimer += deltaTime;
   
   // Reset if window expired without reaching threshold
   if (m_shieldBashWindowTimer >= shieldBashWindow)
    {
           _MESSAGE("VRInputHandler: Shield bash window expired. Count was %d/%d - resetting",
   m_shieldBashCount, shieldBashThreshold);
      m_shieldBashCount = 0;
     m_shieldBashWindowTimer = 0.0f;
    }
        }
    }

    void VRInputHandler::OnGrabbed(bool isLeftVRController, TESObjectREFR* grabbedRefr)
    {
        VRInputHandler* handler = GetSingleton();

        // Convert VR controller to game hand
        bool isLeftGameHand = VRControllerToGameHand(isLeftVRController);

  const char* vrControllerName = isLeftVRController ? "Left" : "Right";
        const char* gameHandName = isLeftGameHand ? "Left" : "Right";

        if (grabbedRefr)
        {
     _MESSAGE("VRInputHandler: GRAB event - %s VR controller (game %s hand) grabbed object (FormID: %08X)", 
    vrControllerName, gameHandName, grabbedRefr->formID);

   // Get the base form to check what type of object was grabbed
       TESForm* baseForm = grabbedRefr->baseForm;
 if (baseForm)
        {
         _MESSAGE("VRInputHandler:   Base form type: %d, FormID: %08X", 
            baseForm->formType, baseForm->formID);
                
      // Check if grabbed object is a weapon
      if (baseForm->formType == kFormType_Weapon)
      {
        // Check if auto-equip feature is enabled
          if (!autoEquipGrabbedWeaponEnabled)
 {
   _MESSAGE("VRInputHandler: Grabbed weapon but auto-equip is disabled in INI");
   return;
     }
      
            // Check if the OTHER hand has a weapon equipped
 const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
  bool otherHandHasWeapon = isLeftGameHand ? 
          equipState.rightHand.isEquipped : equipState.leftHand.isEquipped;
       
   // Check if this grab is NOT from our collision avoidance system
    bool isFromCollisionAvoidance = EquipManager::GetSingleton()->HasPendingReequip(isLeftGameHand);
 
  if (otherHandHasWeapon && !isFromCollisionAvoidance)
 {
            _MESSAGE("VRInputHandler: Player grabbed a weapon while other hand has weapon equipped!");
         _MESSAGE("VRInputHandler: Starting auto-equip timer for %s VR hand (game %s hand)", 
           vrControllerName, gameHandName);
  
// Start auto-equip timer
          if (isLeftVRController)
         {
            handler->m_autoEquipPendingLeft = true;
     handler->m_autoEquipTimerLeft = 0.0f;
                 handler->m_autoEquipWeaponLeft = grabbedRefr;
   }
    else
   {
    handler->m_autoEquipPendingRight = true;
                 handler->m_autoEquipTimerRight = 0.0f;
            handler->m_autoEquipWeaponRight = grabbedRefr;
                }
         }
   }
      }
     }
    else
        {
            _MESSAGE("VRInputHandler: GRAB event - %s VR controller (game %s hand) (null reference)", 
   vrControllerName, gameHandName);
        }
    }

    void VRInputHandler::OnDropped(bool isLeftVRController, TESObjectREFR* droppedRefr)
    {
   if (!droppedRefr)
            return;

    // Convert VR controller to game hand
        bool isLeftGameHand = VRControllerToGameHand(isLeftVRController);

  _MESSAGE("VRInputHandler: DROP event - %s VR controller (game %s hand) dropped object (FormID: %08X)", 
            isLeftVRController ? "Left" : "Right",
            isLeftGameHand ? "Left" : "Right",
        droppedRefr->formID);

 VRInputHandler* handler = GetSingleton();
        
        // ============================================
 // Determine DROP REASON for logging/tracking
        // ============================================
        bool isAutoEquipWeapon = false;
        bool isCollisionAvoidanceWeapon = false;
        
        // Check if this was a weapon we were waiting to auto-equip
        if (isLeftVRController && handler->m_autoEquipWeaponLeft == droppedRefr)
        {
isAutoEquipWeapon = true;
            _MESSAGE("VRInputHandler: === ACCIDENTAL DROP DETECTED (LEFT) ===");
            _MESSAGE("VRInputHandler:   Weapon was pending auto-equip (grabbed from world)");
    _MESSAGE("VRInputHandler:   Cause: Player released grip OR physics collision knocked it away");
            
   handler->m_autoEquipPendingLeft = false;
            handler->m_autoEquipTimerLeft = 0.0f;
    handler->m_autoEquipWeaponLeft = nullptr;
        }
        else if (!isLeftVRController && handler->m_autoEquipWeaponRight == droppedRefr)
        {
            isAutoEquipWeapon = true;
   _MESSAGE("VRInputHandler: === ACCIDENTAL DROP DETECTED (RIGHT) ===");
 _MESSAGE("VRInputHandler:   Weapon was pending auto-equip (grabbed from world)");
            _MESSAGE("VRInputHandler:   Cause: Player released grip OR physics collision knocked it away");
     
 handler->m_autoEquipPendingRight = false;
            handler->m_autoEquipTimerRight = 0.0f;
 handler->m_autoEquipWeaponRight = nullptr;
        }

        // Check if this is the weapon we were tracking for collision avoidance
    TESObjectREFR* trackedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(isLeftGameHand);
    
      if (trackedWeapon && droppedRefr == trackedWeapon)
        {
     isCollisionAvoidanceWeapon = true;
       _MESSAGE("VRInputHandler: === ACCIDENTAL DROP DETECTED (Collision Avoidance Weapon) ===");
 _MESSAGE("VRInputHandler:   Weapon was grabbed by our collision avoidance system");
  _MESSAGE("VRInputHandler:   Cause: Player released grip OR physics collision knocked it away");
            _MESSAGE("VRInputHandler:   Game hand: %s", isLeftGameHand ? "Left" : "Right");
   
       // IMMEDIATELY teleport weapon to hand and force re-grab
      if (higgsInterface)
   {
       PlayerCharacter* player = *g_thePlayer;
      if (player)
     {
               NiNode* rootNode = player->GetNiRootNode(0);
              if (!rootNode)
           rootNode = player->GetNiRootNode(1);
          
  if (rootNode)
    {
                 // Get the VR controller hand node position
    const char* handNodeName = isLeftVRController ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]";
 BSFixedString handNodeStr(handNodeName);
         NiAVObject* handNode = rootNode->GetObjectByName(&handNodeStr.data);
           
  if (handNode)
         {
    // Teleport weapon directly to hand position
NiPoint3 handPos = handNode->m_worldTransform.pos;
         droppedRefr->pos = handPos;
       
          // Also update the NiNode position if it exists
       NiNode* weaponNode = droppedRefr->GetNiNode();
      if (weaponNode)
              {
    weaponNode->m_worldTransform.pos = handPos;
        }
          
 _MESSAGE("VRInputHandler: Teleported weapon to hand (%.1f, %.1f, %.1f)",
                  handPos.x, handPos.y, handPos.z);
   }
    }
         }
      
    // Force HIGGS to grab it immediately
    _MESSAGE("VRInputHandler: Force re-grabbing with %s VR controller", 
       isLeftVRController ? "LEFT" : "RIGHT");
    higgsInterface->GrabObject(droppedRefr, isLeftVRController);
    
 // Don't clear tracking state or return - let it continue to be monitored
      // but don't do the normal drop handling below
         return;
   }
     }

        // ============================================
      // Handle re-grab attempt for accidental drops
        // ============================================
     if (droppedRefr->baseForm && droppedRefr->baseForm->formType == kFormType_Weapon)
        {
         const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
            bool otherHandHasWeapon = isLeftGameHand ? 
    equipState.rightHand.isEquipped : equipState.leftHand.isEquipped;
   
        if (otherHandHasWeapon && higgsInterface)
            {
          _MESSAGE("VRInputHandler: Weapon dropped while other hand has weapon - attempting immediate re-grab");
   
            // Check if the hand can grab an object right now
    if (higgsInterface->CanGrabObject(isLeftVRController))
       {
         // Use HIGGS to grab the weapon again
        higgsInterface->GrabObject(droppedRefr, isLeftVRController);
         
       _MESSAGE("VRInputHandler: Re-grab command sent for %s VR controller", 
           isLeftVRController ? "LEFT" : "RIGHT");
         return;
 }
        else
            {
        _MESSAGE("VRInputHandler: Cannot re-grab - hand not in grabbable state");
          }
            }
        }

    // ============================================
    // Clear tracking state for collision avoidance weapon
        // ============================================
        if (isCollisionAvoidanceWeapon)
        {
            _MESSAGE("VRInputHandler: Clearing collision avoidance tracking for game %s hand", 
       isLeftGameHand ? "Left" : "Right");
  
     // Clear the dropped weapon reference (by game hand)
            EquipManager::GetSingleton()->ClearDroppedWeaponRef(isLeftGameHand);
            
         // Clear the pending re-equip (by game hand)
    EquipManager::GetSingleton()->ClearPendingReequip(isLeftGameHand);
   
      // Clear the cached FormID (by game hand)
    EquipManager::GetSingleton()->ClearCachedWeaponFormID(isLeftGameHand);
    
      // Clear collision state in VRInputHandler
     if (isLeftGameHand)
            {
          handler->m_higgsCollisionActive = false;
           handler->m_wasHiggsCollisionActive = false;
      handler->m_timeSinceLastCollision = 0.0f;
         handler->m_pendingReequip = false;
           handler->m_pendingReequipTimer = 0.0f;
            }
         else
  {
          handler->m_shieldCollisionActive = false;
       handler->m_wasShieldCollisionActive = false;
          handler->m_timeSinceLastShieldCollision = 0.0f;
      handler->m_pendingReequipRight = false;
  handler->m_pendingReequipRightTimer = 0.0f;
  }
         
 _MESSAGE("VRInputHandler: Cleared all tracking state for game %s hand - weapon was dropped", 
    isLeftGameHand ? "Left" : "Right");
        }
        
    // Log if this was a completely untracked weapon drop (not from our systems)
  if (!isAutoEquipWeapon && !isCollisionAvoidanceWeapon && 
 droppedRefr->baseForm && droppedRefr->baseForm->formType == kFormType_Weapon)
{
        _MESSAGE("VRInputHandler: Untracked weapon dropped (not managed by FalseEdgeVR)");
    }
    }

    void VRInputHandler::OnPulled(bool isLeftVRController, TESObjectREFR* pulledRefr)
    {
VRInputHandler* handler = GetSingleton();

        if (!handler->IsListening())
    return;

        // Convert VR controller to game hand
        bool isLeftGameHand = VRControllerToGameHand(isLeftVRController);
        const char* vrControllerName = isLeftVRController ? "Left" : "Right";
        const char* gameHandName = isLeftGameHand ? "Left" : "Right";

   if (pulledRefr)
        {
            LOG("VRInputHandler: PULL event - %s VR controller (game %s hand) pulled object (FormID: %08X)", 
 vrControllerName, gameHandName, pulledRefr->formID);
  }
        else
        {
     LOG("VRInputHandler: PULL event - %s VR controller (game %s hand) (null reference)", 
         vrControllerName, gameHandName);
        }
    }

    void VRInputHandler::OnCollision(bool isLeftVRController, float mass, float separatingVelocity)
    {
        VRInputHandler* handler = GetSingleton();

        if (!handler->IsListening())
      return;

        // Convert VR controller to game hand
        bool isLeftGameHand = VRControllerToGameHand(isLeftVRController);

// Check if this is a collision involving our grabbed weapon
        if (EquipManager::GetSingleton()->HasPendingReequip(isLeftGameHand))
        {
            handler->OnHiggsCollisionDetected(isLeftGameHand);
        }

 // Check for shield bash - high velocity collision from weapon hitting shield
      // Detect when RIGHT hand (grabbed weapon) collides while LEFT hand has shield
    const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
        bool hasShield = (equipState.leftHand.type == WeaponType::Shield);
        bool rightHandHasWeapon = equipState.rightHand.isEquipped;
        
        // Also check if HIGGS is holding a grabbed weapon in right VR controller
        bool rightHandGrabbedWeapon = false;
   if (higgsInterface)
        {
       TESObjectREFR* grabbed = higgsInterface->GetGrabbedObject(false); // false = right VR controller
       if (grabbed && grabbed->baseForm && grabbed->baseForm->formType == kFormType_Weapon)
   {
         rightHandGrabbedWeapon = true;
      }
        }
     
   // Shield bash detection: collision from right hand (weapon or grabbed weapon) while left has shield
        // The collision comes from the weapon hitting the shield
      if (!isLeftGameHand && hasShield && (rightHandHasWeapon || rightHandGrabbedWeapon) && separatingVelocity > 3.0f)
      {
 _MESSAGE("VRInputHandler: Potential SHIELD BASH - Weapon hit shield! Velocity: %.1f, Mass: %.1f", 
        separatingVelocity, mass);
        _MESSAGE("VRInputHandler:   Right hand equipped: %s, Right hand grabbed: %s",
   rightHandHasWeapon ? "YES" : "NO", rightHandGrabbedWeapon ? "YES" : "NO");
    handler->OnShieldBash();
        }

      // Only log significant collisions to avoid spam
        if (separatingVelocity > 1.0f)
        {
            const char* vrControllerName = isLeftVRController ? "Left" : "Right";
            const char* gameHandName = isLeftGameHand ? "Left" : "Right";
      LOG("VRInputHandler: COLLISION event - %s VR controller (game %s hand), mass: %.2f, velocity: %.2f", 
      vrControllerName, gameHandName, mass, separatingVelocity);
        }
    }

    void VRInputHandler::OnHiggsCollisionDetected(bool isLeftGameHand)
    {
  if (isLeftGameHand)
        {
    if (!m_higgsCollisionActive)
     {
   m_higgsCollisionActive = true;
      _MESSAGE("VRInputHandler: === GRABBED WEAPON (game LEFT hand) TOUCHING EQUIPPED WEAPON ===");
            }
   m_timeSinceLastCollision = 0.0f;
        }
  else
        {
            if (!m_shieldCollisionActive)
{
             m_shieldCollisionActive = true;
         _MESSAGE("VRInputHandler: === GRABBED WEAPON (game RIGHT hand) TOUCHING SHIELD ===");
  }
      m_timeSinceLastShieldCollision = 0.0f;
     }
    }

    void VRInputHandler::CheckCollisionTimeout(float deltaTime)
    {
        // Skip collision avoidance logic if in close combat mode
        if (m_closeCombatMode)
      return;
    
      if (!EquipManager::GetSingleton()->HasPendingReequip(true))
      {
   m_higgsCollisionActive = false;
         m_wasHiggsCollisionActive = false;
       m_timeSinceLastCollision = 0.0f;
     return;
        }

    TESObjectREFR* droppedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(true);
      if (!droppedWeapon)
   {
      _MESSAGE("CheckCollisionTimeout: Dropped weapon ref is NULL - clearing state");
       EquipManager::GetSingleton()->ClearPendingReequip(true);
         EquipManager::GetSingleton()->ClearDroppedWeaponRef(true);
   EquipManager::GetSingleton()->ClearCachedWeaponFormID(true);
    m_higgsCollisionActive = false;
   m_wasHiggsCollisionActive = false;
m_timeSinceLastCollision = 0.0f;
    return;
}
     
   if (higgsInterface)
        {
    TESObjectREFR* higgsHeld = higgsInterface->GetGrabbedObject(true);
      if (higgsHeld != droppedWeapon)
   {
            // HIGGS is not holding our weapon - but give it a few frames to actually grab
            // Track how long HIGGS has NOT been holding it
            static float higgsGrabWaitTime = 0.0f;
  static TESObjectREFR* lastDroppedWeapon = nullptr;
         
       // Reset wait time if this is a new dropped weapon
            if (lastDroppedWeapon != droppedWeapon)
            {
     higgsGrabWaitTime = 0.0f;
        lastDroppedWeapon = droppedWeapon;
            }
            
      higgsGrabWaitTime += deltaTime;
          
   // Only clear state if HIGGS hasn't grabbed after 0.3 seconds
        // This gives HIGGS time to actually grab the object
            if (higgsGrabWaitTime >= 0.3f)
            {
     _MESSAGE("CheckCollisionTimeout: HIGGS not holding our weapon after 0.3s (held: %p, dropped: %p) - clearing state",
          higgsHeld, droppedWeapon);
         EquipManager::GetSingleton()->ClearPendingReequip(true);
                EquipManager::GetSingleton()->ClearDroppedWeaponRef(true);
            EquipManager::GetSingleton()->ClearCachedWeaponFormID(true);
         m_higgsCollisionActive = false;
  m_wasHiggsCollisionActive = false;
    m_timeSinceLastCollision = 0.0f;
                higgsGrabWaitTime = 0.0f;
           lastDroppedWeapon = nullptr;
            }
            return;
        }
        else
        {
     // HIGGS is holding our weapon - reset the wait timer
    static float higgsGrabWaitTime = 0.0f;
    static TESObjectREFR* lastDroppedWeapon = nullptr;
     higgsGrabWaitTime = 0.0f;
         lastDroppedWeapon = nullptr;
        }
   }

        float currentDistance = GetCurrentBladeDistance();
  bool bladesClose = (currentDistance < bladeReequipThreshold);
        
        static int logCounter = 0;
   logCounter++;
if (logCounter % 100 == 0)
        {
            _MESSAGE("CheckCollisionTimeout: Distance=%.2f, Threshold=%.2f, BladesClose=%s, Timer=%.3f, Timeout=%.3f",
    currentDistance, bladeReequipThreshold, bladesClose ? "YES" : "NO", 
m_timeSinceLastCollision, bladeCollisionTimeout);
        }
   
        if (bladesClose)
        {
       m_timeSinceLastCollision = 0.0f;
            m_higgsCollisionActive = true;
        }
        else
        {
 m_timeSinceLastCollision += deltaTime;
        
       if (m_timeSinceLastCollision >= bladeCollisionTimeout)
    {
     m_higgsCollisionActive = false;
                _MESSAGE("VRInputHandler: === GRABBED WEAPON SAFE TO RE-EQUIP ===");
   _MESSAGE("VRInputHandler: Time separated: %.3f sec, Distance: %.2f (threshold: %.2f)", 
   m_timeSinceLastCollision, currentDistance, bladeReequipThreshold);
  
        droppedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(true);
       if (droppedWeapon && droppedWeapon->baseForm)
     {
    _MESSAGE("VRInputHandler: Activating grabbed weapon to add to inventory (RefID: %08X, BaseID: %08X)...",
      droppedWeapon->formID, droppedWeapon->baseForm->formID);
         
   PlayerCharacter* player = *g_thePlayer;
    if (player)
     {
     // Suppress pickup sound during internal re-equip
         EquipManager::s_suppressPickupSound = true;
     bool activated = SafeActivate(droppedWeapon, player, 0, 0, 1, false);
  EquipManager::s_suppressPickupSound = false;
    _MESSAGE("VRInputHandler: Activate result: %s", activated ? "SUCCESS" : "FAILED");
   }
    }
    else
    {
  _MESSAGE("VRInputHandler: WARNING - Dropped weapon ref became invalid before activation!");
 }
   
          // Clear the dropped weapon ref but KEEP the cached FormID for re-equip!
  EquipManager::GetSingleton()->ClearDroppedWeaponRef(true);
      
    // Also clear PendingReequip so CheckCollisionTimeout doesn't clear cached FormID
        EquipManager::GetSingleton()->ClearPendingReequip(true);
       
          m_pendingReequip = true;
     m_pendingReequipIsLeft = true;
     m_pendingReequipTimer = 0.0f;
       _MESSAGE("VRInputHandler: Scheduled re-equip for left hand in %.1f ms", reequipDelay * 1000.0f);
            }
        }
    }

    void VRInputHandler::CheckPendingReequip(float deltaTime)
    {
        if (m_pendingReequip)
        {
            m_pendingReequipTimer += deltaTime;
  
       if (m_pendingReequipTimer >= reequipDelay)
     {
     _MESSAGE("VRInputHandler: Re-equipping weapon to %s hand after %.3f ms delay", 
 m_pendingReequipIsLeft ? "left" : "right", m_pendingReequipTimer * 1000.0f);
 
              m_pendingReequip = false;
         m_pendingReequipTimer = 0.0f;
 
            if (m_pendingReequipIsLeft)
      {
        // Suppress draw sound during collision re-equip
        EquipManager::s_suppressDrawSound = true;
     EquipManager::GetSingleton()->ForceReequipLeftHand();
         EquipManager::s_suppressDrawSound = false;
           m_leftHandOnCooldown = true;
m_leftHandCooldownTimer = 0.0f;
  _MESSAGE("VRInputHandler: Started %.0fms cooldown for left hand", bladeReequipCooldown * 1000.0f);
       }
       else
  {
        // Suppress draw sound during shield collision re-equip
        EquipManager::s_suppressDrawSound = true;
        EquipManager::GetSingleton()->ForceReequipRightHand();
   EquipManager::s_suppressDrawSound = false;
 m_rightHandOnCooldown = true;
 m_rightHandCooldownTimer = 0.0f;
          _MESSAGE("VRInputHandler: Started %.0fms cooldown for right hand (shield)", shieldReequipCooldown * 1000.0f);
        }
        }
        }
  
  if (m_pendingReequipRight)
   {
            m_pendingReequipRightTimer += deltaTime;
    
       if (m_pendingReequipRightTimer >= shieldReequipDelay)
    {
       _MESSAGE("VRInputHandler: Re-equipping weapon to RIGHT hand after %.3f ms delay (shield collision)", 
    m_pendingReequipRightTimer * 1000.0f);
 
   m_pendingReequipRight = false;
        m_pendingReequipRightTimer = 0.0f;
    
      // Suppress draw sound during shield collision re-equip
        EquipManager::s_suppressDrawSound = true;
  EquipManager::GetSingleton()->ForceReequipRightHand();
        EquipManager::s_suppressDrawSound = false;
    m_rightHandOnCooldown = true;
    m_rightHandCooldownTimer = 0.0f;
          _MESSAGE("VRInputHandler: Started %.0fms cooldown for right hand (shield)", shieldReequipCooldown * 1000.0f);
    }
     }
    }

    void VRInputHandler::CheckAutoEquipGrabbedWeapon(float deltaTime)
    {
        // If in close combat mode, weapons are force-equipped immediately
        // so skip the normal delayed auto-equip logic
        if (m_closeCombatMode)
     return;
          
        // First, check if the OTHER hand still has a weapon equipped
     // If player manually unequipped their main hand weapon, cancel auto-equip
     const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
        
      if (m_autoEquipPendingLeft && m_autoEquipWeaponLeft)
   {
      // Left VR controller is holding grabbed weapon - check if RIGHT game hand still has weapon
            bool isLeftGameHand = VRControllerToGameHand(true);
 bool otherHandHasWeapon = isLeftGameHand ? 
   equipState.rightHand.isEquipped : equipState.leftHand.isEquipped;
  
       if (!otherHandHasWeapon)
 {
             _MESSAGE("VRInputHandler: Auto-equip cancelled for LEFT VR hand - other hand no longer has weapon equipped");
     m_autoEquipPendingLeft = false;
       m_autoEquipTimerLeft = 0.0f;
           m_autoEquipWeaponLeft = nullptr;
   }
            else if (!higgsInterface || higgsInterface->GetGrabbedObject(true) != m_autoEquipWeaponLeft)
  {
       _MESSAGE("VRInputHandler: Auto-equip cancelled for LEFT VR hand - weapon no longer held");
  m_autoEquipPendingLeft = false;
     m_autoEquipTimerLeft = 0.0f;
     m_autoEquipWeaponLeft = nullptr;
            }
  else
       {
        // Check distance between GRABBED weapon and EQUIPPED weapon in the other hand
     // This is the key fix - we need to check grabbed-to-equipped distance, not equipped-to-equipped
          float bladeDistance = GetGrabbedToEquippedDistance(true);  // true = left VR controller
        
                // Reset timer if blades are within imminent collision range (friction/sliding)
  if (bladeDistance < bladeImminentThreshold)
            {
       // Blades are close (friction range) - reset timer
   if (m_autoEquipTimerLeft > 0.0f)
      {
            static bool loggedReset = false;
     if (!loggedReset)
  {
       _MESSAGE("VRInputHandler: Auto-equip timer reset - grabbed weapon near equipped (dist: %.2f < %.2f)",
         bladeDistance, bladeImminentThreshold);
              loggedReset = true;
        }
    }
       m_autoEquipTimerLeft = 0.0f;
     }
       else
           {
            // Blades are far enough apart - increment timer
         static bool loggedReset = false;
     loggedReset = false;  // Reset log flag when blades separate
     
       m_autoEquipTimerLeft += deltaTime;
    
  if (m_autoEquipTimerLeft >= autoEquipGrabbedWeaponDelay)
        {
     _MESSAGE("VRInputHandler: Auto-equipping grabbed weapon to LEFT game hand after %.1f sec",
                   autoEquipGrabbedWeaponDelay);
 
             TESForm* weaponForm = m_autoEquipWeaponLeft->baseForm;
     if (weaponForm)
      {
   bool isLeftGameHand = VRControllerToGameHand(true);

            PlayerCharacter* player = *g_thePlayer;
    if (player)
      {
// Suppress pickup sound during internal re-equip
           EquipManager::s_suppressPickupSound = true;
 bool activated = SafeActivate(m_autoEquipWeaponLeft, player, 0, 0, 1, true);
       EquipManager::s_suppressPickupSound = false;
   _MESSAGE("VRInputHandler: Activate grabbed weapon result: %s", activated ? "SUCCESS" : "FAILED");
 
      if (activated)
      {
    ::EquipManager* equipMan = ::EquipManager::GetSingleton();
  if (equipMan)
   {
    BGSEquipSlot* slot = isLeftGameHand ? GetLeftHandSlot() : GetRightHandSlot();
    // Suppress draw sound during auto-equip
      EquipManager::s_suppressDrawSound = true;
   CALL_MEMBER_FN(equipMan, EquipItem)(player, weaponForm, nullptr, 1, slot, false, true, false, nullptr);
 EquipManager::s_suppressDrawSound = false;
   _MESSAGE("VRInputHandler: Equipped weapon to %s game hand (silent)",
isLeftGameHand ? "LEFT" : "RIGHT");
          
        // Start cooldown to prevent immediate collision detection re-triggering
   if (isLeftGameHand)
     {
         m_leftHandOnCooldown = true;
    m_leftHandCooldownTimer = 0.0f;
  _MESSAGE("VRInputHandler: Started %.0fms cooldown for left hand", bladeReequipCooldown * 1000.0f);
       }
        else
  {
  m_rightHandOnCooldown = true;
         m_rightHandCooldownTimer = 0.0f;
             _MESSAGE("VRInputHandler: Started %.0fms cooldown for right hand", bladeReequipCooldown * 1000.0f);
     }
    }
    }
        }
   }
        
        m_autoEquipPendingLeft = false;
  m_autoEquipTimerLeft = 0.0f;
         m_autoEquipWeaponLeft = nullptr;
         }
    }
  }
        }
        
      if (m_autoEquipPendingRight && m_autoEquipWeaponRight)
        {
      // Right VR controller is holding grabbed weapon - check if LEFT game hand still has weapon
       bool isLeftGameHand = VRControllerToGameHand(false);
   bool otherHandHasWeapon = isLeftGameHand ? 
   equipState.rightHand.isEquipped : equipState.leftHand.isEquipped;
            
  if (!otherHandHasWeapon)
    {
       _MESSAGE("VRInputHandler: Auto-equipCancelled for RIGHT VR hand - other hand no longer has weapon equipped");
      m_autoEquipPendingRight = false;
      m_autoEquipTimerRight = 0.0f;
           m_autoEquipWeaponRight = nullptr;
 }
     else if (!higgsInterface || higgsInterface->GetGrabbedObject(false) != m_autoEquipWeaponRight)
       {
   _MESSAGE("VRInputHandler: Auto-equipCancelled for RIGHT VR hand - weapon no longer held");
    m_autoEquipPendingRight = false;
      m_autoEquipTimerRight = 0.0f;
    m_autoEquipWeaponRight = nullptr;
     }
      else
       {
  // Check distance between GRABBED weapon and EQUIPPED weapon in the other hand
 float bladeDistance = GetGrabbedToEquippedDistance(false);  // false = right VR controller
    
 // Reset timer if blades are within imminent collision range (friction/sliding)
         if (bladeDistance < bladeImminentThreshold)
   {
    // Blades are close (friction range) - reset timer
        if (m_autoEquipTimerRight > 0.0f)
         {
                 static bool loggedResetRight = false;
         if (!loggedResetRight)
      {
         _MESSAGE("VRInputHandler: Auto-equip timer reset (RIGHT) - grabbed weapon near equipped (dist: %.2f < %.2f)",
      bladeDistance, bladeImminentThreshold);
      loggedResetRight = true;
          }
         }
  m_autoEquipTimerRight = 0.0f;
     }
                else
       {
     // Blades are far enough apart - increment timer
       static bool loggedResetRight = false;
   loggedResetRight = false;  // Reset log flag when blades separate
           
          m_autoEquipTimerRight += deltaTime;
       
      if (m_autoEquipTimerRight >= autoEquipGrabbedWeaponDelay)
              {
    _MESSAGE("VRInputHandler: Auto-equipping grabbed weapon to RIGHT game hand after %.1f sec",
    autoEquipGrabbedWeaponDelay);
  
          TESForm* weaponForm = m_autoEquipWeaponRight->baseForm;
        if (weaponForm)
       {
         bool isLeftGameHand = VRControllerToGameHand(false);
   
  PlayerCharacter* player = *g_thePlayer;
          if (player)
  {
      // Suppress pickup sound during internal re-equip
         EquipManager::s_suppressPickupSound = true;
      bool activated = SafeActivate(m_autoEquipWeaponRight, player, 0, 0, 1, true);
         EquipManager::s_suppressPickupSound = false;
 _MESSAGE("VRInputHandler: Activate grabbed weapon result: %s", activated ? "SUCCESS" : "FAILED");
     
  if (activated)
   {
  ::EquipManager* equipMan = ::EquipManager::GetSingleton();
      if (equipMan)
        {
   BGSEquipSlot* slot = isLeftGameHand ? GetLeftHandSlot() : GetRightHandSlot();
   // Suppress draw sound during auto-equip
   EquipManager::s_suppressDrawSound = true;
 CALL_MEMBER_FN(equipMan, EquipItem)(player, weaponForm, nullptr, 1, slot, false, true, false, nullptr);
  EquipManager::s_suppressDrawSound = false;
           _MESSAGE("VRInputHandler: Equipped weapon to %s game hand (silent)",
   isLeftGameHand ? "LEFT" : "RIGHT");
     
       // Start cooldown to prevent immediate collision detection re-triggering
         if (isLeftGameHand)
  {
            m_leftHandOnCooldown = true;
           m_leftHandCooldownTimer = 0.0f;
     _MESSAGE("VRInputHandler: Started %.0fms cooldown for left hand (auto-equip)", bladeReequipCooldown * 1000.0f);
        }
       else
  {
           m_rightHandOnCooldown = true;
    m_rightHandCooldownTimer = 0.0f;
   _MESSAGE("VRInputHandler: Started %.0fms cooldown for right hand (auto-equip)", bladeReequipCooldown * 1000.0f);
      }
   }
   }
      }
      }
      
      m_autoEquipPendingRight = false;
   m_autoEquipTimerRight = 0.0f;
     m_autoEquipWeaponRight = nullptr;
         }
             }
   }
      }
    }

    float VRInputHandler::GetCurrentBladeDistance() const
    {
      WeaponGeometryTracker* tracker = WeaponGeometryTracker::GetSingleton();
    if (!tracker)
      return 9999.0f;
        
      const BladeGeometry& leftGeom = tracker->GetBladeGeometry(true);
  const BladeGeometry& rightGeom = tracker->GetBladeGeometry(false);
 
        if (!leftGeom.isValid || !rightGeom.isValid)
            return 9999.0f;
     
        NiPoint3 leftMid, rightMid;
    leftMid.x = (leftGeom.basePosition.x + leftGeom.tipPosition.x) * 0.5f;
 leftMid.y = (leftGeom.basePosition.y + leftGeom.tipPosition.y) * 0.5f;
        leftMid.z = (leftGeom.basePosition.z + leftGeom.tipPosition.z) * 0.5f;
        
    rightMid.x = (rightGeom.basePosition.x + rightGeom.tipPosition.x) * 0.5f;
        rightMid.y = (rightGeom.basePosition.y + rightGeom.tipPosition.y) * 0.5f;
    rightMid.z = (rightGeom.basePosition.z + rightGeom.tipPosition.z) * 0.5f;
 
        float dx = leftMid.x - rightMid.x;
   float dy = leftMid.y - rightMid.y;
      float dz = leftMid.z - rightMid.z;
        
 return sqrt(dx*dx + dy*dy + dz*dz);
    }

  float VRInputHandler::GetGrabbedToEquippedDistance(bool isLeftVRController) const
    {
        // Get the HIGGS grabbed weapon position
  if (!higgsInterface)
        return 9999.0f;
        
        TESObjectREFR* grabbedWeapon = higgsInterface->GetGrabbedObject(isLeftVRController);
        if (!grabbedWeapon)
    return 9999.0f;
     
        // Get the grabbed weapon's NiNode for position
        NiNode* grabbedNode = grabbedWeapon->GetNiNode();
        if (!grabbedNode)
            return 9999.0f;
        
        NiPoint3 grabbedPos = grabbedNode->m_worldTransform.pos;
        
        // Get the equipped weapon geometry from the OTHER hand
        WeaponGeometryTracker* tracker = WeaponGeometryTracker::GetSingleton();
        if (!tracker)
     return 9999.0f;
        
        // If left VR controller is grabbing, check against RIGHT hand equipped weapon
        // (Remember: left VR controller = left game hand in standard mode)
        bool isLeftGameHand = VRControllerToGameHand(isLeftVRController);
        const BladeGeometry& equippedGeom = tracker->GetBladeGeometry(!isLeftGameHand);
        
        if (!equippedGeom.isValid)
  return 9999.0f;
        
        // Calculate distance from grabbed weapon to equipped weapon's midpoint
        NiPoint3 equippedMid;
        equippedMid.x = (equippedGeom.basePosition.x + equippedGeom.tipPosition.x) * 0.5f;
        equippedMid.y = (equippedGeom.basePosition.y + equippedGeom.tipPosition.y) * 0.5f;
        equippedMid.z = (equippedGeom.basePosition.z + equippedGeom.tipPosition.z) * 0.5f;
        
        float dx = grabbedPos.x - equippedMid.x;
        float dy = grabbedPos.y - equippedMid.y;
   float dz = grabbedPos.z - equippedMid.z;
     
        return sqrt(dx*dx + dy*dy + dz*dz);
    }

    void VRInputHandler::OnStartTwoHanding()
    {
        _MESSAGE("VRInputHandler: TWO-HANDING started");

        VRInputHandler* handler = GetSingleton();
        if (handler->IsListening())
        {
  _MESSAGE("VRInputHandler:   Player is dual wielding and started two-handing a weapon");
     }
    }

    void VRInputHandler::OnStopTwoHanding()
    {
        _MESSAGE("VRInputHandler: TWO-HANDING stopped");
    }

    void VRInputHandler::OnShieldCollisionDetected()
    {
        if (!m_shieldCollisionActive)
     {
            _MESSAGE("VRInputHandler: === RIGHT HAND WEAPON TOUCHING SHIELD ===");
        }
        m_shieldCollisionActive = true;
      m_timeSinceLastShieldCollision = 0.0f;
    }

    void VRInputHandler::CheckShieldCollisionTimeout(float deltaTime)
    {
        // Skip collision avoidance logic if in close combat mode
        if (m_closeCombatMode)
            return;
            
        if (!EquipManager::GetSingleton()->HasPendingReequip(false))
        {
    m_shieldCollisionActive = false;
   m_wasShieldCollisionActive = false;
            m_timeSinceLastShieldCollision = 0.0f;
            return;
  }

    TESObjectREFR* droppedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(false);
 if (!droppedWeapon)
        {
         _MESSAGE("CheckShieldCollisionTimeout: Dropped weapon ref is NULL - clearing state");
   EquipManager::GetSingleton()->ClearPendingReequip(false);
   EquipManager::GetSingleton()->ClearDroppedWeaponRef(false);
     EquipManager::GetSingleton()->ClearCachedWeaponFormID(false);
       m_shieldCollisionActive = false;
        m_wasShieldCollisionActive = false;
          m_timeSinceLastShieldCollision = 0.0f;
    return;
        }
   
  if (higgsInterface)
        {
     TESObjectREFR* higgsHeld = higgsInterface->GetGrabbedObject(false);
 if (higgsHeld != droppedWeapon)
       {
            // HIGGS is not holding our weapon - but give it a few frames to actually grab
   // Track how long HIGGS has NOT been holding it
 static float higgsGrabWaitTimeRight = 0.0f;
       static TESObjectREFR* lastDroppedWeaponRight = nullptr;
            
        // Reset wait time if this is a new dropped weapon
      if (lastDroppedWeaponRight != droppedWeapon)
      {
    higgsGrabWaitTimeRight = 0.0f;
                lastDroppedWeaponRight = droppedWeapon;
    }
      
      higgsGrabWaitTimeRight += deltaTime;
     
         // Only clear state if HIGGS hasn't grabbed after 0.3 seconds
    // This gives HIGGS time to actually grab the object
            if (higgsGrabWaitTimeRight >= 0.3f)
       {
              _MESSAGE("CheckShieldCollisionTimeout: HIGGS not holding our weapon after 0.3s (held: %p, ours: %p) - clearing state",
   higgsHeld, droppedWeapon);
         EquipManager::GetSingleton()->ClearPendingReequip(false);
       EquipManager::GetSingleton()->ClearDroppedWeaponRef(false);
  EquipManager::GetSingleton()->ClearCachedWeaponFormID(false);
         m_shieldCollisionActive = false;
      m_wasShieldCollisionActive = false;
   m_timeSinceLastShieldCollision = 0.0f;
        higgsGrabWaitTimeRight = 0.0f;
      lastDroppedWeaponRight = nullptr;
     }
          return;
  }
        else
        {
            // HIGGS is holding our weapon - reset the wait timer
       static float higgsGrabWaitTimeRight = 0.0f;
      static TESObjectREFR* lastDroppedWeaponRight = nullptr;
            higgsGrabWaitTimeRight = 0.0f;
            lastDroppedWeaponRight = nullptr;
        }
   }

    float currentDistance = GetCurrentWeaponShieldDistance();
        bool weaponClose = (currentDistance < shieldReequipThreshold);
      
     static int logCounter = 0;
  logCounter++;
        if (logCounter % 100 == 0)
 {
  _MESSAGE("CheckShieldCollisionTimeout: Distance=%.2f, Threshold=%.2f, WeaponClose=%s, Timer=%.3f, Timeout=%.3f",
      currentDistance, shieldReequipThreshold, weaponClose ? "YES" : "NO", 
    m_timeSinceLastShieldCollision, shieldCollisionTimeout);
    }
        
  if (weaponClose)
    {
        m_timeSinceLastShieldCollision = 0.0f;
         m_shieldCollisionActive = true;
     }
        else
    {
m_timeSinceLastShieldCollision += deltaTime;
      
    if (m_timeSinceLastShieldCollision >= shieldCollisionTimeout)
       {
     m_shieldCollisionActive = false;
       _MESSAGE("VRInputHandler: === RIGHT HAND WEAPON SAFE TO RE-EQUIP ===");
       _MESSAGE("VRInputHandler: Time separated: %.3f sec, Distance: %.2f (threshold: %.2f)", 
        m_timeSinceLastShieldCollision, currentDistance, shieldReequipThreshold);
  
     droppedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(false);
      if (droppedWeapon && droppedWeapon->baseForm)
        {
   _MESSAGE("VRInputHandler: Activating grabbed RIGHT weapon to add to inventory (RefID: %08X, BaseID: %08X)...",
droppedWeapon->formID, droppedWeapon->baseForm->formID);
     
     PlayerCharacter* player = *g_thePlayer;
   if (player)
 {
     // Suppress pickup sound during internal re-equip
         EquipManager::s_suppressPickupSound = true;
     bool activated = SafeActivate(droppedWeapon, player, 0, 0, 1, false);
  EquipManager::s_suppressPickupSound = false;
    _MESSAGE("VRInputHandler: Activate result: %s", activated ? "SUCCESS" : "FAILED");
           }
  }
    else
{
      _MESSAGE("VRInputHandler: WARNING - Dropped weapon ref became invalid before activation!");
}
    
 // Clear the dropped weapon ref but KEEP the cached FormID for re-equip!
        EquipManager::GetSingleton()->ClearDroppedWeaponRef(false);

        // Also clear PendingReequip so CheckShieldCollisionTimeout doesn't clear cached FormID
   EquipManager::GetSingleton()->ClearPendingReequip(false);

    m_pendingReequipRight = true;
          m_pendingReequipRightTimer = 0.0f;
        _MESSAGE("VRInputHandler: Scheduled re-equip for RIGHT hand in %.1f ms", shieldReequipDelay * 1000.0f);
  }
        }
    }

float VRInputHandler::GetCurrentWeaponShieldDistance() const
    {
 // First, check if we have a HIGGS-grabbed weapon for the right hand (shield collision case)
   TESObjectREFR* droppedWeapon = EquipManager::GetSingleton()->GetDroppedWeaponRef(false);
        if (droppedWeapon && higgsInterface)
  {
       TESObjectREFR* higgsHeld = higgsInterface->GetGrabbedObject(false); // false = right VR controller
    if (higgsHeld == droppedWeapon)
   {
        // Get position of HIGGS grabbed weapon
       NiNode* weaponNode = droppedWeapon->GetNiNode();
    if (weaponNode)
    {
        NiPoint3 weaponPos = weaponNode->m_worldTransform.pos;
       
      // Get shield position from player's left hand
   PlayerCharacter* player = *g_thePlayer;
       if (player)
 {
          NiNode* rootNode = player->GetNiRootNode(0);
   if (!rootNode)
        rootNode = player->GetNiRootNode(1);
           
        if (rootNode)
     {
    BSFixedString shieldNodeStr("SHIELD");
         NiAVObject* shieldNode = rootNode->GetObjectByName(&shieldNodeStr.data);
       if (shieldNode)
       {
     NiPoint3 shieldPos = shieldNode->m_worldTransform.pos;
 
      float dx = weaponPos.x - shieldPos.x;
       float dy = weaponPos.y - shieldPos.y;
        float dz = weaponPos.z - shieldPos.z;
        return sqrt(dx*dx + dy*dy + dz*dz);
  }
   }
   }
    }
    }
        }
   
 // Fallback to geometry tracker (for equipped weapons)
       WeaponGeometryTracker* tracker = WeaponGeometryTracker::GetSingleton();
        if (!tracker)
   return 9999.0f;
  
        const BladeGeometry& rightGeom = tracker->GetBladeGeometry(false);
        
      if (!rightGeom.isValid)
          return 9999.0f;
      
       const BladeGeometry& leftGeom = tracker->GetBladeGeometry(true);
 
       if (!leftGeom.isValid)
  return 9999.0f;
  
  NiPoint3 shieldCenter;
        shieldCenter.x = (leftGeom.basePosition.x + leftGeom.tipPosition.x) * 0.5f;
        shieldCenter.y = (leftGeom.basePosition.y + leftGeom.tipPosition.y) * 0.5f;
   shieldCenter.z = (leftGeom.basePosition.z + leftGeom.tipPosition.z) * 0.5f;
     
    NiPoint3 weaponTip = rightGeom.tipPosition;
    
   float dx = weaponTip.x - shieldCenter.x;
    float dy = weaponTip.y - shieldCenter.y;
   float dz = weaponTip.z - shieldCenter.z;
        
      return sqrt(dx*dx + dy*dy + dz*dz);
    }
    void VRInputHandler::ClearAllState()
    {
   _MESSAGE("VRInputHandler: Clearing all tracking state");
        
        m_higgsCollisionActive = false;
        m_wasHiggsCollisionActive = false;
    m_timeSinceLastCollision = 0.0f;
        
        m_shieldCollisionActive = false;
        m_wasShieldCollisionActive = false;
        m_timeSinceLastShieldCollision = 0.0f;
        
        m_pendingReequip = false;
        m_pendingReequipIsLeft = false;
    m_pendingReequipTimer = 0.0f;
 
        m_pendingReequipRight = false;
  m_pendingReequipRightTimer = 0.0f;
      
        m_leftHandCooldownTimer = 0.0f;
        m_rightHandCooldownTimer = 0.0f;
    m_leftHandOnCooldown = false;
     m_rightHandOnCooldown = false;
        
        m_autoEquipPendingLeft = false;
        m_autoEquipPendingRight = false;
        m_autoEquipTimerLeft = 0.0f;
m_autoEquipTimerRight = 0.0f;
   m_autoEquipWeaponLeft = nullptr;
        m_autoEquipWeaponRight = nullptr;
        
        // Clear combat tracking
        m_isInCombat = false;
      m_closestTargetDistance = 9999.0f;
        m_closestTargetHandle = 0;
        m_closeCombatMode = false;
        m_combatLogTimer = 0.0f;
   
        // Clear shield bash tracking completely on death/load
        m_shieldBashCount = 0;
        m_shieldBashWindowTimer = 0.0f;
        m_shieldBashLockoutActive = false;
        m_shieldBashLockoutTimer = 0.0f;
    
      EquipManager::GetSingleton()->ClearDroppedWeaponRef(true);
        EquipManager::GetSingleton()->ClearDroppedWeaponRef(false);
        EquipManager::GetSingleton()->ClearPendingReequip(true);
        EquipManager::GetSingleton()->ClearPendingReequip(false);
        EquipManager::GetSingleton()->ClearCachedWeaponFormID(true);
EquipManager::GetSingleton()->ClearCachedWeaponFormID(false);
        
   _MESSAGE("VRInputHandler: All tracking state cleared");
    }

    float VRInputHandler::GetGrabbedWeaponVelocity(bool isLeftGameHand) const
    {
        WeaponGeometryTracker* tracker = WeaponGeometryTracker::GetSingleton();
        if (!tracker)
       return 0.0f;
        
     const BladeGeometry& geom = tracker->GetBladeGeometry(isLeftGameHand);
        if (!geom.isValid)
return 0.0f;
        
        float vx = geom.tipVelocity.x;
      float vy = geom.tipVelocity.y;
     float vz = geom.tipVelocity.z;
    
     return sqrt(vx*vx + vy*vy + vz*vz);
    }

    // ============================================
    // Convenience Functions
    // ============================================

    void InitializeVRInput()
    {
        VRInputHandler::GetSingleton()->Initialize();
 }
}
