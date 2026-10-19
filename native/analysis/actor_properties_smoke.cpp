#include "../host/file_assets.hpp"
#include "../app/actor_animation.hpp"
#include <iostream>
class Assets final:public hades::AssetStore {
    hades::FileAssets bank_,properties_;
public:
    bool expose=true;
    Assets(const char* bank,const char* properties):bank_(bank),properties_(properties){}
    std::unique_ptr<hades::AssetFile> open(std::string_view name)override {
        if(name=="actor-properties.bin")return expose?properties_.open(name):nullptr;
        return bank_.open(name);
    }
};
int main(int argc,char** argv){try{
    if(argc!=3&&argc!=4)throw std::runtime_error("actor_properties_smoke BANK PROPERTIES [v2|v3]");
    Assets assets(argv[1],argv[2]);hades::ActorAnimationBank bank;bank.load(assets);
    size_t slides=0,books=0;
    for(size_t i=0;i<bank.clips().size();++i){
        if(bank.exactOwnerImmobile(i)!=false||bank.exactBlockNewOwnerAnims(i)!=false)throw std::runtime_error("Original primary default mismatch");
        if(argc==4?bank.exactAnimationZ(i)!=100.f:bank.exactAnimationZ(i).has_value())throw std::runtime_error("AirborneZ explicit native100 default/unknown mismatch");
        if(argc==4&&std::string(argv[3])=="v3"&&(bank.exactOwnerInvulnerable(i)!=false||bank.exactOwnerNoArmorCoverage(i)!=false||bank.exactOwnerNoInvulnerableCoverage(i)!=false))throw std::runtime_error("Original primary HAP3 default mismatch");
        for(size_t s=0;s<bank.clips()[i].slides.size();++s){
            if(bank.exactAirborne(i,s)!=false)throw std::runtime_error("Original airborne default mismatch");++slides;
        }
        books+=bool(bank.clips()[i].flags&hades::ActorBook);
    }
    assets.expose=false;bank.load(assets);
    for(size_t i=0;i<bank.clips().size();++i){
        if(bank.exactOwnerImmobile(i).has_value()||bank.exactBlockNewOwnerAnims(i).has_value())throw std::runtime_error("Missing companion coerced unknown");
        if(bank.exactOwnerInvulnerable(i).has_value()||bank.exactOwnerNoArmorCoverage(i).has_value()||bank.exactOwnerNoInvulnerableCoverage(i).has_value())throw std::runtime_error("Missing HAP3 companion coerced unknown");
        if(bank.exactAnimationZ(i).has_value())throw std::runtime_error("Missing AirborneZ companion coerced unknown");
        if(bank.clips()[i].flags&hades::ActorBook){if(bank.exactAirborne(i,0)!=false)throw std::runtime_error("Book native type gate missing");}
        else if(bank.exactAirborne(i,0).has_value())throw std::runtime_error("Missing SlideData companion coerced unknown");
    }
    std::cout<<"{\"result\":\"passed\",\"clips\":"<<bank.clips().size()<<",\"source_frame_bindings\":"<<slides<<",\"book_clips\":"<<books<<",\"absent_companion_unknown_preserved\":true,\"native_world_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
