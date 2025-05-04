#include <common/FileSystem.h>
#include <pcsx2/Config.h>

#include <fstream>
#include <common/Path.h>

#include "Memory.h"

#include "Common.h"
#include "PTR2Common.h"
#include <common/StringUtil.h>
#include <pcsx2/mods/ActiveMods.h>
#include <pcsx2/mods/PriorityList.h>
#include <pcsx2/Host.h>
#include <pcsx2/VMManager.h>
#include "common/SettingsInterface.h"
using namespace PTR2;

bool files_to_delete;
std::string g_loading = "";
#pragma pack(push, 1)

//backwards compatiblity
struct p2m_header_v1 //v1, v2
{
	char p2m_magic[4];
	u16 version;
	u16 file_count;
	char reserved[8]; 
	u32 meta_offset; 
	u32 meta_size;
	u32 path_offset;
	u32 path_size;
	u32 type_offset;
	u32 type_size;
	u32 size_offset;
	u32 size_size;
	u32 body_offset;
	u32 body_size;
	char reserved2[8]; 
};

struct p2m_header //v3
{
	char p2m_magic[4];
	u16 version;
	char reserved[2];
	u32 file_count;
	u32 tex_file_count;
	u32 meta_offset;
	u32 meta_size;
	u32 path_offset;
	u32 path_size;
	u32 type_offset;
	u32 type_size;
	u32 size_offset;
	u32 size_size;
	u32 body_offset;
	u32 body_size;
	u32 tex_path_offset;
	u32 tex_path_size;
	u32 tex_size_offset;
	u32 tex_size_size;
	u32 tex_body_offset;
	u32 tex_body_size;
};

#pragma pack(pop)

struct STDAT_DAT
{
	u32 id; //i think
	char section_name[4];
	float bpm;
	u32 null;
	u32 olm_addr1;
	u32 olm_addr2;
	u32 olm_addr3;
	FILE_STR file1; //e.g int, if file = vs08vs0.int , end
	FILE_STR file2; //e.g c wp2
	FILE_STR file3; //e.g g wp2
	FILE_STR file4; //e.g ba wp2
	u32 olm_addr4; //null for cutscenes/boxy 
};

struct olm_struct
{
	FILE_STR file; //if ST00 end
	u32 unk;
	u32 unk_olm_addr;
	u32 stage_name_pos; //if TITLE end
};

/*
file map
0007ff70 RAM: 0017EF70
FILE_STR logo int
u32 null
FILE_STR stmenu int
u32 null
FILE_STR EXT00 - EXT09 wp2

*/

static_assert(sizeof(p2m_header_v1) == 64, "p2m_header_v1 struct not packed to 64 bytes");
static_assert(sizeof(p2m_header) == 0x50, "p2m_header_v3 struct not packed to 0x50 bytes");
static_assert(sizeof(sceCdlFILE) == 0x24, "sceCdlFILE struct not packed to 0x24 bytes");
static_assert(sizeof(FILE_STR) == 0x2C, "FILE_STR struct not packed to 0x2C bytes");
static_assert(sizeof(STDAT_DAT) == 0xD0, "STDAT_DAT struct not packed to 0xD0 bytes");
static_assert(sizeof(olm_struct) == 0x38, "olm_struct struct not packed to 0x38 bytes");

enum ModType : u16
{
	isoFile = 0,
	intAsset = 1,
	//pcsx2Tex = 2
};
struct mod_file
{
	std::string path;
	ModType type;
	u32 size;
	u32 pos;
	bool tmp;
};

struct tex_file
{
	std::string path;
	u32 size;
	u32 pos;
};

bool copyStream(FILE* from, FILE* to, int size)
{
	//streams must be set up at correct positions

	const int buf_size = 4096;
	u8 buf[buf_size];
	int total_copied = buf_size;
	while (size > total_copied)
	{

		std::fread(&buf, sizeof(buf[0]), buf_size, from);
		std::fwrite(&buf, sizeof(buf[0]), buf_size, to);
		total_copied += buf_size;
	}
	std::fread(&buf, size + buf_size - total_copied, 1, from);
	std::fwrite(&buf, size + buf_size - total_copied, 1, to);

	return true;
}
bool readBytes(u32& mem, void* dst, u32 size)
{
	if (vtlb_memSafeReadBytes(mem, dst, size))
	{
		mem += size;
		return true;
	}
	return false;
}
bool toggleTexReplacementSetting(bool new_value)
{
	std::string section = "EmuCore/GS";
	std::string key = "LoadTextureReplacements";

	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	bool value = bsi->GetBoolValue(section.c_str(), key.c_str(), false);
	if (value == new_value)
	{
		return true;
	}
	else
	{
		bsi->SetBoolValue(section.c_str(), key.c_str(), new_value);
		Host::RunOnCPUThread([]() { VMManager::ApplySettings(); });
	}
	
}

static std::string GetPTR2ModDirectory()
{
	return Path::Combine(EmuFolders::PTR2, "/MOD");
}

static std::string GetTexReplacementDirectory(std::string modname)
{
	int priority;
	PriorityList::GetPriority(modname, priority);
	std::string folder_name = std::to_string(priority) + "_" + modname;
	return Path::Combine(Path::Combine(EmuFolders::Textures, "/ptr2real/replacements"), folder_name);
}
static std::string GetTexUnloadDirectory(std::string modname)
{
	return Path::Combine(Path::Combine(EmuFolders::PTR2InstalledMods, Path::StripExtension(modname)), "/textures/");
}

bool isIntAssetCheap(std::string path)
{
	size_t slash = path.find('\\');
	if (path.substr(0, slash) == "DATA" || path.substr(0, slash) == "data")
		return true;
	return false;
}
bool isIntAsset(std::string path)
{
	std::string extension = StringUtil::toUpper(Path::GetExtension(path));
	if (extension != "WP2" && extension != "INT" && extension != "OLM" && extension != "XTR")
	{
		return true;
	}
	return false;
}

static std::string GetEnabledModFilePath(std::string path)
{
	//remove directory if the file is loaded from ISO, as we have limited space in the ELF path
	if (!isIntAssetCheap(path))
	{
		size_t slash = path.find('\\');
		path.erase(0, slash);
	}
	std::string mod_dir = GetPTR2ModDirectory();
	std::string mod_file = Path::Combine(mod_dir, path);
	return mod_file;
}

static std::string GetDisabledActiveModFilePath(std::string path)
{
	std::string mod;
	ActiveMods::GetMod(path, mod);
	std::string mod_dir = Path::Combine(EmuFolders::PTR2InstalledMods, mod);
	std::string mod_file = Path::Combine(Path::StripExtension(mod_dir), path);
	return mod_file;
}

static std::string GetDisabledModFilePath(std::string modname, std::string path)
{
	std::string mod_dir = Path::Combine(EmuFolders::PTR2InstalledMods, modname);
	std::string mod_file = Path::Combine(Path::StripExtension(mod_dir), path);
	return mod_file;
}

std::string GetP2MPathFromModName(std::string modname)
{
	return Path::Combine(EmuFolders::PTR2InstalledMods, modname);
}

bool LoadTexFiles(std::string modname)
{
	std::string source_folder = GetTexUnloadDirectory(modname);
	std::string destination_folder = GetTexReplacementDirectory(modname);

	if (!FileSystem::DirectoryExists(source_folder.c_str())) //error
		return false;
	if (FileSystem::DirectoryExists(destination_folder.c_str())) //already loaded
		return true;

	//ensure texture replacements game directory exists
	std::string replacementsPath = Path::Combine(EmuFolders::Textures, "/ptr2real/replacements");
	FileSystem::EnsureDirectoryExists(replacementsPath.c_str(), true);

	FileSystem::RenamePath(source_folder.c_str(), destination_folder.c_str());

	//if texture replacements setting is off, turn it on
	toggleTexReplacementSetting(true);

	return true;
}
bool UnloadTexFiles(std::string modname)
{
	std::string source_folder = GetTexReplacementDirectory(modname);
	std::string destination_folder = GetTexUnloadDirectory(modname);

	if (!FileSystem::DirectoryExists(source_folder.c_str()))
		return false; //there were no tex replacements?
	if (FileSystem::DirectoryExists(destination_folder.c_str())) //already unloaded
		return true;

	std::string unloadedPath = Path::Combine(EmuFolders::Textures, "/ptr2real/unloaded");
	FileSystem::EnsureDirectoryExists(unloadedPath.c_str(), true);
	FileSystem::RenamePath(source_folder.c_str(), destination_folder.c_str());
	
	//if all textures are unloaded, turn texture replacements off
	if (FileSystem::DirectoryIsEmpty(Path::Combine(EmuFolders::Textures, "/ptr2real/replacements").c_str()))
	{
		toggleTexReplacementSetting(false);
	}

	return true;
}

bool PriorityRemove(std::string modname)
{
	std::vector<std::string> priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		UnloadTexFiles(mod);
	}
	PriorityList::Remove(modname);
	priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		LoadTexFiles(mod);
	}
	return true;
}
bool PriorityAdd(std::string modname, int newIndex)
{
	std::vector<std::string> priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		UnloadTexFiles(mod);
	}
	PriorityList::Add(modname, newIndex);
	priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		LoadTexFiles(mod);
	}
	return true;
}
bool PriorityPush(std::string modname, bool dontLoadTex)
{
	std::vector<std::string> priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		UnloadTexFiles(mod);
	}
	PriorityList::Push(modname);
	priorities = PriorityList::Get();
	for (std::string mod : priorities)
	{
		if (mod == modname && dontLoadTex)
			continue;
		else
			LoadTexFiles(mod);
	}
	return true;
}

//unused, left as a POC - see comments in parseP2MHeader
void parseP2Mv1(p2m_header_v1& hd_v1, p2m_header& hd)
{
	hd.p2m_magic[0] = 0x50;
	hd.p2m_magic[1] = 0x32;
	hd.p2m_magic[2] = 0x4D;
	hd.p2m_magic[3] = 0x11;
	hd.version = hd_v1.version;
	hd.reserved[0] = 0;
	hd.reserved[1] = 0;
	hd.file_count = hd_v1.file_count;
	hd.tex_file_count = 0;
	hd.meta_offset = hd_v1.meta_offset;
	hd.meta_size = hd_v1.meta_size;
	hd.path_offset = hd_v1.path_offset;
	hd.path_size = hd_v1.path_size;
	hd.type_offset = hd_v1.type_offset;
	hd.type_size = hd_v1.type_size;
	hd.size_offset = hd_v1.size_offset;
	hd.size_size = hd_v1.size_size;
	hd.body_offset = hd_v1.body_offset;
	hd.body_size = hd_v1.body_size;
	hd.tex_path_offset = 0;
	hd.tex_path_size = 0;
	hd.tex_size_offset = 0;
	hd.tex_size_size = 0;
	hd.tex_body_offset = 0;
	hd.tex_body_size = 0;
	return;
}
bool parseP2MHeader(FILE* stream, p2m_header& hd)
{
	//make sure at beginning of file
	std::fseek(stream, 0, SEEK_SET);

	u32 p2m_magic;
	std::fread(&p2m_magic, 4, 1, stream);
	if (p2m_magic != 290271824) //its not a p2m file!!!
		return false;
	u16 p2m_version;
	std::fread(&p2m_version, 2, 1, stream);

	if (p2m_version < 3) //backwards compatibility
	{
		return false;
		//p2m v1, v2 have a bug where all file.type are put down as 0
		//a workaround could be made with manually reading the file extensions from the path
		//however v1,v2 are indev p2m versions so supporting these isnt necessary
		
		//commented out below is how v1,v2 would be parsed if the bug didnt exist
		//left as a proof of concept for how we will deal with backwards compatibility in the future
		/*
		p2m_header_v1 hd_v1;
		std::fseek(stream, 0, SEEK_SET);
		std::fread(&hd_v1, sizeof(p2m_header_v1), 1, stream);
		parseP2Mv1(hd_v1, hd);
		*/
	}
	else if (p2m_version > 3) //FUTURE! FUUTUUURE! FUUUUTUUUURE!
	{
		return false;
	}
	else
	{
		std::fseek(stream, 0, SEEK_SET);
		std::fread(&hd, sizeof(p2m_header), 1, stream);
	}
	return true;
}

mod_file GetP2MFile(FILE* stream, p2m_header& hd, int index)
{
	mod_file file;

	//read
	int off = 0; //ftell(stream);
	std::string path;
	for (int i = 0; i < index + 1; i++)
	{
		char buf[999];
		std::fseek(stream, hd.path_offset + off, SEEK_SET);
		fgets(buf, sizeof(buf), stream);
		path = buf;
		//fgets puts fp to end, so keep track ourselves
		off += path.length() + 1;
	}
	file.path = path;

	//read type
	std::fseek(stream, hd.type_offset + (sizeof(u16) * index), SEEK_SET);
	ModType type;
	std::fread(&type, sizeof(u16), 1, stream);
	file.type = type;

	//read size/pos
	std::fseek(stream, hd.size_offset + ((sizeof(u32) * 2) * index), SEEK_SET);
	u32 size;
	u32 pos;
	std::fread(&size, sizeof(u32), 1, stream);
	std::fread(&pos, sizeof(u32), 1, stream);
	file.size = size;
	file.pos = pos;

	file.tmp = false;

	return file;
}
std::vector<mod_file> GetP2MFiles(FILE* stream, p2m_header& hd)
{
	parseP2MHeader(stream, hd);

	std::vector<mod_file> files;

	for (int i = 0; i < hd.file_count; i++)
	{
		mod_file file = GetP2MFile(stream, hd, i);
		files.push_back(file);
	}
	return files;
}
std::vector<mod_file> GetP2MFiles(FILE* stream, std::string modpath)
{
	const auto fp = FileSystem::OpenManagedCFile(modpath.c_str(), "rb");
	p2m_header hd;
	return GetP2MFiles(stream, hd);
}
std::vector<mod_file> GetP2MFiles(std::string modpath)
{
	const auto fp = FileSystem::OpenManagedCFile(modpath.c_str(), "rb");
	p2m_header hd;
	return GetP2MFiles(fp.get(), hd);
}
std::vector<mod_file> GetP2MFilesByModName(std::string modname)
{
	std::string filename = GetP2MPathFromModName(modname);
	return GetP2MFiles(filename);
}

mod_file GetP2MFile(FILE* stream, p2m_header& hd, std::string path)
{
	std::fseek(stream, hd.path_offset, SEEK_SET);

	long off = ftell(stream);
	int i = 0;
	for (i; i < hd.file_count; i++)
	{
		char buf[999];
		if (fgets(buf, sizeof(buf), stream) != nullptr)
		{
			std::string found_path = buf;
			if (StringUtil::compareNoCase(found_path, path))
			{
				break;
			}
			//fgets puts fp to end, so reset it;
			off += found_path.length() + 1;
			std::fseek(stream, off, SEEK_SET);
		}
	}

	return GetP2MFile(stream, hd, i);
}

mod_file GetP2MFileByEntry(std::pair<std::string, std::string> entry)
{
	std::string filename = GetP2MPathFromModName(entry.second);
	const auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "rb");
	p2m_header hd;
	parseP2MHeader(fp.get(), hd);
	return GetP2MFile(fp.get(), hd, entry.first);
}

std::vector<tex_file> GetP2MTexFiles(FILE* stream, p2m_header& hd)
{
	std::vector<tex_file> tex_files;

	for (int i = 0; i < hd.tex_file_count; i++)
	{
		tex_file tex_file;
		//read
		int off = 0; //ftell(stream);
		std::string path;
		for (int i2 = 0; i2 < i + 1; i2++)
		{
			char buf[999];
			std::fseek(stream, hd.tex_path_offset + off, SEEK_SET);
			fgets(buf, sizeof(buf), stream);
			path = buf;
			//fgets puts fp to end, so keep track ourselves
			off += path.length() + 1;
		}
		tex_file.path = path;

		//read size/pos
		std::fseek(stream, hd.tex_size_offset + (8 * i), SEEK_SET);
		u32 size;
		u32 pos;
		std::fread(&size, sizeof(u32), 1, stream);
		std::fread(&pos, sizeof(u32), 1, stream);
		tex_file.size = size;
		tex_file.pos = pos;

		tex_files.push_back(tex_file);
	}
	return tex_files;
}

bool ELFfilenameFound(u32 mem, std::string filename)
{
	char dst[24];
	vtlb_memSafeReadBytes(mem, dst, sizeof(dst));
	std::string str(dst);

	filename = StringUtil::toUpper(filename);

	if (str.find(filename) != std::string::npos)
	{
		return true;
	}

	return false;
}
bool ELFpathFound(int& path_off, int& file_off, std::string filename)
{
	u32 mem_pointer = 0x0017EF70;
	//Console.WriteLn("Mem pointer: %u", mem_pointer);
	FILE_STR file;
	//Console.WriteLn("file size: %u", sizeof(file));
	for (int i = 0; i < 2; i++) //loop through logo/stmenu structs
	{
		readBytes(mem_pointer, &file, sizeof(file));
		if (ELFfilenameFound(file.fname_p, filename))
		{
			path_off = file.fname_p;
			file_off = mem_pointer - sizeof(file);
			return true;
		}

		mem_pointer += 4;
	}
	
	for (int i = 0; i < 10; i++) //loop through ext0X structs
	{
		readBytes(mem_pointer, &file, sizeof(file));
		if (ELFfilenameFound(file.fname_p, filename))
		{
			path_off = file.fname_p;
			file_off = mem_pointer - sizeof(file);
			return true;
		}
	}
	
	STDAT_DAT stdat;
	do //loop through stdata structs
	{
		readBytes(mem_pointer, &stdat, sizeof(stdat));
		
		//loop through file_strs inside stdat
		u32 offsets[4] = {stdat.file1.fname_p, stdat.file2.fname_p, stdat.file3.fname_p, stdat.file4.fname_p};
		for (int i = 0; i < 4; i++)
		{
			if (offsets[i] != 0 && ELFfilenameFound(offsets[i], filename))
			{
				path_off = offsets[i];
				file_off = mem_pointer - 4 - sizeof(file) * (5 - (i + 1));
				return true;
			}

		}

	} while (!ELFfilenameFound(stdat.file1.fname_p, "VS08VS0.INT"));
	olm_struct olm;

	do // Loop through OLM structs
	{
		readBytes(mem_pointer, &olm, sizeof(olm));
		if (ELFfilenameFound(olm.file.fname_p, filename))
		{
			path_off = olm.file.fname_p;
			file_off = mem_pointer - sizeof(olm);
			return true;
		}

	} while (!ELFfilenameFound(olm.file.fname_p, "STG00.OLM") || !ELFfilenameFound(olm.stage_name_pos, "TITLE"));

	return false;
}

bool PatchELFPath(std::string path, bool unpatch, bool tmp)
{
	std::string filename = Path::GetFileName(path).data();
	int off, file_off = 0;
	if (ELFpathFound(off, file_off, filename))
	{
		char patch[24];
		std::string strpath;
		if (unpatch)
		{
			strpath = "host:\\" + StringUtil::toUpper(path);
		}
		else if (tmp)
		{
			strpath = "host:\\TMP\\" + filename;
		}
		else
		{
			strpath = "host:\\MOD\\" + filename;
		}
		strncpy(patch, strpath.c_str(), sizeof(patch));
		vtlb_memSafeWriteBytes(off, patch, sizeof(patch));

		//patch search value
		char search[1] = {0};
		vtlb_memSafeWriteBytes(file_off + 3, search, 1);
		return true;
	}
	return false;
}
bool PatchELFPath(mod_file file)
{
	return PatchELFPath(file.path, false, file.tmp);
}
bool PatchELFPath(std::string path, bool tmp)
{
	return PatchELFPath(path, false, tmp);
}
bool UnPatchELFPath(mod_file file)
{
	return PatchELFPath(file.path, true, false);
}
bool UnPatchELFPath(std::string path)
{
	return PatchELFPath(path, true, false);
}

	/*
bool isIntAsset(std::string path)
{
	
	std::string extension = Path::GetExtension(path.data()).data();
	transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
	if (extension.find("WP2") == std::string::npos && extension.find("INT") == std::string::npos && extension.find("XTR") == std::string::npos && extension.find("XTR") == std::string::npos)
		return true;
	else
		return false;
	
}
*/
bool ApplyModFile(mod_file file)
{
	//dont patch files from unpacked INT/XTR files as they aren't in the ISO paths
	//they are loaded using PTR2Hooks, which will use the activemod cache file
	if (file.type == isoFile)
	{
		if (!PatchELFPath(file))
		{
			return false;
		}
	}
}

bool StartUpApplyActiveMods()
{
	std::vector<std::pair<std::string, std::string>> activeModCache = ActiveMods::GetAll();
	int file_count = activeModCache.size();

	//try and load textures for all mods
	for (std::string modname : PriorityList::Get())
		LoadTexFiles(modname);

	//load mod files
	for (std::pair<std::string, std::string> entry : activeModCache)
	{
		mod_file file = GetP2MFileByEntry(entry);
		if (!ApplyModFile(file))
		{
			return false;
		}
	}
	return true;
}

bool isInDeleteCache(std::string path)
{
	const std::string deletecache_filename(Path::Combine(EmuFolders::Cache, "deletefile.cache"));

	const auto fp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	for (int i = 0; i < file_count; i++)
	{
		std::string delete_path;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			return false;
		delete_path = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += delete_path.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		if (StringUtil::compareNoCase(delete_path, path))
		{
			return true;
		}
	}
	return false;
}
bool addDeleteEntry(std::string path)
{
	const std::string deletecache_filename(Path::Combine(EmuFolders::Cache, "deletefile.cache"));

	const auto fp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "rb+");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	std::fseek(fp.get(), 0, SEEK_END);
	fputs(path.c_str(), fp.get());
	fputc('\0', fp.get());

	file_count++;
	std::fseek(fp.get(), 0, SEEK_SET);
	std::fwrite(&file_count, sizeof(file_count), 1, fp.get());

	return true;
}

/* unused
bool removeDeleteEntry(std::string path)
{
	const std::string deletecache_filename(Path::Combine(EmuFolders::Cache, "deletefile.cache"));

	auto fp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	std::vector<std::string> new_paths;
	for (int i = 0; i < file_count; i++)
	{
		std::string active_path;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			return false;
		active_path = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += active_path.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		if (path != active_path)
		{
			new_paths.push_back(active_path);
		}
	}
	fp.reset();
	FileSystem::DeleteFilePath(deletecache_filename.c_str());
	const auto newfp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "wb");
	u16 new_filecount = new_paths.size();

	std::fwrite(&new_filecount, sizeof(new_filecount), 1, newfp.get());
	for (int i = 0; i < new_filecount; i++)
	{
		fputs(new_paths[i].c_str(), newfp.get());
		fputc('\0', newfp.get());
	}
	return true;
}*/


bool isTexLoaded(std::string modname)
{
	std::string tex_folder = GetTexReplacementDirectory(modname);
	std::string destination_folder = GetTexUnloadDirectory(modname);

	return FileSystem::DirectoryExists(tex_folder.c_str());
}

bool refreshPriorityList()
{
	std::vector<std::string> list = PriorityList::Get();
	for (std::string mod : list)
	{
		if (!ActiveMods::ContainsMod(mod) && !isTexLoaded(mod))
			PriorityRemove(mod);
	}
	return true;
}

bool ExtractTexFilesFromP2M(FILE* stream, std::string modname, std::vector<tex_file> tex_files)
{
	//copy texture files from the p2m
	std::string destination_folder = GetTexUnloadDirectory(modname);
	FileSystem::EnsureDirectoryExists(destination_folder.c_str(), true);

	for (tex_file file : tex_files)
	{
		std::fseek(stream, file.pos, SEEK_SET);

		std::string destination_file = Path::Combine(destination_folder, file.path);
	
		const auto newfp = FileSystem::OpenManagedCFile(destination_file.c_str(), "w+b");
		const auto new_stream = newfp.get();

		copyStream(stream, new_stream, file.size);
	}

	return true;
}
bool ExtractTexFilesFromP2M(std::string modname)
{
	std::string filename = GetP2MPathFromModName(modname);
	const auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "rb");
	p2m_header hd;
	std::vector<tex_file> tex_files = GetP2MTexFiles(fp.get(), hd);

	return ExtractTexFilesFromP2M(fp.get(), modname, tex_files);
}

/// <summary>
/// Copies mod file data from P2M to destination file
/// </summary>
/// <param name="stream"></param>
/// <param name="file"></param>
/// <returns></returns>
bool CopyModFileFromP2M(FILE* stream, mod_file file, std::string modname, bool enabled)
{
	std::string real_path;
	//if (file.tmp)
	//	real_path = Path::Combine(Path::Combine(EmuFolders::PTR2, "/TMP"), Path::GetFileName(file.path));
	//else
	if (enabled)
		real_path = GetEnabledModFilePath(file.path);
	else
		real_path = GetDisabledModFilePath(modname, file.path);

	std::string real_path_folder = std::string(Path::GetDirectory(real_path));

	std::fseek(stream, file.pos, SEEK_SET);

	FileSystem::EnsureDirectoryExists(real_path_folder.c_str(), true);
	const auto newfp = FileSystem::OpenManagedCFile(real_path.c_str(), "w+b");
	if (!newfp)
		return false;
	const auto new_stream = newfp.get();

	return copyStream(stream, new_stream, file.size);
}


//CURRENTLY DISABLED AS ITS BROKE AAAAAa:
/*
//sometimes loading a new mod isnt possible because the current active file is in use by the game
//instead of stopping the user, we can put the new mod in a TMP folder and tell the game it's there
//the current active file path is put in a deletecache file similar to activemodscache, and will be deleted
//as soon as the game has stopped using it (tries every frame), then the new mod is copied from TMP to the MOD folder
//if the file in TMP is locked up by the game, just extract it again from the p2m into MOD, and put the TMP
//file in the delete cache - this is a long workaround but should make the user experience more smooth
bool TryDeleteFiles()
{
	const std::string deletecache_filename(Path::Combine(EmuFolders::Cache, "deletefile.cache"));

	//cache file validation
	if (!FileSystem::FileExists(deletecache_filename.c_str()))
	{
		u16 data = 0;
		if (!FileSystem::WriteBinaryFile(deletecache_filename.c_str(), &data, 2))
		{
			//todo: error handling
		}
		return false;
	}

	auto fp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	std::vector<std::string> paths;
	//get filepaths in deletecache
	for (int i = 0; i < file_count; i++)
	{
		std::string active_path;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			return false;
		active_path = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += active_path.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		paths.push_back(active_path);
	}
	//for each file in deletecache
	std::vector<std::string> newpaths;

	for (std::string path : paths)
	{
		std::string filepath = Path::Combine(EmuFolders::PTR2, path);
		//first, try to delete
		if (!FileSystem::FileExists(filepath.c_str()))
		{
			continue;
		}
		if (!FileSystem::DeleteFilePath(filepath.c_str()))
		{
			//file must be still in use, keep in deletecache
			newpaths.push_back(path);
			continue;
		}

		//if deleted file was in MOD, check for replacement file in TMP
		if (Path::GetDirectory(path) == "MOD")
		{
			std::string pathTMP = Path::Combine(Path::Combine(EmuFolders::PTR2, "/TMP"), Path::GetFileName(path));
			std::string pathMOD = Path::Combine(GetPTR2ModDirectory(), Path::GetFileName(path));

			if (!FileSystem::FileExists(pathTMP.c_str()))
				continue;

			if (FileSystem::CopyFilePath(pathTMP.c_str(), pathMOD.c_str(), false))
			{
				if (!FileSystem::DeleteFilePath(pathTMP.c_str()))
				{
					//file was able to be copied from, but cannot be deleted
					//unsure if this would ever actually happen, but handle it anyway
					newpaths.push_back(pathTMP); //add to delete cache
				}
				//could make quicker by storing file type in delete cache file
				//so I can just check if its an ISO file in here and call PatcHElfPath directly
				std::string mod;
				ActiveMods::GetMod(path, mod);
				std::pair<std::string, std::string> entry(path, mod);
				mod_file file = GetP2MFileByEntry(entry);

				ApplyModFile(file);
			}
			else
			{
				//file is still in use, extract it from p2m file again instead
				std::string mod;
				ActiveMods::GetMod(path, mod);
				std::string modpath = Path::Combine(EmuFolders::PTR2InstalledMods, mod);

				std::pair<std::string, std::string> entry(path, mod);
				mod_file file = GetP2MFileByEntry(entry);

				const auto fp = FileSystem::OpenManagedCFile(modpath.c_str(), "rb");
				if (!fp)
					return false;

				//extract file to MODS folder
				CopyModFileFromP2M(fp.get(), file, true);

				ApplyModFile(file);

				newpaths.push_back(pathTMP); //add to delete cache
			}
		}
		//std::string name = Path::GetFileName(path);
	}
	fp.reset();
	FileSystem::DeleteFilePath(deletecache_filename.c_str());
	const auto newfp = FileSystem::OpenManagedCFile(deletecache_filename.c_str(), "wb");
	u16 new_filecount = newpaths.size();

	std::fwrite(&new_filecount, sizeof(new_filecount), 1, newfp.get());
	for (int i = 0; i < new_filecount; i++)
	{
		fputs(newpaths[i].c_str(), newfp.get());
		fputc('\0', newfp.get());
	}
	if (new_filecount == 0)
	{
		//flag bool
		files_to_delete = false;
	}
	return true;
}

*/

//UNUSED
/*
bool moveAllModFiles(FILE* stream, std::vector<mod_file> files)
{
	for (mod_file file : files)
	{
		CopyModFileFromP2M(stream, file);
	}
	return true;
}
*/

bool disableModEntry(std::string path)
{
	if (FileSystem::RenamePath(GetEnabledModFilePath(path).c_str(), GetDisabledActiveModFilePath(path).c_str()))
	{
		return false; //todo error handling
		//addDeleteEntry(path);
		//files_to_delete = true;
	}

	ActiveMods::RemoveEntry(path);
	refreshPriorityList();
	std::string mod_file = GetEnabledModFilePath(path);

	//could be unpatching unecessarily if its an int asset file
	//but proooobably still cheaper than fetching the actual file from p2m to check
	UnPatchELFPath(path); 
	return true;
}
bool installMod(std::string file_path)
{
	g_loading = file_path;
	std::string mod = Path::GetFileName(file_path).data();

	std::string p2m_dest_path = Path::Combine(EmuFolders::PTR2InstalledMods, mod);
	if (FileSystem::FileExists(p2m_dest_path.c_str()))
	{
		return false; //todo error mod with that name already exists...
	}
	//todo: available space on disk error handle or check
	
	//Copy .P2M to installed mods folder
	if (!FileSystem::CopyFilePath(file_path.c_str(), p2m_dest_path.c_str(), false))
		return false;

	const auto fp = FileSystem::OpenManagedCFile(p2m_dest_path.c_str(), "rb");

	p2m_header hd;

	//get files of mod
	std::vector<mod_file> files = GetP2MFiles(fp.get(), hd);

	/// extract mod files
	for (mod_file file : files)
	{
		//extract file to install folder at mods/[modname]
		CopyModFileFromP2M(fp.get(), file, mod, false);
	}

	//if there are texture replacements
	if (hd.tex_file_count > 0)
	{
		//apply tex files
		std::vector<tex_file> tex_files = GetP2MTexFiles(fp.get(), hd);

		//load tex files
		ExtractTexFilesFromP2M(fp.get(), mod, tex_files);
	}
	g_loading = "";
	return true;
}

bool toggleMod(std::string filename, bool enable)
{
	g_loading = filename;
	std::string mod = Path::GetFileName(filename).data();

	if (enable)
	{
		const auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "rb");
		//p2m_header hd;
		//get files of mod
		//std::vector<mod_file> files = GetP2MFiles(fp.get(), hd);

		std::string mod_dir = (std::string)Path::StripExtension(filename);
		//check activemods
		//if files exist, disable mods associated
		FileSystem::FindResultsArray results;
		FileSystem::FindFiles(mod_dir.c_str(), "*", FILESYSTEM_FIND_RECURSIVE | FILESYSTEM_FIND_FILES, &results);
		std::vector<std::pair<std::string, std::string>> entries;
		for (const FILESYSTEM_FIND_DATA& fd : results)
		{
			if (StringUtil::ContainsSubString(fd.FileName, "textures"))
			{
				continue;
			}
			//move file to ptr2/MOD folder
			std::string rel_path = Path::MakeRelative(fd.FileName, mod_dir);
			std::string dest_path = GetEnabledModFilePath(rel_path);
			std::string dest_dir = std::string(Path::GetDirectory(dest_path));
			FileSystem::EnsureDirectoryExists(dest_dir.c_str(), true);

			if (!FileSystem::RenamePath(fd.FileName.c_str(), dest_path.c_str()))
				return false; //todo error handling


			//std::string extension = StringUtil::toUpper(Path::GetExtension(rel_path));
			//if (extension != "WP2" && extension != "INT" && extension != "OLM" && extension != "XTR")
			if (!isIntAssetCheap(rel_path))
			{
				PatchELFPath(rel_path, false, false);
			}
			std::pair<std::string, std::string> entry(rel_path, mod);
			//ActiveMods::Add(entry);
			entries.push_back(entry);
		}

		/*
		for (mod_file file : files)
		{
			std::string existing_mod;
			if (ActiveMods::GetMod(file.path, existing_mod) == true)
			{

				//disableMod(existing_mod);
				disableModEntry(file.path);
			
			
				//if file in delete cache, mark it to be put in TMP
				//if (isInDeleteCache(file.path) && file.type != intAsset) //dont count int asset as TMP workaround isnt necessary
				//{
				//	file.tmp = true;
				//}
			
			}

			//move file to ptr2/MOD folder
			std::string dest_path = GetEnabledModFilePath(file.path);
			std::string dest_dir = std::string(Path::GetDirectory(dest_path));
			FileSystem::EnsureDirectoryExists(dest_dir.c_str(), true);
		
			if (!FileSystem::RenamePath(GetDisabledModFilePath(mod, file.path).c_str(), dest_path.c_str()))
				return false; //todo error handling

			ApplyModFile(file);
			//add to activemods
			std::pair<std::string, std::string> entry(file.path, mod);
			//ActiveMods::Add(entry);
			entries.push_back(entry);
		}
		*/

		ActiveMods::AddMultiple(entries);

		//add to prioritylist
		PriorityPush(mod, true);

		//if there are texture replacements
		//if (hd.tex_file_count > 0)
		//{
		LoadTexFiles(mod);
		//}
	}
	else
	{
		//get files of mod
		//std::vector<mod_file> files = GetP2MFilesByModName(modname);
		std::vector<std::string> paths;
		ActiveMods::GetPaths(mod, paths);

		//delete files from MOD folder
		for (std::string path : paths)
		{
			std::string full_path = GetEnabledModFilePath(path);

			if (!FileSystem::RenamePath(full_path.c_str(), GetDisabledModFilePath(mod, path).c_str()))
			{
				//addDeleteEntry(file.path);
				//files_to_delete = true;
			}

			//if (file.type == isoFile)
			if (!isIntAssetCheap(path))
			{
				UnPatchELFPath(path);
			}
		}

		//remove from activemods
		ActiveMods::RemoveMod(mod);
		//removeActiveModEntry(modname);

		//remove from prioritylist (also unloads texs
		PriorityRemove(mod);
	
	}
	g_loading = "";
	return true;
}

bool ApplySingleModEntry(std::string path, std::string modname)
{
	std::string modpath = GetP2MPathFromModName(modname);

	const auto fp = FileSystem::OpenManagedCFile(modpath.c_str(), "rb");
	//get files of mod
	p2m_header hd;
	parseP2MHeader(fp.get(), hd);

	mod_file file = GetP2MFile(fp.get(), hd, path);

	/*
	//if file in delete cache, mark it to be put in TMP
	if (isInDeleteCache(path))
	{
		file.tmp = true;
	}
	*/

	//extract file to MODS folder
	CopyModFileFromP2M(fp.get(), file, modname, true);

	ApplyModFile(file);

	return true;
	
}

//when priorities have been reordered, reload mod files
//should not be called if mods have been removed/added
bool RefreshMods() 
{
	std::vector<std::string> priorities = PriorityList::Get();
	int mod_count = priorities.size();
	
	//get list of mod entries after priorities
	std::map<std::string, std::string> entries; //we dont need order but map is probably better than unordered anyway
	for (int i = mod_count - 1; i > -1; i--) //iterate lowest priority first
	{
		std::string modname = priorities[i];
		std::vector<mod_file> files = GetP2MFilesByModName(modname);
		for (mod_file file : files)
		{
			entries.insert_or_assign(file.path, modname);
			//entries.push_back(entry);
		}
	}

	//remove entries already applied
	std::vector<std::pair<std::string, std::string>> cache = ActiveMods::GetAll();
	for (std::pair<std::string, std::string> entry : cache)
	{
		if (entry.first == "textureREPLACEMENTS") //leave texture replacement entries in
		{
			continue;
		}
		if ( entries[entry.first] == entry.second )
		{
			entries.erase(entry.first);
		}
		else //remove entries we are going to replace
			ActiveMods::RemoveEntry(entry.first);
	}
	refreshPriorityList();

	//convert map to vector of pairs, so we can access both key and value
	std::vector<std::pair<std::string, std::string>> new_cache;
	new_cache.assign(entries.begin(), entries.end());

	//apply our new entries
	for (std::pair<std::string, std::string> entry : new_cache)
	{
		ApplySingleModEntry(entry.first, entry.second);
		ActiveMods::Add(entry);
	}

	return true;
}

//for reordering prioritiy
bool AdjustModPriority(std::string modname, int newIndex)
{
	PriorityRemove(modname);
	PriorityAdd(modname, newIndex);
	return RefreshMods();
}

bool IsP2M(const char* filename, std::string& title, std::string& author, std::string& description, bool& enabled)
{
	const auto fp = FileSystem::OpenManagedCFile(filename, "rb");
	if (!fp)
		return false;

	std::string filename_dir = (std::string)Path::StripExtension(filename);

	//e.g if user dumped the p2m in the mods folder when (they shouldn't
	if (!FileSystem::DirectoryExists(filename_dir.c_str())) 
		return false;

	//e.g if user dumped the p2m in the mods folder and made an empty folder with the modname (they shouldn't
	if (FileSystem::DirectoryIsEmpty(filename_dir.c_str()) && !PriorityList::ContainsMod((std::string)Path::GetFileName(filename)))
		return false;

	char p2m_magic[4] = {0x50, 0x32, 0x4D, 0x11};

	p2m_header hd;

	if (!parseP2MHeader(fp.get(), hd))
		return false;
	if (std::strncmp(hd.p2m_magic, p2m_magic, 4) != 0)
		return false; //if magic not found, return false

	//read title, author, description

	//change this to fgets probably
	std::ifstream file;
	file.open(filename);
	if (!file.is_open()) return false;
	file.seekg(hd.meta_offset, file.beg);

	for (int i = 1; i <= 3; i++)
	{
		char letter;
		while (file.get(letter) && letter != '\0')
		{
			switch (i)
			{
				case 1:
					title += letter;
					break;
				case 2:
					author += letter;
					break;
				case 3:
					description += letter;
					break;
			}
		}
	}
	
	return true;
}

bool SaveStateBase::activeModsFreeze()
{

	//dont need to save the actual "activemods" object, save priority list instead

	if (!FreezeTag("activeMods"))
	{
		//workaround so that old save states still load
		Console.Error("PTR2PLUS Warning: You are trying to load a save state that wasn't made in ptr2plus, this may cause issues.");
		m_idx -= 32;
		//TODO: unload all mods here
		m_error = false;
		return false;
	}
	std::vector<std::string> current_priorities = PriorityList::Get();
	int current_count = current_priorities.size();
	std::vector<std::string> priorities;

	//if saving
	if (!IsLoading())
	{
		Freeze(current_count);
		for (int i = 0; i < current_count; i++)
		{
			FreezeString(current_priorities[i]);
		}
		return true;
	}
	else
	{
		Freeze(current_count);
		for (int i = 0; i < current_count; i++)
		{
			std::string priority = "";
			FreezeString(priority);
			priorities.push_back(priority);
		}

		//the following code uses a similar logic to RefreshMods():
		//filter mod files by priority using std::map
		//remove any files from the map that are already loaded,
		//so we dont waste time loading files we dont have to
		//
		//this does not apply to tex files

		int mod_count = priorities.size();

		//get list of mod entries after priorities
		std::map<std::string, std::string> entries; //we dont need order but map is probably better than unordered anyway
		for (int i = mod_count - 1; i > -1; i--) //iterate lowest priority first
		{
			std::string modname = priorities[i];
			std::vector<mod_file> files = GetP2MFilesByModName(modname);
			for (mod_file file : files)
			{
				entries.insert_or_assign(file.path, modname);
				//entries.push_back(entry);
			}
		}

		//remove entries already applied
		std::vector<std::pair<std::string, std::string>> cache = ActiveMods::GetAll();
		for (std::pair<std::string, std::string> entry : cache)
		{
			//if current file in new files
			if (entries[entry.first] == entry.second)
			{
				//remove from new files
				entries.erase(entry.first);
			}
			else //otherwise unload current file
			{
				//remove entries we are going to replace
				ActiveMods::RemoveEntry(entry.first);

				//taken from disableModEntry() -- because we dont want to unpatch elf
				std::string mod_file = GetEnabledModFilePath(entry.first);

				if (!FileSystem::RenamePath(mod_file.c_str(), GetDisabledActiveModFilePath(entry.first).c_str()))
				{
					//addDeleteEntry(entry.first);
					//files_to_delete = true;
				}
			}
		}

		//unload current texs and load new ones
		//we unload all even if they're still used because its just renaming paths
		//so not a performance hit for unloading and loading up again
		for (std::string mod : current_priorities)
		{
			UnloadTexFiles(mod);
		}
		PriorityList::Save(priorities);
		for (std::string mod : priorities)
		{
			if (!LoadTexFiles(mod))
			{
				ExtractTexFilesFromP2M(mod);
				LoadTexFiles(mod);
			}
		}

		//convert map to vector of pairs, so we can access both key and value
		std::vector<std::pair<std::string, std::string>> new_cache;
		new_cache.assign(entries.begin(), entries.end());

		//apply our new entries
		for (std::pair<std::string, std::string> entry : new_cache)
		{
			ApplySingleModEntry(entry.first, entry.second);
			ActiveMods::Add(entry);
		}
	}
	return true;
}