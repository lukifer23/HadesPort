#pragma once
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace hades {
// Exact bounded translation of the matching 2020 EngineWin64s.dll
// XXH_INLINE_XXH3_64bits RVAadba0, VAadbaa..adef1. No assumption that an
// installed/current xxhash version matches this shipped algorithm. Native
// secret bytes read from RVA4c9e00. Longer keys are explicitly unsupported.
inline bool nativeEffectNameHash(std::string_view input,uint64_t& result,std::string& error){
    constexpr uint64_t secret[]{0xbe4ba423396cfeb8ULL,0x1cad21f72c81017cULL,0xdb979083e96dd4deULL,0x1f67b3b7a4a44072ULL,
        0x78e5c0cc4ee679cbULL,0x2172ffcc7dd05a82ULL,0x8e2443f7744608b8ULL,0x4c263a81e69035e0ULL,
        0xcb00c391bb52283cULL,0xa32e531b8b65d088ULL,0x4ef90da297486471ULL,0xd8acdea946ef1938ULL,
        0x3f349ce33f76faa8ULL,0x1d4f0bc7c7bbdcf9ULL,0x3159b4cd4be0518aULL,0x647378d9c97e9fc8ULL};
    const size_t n=input.size();if(n>128){error="Original bounded EffectName hash supports at most128 source UTF8 bytes";return false;}
    auto read=[&](size_t at,size_t count){uint64_t x=0;for(size_t i=0;i<count;++i)x|=uint64_t(static_cast<uint8_t>(input[at+i]))<<(8*i);return x;};
    auto fold=[](uint64_t a,uint64_t b){const __uint128_t p=__uint128_t(a)*b;return uint64_t(p)^uint64_t(p>>64);};
    auto avalanche=[](uint64_t x){x^=x>>37;x*=0x165667919e3779f9ULL;return x^(x>>32);};
    auto avalanche64=[](uint64_t x){x^=x>>33;x*=0xc2b2ae3d27d4eb4fULL;x^=x>>29;x*=0x165667b19e3779f9ULL;return x^(x>>32);};
    if(n>16){uint64_t acc=n*0x9e3779b185ebca87ULL;
        auto mix=[&](size_t at,size_t word){return fold(read(at,8)^secret[word],read(at+8,8)^secret[word+1]);};
        if(n>32){if(n>64){if(n>96){acc+=mix(48,12);acc+=mix(n-64,14);}acc+=mix(32,8);acc+=mix(n-48,10);}acc+=mix(16,4);acc+=mix(n-32,6);}
        acc+=mix(0,0);acc+=mix(n-16,2);result=avalanche(acc);
    }else if(n>8){const uint64_t lo=read(0,8)^secret[3]^secret[4],hi=read(n-8,8)^secret[5]^secret[6];
        uint64_t swapped=0;for(unsigned i=0;i<8;++i)swapped|=((lo>>(8*i))&255)<<(8*(7-i));
        result=avalanche(uint64_t(n)+swapped+hi+fold(lo,hi));
    }else if(n>=4){uint64_t value=((read(0,4)<<32)+read(n-4,4))^secret[1]^secret[2];
        value^=std::rotl(value,49)^std::rotl(value,24);value*=0x9fb21c651e98df25ULL;
        value^=(value>>35)+n;value*=0x9fb21c651e98df25ULL;result=value^(value>>28);
    }else if(n){const uint64_t combined=(read(0,1)<<16)|(read(n>>1,1)<<24)|(uint64_t(n)<<8)|read(n-1,1);
        result=avalanche64(combined^uint32_t(secret[0])^uint32_t(secret[0]>>32));
    }else result=avalanche64(secret[7]^secret[8]);
    error.clear();return true;
}
}
