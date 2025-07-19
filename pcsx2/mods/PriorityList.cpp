#include <common/Path.h>
#include <pcsx2/Config.h>
#include <common/FileSystem.h>
#include <mods/PriorityList.h>
#include <common/StringUtil.h>
#include <common/Console.h>

static std::string PriorityList::GetFilename()
{
	return Path::Combine(EmuFolders::Cache, "modpriority.cache");
}

static bool PriorityList::CacheFileValidation()
{
	const std::string modspriority_filename(GetFilename());
	if (!FileSystem::FileExists(modspriority_filename.c_str()))
	{
		u16 data = 0;
		return FileSystem::WriteBinaryFile(modspriority_filename.c_str(), &data, 2);
	}
	return true;
}

//returns list of modnames in order from highest to lowest priority (0 first
std::vector<std::string> PriorityList::Get()
{
	CacheFileValidation(); //todo: error handling

	const std::string modspriority_filename(GetFilename());
	auto fp = FileSystem::OpenManagedSharedCFile(modspriority_filename.c_str(), "rb+", FileSystem::FileShareMode::DenyNone);
	if (!fp)
		Console.WriteLn("Error reading modspriority list - Is something else accessing it?");;
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		Console.WriteLn("Error reading modspriority list: bad file count");

	std::vector<std::string> modnames_list;
	for (int i = 0; i < file_count; i++)
	{
		std::string found_mod_name;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			Console.WriteLn("Error reading modspriority list: bad mod name");
		found_mod_name = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += found_mod_name.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		modnames_list.push_back(found_mod_name);
	}
	fp.reset();
	return modnames_list;
}
bool PriorityList::GetPriority(std::string modname, int& priority)
{
	if (!CacheFileValidation())
		return false;

	const std::string modspriority_filename(GetFilename());

	auto fp = FileSystem::OpenManagedSharedCFile(modspriority_filename.c_str(), "rb+", FileSystem::FileShareMode::DenyNone);
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		Console.WriteLn("Error reading modspriority list: bad file count");

	for (int i = 0; i < file_count; i++)
	{
		std::string found_mod_name;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			Console.WriteLn("Error reading modspriority list: bad mod name");
		found_mod_name = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += found_mod_name.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		if (StringUtil::compareNoCase(found_mod_name, modname))
		{
			priority = i;
			return true;
		}
	}
	return false;
}
bool PriorityList::GetModName(int priority, std::string& modname)
{
	if (!CacheFileValidation())
		return false;

	const std::string modspriority_filename(GetFilename());

	auto fp = FileSystem::OpenManagedSharedCFile(modspriority_filename.c_str(), "rb+", FileSystem::FileShareMode::DenyNone);
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		Console.WriteLn("Error reading modspriority list: bad file count");

	for (int i = 0; i < file_count; i++)
	{
		std::string found_mod_name;

		long off = ftell(fp.get());

		char buf[900];
		if (fgets(buf, sizeof(buf), fp.get()) == nullptr)
			Console.WriteLn("Error reading modspriority list: bad mod name");
		found_mod_name = buf;

		//fgets puts file offset at end for some reason, so reset it:
		off += found_mod_name.length() + 1;
		std::fseek(fp.get(), off, SEEK_SET);

		if (i == priority)
		{
			modname = found_mod_name;
			return true;
		}
	}
	return false;
}
bool PriorityList::Save(std::vector<std::string> priority_list)
{
	if (!CacheFileValidation())
		return false;

	const std::string modspriority_filename(GetFilename());

	const auto fp = FileSystem::OpenManagedSharedCFile(modspriority_filename.c_str(), "wb", FileSystem::FileShareMode::DenyNone);
	u16 file_count = priority_list.size();
	std::fwrite(&file_count, sizeof(file_count), 1, fp.get());
	for (std::string modname : priority_list)
	{
		fputs(modname.c_str(), fp.get());
		fputc('\0', fp.get());
	}
	return true;
}
bool PriorityList::Add(std::string modname, int index)
{
	std::vector<std::string> priority_list = Get();

	std::vector<std::string> new_priority_list;
	for (int i = 0; i < priority_list.size() + 1; i++)
	{
		if (i == index)
		{
			new_priority_list.push_back(modname);
		}
		else if (i > index)
		{
			new_priority_list.push_back(priority_list[i - 1]);
		}
		else //if i < index
		{
			new_priority_list.push_back(priority_list[i]);
		}
	}

	return Save(new_priority_list);
}
//default add to list at highest priority
bool PriorityList::Push(std::string modname)
{
	return Add(modname, 0);
}


bool PriorityList::Remove(std::string modname)
{
	std::vector<std::string> priority_list = Get();

	std::vector<std::string> new_priority_list;
	for (std::string list_modname : priority_list)
	{
		if (modname != list_modname)
		{
			new_priority_list.push_back(list_modname);
		}
	}

	return Save(new_priority_list);
}

bool PriorityList::ContainsMod(std::string modname)
{
	std::vector<std::string> priority_list = Get();
	for (std::string list_modname : priority_list)
	{
		if (modname == list_modname)
			return true;
	}
	return false;
}