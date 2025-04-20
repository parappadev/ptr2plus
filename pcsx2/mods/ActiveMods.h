#pragma once

namespace ActiveMods
{
	static std::string GetFilename();

	static bool CacheFileValidation();

	bool ReadOne(FILE* stream, std::pair<std::string, std::string>& entry);

	bool ContainsMod(const std::string mod);

	bool GetPaths(const std::string mod, std::vector<std::string>& paths);
	bool GetMod(const std::string path, std::string& mod);
	std::vector<std::pair<std::string, std::string>> GetAll();

	bool RemoveEntry(std::string path);
	bool RemoveMod(std::string modname);

	bool AddMultiple(std::vector<std::pair<std::string, std::string>> entries);
	bool Add(std::pair<std::string, std::string> entry);

	bool Save(std::vector<std::pair<std::string, std::string>> activeModCache);
}
