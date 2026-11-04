#pragma once
#include <windows.h>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cstring>
#include <QTemporaryDir>
namespace compatibility {
struct Result { bool allowed=false; QString reason; bool commandMetadataMatches=false; };
struct Pe { quint32 timestamp=0,size=0; };
inline bool pe(const QString& path,Pe& out){
 QFile f(path);if(!f.open(QIODevice::ReadOnly))return false;
 IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
 if(f.read(reinterpret_cast<char*>(&dos),sizeof(dos))!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1048576||!f.seek(dos.e_lfanew)||f.read(reinterpret_cast<char*>(&nt),sizeof(nt))!=sizeof(nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
 out={nt.FileHeader.TimeDateStamp,nt.OptionalHeader.SizeOfImage};return out.size>0;
}
inline QJsonObject json(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>65536)return {};return QJsonDocument::fromJson(f.readAll()).object();}
inline Result evaluate(const QJsonObject& profile,const QJsonObject& buttons,const QJsonObject& commands,int version,Pe client,Pe engine){
 const auto positive=[](const QJsonValue& v){return v.isDouble()&&v.toDouble()>0&&v.toDouble()==double(v.toInteger());};
 if(!positive(profile["clientVersion"])||!positive(profile["moduleTimestamp"])||!positive(profile["moduleSize"])||!QRegularExpression("^[0-9a-f]{64}$").match(profile["clientSha256"].toString()).hasMatch())return {false,QStringLiteral("Профиль совместимости отсутствует или повреждён")};
 if(version<=0||!client.size)return {false,QStringLiteral("Не удалось проверить файлы CS2; запуск навигатора заблокирован")};
 if(profile["clientVersion"].toInt()!=version)return {false,QStringLiteral("Навигатор несовместим: CS2 %1, профиль %2").arg(version).arg(profile["clientVersion"].toInt())};
 if(client.timestamp!=profile["moduleTimestamp"].toInteger()||client.size!=profile["moduleSize"].toInteger())return {false,QStringLiteral("Навигатор несовместим: изменён client.dll")};
 const auto digest=profile["clientSha256"].toString();
 if(!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(digest).hasMatch()||buttons["clientSha256"].toString()!=digest)return {false,QStringLiteral("Профиль фонового ввода отсутствует или несовместим")};
 // GameCommands is optional in MemoryInput. Its own runtime verifier still
 // refuses incompatible commands; a failed command profile must not disable
 // independently validated input in an already joined match.
 if(!engine.size||!positive(commands["timestamp"])||!positive(commands["size"])||engine.timestamp!=commands["timestamp"].toInteger()||engine.size!=commands["size"].toInteger())return {true,QStringLiteral("Профиль ввода совпал. Автовыбор команды, закупка и выход недоступны: профиль engine2.dll не подтверждён"),false};
 return {true,QStringLiteral("Метаданные профилей совпали; работа в игре не подтверждена"),true};
}
// Disk-only preflight. Never opens the game for writing or calls game functions.
// Existing runtime validation remains mandatory even when this succeeds.
inline Result inspect(const QString& game,const QString& assets){
 if(QFile::exists(QDir(assets).filePath(".profile-update.lock"))||QFile::exists(QDir(assets).filePath(".profile-update-pending.json")))
  return {false,QStringLiteral("Обновление профилей не завершено: дождитесь окончания или выполните восстановление")};
 QFile inf(QDir(game).filePath("game/csgo/steam.inf"));int version=0;
 if(inf.open(QIODevice::ReadOnly)&&inf.size()<65536){auto match=QRegularExpression("(?:^|[\\r\\n])ClientVersion=([0-9]+)(?:[\\r\\n]|$)").match(QString::fromUtf8(inf.readAll()));if(match.hasMatch())version=match.captured(1).toInt();}
 Pe c,e;pe(QDir(game).filePath("game/csgo/bin/win64/client.dll"),c);pe(QDir(game).filePath("game/bin/win64/engine2.dll"),e);
 return evaluate(json(QDir(assets).filePath("memory.json")),json(QDir(assets).filePath("input-buttons.json")),json(QDir(assets).filePath("game-commands.json")),version,c,e);
}
inline QString botLabel(const Result& result,const QString& observed){return result.allowed?observed:result.reason;}
inline int selfTest(){
 QJsonObject p{{"clientVersion",10},{"moduleTimestamp",100},{"moduleSize",200},{"clientSha256",QString(64,'a')}};
 QJsonObject b{{"clientSha256",QString(64,'a')}},e{{"timestamp",300},{"size",400}};
 auto check=[&](int v,Pe c,Pe engine){return evaluate(p,b,e,v,c,engine).allowed;};
 QTemporaryDir tx;
 if(!tx.isValid())return 12;
 for(const auto* name:{".profile-update.lock",".profile-update-pending.json"}){
  QFile file(tx.path()+"/"+name);if(!file.open(QIODevice::WriteOnly))return 13;file.write("test");file.close();
  auto blocked=inspect("missing-game",tx.path());
  if(blocked.allowed||!blocked.reason.contains(QStringLiteral("Обновление профилей")))return 14;
  file.remove();
 }
 if(!check(10,{100,200},{300,400}))return 1;
 if(check(11,{100,200},{300,400})||check(10,{101,200},{300,400})||check(10,{100,201},{300,400})||check(0,{100,200},{300,400}))return 2;
 auto optional=evaluate(p,b,e,10,{100,200},{301,400});
 if(!optional.allowed||optional.commandMetadataMatches||optional.reason.isEmpty())return 7;
 optional=evaluate(p,b,{},10,{100,200},{});
 if(!optional.allowed||optional.commandMetadataMatches)return 8;
 if(!evaluate(p,b,e,10,{100,200},{300,400}).commandMetadataMatches)return 9;
 p.remove("clientSha256");b.remove("clientSha256");if(check(10,{100,200},{300,400}))return 10;
 p["clientSha256"]="invalid";b["clientSha256"]="invalid";if(check(10,{100,200},{300,400}))return 11;
 p["clientSha256"]=QString(64,'a');
 b["clientSha256"]=QString(64,'b');if(check(10,{100,200},{300,400}))return 3;
 b["clientSha256"]=QString(64,'a');p.remove("moduleTimestamp");if(check(10,{100,200},{300,400}))return 4;
 if(inspect("missing-game","missing-assets").allowed)return 5;
 if(botLabel({false,"blocked"},"running")!="blocked"||botLabel({true,"metadata only"},"stopped")!="stopped")return 6;
 return 0;
}
}
