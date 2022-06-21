#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif
	
resource 'PiPL' (16000) {
	{	/* array properties: 12 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"Vulkanator"
		},
		/* [3] */
		Category {
			"Vulkan Test"
		},
#ifdef AE_OS_WIN
	#ifdef AE_PROC_INTELx64
		CodeWin64X86 {"EntryPoint"},
	#else
		CodeWin32X86 {"EntryPoint"},
	#endif
#else
	#ifdef AE_OS_MAC
		CodeMachOPowerPC {"EntryPoint"},
		CodeMacIntel32 {"EntryPoint"},
		CodeMacIntel64 {"EntryPoint"},
	#endif
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			524289	/* 1.0 */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
		0x02000000 // 33554432

		},
		AE_Effect_Global_OutFlags_2 {
		0x00001408 // 5128
		},
		/* [11] */
		AE_Effect_Match_Name {
			"WUNK Vulkanator"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		}
	}
};

