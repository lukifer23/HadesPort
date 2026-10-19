// Read-only asset overlay for a focused headless boot-phase experiment.
#include "../host/file_assets.hpp"
#include "../app/lua_runtime.hpp"
#include <cstring>
#include <iostream>
#include <vector>

class MemoryAsset final : public hades::AssetFile {
    std::string bytes_; size_t cursor_ = 0;
public:
    explicit MemoryAsset(std::string bytes) : bytes_(std::move(bytes)) {}
    size_t size() const override { return bytes_.size(); }
    size_t remaining() const override { return bytes_.size() - cursor_; }
    size_t read(void* target,size_t count) override {
        count=std::min(count,remaining()); std::memcpy(target,bytes_.data()+cursor_,count); cursor_+=count; return count;
    }
};
class AuditedAssets final : public hades::AssetStore {
    hades::FileAssets files_; bool inactive_;
public:
    AuditedAssets(const char* root,bool inactive) : files_(root),inactive_(inactive) {}
    std::unique_ptr<hades::AssetFile> open(std::string_view name) override {
        auto file=files_.open(name);
        if(!file || (name!="Scripts/RoomManager.lua" && !(inactive_ && name=="DecodedMapObjects.lua"))) return file;
        std::string bytes(file->size(),'\0'); size_t at=0;
        while(at<bytes.size()) { const size_t read=file->read(bytes.data()+at,bytes.size()-at); if(!read)throw std::runtime_error("Audit asset read failed"); at+=read; }
        if(name=="DecodedMapObjects.lua") {
            bytes="local records=(function()\n"+bytes+"\nend)(); for _,record in ipairs(records) do record.active=false end; return records\n";
        } else {
            // Instrumentation is appended only to the compiler's memory view.
            // Original on-disk scripts and production implementation are unchanged.
            bytes+=R"LUA(
local function canonical(value,seen)
  local kind=type(value)
  if kind=='string' then return string.format('%q',value) end
  if kind~='table' then return kind..':'..tostring(value) end
  if seen[value] then return '<cycle>' end
  seen[value]=true
  local keys={}; for key in pairs(value) do keys[#keys+1]=key end
  table.sort(keys,function(a,b) if type(a)~=type(b) then return type(a)<type(b) end; return tostring(a)<tostring(b) end)
  local out={'{'}
  for _,key in ipairs(keys) do out[#out+1]=canonical(key,seen)..'='..canonical(value[key],seen)..';' end
  out[#out+1]='}';seen[value]=nil;return table.concat(out)
end
for _,name in ipairs({'GetIdsByType','GetMapThings','GetLocation','SpawnObstacle','Destroy','GetIds','GetInactiveIds','GetInactiveIdsByType'}) do
  local original=_G[name]
  _G[name]=function(args)
    print('PHASE_API '..name..' '..canonical(args,{}))
    return original(args)
  end
end
local originalDispatch=dispatch
dispatch=function(callback,args)
  originalDispatch(callback,args)
  print('PHASE_STATE '..canonical({GameState=GameState,CurrentRun=CurrentRun},{}))
end
)LUA";
        }
        return std::make_unique<MemoryAsset>(std::move(bytes));
    }
};
int main(int argc,char** argv) {
    if(argc!=3) return 64;
    AuditedAssets assets(argv[1],std::strcmp(argv[2],"inactive")==0);
    ScriptRuntime runtime(&assets);runtime.initialize();const bool booted=runtime.bootRoom();
    std::cout<<runtime.stateSummary()<<" booted="<<booted<<'\n';
    return booted?0:2;
}
