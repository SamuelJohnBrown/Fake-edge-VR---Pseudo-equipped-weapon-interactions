#include "WeaponGeometry.h"
#include "Engine.h"
#include "EquipManager.h"
#include "VRInputHandler.h"
#include "skse64/GameRTTI.h"
#include "skse64/NiNodes.h"
#include <cmath>
#include <cfloat>

namespace FalseEdgeVR
{
    // ============================================
    // WeaponGeometryTracker Implementation
    // ============================================

 WeaponGeometryTracker* WeaponGeometryTracker::GetSingleton()
    {
        static WeaponGeometryTracker instance;
        return &instance;
    }

    void WeaponGeometryTracker::Initialize()
    {
  if (m_initialized)
        return;

        LOG("WeaponGeometryTracker: Initializing...");
        
m_geometryState.leftHand.Clear();
        m_geometryState.rightHand.Clear();
        m_lastCollision.Clear();
      m_bladesInContact = false;
    m_wasInContact = false;
        m_framesSinceEquipChange = 0;  // Track frames since equipment changed
        
        // Load thresholds from config
        m_collisionThreshold = bladeCollisionThreshold;
      m_imminentThreshold = bladeImminentThreshold;
  
_MESSAGE("WeaponGeometryTracker: Collision threshold: %.2f, Imminent threshold: %.2f", 
    m_collisionThreshold, m_imminentThreshold);
   
        m_initialized = true;
LOG("WeaponGeometryTracker: Initialized successfully");
    }

    void WeaponGeometryTracker::Update(float deltaTime)
    {
   static int updateCount = 0;
 static bool loggedOnce = false;
        
        if (!m_initialized)
      return;

     updateCount++;
 
 // Log once to confirm update is being called
        if (!loggedOnce)
   {
       _MESSAGE("WeaponGeometryTracker::Update - First update call!");
loggedOnce = true;
        }

        PlayerCharacter* player = *g_thePlayer;
        if (!player || !player->loadedState)
    return;

      const PlayerEquipState& equipState = EquipManager::GetSingleton()->GetEquipState();
      
        // DIRECT check for shields - more reliable than EquipManager state
   TESForm* leftEquipped = player->GetEquippedObject(true);
   TESForm* rightEquipped = player->GetEquippedObject(false);
        bool leftIsShield = leftEquipped && EquipManager::IsShield(leftEquipped);
 bool rightIsShield = rightEquipped && EquipManager::IsShield(rightEquipped);
        
        // Check for equipment changes - reset grace period if weapons changed
        // Note: Ignore shields - they are handled by ShieldCollisionTracker
   UInt32 currentLeftFormID = (leftEquipped && !leftIsShield) ? leftEquipped->formID : 0;
        UInt32 currentRightFormID = (rightEquipped && !rightIsShield) ? rightEquipped->formID : 0;
    
     if (currentLeftFormID != m_lastLeftWeaponFormID || currentRightFormID != m_lastRightWeaponFormID)
   {
        _MESSAGE("WeaponGeometryTracker: Equipment changed! Left: %08X->%08X, Right: %08X->%08X",
    m_lastLeftWeaponFormID, currentLeftFormID,
    m_lastRightWeaponFormID, currentRightFormID);
     _MESSAGE("WeaponGeometryTracker: Starting %d frame grace period before collision detection", EQUIP_GRACE_FRAMES);
            
            m_lastLeftWeaponFormID = currentLeftFormID;
   m_lastRightWeaponFormID = currentRightFormID;
m_framesSinceEquipChange = 0;
          
    // Clear geometry to force fresh calculation
         m_geometryState.leftHand.Clear();
     m_geometryState.rightHand.Clear();
            m_bladesInContact = false;
  m_wasInContact = false;
    m_collisionImminent = false;
            m_wasImminent = false;
 }
 
        // Increment frame counter
   m_framesSinceEquipChange++;
        
        // Check if left hand has HIGGS-grabbed weapon (from our collision avoidance)
        bool leftHandHiggsGrabbed = false;
        TESObjectREFR* higgsHeldLeft = nullptr;
        
        if (EquipManager::GetSingleton()->HasPendingReequip(true))
        {
    // Get the dropped weapon reference we created
      higgsHeldLeft = EquipManager::GetSingleton()->GetDroppedWeaponRef(true);
    if (higgsHeldLeft)
        {
  // Check if HIGGS is actually holding it
 if (higgsInterface)
    {
   TESObjectREFR* currentlyHeld = higgsInterface->GetGrabbedObject(true);
     if (currentlyHeld == higgsHeldLeft)
   {
   leftHandHiggsGrabbed = true;
      }
       }
        }
   }
        
        // Debug logging for HIGGS state
        static bool loggedHiggsState = false;
        if (EquipManager::GetSingleton()->HasPendingReequip(true) && !loggedHiggsState)
    {
            _MESSAGE("WeaponGeometry: Pending reequip - DroppedRef: %p, HIGGS holding: %s", 
  higgsHeldLeft, leftHandHiggsGrabbed ? "YES" : "NO");
       loggedHiggsState = true;
        }
   
        // Update left hand geometry - skip if shield (ShieldCollisionTracker handles that)
        if (leftEquipped && !leftIsShield)
      {
      // Normal equipped weapon
            UpdateHandGeometry(true, deltaTime);
        }
        else if (leftHandHiggsGrabbed && higgsHeldLeft)
        {
   // HIGGS-grabbed weapon - update geometry from the grabbed object
        UpdateHiggsGrabbedGeometry(true, higgsHeldLeft, deltaTime);
        }
        else
        {
  m_geometryState.leftHand.Clear();
        }
 
  // Update right hand if weapon equipped - skip if shield
        if (rightEquipped && !rightIsShield)
        {
      UpdateHandGeometry(false, deltaTime);
      }
        else
        {
       m_geometryState.rightHand.Clear();
        }
        
        // Check for blade collision if both hands have valid weapons
    // Also verify geometry is actually reasonable (not at origin)
        bool leftGeomValid = m_geometryState.leftHand.isValid && 
      (m_geometryState.leftHand.bladeLength > 1.0f) &&
       (fabs(m_geometryState.leftHand.basePosition.x) > 0.1f ||
         fabs(m_geometryState.leftHand.basePosition.y) > 0.1f ||
   fabs(m_geometryState.leftHand.basePosition.z) > 0.1f);
     
  bool rightGeomValid = m_geometryState.rightHand.isValid && 
      (m_geometryState.rightHand.bladeLength > 1.0f) &&
    (fabs(m_geometryState.rightHand.basePosition.x) > 0.1f ||
  fabs(m_geometryState.rightHand.basePosition.y) > 0.1f ||
   fabs(m_geometryState.rightHand.basePosition.z) > 0.1f);

        if (leftGeomValid && rightGeomValid)
  {
            // Log once when both weapons are valid (including HIGGS grabbed)
        static bool loggedBothValid = false;
         static bool loggedHiggsTracking = false;
            
     if (!loggedBothValid && !leftHandHiggsGrabbed)
  {
         _MESSAGE("WeaponGeometryTracker: Both hands have valid geometry!");
     loggedBothValid = true;
   }
  
          if (leftHandHiggsGrabbed && !loggedHiggsTracking)
   {
       _MESSAGE("WeaponGeometryTracker: Tracking HIGGS-grabbed weapon + equipped weapon");
   _MESSAGE("  Left (HIGGS):  Base(%.1f, %.1f, %.1f) Tip(%.1f, %.1f, %.1f)",
         m_geometryState.leftHand.basePosition.x,
        m_geometryState.leftHand.basePosition.y,
    m_geometryState.leftHand.basePosition.z,
        m_geometryState.leftHand.tipPosition.x,
    m_geometryState.leftHand.tipPosition.y,
              m_geometryState.leftHand.tipPosition.z);
        _MESSAGE("Right (Equipped): Base(%.1f, %.1f, %.1f) Tip(%.1f, %.1f, %.1f)",
           m_geometryState.rightHand.basePosition.x,
       m_geometryState.rightHand.basePosition.y,
   m_geometryState.rightHand.basePosition.z,
  m_geometryState.rightHand.tipPosition.x,
      m_geometryState.rightHand.tipPosition.y,
 m_geometryState.rightHand.tipPosition.z);
    loggedHiggsTracking = true;
            }
     
   m_wasInContact = m_bladesInContact;
          m_wasImminent = m_collisionImminent;
    
     BladeCollisionResult collision;
 bool detected = CheckBladeCollision(collision);
   
      // Log distance periodically when HIGGS grabbed
            static int distanceLogCounter = 0;
   if (leftHandHiggsGrabbed)
            {
      distanceLogCounter++;
 if (distanceLogCounter % 100 == 1)  // Log every 100 frames
    {
        _MESSAGE("HIGGS Blade Distance Check: %.2f (touch threshold: %.2f, imminent: %.2f)",
         collision.closestDistance, m_collisionThreshold, m_imminentThreshold);
    }
    }
    // Update collision state
            m_bladesInContact = collision.isColliding;
  m_collisionImminent = collision.isImminent;
 
  if (m_bladesInContact)
       {
    m_lastCollision = collision;

                // Fire collision callback if blades just came into contact
    if (!m_wasInContact && m_collisionCallback)
         {
m_collisionCallback(collision);
                }
    
                // Log collision event (only on initial contact)
         if (!m_wasInContact)
        {
        _MESSAGE("WeaponGeometry: === BLADES TOUCHING === (HIGGS grabbed: %s)", 
  leftHandHiggsGrabbed ? "YES" : "NO");
 _MESSAGE("  Collision Point: (%.2f, %.2f, %.2f)",
    collision.collisionPoint.x,
       collision.collisionPoint.y,
  collision.collisionPoint.z);
   _MESSAGE("  Distance: %.2f", collision.closestDistance);
 }

        // Check for X-POSE every frame while blades are touching
  CheckXPose(m_geometryState.leftHand, m_geometryState.rightHand);
  }
     else if (m_collisionImminent)
            {
                m_lastCollision = collision;
  
     // Fire imminent callback if collision just became imminent
        if (!m_wasImminent && !m_wasInContact && m_imminentCallback)
       {
   m_imminentCallback(collision);
   }
    
    // Log imminent collision and trigger unequip (only when first detected, and only if not already HIGGS grabbed)
       // Also check cooldown - don't trigger if we just re-equipped (prevents rapid cycling when blades slide)
       // EXCEPTION: Backup threshold bypasses cooldown as a safety net
        // IMPORTANT: Don't trigger during grace period after equipment change
    bool leftHandOnCooldown = VRInputHandler::GetSingleton()->IsHandOnCooldown(true);
     bool withinBackupOnly = (collision.closestDistance <= bladeImminentThresholdBackup) && 
           (collision.closestDistance > m_imminentThreshold);
        bool inGracePeriod = (m_framesSinceEquipChange < EQUIP_GRACE_FRAMES);
      
        if (inGracePeriod)
   {
     // During grace period, don't trigger - just log once
         static bool loggedGracePeriod = false;
            if (!loggedGracePeriod)
        {
        _MESSAGE("WeaponGeometryTracker: In grace period (%d/%d frames) - collision detection disabled",
        m_framesSinceEquipChange, EQUIP_GRACE_FRAMES);
      loggedGracePeriod = true;
 }
        }
        else if (!m_wasImminent && !m_wasInContact && !leftHandHiggsGrabbed && 
 (!leftHandOnCooldown || withinBackupOnly))  // Backup threshold bypasses cooldown
    {
 // Reset grace period log flag
    static bool loggedGracePeriod = false;
        loggedGracePeriod = false;
         
        // Check if we're in close combat mode - if so, don't trigger unequip
       if (VRInputHandler::GetSingleton()->IsInCloseCombatMode())
      {
     static bool loggedCloseCombatSkip = false;
  if (!loggedCloseCombatSkip)
                 {
_MESSAGE("WeaponGeometry: Collision imminent but CLOSE COMBAT MODE active - skipping unequip");
          loggedCloseCombatSkip = true;
                  }
  }
     else
     {
         static bool loggedCloseCombatSkip = false;
     loggedCloseCombatSkip = false;
    
_MESSAGE("WeaponGeometry: COLLISION IMMINENT!%s", withinBackupOnly ? " (BACKUP THRESHOLD - bypassing cooldown)" : "");
        _MESSAGE("  Distance: %.2f, Time to collision: %.3f sec",
collision.closestDistance,
  collision.timeToCollision);
 
      // Unequip the left GAME hand weapon and have HIGGS grab it
  // Note: ForceUnequipAndGrab takes GAME HAND, not VR controller
  _MESSAGE("WeaponGeometry: Triggering game LEFT hand unequip + HIGGS grab to prevent collision!");
   EquipManager::GetSingleton()->ForceUnequipAndGrab(true);  // true = left GAME hand
     }
   }
 else if (!m_wasImminent && !m_wasInContact && leftHandOnCooldown && !withinBackupOnly && !inGracePeriod)
 {
        // Log that we skipped due to cooldown (only once per cooldown period)
     static bool loggedCooldownSkip = false;
      if (!loggedCooldownSkip)
           {
   _MESSAGE("WeaponGeometry: Collision imminent but LEFT hand on cooldown - allowing blades to slide");
            loggedCooldownSkip = true;
          }
    }
   else if (!leftHandOnCooldown && !inGracePeriod)
    {
 // Reset the logged flag when cooldown allows triggering again
static bool loggedCooldownSkip = false;
     loggedCooldownSkip = false;
   }
        }
else
      {
    // Blades no longer colliding or imminent (geometry-based detection)
     if (m_wasInContact)
{
    _MESSAGE("WeaponGeometry: === BLADES NO LONGER TOUCHING === (HIGGS grabbed: %s)",
  leftHandHiggsGrabbed ? "YES" : "NO");
     _MESSAGE("  Current distance: %.2f (threshold: %.2f)", 
         collision.closestDistance, m_collisionThreshold);
    
    // End X-pose and stop blocking when blades separate
  if (m_inXPose)
         {
_MESSAGE("WeaponGeometry: *** X-POSE ENDED *** (blades separated)");
      m_inXPose = false;
}

    // Always stop blocking when blades separate (if player is blocking)
        if (IsBlocking())
  {
            _MESSAGE("WeaponGeometry: Stopping block (blades separated)");
            StopBlocking();
        }
       }
      
 m_lastCollision.Clear();
        }
     }
else
   {
            // Geometry not valid for both hands
    if (m_geometryState.leftHand.isValid || m_geometryState.rightHand.isValid)
        {
         // One hand has geometry but validation failed
       static bool loggedInvalidGeom = false;
          if (!loggedInvalidGeom && (m_geometryState.leftHand.isValid && !leftGeomValid))
  {
  _MESSAGE("WeaponGeometryTracker: Left hand geometry invalid - bladeLength: %.2f, pos: (%.2f, %.2f, %.2f)",
          m_geometryState.leftHand.bladeLength,
       m_geometryState.leftHand.basePosition.x,
            m_geometryState.leftHand.basePosition.y,
         m_geometryState.leftHand.basePosition.z);
      loggedInvalidGeom = true;
     }
           if (!loggedInvalidGeom && (m_geometryState.rightHand.isValid && !rightGeomValid))
     {
       _MESSAGE("WeaponGeometryTracker: Right hand geometry invalid - bladeLength: %.2f, pos: (%.2f, %.2f, %.2f)",
          m_geometryState.rightHand.bladeLength,
 m_geometryState.rightHand.basePosition.x,
       m_geometryState.rightHand.basePosition.y,
     m_geometryState.rightHand.basePosition.z);
         loggedInvalidGeom = true;
 }
     }
    
  m_bladesInContact = false;
  m_collisionImminent = false;
 m_wasInContact = false;
  m_wasImminent = false;
        }
  }

    // Update geometry for a HIGGS-grabbed weapon
    void WeaponGeometryTracker::UpdateHiggsGrabbedGeometry(bool isLeftHand, TESObjectREFR* grabbedRef, float deltaTime)
    {
        BladeGeometry& geometry = isLeftHand ? m_geometryState.leftHand : m_geometryState.rightHand;

        // Store previous positions for velocity calculation
        geometry.prevTipPosition = geometry.tipPosition;
        geometry.prevBasePosition = geometry.basePosition;
 
        if (!grabbedRef)
        {
   static bool loggedNoRef = false;
      if (!loggedNoRef)
          {
        _MESSAGE("UpdateHiggsGrabbedGeometry: No grabbed ref!");
            loggedNoRef = true;
        }
        geometry.isValid = false;
     return;
        }

        // Get the 3D node of the grabbed object
        NiNode* objectNode = grabbedRef->GetNiNode();
     if (!objectNode)
        {
    static bool loggedNoNode = false;
       if (!loggedNoNode)
 {
        _MESSAGE("UpdateHiggsGrabbedGeometry: No NiNode for grabbed object!");
           loggedNoNode = true;
          }
    geometry.isValid = false;
        return;
        }

        // Get weapon info from the base form
        TESForm* baseForm = grabbedRef->baseForm;
        TESObjectWEAP* weapon = DYNAMIC_CAST(baseForm, TESForm, TESObjectWEAP);
     
        if (!weapon)
        {
     static bool loggedNoWeapon = false;
            if (!loggedNoWeapon)
            {
                _MESSAGE("UpdateHiggsGrabbedGeometry: Base form is not a weapon!");
        loggedNoWeapon = true;
  }
            geometry.isValid = false;
       return;
        }

  // Calculate blade positions from the grabbed object's transform
        float reach = weapon->gameData.reach;
   float bladeLength = reach * 70.0f;
   
        // Base position is the object's world position
        geometry.basePosition = objectNode->m_worldTransform.pos;
 
        // Get blade direction from object's rotation
        NiMatrix33& rot = objectNode->m_worldTransform.rot;
        NiPoint3 bladeDirection(
            rot.data[0][1],
  rot.data[1][1],
     rot.data[2][1]
        );
     
      float dirLength = sqrt(bladeDirection.x * bladeDirection.x +
            bladeDirection.y * bladeDirection.y +
            bladeDirection.z * bladeDirection.z);
 if (dirLength > 0.0001f)
        {
   bladeDirection.x /= dirLength;
    bladeDirection.y /= dirLength;
         bladeDirection.z /= dirLength;
        }

        // Tip position
        geometry.tipPosition.x = geometry.basePosition.x + bladeDirection.x * bladeLength;
        geometry.tipPosition.y = geometry.basePosition.y + bladeDirection.y * bladeLength;
        geometry.tipPosition.z = geometry.basePosition.z + bladeDirection.z * bladeLength;
    
   // Calculate blade length
        NiPoint3 bladeVector;
        bladeVector.x = geometry.tipPosition.x - geometry.basePosition.x;
        bladeVector.y = geometry.tipPosition.y - geometry.basePosition.y;
        bladeVector.z = geometry.tipPosition.z - geometry.basePosition.z;
        geometry.bladeLength = sqrt(bladeVector.x * bladeVector.x + 
bladeVector.y * bladeVector.y + 
          bladeVector.z * bladeVector.z);
  
        // Calculate velocities (units per second)
 if (deltaTime > 0.0f && (geometry.prevTipPosition.x != 0.0f || 
            geometry.prevTipPosition.y != 0.0f || 
            geometry.prevTipPosition.z != 0.0f))
        {
          geometry.tipVelocity.x = (geometry.tipPosition.x - geometry.prevTipPosition.x) / deltaTime;
    geometry.tipVelocity.y = (geometry.tipPosition.y - geometry.prevTipPosition.y) / deltaTime;
        geometry.tipVelocity.z = (geometry.tipPosition.z - geometry.prevTipPosition.z) / deltaTime;
        
            geometry.baseVelocity.x = (geometry.basePosition.x - geometry.prevBasePosition.x) / deltaTime;
   geometry.baseVelocity.y = (geometry.basePosition.y - geometry.prevBasePosition.y) / deltaTime;
         geometry.baseVelocity.z = (geometry.basePosition.z - geometry.prevBasePosition.z) / deltaTime;
    }
      
        geometry.isValid = true;
        
        // Log periodically to confirm tracking is working
        static int logCounter = 0;
logCounter++;
        if (logCounter % 500 == 1)  // Log every 500 frames
        {
            _MESSAGE("HIGGS Grabbed Geometry Update - Base(%.1f, %.1f, %.1f) Tip(%.1f, %.1f, %.1f) Length: %.1f",
       geometry.basePosition.x, geometry.basePosition.y, geometry.basePosition.z,
     geometry.tipPosition.x, geometry.tipPosition.y, geometry.tipPosition.z,
     geometry.bladeLength);
        }
    }

    void WeaponGeometryTracker::UpdateHandGeometry(bool isLeftHand, float deltaTime)
    {
        BladeGeometry& geometry = isLeftHand ? m_geometryState.leftHand : m_geometryState.rightHand;

        // Store previous positions for velocity calculation
        geometry.prevTipPosition = geometry.tipPosition;
        geometry.prevBasePosition = geometry.basePosition;
        
        // Get the weapon node
        NiAVObject* weaponNode = GetWeaponNode(isLeftHand);
        if (!weaponNode)
        {
    static bool loggedLeftFail = false;
      static bool loggedRightFail = false;
if (isLeftHand && !loggedLeftFail)
            {
    _MESSAGE("WeaponGeometryTracker: Failed to get LEFT weapon node!");
         loggedLeftFail = true;
         }
          else if (!isLeftHand && !loggedRightFail)
      {
            _MESSAGE("WeaponGeometryTracker: Failed to get RIGHT weapon node!");
              loggedRightFail = true;
 }
  geometry.isValid = false;
            return;
        }
        
        // Get the equipped weapon form
        PlayerCharacter* player = *g_thePlayer;
 TESForm* equippedForm = player->GetEquippedObject(isLeftHand);
      TESObjectWEAP* weapon = DYNAMIC_CAST(equippedForm, TESForm, TESObjectWEAP);
      
        if (!weapon)
    {
      static bool loggedLeftNoWeap = false;
            static bool loggedRightNoWeap = false;
   if (isLeftHand && !loggedLeftNoWeap)
     {
 _MESSAGE("WeaponGeometryTracker: LEFT hand - no weapon form (FormID: %08X, Type: %d)", 
        equippedForm ? equippedForm->formID : 0,
        equippedForm ? equippedForm->formType : -1);
  loggedLeftNoWeap = true;
          }
      else if (!isLeftHand && !loggedRightNoWeap)
     {
                _MESSAGE("WeaponGeometryTracker: RIGHT hand - no weapon form (FormID: %08X, Type: %d)", 
    equippedForm ? equippedForm->formID : 0,
         equippedForm ? equippedForm->formType : -1);
     loggedRightNoWeap = true;
     }
            geometry.isValid = false;
  return;
        }
 
        // Log success once per hand
        static bool loggedLeftSuccess = false;
        static bool loggedRightSuccess = false;
        if (isLeftHand && !loggedLeftSuccess)
        {
_MESSAGE("WeaponGeometryTracker: LEFT hand - Got weapon node and form! Reach: %.2f", weapon->gameData.reach);
 loggedLeftSuccess = true;
        }
      else if (!isLeftHand && !loggedRightSuccess)
    {
            _MESSAGE("WeaponGeometryTracker: RIGHT hand - Got weapon node and form! Reach: %.2f", weapon->gameData.reach);
  loggedRightSuccess = true;
  }
        
        // Calculate blade positions
        geometry.basePosition = CalculateBladeBase(weaponNode, isLeftHand);
        geometry.tipPosition = CalculateBladeTip(weaponNode, weapon, isLeftHand);
      
        // Calculate blade length
        NiPoint3 bladeVector;
 bladeVector.x = geometry.tipPosition.x - geometry.basePosition.x;
        bladeVector.y = geometry.tipPosition.y - geometry.basePosition.y;
      bladeVector.z = geometry.tipPosition.z - geometry.basePosition.z;
     geometry.bladeLength = sqrt(bladeVector.x * bladeVector.x + 
   bladeVector.y * bladeVector.y + 
         bladeVector.z * bladeVector.z);
  
        // Calculate velocities (units per second)
        if (deltaTime > 0.0f && (geometry.prevTipPosition.x != 0.0f || 
  geometry.prevTipPosition.y != 0.0f || 
     geometry.prevTipPosition.z != 0.0f))
  {
            geometry.tipVelocity.x = (geometry.tipPosition.x - geometry.prevTipPosition.x) / deltaTime;
            geometry.tipVelocity.y = (geometry.tipPosition.y - geometry.prevTipPosition.y) / deltaTime;
            geometry.tipVelocity.z = (geometry.tipPosition.z - geometry.prevTipPosition.z) / deltaTime;
        
            geometry.baseVelocity.x = (geometry.basePosition.x - geometry.prevBasePosition.x) / deltaTime;
  geometry.baseVelocity.y = (geometry.basePosition.y - geometry.prevBasePosition.y) / deltaTime;
         geometry.baseVelocity.z = (geometry.basePosition.z - geometry.prevBasePosition.z) / deltaTime;
        }
      
        geometry.isValid = true;
    }

    NiAVObject* WeaponGeometryTracker::GetWeaponNode(bool isLeftHand)
    {
        PlayerCharacter* player = *g_thePlayer;
        if (!player || !player->loadedState)
        {
        static bool loggedNoPlayer = false;
      if (!loggedNoPlayer)
            {
      _MESSAGE("WeaponGeometryTracker::GetWeaponNode - No player or loadedState!");
         loggedNoPlayer = true;
 }
        return nullptr;
        }

        NiNode* rootNode = player->GetNiRootNode(0); // First person root
        if (!rootNode)
{
        rootNode = player->GetNiRootNode(1); // Third person root
     }
        
    if (!rootNode)
   {
            static bool loggedNoRoot = false;
      if (!loggedNoRoot)
        {
 _MESSAGE("WeaponGeometryTracker::GetWeaponNode - No root node found!");
                loggedNoRoot = true;
            }
        return nullptr;
        }

        // Log available nodes once for debugging
        static bool loggedNodes = false;
  if (!loggedNodes)
        {
            _MESSAGE("WeaponGeometryTracker: Root node found: %s", rootNode->m_name ? rootNode->m_name : "unnamed");
     _MESSAGE("WeaponGeometryTracker: Searching for weapon offset nodes...");
  loggedNodes = true;
      }

        const char* nodeName = GetWeaponOffsetNodeName(isLeftHand);
      
        BSFixedString nodeNameStr(nodeName);
        NiAVObject* weaponNode = rootNode->GetObjectByName(&nodeNameStr.data);
        
        if (!weaponNode)
        {
        static bool loggedLeftNotFound = false;
         static bool loggedRightNotFound = false;
   if (isLeftHand && !loggedLeftNotFound)
            {
     _MESSAGE("WeaponGeometryTracker: Node '%s' NOT FOUND in skeleton!", nodeName);
    loggedLeftNotFound = true;
 }
            else if (!isLeftHand && !loggedRightNotFound)
      {
       _MESSAGE("WeaponGeometryTracker: Node '%s' NOT FOUND in skeleton!", nodeName);
   loggedRightNotFound = true;
            }
        }
        
        return weaponNode;
    }

    const char* WeaponGeometryTracker::GetWeaponOffsetNodeName(bool isLeftHand)
    {
        // In Skyrim VR:
        // Left hand weapon node is called "SHIELD" (even for weapons, not just shields!)
        // Right hand weapon node is called "WEAPON"
        if (isLeftHand)
        {
return "SHIELD";
        }
        else
        {
            return "WEAPON";
     }
    }

    NiPoint3 WeaponGeometryTracker::CalculateBladeBase(NiAVObject* weaponNode, bool isLeftHand)
    {
        if (!weaponNode)
        return NiPoint3(0, 0, 0);

        return NiPoint3(
     weaponNode->m_worldTransform.pos.x,
 weaponNode->m_worldTransform.pos.y,
         weaponNode->m_worldTransform.pos.z
 );
    }

    NiPoint3 WeaponGeometryTracker::CalculateBladeTip(NiAVObject* weaponNode, TESObjectWEAP* weapon, bool isLeftHand)
    {
        if (!weaponNode || !weapon)
         return NiPoint3(0, 0, 0);

        float reach = weapon->gameData.reach;
        float bladeLength = reach * 70.0f;
        
        NiMatrix33& rot = weaponNode->m_worldTransform.rot;
        
        NiPoint3 bladeDirection(
            rot.data[0][1],
         rot.data[1][1],
            rot.data[2][1]
);
     
        float dirLength = sqrt(bladeDirection.x * bladeDirection.x +
  bladeDirection.y * bladeDirection.y +
         bladeDirection.z * bladeDirection.z);
        if (dirLength > 0.0001f)
        {
  bladeDirection.x /= dirLength;
     bladeDirection.y /= dirLength;
        bladeDirection.z /= dirLength;
    }
        
        NiPoint3 basePos = CalculateBladeBase(weaponNode, isLeftHand);

        return NiPoint3(
            basePos.x + bladeDirection.x * bladeLength,
            basePos.y + bladeDirection.y * bladeLength,
          basePos.z + bladeDirection.z * bladeLength
  );
    }

    const BladeGeometry& WeaponGeometryTracker::GetBladeGeometry(bool isLeftHand) const
    {
        return isLeftHand ? m_geometryState.leftHand : m_geometryState.rightHand;
    }

    void WeaponGeometryTracker::LogGeometryState()
    {
        if (m_geometryState.leftHand.isValid)
        {
    LOG("WeaponGeometry: Left Hand - Base(%.2f, %.2f, %.2f) Tip(%.2f, %.2f, %.2f) Length: %.2f",
       m_geometryState.leftHand.basePosition.x,
    m_geometryState.leftHand.basePosition.y,
             m_geometryState.leftHand.basePosition.z,
      m_geometryState.leftHand.tipPosition.x,
      m_geometryState.leftHand.tipPosition.y,
  m_geometryState.leftHand.tipPosition.z,
         m_geometryState.leftHand.bladeLength);
        }
        
        if (m_geometryState.rightHand.isValid)
        {
  LOG("WeaponGeometry: Right Hand - Base(%.2f, %.2f, %.2f) Tip(%.2f, %.2f, %.2f) Length: %.2f",
    m_geometryState.rightHand.basePosition.x,
          m_geometryState.rightHand.basePosition.y,
    m_geometryState.rightHand.basePosition.z,
         m_geometryState.rightHand.tipPosition.x,
  m_geometryState.rightHand.tipPosition.y,
           m_geometryState.rightHand.tipPosition.z,
           m_geometryState.rightHand.bladeLength);
        }
    }

    // ============================================
    // Blade Collision Detection
    // ============================================

    bool WeaponGeometryTracker::CheckBladeCollision(BladeCollisionResult& outResult)
    {
     outResult.Clear();
        
        if (!m_geometryState.leftHand.isValid || !m_geometryState.rightHand.isValid)
  return false;
     
        const NiPoint3& leftBase = m_geometryState.leftHand.basePosition;
        const NiPoint3& leftTip = m_geometryState.leftHand.tipPosition;
  const NiPoint3& rightBase = m_geometryState.rightHand.basePosition;
        const NiPoint3& rightTip = m_geometryState.rightHand.tipPosition;
        
        float leftParam, rightParam;
        NiPoint3 closestLeft, closestRight;
        
        float distance = ClosestDistanceBetweenSegments(
            leftBase, leftTip,
            rightBase, rightTip,
     leftParam, rightParam,
         closestLeft, closestRight
  );
        
        outResult.closestDistance = distance;
        outResult.leftBladeParameter = leftParam;
    outResult.rightBladeParameter = rightParam;
        outResult.leftBladeContactPoint = closestLeft;
    outResult.rightBladeContactPoint = closestRight;
 
        outResult.collisionPoint.x = (closestLeft.x + closestRight.x) * 0.5f;
        outResult.collisionPoint.y = (closestLeft.y + closestRight.y) * 0.5f;
 outResult.collisionPoint.z = (closestLeft.z + closestRight.z) * 0.5f;
        
        // Calculate velocities at closest points
        NiPoint3 leftVel, rightVel;
        
  leftVel.x = m_geometryState.leftHand.baseVelocity.x + 
      leftParam * (m_geometryState.leftHand.tipVelocity.x - m_geometryState.leftHand.baseVelocity.x);
        leftVel.y = m_geometryState.leftHand.baseVelocity.y + 
 leftParam * (m_geometryState.leftHand.tipVelocity.y - m_geometryState.leftHand.baseVelocity.y);
        leftVel.z = m_geometryState.leftHand.baseVelocity.z + 
     leftParam * (m_geometryState.leftHand.tipVelocity.z - m_geometryState.leftHand.baseVelocity.z);
        
        rightVel.x = m_geometryState.rightHand.baseVelocity.x + 
  rightParam * (m_geometryState.rightHand.tipVelocity.x - m_geometryState.rightHand.baseVelocity.x);
        rightVel.y = m_geometryState.rightHand.baseVelocity.y + 
    rightParam * (m_geometryState.rightHand.tipVelocity.y - m_geometryState.rightHand.baseVelocity.y);
        rightVel.z = m_geometryState.rightHand.baseVelocity.z + 
            rightParam * (m_geometryState.rightHand.tipVelocity.z - m_geometryState.rightHand.baseVelocity.z);
        
      NiPoint3 relVel;
   relVel.x = leftVel.x - rightVel.x;
        relVel.y = leftVel.y - rightVel.y;
        relVel.z = leftVel.z - rightVel.z;
   
        outResult.relativeVelocity = sqrt(relVel.x * relVel.x + relVel.y * relVel.y + relVel.z * relVel.z);
        
        // Calculate closing velocity (positive = approaching, negative = separating)
      // Direction from left closest point to right closest point
   NiPoint3 separationDir;
        separationDir.x = closestRight.x - closestLeft.x;
        separationDir.y = closestRight.y - closestLeft.y;
   separationDir.z = closestRight.z - closestLeft.z;
   
        float sepLength = sqrt(separationDir.x * separationDir.x + 
               separationDir.y * separationDir.y + 
             separationDir.z * separationDir.z);
  if (sepLength > 0.0001f)
        {
            separationDir.x /= sepLength;
            separationDir.y /= sepLength;
    separationDir.z /= sepLength;
  }
        
        // Closing velocity is the component of relative velocity along separation direction
        float closingVelocity = Dot(relVel, separationDir);
        
        // Estimate time to collision
        outResult.timeToCollision = EstimateTimeToCollision(distance, closingVelocity);

        // Check collision states
      outResult.isColliding = (distance <= m_collisionThreshold);
  
  // Imminent collision detection - trigger if:
        // 1. Within primary distance threshold AND approaching with significant velocity, OR
  // 2. Within backup distance threshold AND approaching with significant velocity, OR
        // 3. Time to collision is very short AND within a reasonable distance (fast swings!)
        //
        // IMPORTANT: We require a minimum closing velocity to avoid false positives from hand tremor/jitter
        // A closing velocity of ~50 units/sec means blades are actually moving toward each other intentionally
      const float MIN_CLOSING_VELOCITY = 50.0f;  // Minimum velocity to consider "approaching"
        
        bool withinPrimaryThreshold = (distance <= m_imminentThreshold) && (closingVelocity >= MIN_CLOSING_VELOCITY);
        bool withinBackupThreshold = (distance <= bladeImminentThresholdBackup) && (closingVelocity >= MIN_CLOSING_VELOCITY);
        
 // Fast approach only triggers if BOTH time is short AND distance is within backup threshold
        // This prevents false positives at large distances
      bool fastApproaching = (outResult.timeToCollision > 0.0f) && 
     (outResult.timeToCollision < bladeTimeToCollisionThreshold) &&
   (distance <= bladeImminentThresholdBackup);
     
        outResult.isImminent = !outResult.isColliding && (withinPrimaryThreshold || withinBackupThreshold || fastApproaching);
  
        return outResult.isColliding || outResult.isImminent;
    }

    float WeaponGeometryTracker::EstimateTimeToCollision(float distance, float closingVelocity)
    {
        // If not approaching (velocity <= 0) or already colliding, return -1
        if (closingVelocity <= 0.0f || distance <= m_collisionThreshold)
          return -1.0f;
        
        // Time = distance / velocity
    float timeToCollision = (distance - m_collisionThreshold) / closingVelocity;
   
        // Cap at a reasonable maximum (e.g., 2 seconds)
  if (timeToCollision > 2.0f)
   return -1.0f;
      
        return timeToCollision;
    }

    float WeaponGeometryTracker::ClosestDistanceBetweenSegments(
  const NiPoint3& p1, const NiPoint3& q1,
    const NiPoint3& p2, const NiPoint3& q2,
     float& outParam1, float& outParam2,
        NiPoint3& outClosestPoint1, NiPoint3& outClosestPoint2)
    {
        NiPoint3 d1, d2, r;
   d1.x = q1.x - p1.x;
     d1.y = q1.y - p1.y;
        d1.z = q1.z - p1.z;
        
        d2.x = q2.x - p2.x;
        d2.y = q2.y - p2.y;
     d2.z = q2.z - p2.z;
        
        r.x = p1.x - p2.x;
        r.y = p1.y - p2.y;
        r.z = p1.z - p2.z;
        
        float a = Dot(d1, d1);
 float e = Dot(d2, d2);
 float f = Dot(d2, r);
        
        float s, t;
        
        if (a <= 0.0001f && e <= 0.0001f)
        {
    s = t = 0.0f;
         outClosestPoint1 = p1;
    outClosestPoint2 = p2;
}
        else if (a <= 0.0001f)
        {
s = 0.0f;
     t = Clamp(f / e, 0.0f, 1.0f);
        }
        else
        {
       float c = Dot(d1, r);
            if (e <= 0.0001f)
     {
          t = 0.0f;
     s = Clamp(-c / a, 0.0f, 1.0f);
       }
    else
            {
   float b = Dot(d1, d2);
                float denom = a * e - b * b;
    
                if (denom != 0.0f)
    {
        s = Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
     }
          else
           {
         s = 0.0f;
     }
    
         t = (b * s + f) / e;
 
          if (t < 0.0f)
{
         t = 0.0f;
         s = Clamp(-c / a, 0.0f, 1.0f);
                }
    else if (t > 1.0f)
                {
       t = 1.0f;
           s = Clamp((b - c) / a, 0.0f, 1.0f);
                }
    }
        }
        
        outParam1 = s;
        outParam2 = t;
        
        outClosestPoint1 = PointAlongSegment(p1, q1, s);
        outClosestPoint2 = PointAlongSegment(p2, q2, t);
        
        NiPoint3 diff;
        diff.x = outClosestPoint1.x - outClosestPoint2.x;
        diff.y = outClosestPoint1.y - outClosestPoint2.y;
        diff.z = outClosestPoint1.z - outClosestPoint2.z;
        
        return sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
    }

    float WeaponGeometryTracker::Dot(const NiPoint3& a, const NiPoint3& b)
    {
      return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float WeaponGeometryTracker::Clamp(float value, float min, float max)
    {
   if (value < min) return min;
      if (value > max) return max;
  return value;
    }

    NiPoint3 WeaponGeometryTracker::PointAlongSegment(const NiPoint3& start, const NiPoint3& end, float t)
    {
        NiPoint3 result;
 result.x = start.x + t * (end.x - start.x);
        result.y = start.y + t * (end.y - start.y);
        result.z = start.z + t * (end.z - start.z);
   return result;
    }

    void WeaponGeometryTracker::CheckXPose(const BladeGeometry& leftBlade, const BladeGeometry& rightBlade)
    {
     // Save previous state
        m_wasInXPose = m_inXPose;
  
        if (!leftBlade.isValid || !rightBlade.isValid)
      {
            m_inXPose = false;
            if (m_wasInXPose)
            {
  _MESSAGE("WeaponGeometry: *** X-POSE ENDED *** (blade geometry invalid)");
       }
  return;
        }

        // Get blade direction vectors (from base to tip)
        NiPoint3 leftDir;
        leftDir.x = leftBlade.tipPosition.x - leftBlade.basePosition.x;
        leftDir.y = leftBlade.tipPosition.y - leftBlade.basePosition.y;
        leftDir.z = leftBlade.tipPosition.z - leftBlade.basePosition.z;

        NiPoint3 rightDir;
        rightDir.x = rightBlade.tipPosition.x - rightBlade.basePosition.x;
        rightDir.y = rightBlade.tipPosition.y - rightBlade.basePosition.y;
        rightDir.z = rightBlade.tipPosition.z - rightBlade.basePosition.z;

     // Normalize the direction vectors
     float leftLen = sqrt(leftDir.x * leftDir.x + leftDir.y * leftDir.y + leftDir.z * leftDir.z);
        float rightLen = sqrt(rightDir.x * rightDir.x + rightDir.y * rightDir.y + rightDir.z * rightDir.z);

     if (leftLen < 0.001f || rightLen < 0.001f)
 {
            m_inXPose = false;
   if (m_wasInXPose)
    {
      _MESSAGE("WeaponGeometry: *** X-POSE ENDED *** (blade length too short)");
     }
      return;
   }

        leftDir.x /= leftLen;
  leftDir.y /= leftLen;
        leftDir.z /= leftLen;

        rightDir.x /= rightLen;
   rightDir.y /= rightLen;
    rightDir.z /= rightLen;

        // Get player forward direction (Y axis in Skyrim is forward)
        PlayerCharacter* player = *g_thePlayer;
        if (!player)
     {
        m_inXPose = false;
       if (m_wasInXPose)
  {
    _MESSAGE("WeaponGeometry: *** X-POSE ENDED *** (no player)");
     }
  return;
        }

      NiPoint3 playerForward;
        // Use player's rotation angle (rot.z is heading in radians)
        float heading = player->rot.z;
        playerForward.x = sin(heading);
        playerForward.y = cos(heading);
        playerForward.z = 0.0f;

        // Calculate dot product between blades (negative = crossing/X shape)
        float bladeDot = leftDir.x * rightDir.x + leftDir.y * rightDir.y + leftDir.z * rightDir.z;
    
      // Calculate angle between blades
        float bladeAngle = acos(Clamp(bladeDot, -1.0f, 1.0f)) * (180.0f / 3.14159f);

        // Calculate how much each blade is facing forward (dot with player forward)
   float leftForwardDot = leftDir.x * playerForward.x + leftDir.y * playerForward.y;
    float rightForwardDot = rightDir.x * playerForward.x + rightDir.y * playerForward.y;

        // Check if blades are pointing upward (positive Z component)
        bool leftPointingUp = leftDir.z > 0.3f;  // At least 30% upward
        bool rightPointingUp = rightDir.z > 0.3f;

        // X-pose criteria:
        // 1. Blades are crossing (angle between them is 30-150 degrees - not parallel, not same direction)
        // 2. Both blades are pointing somewhat upward
 // 3. Blades are roughly facing forward (not pointing backward)
        bool isCrossing = (bladeAngle > 30.0f && bladeAngle < 150.0f);
        bool bothPointingUp = leftPointingUp && rightPointingUp;
        bool facingForward = (leftForwardDot > -0.5f) && (rightForwardDot > -0.5f);

        m_inXPose = isCrossing && bothPointingUp && facingForward;

        // Log state changes
   if (m_inXPose && !m_wasInXPose)
   {
      _MESSAGE("WeaponGeometry: X-POSE CHECK:");
  _MESSAGE("  Left blade dir: (%.2f, %.2f, %.2f)", leftDir.x, leftDir.y, leftDir.z);
   _MESSAGE("  Right blade dir: (%.2f, %.2f, %.2f)", rightDir.x, rightDir.y, rightDir.z);
         _MESSAGE("  Blade angle: %.1f degrees (crossing: %s)", bladeAngle, isCrossing ? "YES" : "NO");
   _MESSAGE("  Left pointing up: %s (z=%.2f), Right pointing up: %s (z=%.2f)",
 leftPointingUp ? "YES" : "NO", leftDir.z,
      rightPointingUp ? "YES" : "NO", rightDir.z);
  _MESSAGE("  Facing forward: %s (leftDot=%.2f, rightDot=%.2f)",
 facingForward ? "YES" : "NO", leftForwardDot, rightForwardDot);
         _MESSAGE("WeaponGeometry: *** X-POSE DETECTED! *** Blades crossed facing forward!");
         
         // Start blocking when X-pose begins
         StartBlocking();
      }
   else if (!m_inXPose && m_wasInXPose)
        {
    _MESSAGE("WeaponGeometry: *** X-POSE ENDED ***");
  _MESSAGE("  Blade angle: %.1f degrees (crossing: %s)", bladeAngle, isCrossing ? "YES" : "NO");
       _MESSAGE("  Left pointing up: %s (z=%.2f), Right pointing up: %s (z=%.2f)",
     leftPointingUp ? "YES" : "NO", leftDir.z,
   rightPointingUp ? "YES" : "NO", rightDir.z);
            _MESSAGE("  Facing forward: %s (leftDot=%.2f, rightDot=%.2f)",
 facingForward ? "YES" : "NO", leftForwardDot, rightForwardDot);
         
         // Stop blocking when X-pose ends
         StopBlocking();
 }
    }

    // ============================================
    // Convenience Functions
    // ============================================

    void InitializeWeaponGeometryTracker()
  {
      WeaponGeometryTracker::GetSingleton()->Initialize();
    }

    void UpdateWeaponGeometry(float deltaTime)
    {
        WeaponGeometryTracker::GetSingleton()->Update(deltaTime);
    }
}
