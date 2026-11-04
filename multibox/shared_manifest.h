#pragma once
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace shared_manifest {
inline std::string read(const std::filesystem::path& path) {
 std::ifstream file(path,std::ios::binary); if(!file) throw std::runtime_error("Cannot read Steam manifest");
 std::ostringstream out; out<<file.rdbuf(); auto text=out.str();
 if(text.empty()||text.size()>1024*1024) throw std::runtime_error("Invalid Steam manifest size"); return text;
}
inline std::string field(const std::string& text,const std::string& key) {
 std::regex pattern("\""+key+"\"\\s*\"([^\"]*)\""); std::smatch match;
 if(!std::regex_search(text,match,pattern)) throw std::runtime_error("Missing Steam manifest field: "+key);
 if(std::regex_search(match.suffix().str(),pattern)) throw std::runtime_error("Duplicate Steam manifest field: "+key);
 return match[1];
}
inline std::string config(const std::string& text,const std::string& key) {
 std::regex pattern("\""+key+"\"\\s*\\{([^{}]*)\\}"); std::smatch match;
 if(!std::regex_search(text,match,pattern)) throw std::runtime_error("Unsupported Steam manifest configuration");
 return std::regex_replace(match[1].str(),std::regex("\\s+"),"");
}
inline std::string prepare(const std::string& source,const std::string& target) {
 if(field(source,"appid")!="730"||field(target,"appid")!="730"||field(source,"StateFlags")!="4"||field(source,"UpdateResult")!="0"||field(source,"StagingSize")!="0")
  throw std::runtime_error("Update CS2 in the main Steam first; shared installation is not ready");
 if(field(source,"installdir")!=field(target,"installdir")) throw std::runtime_error("Shared CS2 install directories differ");
 for(auto key:{"UserConfig","MountedConfig"}) if(config(source,key)!=config(target,key))
  throw std::runtime_error("CS2 language/DLC configuration differs; automatic manifest sync refused");
 if(!std::regex_match(field(source,"buildid"),std::regex("[1-9][0-9]*"))) throw std::runtime_error("Invalid installed Steam build");
 auto result=source;
 for(auto key:{"LastOwner","LastPlayed","AutoUpdateBehavior","AllowOtherDownloadsWhileRunning"}) {
  auto value=field(target,key); if(!std::regex_match(value,std::regex("[0-9]+"))) throw std::runtime_error("Invalid Steam account setting");
  result=std::regex_replace(result,std::regex("\""+std::string(key)+"\"\\s*\"[^\"]*\""),"\""+std::string(key)+"\"\t\t\""+value+"\"");
 }
 return result;
}
// Caller must ensure the target sandbox is idle. Never grant it write access to shared game files.
inline void sync(const std::filesystem::path& mainSteam,const std::filesystem::path& isolatedSteam,
                 const std::filesystem::path& game,const std::filesystem::path& destination) {
 const auto source=mainSteam.parent_path()/L"steamapps"/L"appmanifest_730.acf";
 const auto base=isolatedSteam.parent_path()/L"steamapps";
 if(!std::filesystem::equivalent(base/L"common"/game.filename(),game)) throw std::runtime_error("Shared CS2 junction is not confirmed");
 if(!std::filesystem::equivalent(mainSteam.parent_path()/L"steamapps"/L"common"/game.filename(),game)) throw std::runtime_error("Main CS2 installation is not confirmed");
 auto current=std::filesystem::exists(destination)?destination:base/L"appmanifest_730.acf";
 const auto original=read(source), old=read(current);
 auto updated=prepare(original,old);
 if(field(original,"buildid")==field(old,"buildid")&&field(old,"StateFlags")=="4"&&field(old,"UpdateResult")=="0") return;
 std::filesystem::create_directories(destination.parent_path());
 auto backup=destination; backup+=L".before-shared-sync-"+std::to_wstring(GetTickCount64());
 std::filesystem::copy_file(current,backup);
 auto temporary=destination; temporary+=L".shared-sync.tmp";
 if(std::filesystem::exists(temporary)) throw std::runtime_error("Previous manifest sync was interrupted; inspect temporary file");
 {std::ofstream output(temporary,std::ios::binary); output<<updated; output.close(); if(!output) throw std::runtime_error("Cannot write Steam manifest");}
 if(read(source)!=original) {std::filesystem::remove(temporary); throw std::runtime_error("Main Steam updated during manifest sync; retry");}
 if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot commit Steam manifest");
}
}
