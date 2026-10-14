// State shared between "TR-MPC" (the primary: owns the engines) and "TR-MPC Tap" / "TR-MPC Tap FX" instances in the same MPC
// process. The tap libraries find it through trmpc_tap_shared(), exported by trmpc.so (dlopen RTLD_NOLOAD). Same scheme as
// Machinemodule's taps (mpc-vst-machinedrum/vst/tap_shared.h): one slot or send per tap so MPC's own mixer, submixes and
// insert/send effects can process it.
#pragma once
#include <atomic>
#include <cstdint>

namespace trtap
{
	constexpr int kSlots = 16, kFrames = 128, kRing = 8;
	// planes: 2*s, 2*s+1 = slot s post-voice, post-pan L/R; kPlaneRev / kPlaneDel = the mono reverb / delay send buses
	constexpr int kPlaneRev = 2 * kSlots, kPlaneDel = kPlaneRev + 1, kPlanes = kPlaneDel + 1;
	// tap sources, one bit each in a tap's mask (any number at once, summed): bits 0-15 the slots, 16 reverb send, 17 delay send
	constexpr int kBitRev = kSlots, kBitDel = kSlots + 1, kSources = kSlots + 2;

	struct Shared
	{
		static constexpr uint32_t kMagic = 0x54524d50;
		uint32_t magic = kMagic;
		std::atomic<const void*> owner{nullptr};	// the primary that publishes (the first one created)
		std::atomic<uint32_t> written{0};			// blocks published
		std::atomic<uint32_t> hostRead{0};			// blocks the primary's host has taken
		std::atomic<int64_t> hostCallUs{0};			// steady-clock time (us) of the primary's last host call
		std::atomic<int> tapped[kSlots];			// taps reading each slot: that slot's dry leaves the primary's main mix
		std::atomic<int> tapsRev{0}, tapsDel{0};	// taps reading a send bus: the primary's own reverb / delay no longer get it
		alignas(64) int16_t data[kRing][kPlanes][kFrames];
	};
}

extern "C" trtap::Shared* trmpc_tap_shared(void);
