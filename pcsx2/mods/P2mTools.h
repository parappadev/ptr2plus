#pragma once

#include <string>
#include <vector>

extern bool files_to_delete;
extern std::string g_loading;
extern bool TryDeleteFiles();
extern bool ContainsMod(const std::string mod);
extern bool Get(const std::string path, std::string& mod);
extern bool Get(const std::string mod, std::vector<std::string>& paths);

extern bool removeActiveModEntry(std::string mod);
extern bool addActiveModEntry(std::string filename, std::vector<std::string> paths);

extern bool ApplySingleModEntry(std::string path, std::string modname);
extern bool addDeleteEntry(std::string path);
extern bool ExtractTexFilesFromP2M(std::string mod);

extern std::vector<std::string> Get();
extern bool AdjustModPriority(std::string modname, int newIndex);

extern bool StartUpApplyActiveMods();
extern bool toggleMod(std::string filename, bool state);
extern bool installMod(std::string filename);

extern bool IsP2M(const char* filename, std::string& title, std::string& author, std::string& description, bool& enabled);