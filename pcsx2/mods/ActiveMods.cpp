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
	if (FileSystem::FileExists(activemods_filename.c_str()))
		return true;
	else
	{
		u16 data = 0;
		return FileSystem::WriteBinaryFile(activemods_filename.c_str(), &data, 2);
	}
}
	/* This would be quicker
bool ActiveMods::ContainsMod(const std::string mod)
{
	const std::string activemods_filename(GetFilename());

	const auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	//int file_off = sizeof(file_count);
	for (int i = 0; i < file_count; i++)
	{
		std::pair<std::string, std::string> entry;
		ReadOne(fp.get(), entry);

		if (entry.second == mod)
		{
			return true;
		}
	}
	return false;
}
*/
bool ActiveMods::ContainsMod(const std::string mod)
{
	std::vector<std::string> paths;
	GetPaths(mod, paths);
	if (paths.size() > 0)
		return true;
	else
		return false;
}
bool ActiveMods::GetPaths(const std::string mod, std::vector<std::string>& paths)
{
	if (!CacheFileValidation())
		return false;

	const std::string activemods_filename(GetFilename());

	const auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	for (int i = 0; i < file_count; i++)
	{
		std::pair<std::string, std::string> entry;
		ReadOne(fp.get(), entry);

		if(StringUtil::compareNoCase(entry.second, mod))
		{
			paths.push_back(entry.first);
		}
	}
	return true;
}

bool ActiveMods::GetMod(const std::string path, std::string& mod)
{
	if (!CacheFileValidation())
		return false;

	const std::string activemods_filename(GetFilename());

	const auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	for (int i = 0; i < file_count; i++)
	{
		std::pair<std::string, std::string> entry;
		ReadOne(fp.get(), entry);

		if (StringUtil::compareNoCase(entry.first, path))
		{
			mod = entry.second;
			return true;
		}
	}
	return false;
}

std::vector<std::pair<std::string, std::string>> ActiveMods::GetAll()
{
	CacheFileValidation(); //todo: error handling
	const std::string activemods_filename(GetFilename());
	auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb+");
	auto stream = fp.get();
	u16 file_count;

	std::fread(&file_count, sizeof(u16), 1, stream);

	std::vector<std::pair<std::string, std::string>> activeModCache;
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
	std::vector<std::pair<std::string, std::string>> activeModCache = GetAll();
	std::vector<std::pair<std::string, std::string>> newActiveModCache;

	int length = activeModCache.size();
	for (int i = 0; i < length; i++){
		if (!StringUtil::compareNoCase(activeModCache[i].first, path))
		{
			newActiveModCache.push_back(activeModCache[i]);
		}
	}
	Save(newActiveModCache);
	return true;
}

bool ActiveMods::RemoveMod(std::string modname)
{
	std::vector<std::pair<std::string, std::string>> activeModCache = GetAll();
	std::vector<std::pair<std::string, std::string>> newActiveModCache;

	int length = activeModCache.size();
	for (int i = 0; i < length; i++)
	{
		if (!StringUtil::compareNoCase(activeModCache[i].second, modname))
		{
			newActiveModCache.push_back(activeModCache[i]);
		}
	}
	Save(newActiveModCache);
	return true;
}

bool ActiveMods::AddMultiple(std::vector<std::pair<std::string, std::string>> entries)
{
	std::vector<std::pair<std::string, std::string>> activeModCache = GetAll();
	for (std::pair<std::string, std::string> entry : entries)
	{
		activeModCache.push_back(entry);
	}
	Save(activeModCache);
	return true;
}

bool ActiveMods::Add(std::pair<std::string, std::string> entry)
{
	std::vector<std::pair<std::string, std::string>> activeModCache = GetAll();
	activeModCache.push_back(entry);
	Save(activeModCache);
	return true;
}
/*  This would be quicker
bool Add(std::string filename, std::vector<std::string> paths)
{
	const std::string activemods_filename(GetFilename());

	const auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "rb+");
	u16 file_count;
	if (std::fread(&file_count, 2, 1, fp.get()) != 1)
		return false;

	std::fseek(fp.get(), 0, SEEK_END);
	for (std::string path : paths)
	{
		fputs(path.c_str(), fp.get());
		fputc('\0', fp.get());
		fputs(filename.c_str(), fp.get());
		fputc('\0', fp.get());

		file_count++;
	}
	
	std::fseek(fp.get(), 0, SEEK_SET);
	std::fwrite(&file_count, sizeof(file_count), 1, fp.get());

	return true;
}
*/


//read a single activemod file chunk (path, modname) and return
bool ActiveMods::ReadOne(FILE* stream, std::pair<std::string, std::string>& entry)
{
	long off = ftell(stream);

	char buf[900];
	if (fgets(buf, sizeof(buf), stream) == nullptr)
		return false;
	entry.first = buf;

	//fgets puts file offset at end for some reason, so reset it:
	off += entry.first.length() + 1;
	std::fseek(stream, off, SEEK_SET);

	char buf2[900];
	if (fgets(buf2, sizeof(buf2), stream) == nullptr)
		return false;
	entry.second = buf2;

	std::fseek(stream, off + entry.second.length() + 1, SEEK_SET);

	return true;
}



//path, modname
bool ActiveMods::Save(std::vector<std::pair<std::string, std::string>> activeModCache)
{
	if (!CacheFileValidation())
		return false;
	const std::string activemods_filename(GetFilename());

	FileSystem::DeleteFilePath(activemods_filename.c_str());

	auto fp = FileSystem::OpenManagedCFile(activemods_filename.c_str(), "wb");

	u16 file_count = activeModCache.size();

	std::fwrite(&file_count, sizeof(file_count), 1, fp.get());
	for (int i = 0; i < file_count; i++)
	{
		fputs(activeModCache[i].first.c_str(), fp.get());
		fputc('\0', fp.get());
		fputs(activeModCache[i].second.c_str(), fp.get());
		fputc('\0', fp.get());
	}
	return true;
}

