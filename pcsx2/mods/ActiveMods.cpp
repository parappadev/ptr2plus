#include <common/FileSystem.h>
#include <common/Path.h>
#include <pcsx2/Config.h>
#include <common/StringUtil.h>
#include <mods/ActiveMods.h>

static std::string ActiveMods::GetFilename()
{
	return Path::Combine(EmuFolders::Cache, "activemods.cache");
}

static bool ActiveMods::CacheFileValidation()
{
	const std::string activemods_filename(GetFilename());
	if (!FileSystem::FileExists(activemods_filename.c_str()))
	{
		u16 data = 0;
		return FileSystem::WriteBinaryFile(activemods_filename.c_str(), &data, 2);
	}
	return true;
}

bool ActiveMods::ContainsMod(const std::string mod)
{
	for (std::pair<std::string, std::string> entry : activeModCache)
	{
		if (StringUtil::compareNoCase(entry.second, mod))
		{
			return true;
		}
	}
	return false;
}

bool ActiveMods::GetPaths(const std::string mod, std::vector<std::string>& paths)
{
	for (std::pair<std::string, std::string> entry : activeModCache)
	{
		if (StringUtil::compareNoCase(entry.second, mod))
		{
			paths.push_back(entry.first);
		}
	}
	return true;
}

bool ActiveMods::GetMod(const std::string path, std::string& mod)
{
	for (std::pair<std::string, std::string> entry : activeModCache)
	{
		if (StringUtil::compareNoCase(entry.first, path))
		{
			mod = entry.second;
			return true;
		}
	}
	return false;
}

std::vector<std::pair<std::string, std::string>> ActiveMods::Get()
{
	return activeModCache;
}

std::vector<std::pair<std::string, std::string>> ActiveMods::GetFromFile()
{
	CacheFileValidation(); //todo: error handling
	const std::string activemods_filename(GetFilename());
	auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb+");
	auto stream = fp.get();
	u16 file_count;

	std::fread(&file_count, sizeof(u16), 1, stream);

	activeModCache.clear();

	for (int i = 0; i < file_count; i++)
	{
		std::pair<std::string, std::string> entry;
		ReadOne(fp.get(), entry);

		activeModCache.push_back(entry);
	}
	return activeModCache;
}

bool ActiveMods::RemoveEntry(std::string path)
{
	std::erase_if(activeModCache, [path](std::pair<std::string, std::string> x) {
		if (StringUtil::compareNoCase(x.first, path))
			return true;
		else
			return false;
	});

	Save(activeModCache);
	return true;
}

bool ActiveMods::RemoveMod(std::string modname)
{
	std::erase_if(activeModCache, [modname](std::pair<std::string, std::string> x) {
		if (StringUtil::compareNoCase(x.second, modname))
			return true;
		else
			return false;
	});

	Save(activeModCache);
	return true;
}

bool ActiveMods::AddMultiple(std::vector<std::pair<std::string, std::string>> entries)
{
	for (std::pair<std::string, std::string> entry : entries)
	{
		activeModCache.push_back(entry);
	}
	Save(activeModCache);
	return true;
}

bool ActiveMods::Add(std::pair<std::string, std::string> entry)
{
	activeModCache.push_back(entry);

	Save(activeModCache);
	return true;
}


//read a single activemod file chunk (path, modname) and return
bool ActiveMods::ReadOne(FILE* stream, std::pair<std::string, std::string>& entry)
{
	u8 len;

	std::fread(&len, sizeof(len), 1, stream);
	std::string path(len, '\0');
	std::fread(&path[0], 1, len, stream);
	entry.first = path;

	std::fread(&len, sizeof(len), 1, stream);
	std::string mod(len, '\0');
	std::fread(&mod[0], 1, len, stream);
	entry.second = mod;

	return true;
}



//path, modname
bool ActiveMods::Save(std::vector<std::pair<std::string, std::string>> activeModCache)
{
	if (!CacheFileValidation())
		return false;

	//std::sort(activeModCache.begin(), activeModCache.end(), [](auto& left, auto& right) {
	//	return left.first > right.first;
	//});

	const std::string activemods_filename(GetFilename());

	FileSystem::DeleteFilePath(activemods_filename.c_str());

	auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "wb");

	u16 file_count = activeModCache.size();

	std::fwrite(&file_count, sizeof(file_count), 1, fp.get());
	for (int i = 0; i < file_count; i++)
	{
		u8 len = activeModCache[i].first.length();
		std::fwrite(&len, sizeof(len), 1, fp.get());
		fputs(activeModCache[i].first.c_str(), fp.get());

		len = activeModCache[i].second.length();
		std::fwrite(&len, sizeof(len), 1, fp.get());
		fputs(activeModCache[i].second.c_str(), fp.get());
	}
	return true;
}

