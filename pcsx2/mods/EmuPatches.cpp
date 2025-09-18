#include <pcsx2/Config.h>
#include <pcsx2/vtlb.h>
#include "GS/Renderers/Common/GSDevice.h"
#include <common/Path.h>
#include <common/FileSystem.h>

//no interlacing patch - this actually halfs the vertical res which is bad at low res
void EnableInterlacing()
{
	char buf[4] = {0x08, 0x00, 0x42, 0x64};
	vtlb_memSafeWriteBytes(0x0015487C, &buf, 4);
}
void DisableInterlacing()
{
	char buf[4] = {0x00, 0x00, 0x00, 0x00};
	vtlb_memSafeWriteBytes(0x0015487C, &buf, 4);
}

//frogot what these are for
/*
	//sw v0,-0x7BB0(gp) -> NOP
	char buf0[4] = {0x00, 0x00, 0x00, 0x00};
	//vtlb_memSafeWriteBytes(0x001034B8, &buf0, 4);

	//char buf[1] = {0x03};
	//vtlb_memSafeWriteBytes(0x001031F4, &buf, 1);

	char buf2[4] = {0x00};
	//vtlb_memSafeWriteBytes(0x001031FC, &buf2, 1);

	char buf3[4] = {0x00, 0x00, 0x00, 0x00};
	//vtlb_memSafeWriteBytes(0x00103124, &buf3, 4);
*/


//hostfs loading patch - doesnt work when applying to memory for some reason
//currently we apply the patch to the elf file after extraction instead
void HostFSPatch()
{
	/*
	const std::string patch_filename = Path::Combine(EmuFolders::Resources, "hostfspatch.bin");
	const auto fp = FileSystem::OpenManagedCFile(patch_filename.c_str(), "rb");
	if (!fp)
		return;
	FILE* stream = fp.get();
	std::fseek(stream, 0, SEEK_SET);

	char buf[368] = {};
	std::fread(&buf, 368, 1, stream);

	char buf2[3264] = {};
	std::fread(&buf2, 3264, 1, stream);

	vtlb_memSafeWriteBytes(0x0038fb70, &buf, 368);
	vtlb_memSafeWriteBytes(0x0038fdd0, &buf2, 3264);

	//bnel v0,s2 -> beq zero zero //force this branch
	char buf3[4] = {0x09, 0x00, 0x00, 0x10};
	vtlb_memSafeWriteBytes(0x00105240, &buf3, 4);

	//bne v1,v0 -> beq zero zero //force this branch
	char buf4[4] = {0x22, 0x00, 0x00, 0x10};
	vtlb_memSafeWriteBytes(0x001052F4, &buf4, 4);

	//bne v1,zero -> nop
	char buf5[4] = {0x00, 0x00, 0x00, 0x00};
	vtlb_memSafeWriteBytes(0x00105E9C, &buf5, 4);

	//bnel v1,zero -> nop
	char buf6[4] = {0x00, 0x00, 0x00, 0x00};
	vtlb_memSafeWriteBytes(0x00106048, &buf6, 4);

	//bne v1,zero -> nop
	char buf7[4] = {0x00, 0x00, 0x00, 0x00};
	vtlb_memSafeWriteBytes(0x0010663C, &buf7, 4);
	*/
}

// Reactive Aspect Ratio Patch
// This one won't be visible in UI
void PTR2AspectRatioSet(AspectRatioType ar)
{
	char buf[4] = {};
	switch (ar)
	{
		// adapted from Parotaku's widescreen patch
		case AspectRatioType::R4_3:
			//patch=1,EE,0016066c,word,3c013f40
			vtlb_memSafeWriteBytes(0x0016066c, &buf, 4);
			vtlb_memSafeWriteBytes(0x00160678, &buf, 4);
			vtlb_memSafeWriteBytes(0x0016067c, &buf, 4);
			break;
		case AspectRatioType::RAuto4_3_3_2:
			vtlb_memSafeWriteBytes(0x0016066c, &buf, 4);
			vtlb_memSafeWriteBytes(0x00160678, &buf, 4);
			vtlb_memSafeWriteBytes(0x0016067c, &buf, 4);
			break;
		case AspectRatioType::R16_9:
		
			{
				//patch=1,EE,0016066c,word,3c013f40
				char buf2[4] = {0x40, 0x3F, 0x01, 0x3C};
				vtlb_memSafeWriteBytes(0x0016066c, &buf2, 4);
				//patch=1,EE,00160678,word,44810000
				char buf3[4] = {00, 00, 0x81, 0x44};
				vtlb_memSafeWriteBytes(0x00160678, &buf3, 4);
				//patch=1,EE,0016067c,word,4600c602
				char buf4[4] = {0x02, 0xC6, 0x00, 0x46};
				vtlb_memSafeWriteBytes(0x0016067c, &buf4, 4);
			}
			break;
		case AspectRatioType::Stretch:
				u32 width = g_gs_device->GetWindowWidth();
				u32 height = g_gs_device->GetWindowHeight();

				//patch=1,EE,0016066c,word,3c013f40
				char buf[4] = {0x40, 0x3F, 0x01, 0x3C};
				vtlb_memSafeWriteBytes(0x0016066c, &buf, 4);

				// replace the "3f40" (0.75 for 16:9) with our new multiplier
				float heightReal = height * 1.071; // to get 448 to 480
				heightReal = heightReal * 1.25; // this number has no meaning its eyeballed 
				float multiplier = heightReal / float(width);
				char buf2[4] = {};
				memcpy(buf2, &multiplier, 4);
				vtlb_memSafeWriteBytes(0x0016066c, &buf2[2], 1);
				vtlb_memSafeWriteBytes(0x0016066d, &buf2[3], 1);

				//patch=1,EE,00160678,word,44810000
				char buf3[4] = {00, 00, 0x81, 0x44};
				vtlb_memSafeWriteBytes(0x00160678, &buf3, 4);
				//patch=1,EE,0016067c,word,4600c602
				char buf4[4] = {0x02, 0xC6, 0x00, 0x46};
				vtlb_memSafeWriteBytes(0x0016067c, &buf4, 4);
		break;
	}
}

void PTR2AspectRatioSet()
{
	AspectRatioType ar = EmuConfig.GS.AspectRatio;
	PTR2AspectRatioSet(ar);
}

// Patches unused func PackIntDecode to just infinitely call MTCWait(1)
// this is so we can do stuff asynchronously* by jumping to PackIntDecode
// i.e loading INT assets ourselves without the loading screen stuttering/pausing

// *I don't think this is technically async, as we are copying the original game logic and just
// sleeping** after XX milliseconds, to call MTCWait() and give the game time to render out the
// loading screen, either way the end goal of stopping stuttering is achieved

// **In our case we don't sleep, we end the hook, which lets the game start running PackIntDecode again

void createAsyncFunc()
{
	char buf[48] = {
		0xE0, 0xFF, 0xBD, 0x27, // addiu sp -0x20
		0x10, 0x00, 0xA4, 0xFF, // sd a0, 0x10(sp)
		0x00, 0x00, 0x00, 0x00, // nop					<-------------
		0xF6, 0x05, 0x04, 0x0C, // jal 0x001017D8 (MtcWait			 |
		0x01, 0x00, 0x04, 0x24, // addiu a0,zero,0x1  (li a0, 0x1	 |
		0x00, 0x00, 0x00, 0x00, // nop								 |
		0x00, 0x00, 0xA4, 0x8F, // lw a0, (sp)						 |
		0x00, 0x00, 0x00, 0x00, // nop                   -------------  (Gets patched to a beqz, a0, 0x00104E98 by INT_Loader hook in PTR2Hooks.cpp)
		0x00, 0x00, 0x00, 0x00, // nop
		0x10, 0x00, 0xA4, 0xDF, // ld a0, 0x10(sp)
		0x0C, 0x03, 0x00, 0x10, // beq zero zero 0x00105aec
		0x20, 0x00, 0xBD, 0x27  // addiu sp,sp,0x20
	};

	vtlb_memSafeWriteBytes(0x00104e90, &buf, 48);

	// patch jal PackIntDecodeWait to jal PackIntDecode
	char buf2[4] = { 0xA4, 0x13, 0x04, 0x0C };
	vtlb_memSafeWriteBytes(0x00105ad8, &buf2, 4);
}


void ReloadEmuPatches()
{
	if (EmuConfig.EnableNoInterlacingPatches)
		DisableInterlacing();
	//NoInterlacingPatch();
	//HostFSPatch();
	createAsyncFunc();
	PTR2AspectRatioSet();
}