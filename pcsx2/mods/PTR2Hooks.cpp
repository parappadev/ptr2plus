#include "PrecompiledHeader.h"

#include "PTR2Hooks.h"

#include "mods/PTR2Common.h"

#include <Common.h>
#include <VMManager.h>

#include <common/Path.h>
#include <common/FileSystem.h>

#include "x86/iR5900.h"
#include <pcsx2/mods/P2mTools.h>
#include <pcsx2/mods/ActiveMods.h>
#include <pcsx2/DebugTools/MipsAssembler.h>

#include <chrono>

extern void iBranchTest(u32 newpc);

using namespace x86Emitter;
using namespace PTR2;
using namespace std::chrono;

GPRregs regs;

PrHookManager* PrHookMgr()
{
	static PrHookManager hookMgr;
	return &hookMgr;
}

void PrHookManager::InitHooks()
{
	m_gameHash = VMManager::GetCurrentCRC();

	switch (m_gameHash)
	{
		case 0x38E1D1E3: /* Patched PTR2 NTSC-J */
			m_hookMap.insert({0x00104E98, INT_Loader}); //nop in PackIntDecode - called by CdctrlMemIntgDecode 
			m_returnMap.insert({0x00104E98, 0x00104E98});

			m_hookMap.insert({0x00105AD8, Capture_Reg_INT_Loader});
			m_returnMap.insert({0x00105AD8, 0x00105AD8});

			//m_hookMap.insert({0x0010559C, intReadSub});
			//m_returnMap.insert({0x0010559C, 0x001055B8});

			m_hooksInit = true;
			break;
	}
}


//INT Loader globals (ew yucky? 
int g_cur_address_pp = 0;
int g_current_file = 0;
int g_total_copied = 0;
//todo: make g_frametime_ms change according to actual pcsx2 fps: would stop stuttering on high fps (turbo speed
//not simple atm because PerformanceMetrics reports 0fps, we're probably on the wrong thread
float g_frametime_ms = 12;
int g_packFile_fnum = 0;
std::string g_int_path;
std::string g_folder;

std::vector<char> name_chunk(80000); // biggest name_chunk the game has is probably st8.int with ~41,000 - i havent checked XTR

void PrHookManager::INT_Loader()
{
	const time_point beg = high_resolution_clock::now(); // timer for checking when to pause hook

	if (g_current_file == 0 && g_total_copied == 0) //if fresh hook has been called (not from paused
	{
		Console.WriteLn(Color_Green, "[PTR2PLUS] STARTED 'INT_Loader' hook");

		// Patch BEQ ZERO ZERO into PackIntDecode to make an unconditional loop
		// Why am I not using a jump instruction? Because as branches store the address relatively via the PC
		// Doing a relative displacement instead of a jump means we don't have to add cases here for
		// other regions of PTR2 that have different addresses to move to.

		char buf[4] = {0xFA, 0xFF, 0x00, 0x10}; 
		vtlb_memSafeWriteBytes(0x00104EAC, &buf, 4);

		// Find FILE_STR on stack and get int name
		int FILE_STR_pp;
		int int_name_pp;

		vtlb_memSafeReadBytes(regs.n.sp.UD[0] + 0x10, &FILE_STR_pp, 0x04);
		vtlb_memSafeReadBytes(FILE_STR_pp + 0x04, &int_name_pp, 0x04);

		// Get int path to check if correct pointer or not
		std::vector<char> buf2(256); //255 is the max path size in the ISO standard
		vtlb_memSafeReadBytes(int_name_pp, buf2.data(), sizeof(buf2));
		g_int_path = buf2.data();
	
		if (g_int_path.find("INT") == std::string::npos) //if bad FILE_STR_pp
		{
			// it's probably a boxy HKO INT which has the pointer at a different place
			vtlb_memSafeReadBytes(regs.n.sp.UD[0], &FILE_STR_pp, 0x04);
			FILE_STR_pp += 0x1C; //gotta do this for boxy
			vtlb_memSafeReadBytes(FILE_STR_pp + 0x04, &int_name_pp, 0x04);

			// Reread int path now it has been corrected for boxy
			vtlb_memSafeReadBytes(int_name_pp, buf2.data(), sizeof(buf2));
			g_int_path = buf2.data();
		}

		// Get current int header off using a0 - header_size - name_size
		// Probably a better way of finding this (stack pointer maybe)
		// not sure there is actually. this is fine.

		int head_size;
		vtlb_memSafeReadBytes(regs.n.s4.UD[0] + 0x0c, &head_size, 0x04);
		int name_size;
		vtlb_memSafeReadBytes(regs.n.s4.UD[0] + 0x10, &name_size, 0x04);

		int int_head_pp = regs.n.a0.UD[0] - head_size - name_size; //regs.n.s0.UD[0];

		PACKINT_FILE_STR packFile;
		vtlb_memSafeReadBytes(int_head_pp, &packFile, sizeof(packFile));

		g_packFile_fnum = packFile.fnum;

		// Save the name chunk now, before the packed int gets overwritten by unpacked files
		// in the memory pool - the game doesn't care because it doesn't use the name chunk and it
		// already copied the header chunk elsewhere

		vtlb_memSafeReadBytes(int_head_pp + head_size, name_chunk.data(), name_size);

		switch (packFile.ftype)
		{
			case FT_VRAM:
				g_folder = "VRAM";
				break;
			case FT_R1:
				g_folder = "R1";
				break;
			case FT_R2:
				g_folder = "R2";
				break;
			case FT_R3:
				g_folder = "R3";
				break;
			case FT_R4:
				g_folder = "R4";
				break;
			case FT_SND:
				g_folder = "SND";
				break;
			case FT_ONMEM:
				g_folder = "ONMEM";
				break;
		}

#if defined(PCSX2_DEVBUILD)
		Console.WriteLn("[PTR2PLUS] Writing FOLDER: " + g_folder + " to: " + fmt::format("{:#08x}", regs.n.a1.UD[0] + g_cur_address_pp));
#endif
	}
	else
	{
#if defined(PCSX2_DEVBUILD)
		Console.WriteLn(Color_Green, "[PTR2PLUS] RESUMED 'INT_Loader' hook");
	}
	Console.WriteLn("[PTR2PLUS] INT_Loader: g_current_file: %i", g_current_file);
	Console.WriteLn("[PTR2PLUS] INT_Loader: g_total_copied: %i", g_total_copied);
	Console.WriteLn("[PTR2PLUS] INT_Loader: g_cur_address_pp: %i", g_cur_address_pp);
#endif

	// Get cached address
	int write_pp = regs.n.a1.UD[0] + 0x20000000;

	int strings_off = (8 * g_packFile_fnum);

	bool async_break = false;
	int i = g_current_file;

	for (i; i < g_packFile_fnum; i++)
	{
		//get file size and name pointer
		//mods can have different file sizes so we don't actually use this value... commented out for now
		/* int file_size = 0;
		memcpy(&file_size, name_chunk + 4 + (8 * i), 4); */

		int name_pp = 0;
		memcpy(&name_pp, name_chunk.data() + (8 * i), 4);

		//calculate size of name (null terminated)
		int filename_size = 0;
		while (name_chunk[strings_off + name_pp + filename_size] != '\0')
		{
			filename_size++;
		}
		std::vector<char> buf(256); //255 is probably the filename length max
		memcpy(buf.data(), name_chunk.data() + strings_off + name_pp, filename_size);
		buf[filename_size] = 0;
		std::string name = buf.data();

#if defined(PCSX2_DEVBUILD)
		Console.WriteLn("[PTR2PLUS] INT_Loader: Writing FILE: " + name + " to: " + fmt::format("{:#08x}", regs.n.a1.UD[0] + g_cur_address_pp));
#endif
		//remove ".INT"
		std::string int_dir = g_int_path.substr(0, g_int_path.size() - 4);

		std::string int_title(Path::GetFileTitle(g_int_path));

		// Remove "host:\"
		std::string path = int_dir;
		path = path.substr(6, path.size());

		path += "\\" + g_folder + "\\" + name;


		//replace path with modded file if active mod
		std::string mod;
		if (ActiveMods::GetMod(path, mod))
		{
			path = "MOD\\" + int_title + "\\" + g_folder + "\\" + name;
#if defined(PCSX2_DEVBUILD)
			Console.WriteLn(Color_Cyan, "[PTR2PLUS] INT_Loader: Using " + name + " from " + mod + " instead.");
#endif
		}
		std::string final_path = Path::Combine(EmuFolders::PTR2, path);

		// Write bytes from file to memory
		const auto fp = FileSystem::OpenManagedCFile(final_path.c_str(), "rb");
		int fp_file_size = FileSystem::GetPathFileSize(final_path.c_str());
		std::fseek(fp.get(), g_total_copied, SEEK_SET);
		
		const int buf_size = 4096;
		u8 buf3[buf_size];

		
		while (fp_file_size > g_total_copied + buf_size)
		{
			std::fread(&buf3, sizeof(buf3[0]), buf_size, fp.get());
			vtlb_memSafeWriteBytes(write_pp + g_cur_address_pp, &buf3, buf_size);
			g_total_copied += buf_size;
			g_cur_address_pp += buf_size;

			auto end = high_resolution_clock::now();
			auto duration = duration_cast<milliseconds>(end - beg);
			if (duration.count() > g_frametime_ms)
			{
#if defined(PCSX2_DEVBUILD)
				Console.WriteLn("[PTR2PLUS] INT_Loader: %i milliseconds passed, breaking", duration);
#endif
				async_break = true;
				break;
			}
		}
		if (async_break)
			break;
		else
		{
			std::fread(&buf3, fp_file_size - g_total_copied, 1, fp.get());
			vtlb_memSafeWriteBytes(write_pp + g_cur_address_pp, &buf3, fp_file_size - g_total_copied);
			g_cur_address_pp += fp_file_size - g_total_copied;

			// Make sure the offset is 0x10 aligned, this is how the game wants it
			while (g_cur_address_pp % 0x10 != 0)
			{
				memWrite8(write_pp + g_cur_address_pp, 0);
				g_cur_address_pp++;
			}

			// write current address pointer to header (so game knows where the next file is
			// this cant be kept same as original because mod files may have different sizes
			vtlb_memSafeWriteBytes(regs.n.s4.UD[0] + 0x20 + sizeof(u32) * (i + 1), &g_cur_address_pp, 0x04);

			//reset g_total_copied
			g_total_copied = 0;
		}
	}

	g_current_file = i;
	if (!async_break && g_current_file == g_packFile_fnum) // If we have finished loading the INT folder
	{
		//delete[] name_chunk; //free up heap memory
		g_current_file = 0;
		g_cur_address_pp = 0;
		Console.WriteLn(Color_Green, "[PTR2PLUS] FINISHED 'INT_Loader' hook");
		u32 one = 1;
		vtlb_memSafeWriteBytes(regs.n.sp.UD[0] - 0x20, &one, 4);

		char buf3[4] = {0x00, 0x00, 0x00, 0x00}; // Patch PackIntDecode loop to a NOP to escape it
		vtlb_memSafeWriteBytes(0x00104EAC, &buf3, 4);
	}
	else
	{
		// otherwise, hook has ended prematurely to give game time to render the loading screen
#if defined(PCSX2_DEVBUILD)
		Console.WriteLn(Color_Green, "[PTR2PLUS] PAUSED 'INT_Loader' hook");
#endif
	}
}

void PrHookManager::intReadSub()
{
	/* WIP */
}

void PrHookManager::Capture_Reg_INT_Loader()
{
#if defined(PCSX2_DEVBUILD)
	Console.WriteLn(Color_Green, "[PTR2PLUS] STARTED 'Capture_Reg_INT_Loader' hook");
#endif
	regs = cpuRegs.GPR;
#if defined(PCSX2_DEVBUILD)
	Console.WriteLn("[PTR2PLUS] Capture_Reg: cpuregs a0: %u", cpuRegs.GPR.n.a0.UD[0]);
	Console.WriteLn("[PTR2PLUS] Capture_Reg: regs a0: %u", regs.n.a0.UD[0]);
	Console.WriteLn(Color_Green, "[PTR2PLUS] FINISHED 'Capture_Reg_INT_Loader' hook");
#endif

}

bool PrHookManager::RunHooks(const u32 curPC)
{
	if (!m_hooksInit)
		return false;

	// Find the functions to hook
	auto hook = m_hookMap.find(curPC);
	auto ret = m_returnMap.find(curPC);

	if (hook != m_hookMap.end())
	{
#if defined(PCSX2_DEVBUILD)
		Console.WriteLn(Color_Green, "[PTR2PLUS] RunHooks: Hook found at " + fmt::format("{:#08x}", curPC));
#endif
		if (CHECK_EEREC)
			recCall(hook->second); // Recompiler: Appends the hook to the current recompiled x86 block - Doesn't execute immediately!
		else
			hook->second(); // Interpreter: Executes the hook right now

		if (ret != m_returnMap.end()) 
		{
			// If return address is same as hook address, don't change PC, just let game run as normal
			// This is required if we don't want the hook to replace game execution, instead be appended to it - needed for the way the INT loader works to be async/wait
			// Currently this is the case in all of our hooks (CaptureRegs and INT Loader
			if (ret->first == ret->second) 
				return true;

			// We're done, leave the game alone for now
			if (CHECK_EEREC)
			{
				g_branch = 1;

				iFlushCall(FLUSH_EVERYTHING);
				xMOV(ptr32[&cpuRegs.pc], ret->second);
				iBranchTest(ret->second);
			}
			else
				cpuRegs.pc = ret->second;

			return true;
		}
		else
		{
			// We don't know what to set the program counter to... fuck
			Console.WriteLn(Color_Red, "[PTR2] Couldn't find return address!");
		}
	}
	return false;
}