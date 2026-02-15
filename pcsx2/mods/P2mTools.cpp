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

bool IsP2M(const char* filename);

bool files_to_delete;
std::string g_loading = "";
#pragma pack(push, 1)

//backwards compatiblity - not enabled
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

struct p2m_header //v3, v4
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
	return Path::Combine(Path::Combine(EmuFolders::Textures, "/SCPS_150/replacements"), folder_name);
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
	std::string replacementsPath = Path::Combine(EmuFolders::Textures, "/SCPS_150/replacements");
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

	//std::string unloadedPath = Path::Combine(EmuFolders::Textures, "/SCPS_150/unloaded");
	//FileSystem::EnsureDirectoryExists(unloadedPath.c_str(), true);
	FileSystem::RenamePath(source_folder.c_str(), destination_folder.c_str());
	//todo error handling
	
	//if all textures are unloaded, turn texture replacements off
	if (FileSystem::DirectoryIsEmpty(Path::Combine(EmuFolders::Textures, "/SCPS_150/replacements").c_str()))
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
	if (p2m_magic != 290271824)
	{ //its not a p2m file!!!
		Host::AddKeyedOSDMessage("bad_p2m_warning", "Invalid P2M file.", Host::OSD_WARNING_DURATION);
		return false;
	}
	u16 p2m_version;
	std::fread(&p2m_version, 2, 1, stream);

	if (p2m_version < 4) //backwards compatibility
	{
		Host::AddKeyedOSDMessage("bad_p2m_warning", "Unsupported P2M version. Please use a v4 P2M.", Host::OSD_WARNING_DURATION);
		return false;

		//p2m v3 and below stored strings as null terminated instead of length prefixed, which resulted
		//in much worse performance on reads. Unsupported for this reason.

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
	else if (p2m_version > 4) //FUTURE! FUUTUUURE! FUUUUTUUUURE!
	{
		Host::AddKeyedOSDMessage("bad_p2m_warning", "Unsupported P2M version - too new. Consider updating, or use a v4 P2M.", Host::OSD_WARNING_DURATION);
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
	u8 len;
	for (int i = 0; i < index; i++)
	{
		std::fseek(stream, hd.path_offset + off, SEEK_SET);
		std::fread(&len, sizeof(len), 1, stream);
		off += len + 1;
	}
	std::fseek(stream, hd.path_offset + off, SEEK_SET);
	std::fread(&len, sizeof(len), 1, stream);
	std::string path(len, '\0');
	std::fread(&path[0], 1, len, stream);

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
	
	std::vector<mod_file> files;
	if (!parseP2MHeader(stream, hd))
		return files;

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

	int i = 0;
	u8 len;
	for (i; i < hd.file_count; i++)
	{
		std::fread(&len, sizeof(len), 1, stream);
		std::string found_path(len, '\0');
		std::fread(&found_path[0], 1, len, stream);
		if (StringUtil::compareNoCase(found_path, path)) break;
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
		u8 len;
		for (int i2 = 0; i2 < i; i2++)
		{
			std::fseek(stream, hd.tex_path_offset + off, SEEK_SET);
			std::fread(&len, sizeof(len), 1, stream);
			off += len + 1;
		}
		std::fseek(stream, hd.tex_path_offset + off, SEEK_SET);
		std::fread(&len, sizeof(len), 1, stream);
		std::string path(len, '\0');
		std::fread(&path[0], 1, len, stream);

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

//UNUSED
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

bool StartUpApplyActiveMods()
{
	std::vector<std::pair<std::string, std::string>> activeModCache = ActiveMods::GetFromFile();
	int file_count = activeModCache.size();

	//try and load textures for all mods
	for (std::string modname : PriorityList::Get())
		LoadTexFiles(modname);

	return true;
}

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

bool installMod(std::string file_path)
{
	if (file_path.length() == 0)
		return false;

	g_loading = file_path;
	std::string mod = Path::GetFileName(file_path).data();

	std::string p2m_dest_path = Path::Combine(EmuFolders::PTR2InstalledMods, mod);
	if (FileSystem::FileExists(p2m_dest_path.c_str()))
	{
		if ( IsP2M(file_path.c_str()) )
		{
			Host::AddKeyedOSDMessage("bad", "Error: A mod with that name has already been installed.", Host::OSD_WARNING_DURATION);
		}
		g_loading = "";
		return false;
	}
	//todo: available space on disk error handle or check
	
	//Copy .P2M to installed mods folder
	if (!FileSystem::CopyFilePath(file_path.c_str(), p2m_dest_path.c_str(), false))
	{
		Host::AddKeyedOSDMessage("bad_copy", "Error copying P2M to /mods, does it already exist?", Host::OSD_WARNING_DURATION);
		g_loading = "";
		return false;
	}

	const auto fp = FileSystem::OpenManagedCFile(p2m_dest_path.c_str(), "rb");

	p2m_header hd;

	//get files of mod
	std::vector<mod_file> files = GetP2MFiles(fp.get(), hd);
	if (files.empty())
	{
		g_loading = "";
		return false;
	}
		
	/// extract mod files
	for (mod_file file : files)
	{
		//extract file to install folder at mods/[modname]
		if (!CopyModFileFromP2M(fp.get(), file, mod, false))
		{
			Host::AddKeyedOSDMessage("bad_p2m_extract", "Error extracting file from P2M to /mods, does it already exist?", Host::OSD_WARNING_DURATION);
			g_loading = "";
			return false;
		}
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
		//get files of mod
		const auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "rb");

		std::string mod_dir = (std::string)Path::StripExtension(filename);
		FileSystem::FindResultsArray results;
		FileSystem::FindFiles(mod_dir.c_str(), "*", FILESYSTEM_FIND_RECURSIVE | FILESYSTEM_FIND_FILES, &results);
		std::vector<std::pair<std::string, std::string>> entries;
		for (const FILESYSTEM_FIND_DATA& fd : results)
		{
			if (StringUtil::ContainsSubString(fd.FileName, "textures"))
			{
				continue;
			}
			
			std::string rel_path = Path::MakeRelative(fd.FileName, mod_dir);

			std::pair<std::string, std::string> entry(rel_path, mod);
			//ActiveMods::Add(entry);
			entries.push_back(entry);
		}

		//add to active mods list
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
		std::vector<std::string> paths;
		ActiveMods::GetPaths(mod, paths);

		//remove from active mods list
		ActiveMods::RemoveMod(mod);

		//remove from prioritylist (also unloads texs
		PriorityRemove(mod);
	
	}
	g_loading = "";
	return true;
}

//when priorities have been reordered, reset active mods
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
		}
	}

	//set our new active mod entries and refresh priority list - for ui and textures
	ActiveMods::Set(std::vector<std::pair<std::string, std::string>>(entries.begin(), entries.end()));
	refreshPriorityList();

	return true;
}

//for reordering prioritiy
bool AdjustModPriority(std::string modname, int newIndex)
{
	PriorityRemove(modname);
	PriorityAdd(modname, newIndex);
	return RefreshMods();
}
bool IsP2M(const char* filename)
{
	const auto fp = FileSystem::OpenManagedCFile(filename, "rb");
	if (!fp)
	{
		Host::AddKeyedOSDMessage("bad_open", "Error opening: " + std::string(filename), Host::OSD_WARNING_DURATION);
		return false;
	}

	std::string filename_dir = (std::string)Path::StripExtension(filename);

	//e.g if user dumped the p2m in the mods folder when (they shouldn't
	if (!FileSystem::DirectoryExists(filename_dir.c_str()))
	{
		Host::AddKeyedOSDMessage("bad_open", "Error: p2m file already exists: " + std::string(filename), Host::OSD_WARNING_DURATION);
		return false;
	}

	//e.g if user dumped the p2m in the mods folder and made an empty folder with the modname (they shouldn't
	if (FileSystem::DirectoryIsEmpty(filename_dir.c_str()) && !PriorityList::ContainsMod((std::string)Path::GetFileName(filename)))
	{
		Host::AddKeyedOSDMessage("bad_open", "Error: invalid install already in " + filename_dir, Host::OSD_WARNING_DURATION);
		return false;
	}

	p2m_header hd;

	if (!parseP2MHeader(fp.get(), hd))
		return false;

	return true;
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

	//change this to fgets probably. no dont lol fgets has terrible perforamnce. ifstream is fine and good.
	std::ifstream file;
	file.open(filename);
	if (!file.is_open()) return false;
	file.seekg(hd.meta_offset, file.beg);

	u8 len;
	file.read((char*)&len, sizeof(len));
	title = std::string(len, '\0');
	file.read(&title[0], len);

	file.read((char*)&len, sizeof(len));
	author = std::string(len, '\0');
	file.read(&author[0], len);

	file.read((char*)&len, sizeof(len));
	description = std::string(len, '\0');
	file.read(&description[0], len);
	
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
		//filter mod files by priority and reset active mods

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

		ActiveMods::Set(std::vector<std::pair<std::string, std::string>>(entries.begin(), entries.end()));

	}
	return true;
}