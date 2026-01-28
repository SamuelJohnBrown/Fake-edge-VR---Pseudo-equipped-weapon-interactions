#pragma once

#include "Helper.h"
#include "skse64/PluginAPI.h"

namespace FalseEdgeVR
{
	extern SKSETrampolineInterface* g_trampolineInterface;
	extern HiggsPluginAPI::IHiggsInterface001* higgsInterface;
	extern vrikPluginApi::IVrikInterface001* vrikInterface;
	extern SkyrimVRESLPluginAPI::ISkyrimVRESLInterface001* skyrimVRESLInterface;
	extern SKSEVRInterface* g_vrInterface;
	extern PluginHandle g_pluginHandle;
	extern SKSETaskInterface* g_task;

	void StartMod();

	// ============================================
	// Left-Handed Mode Support
	// ============================================
	
	// Check if the game is in left-handed VR mode
	// In left-handed mode, VR controllers are inverted:
	//   - Left VR controller = Right game hand
	//   - Right VR controller = Left game hand
	bool IsLeftHandedMode();
	
	// Convert VR controller hand (from HIGGS callbacks) to game hand
	// HIGGS isLeft parameter refers to VR controller, not game hand
	// Returns: true = game left hand, false = game right hand
	bool VRControllerToGameHand(bool isLeftVRController);
	
	// Convert game hand to VR controller
	// Returns: true = left VR controller, false = right VR controller
	bool GameHandToVRController(bool isLeftGameHand);

	// ============================================
	// Spell Casting
	// ============================================
	
	// Cast a spell on the player (self-cast)
	// formId is the full FormID (e.g., 0x000AA026 for Skyrim.esm spell)
	void CastSpellOnPlayer(UInt32 formId);

	// ============================================
	// Blocking (X-Pose)
	// ============================================
	
	// Start blocking on the player (for X-pose dual wield block)
	void StartBlocking();
	
	// Stop blocking on the player
	void StopBlocking();
	
	// Check if player is currently blocking
	bool IsBlocking();
}