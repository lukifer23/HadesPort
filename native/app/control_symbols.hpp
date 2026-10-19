#pragma once
#include <array>
#include <string_view>
#include <utility>
namespace hades::input {
// Controls::SetupControlScheme RVA0x18c030..18d690: TextSymbol.assign on
// each real RemappableControl. Token dispatch: TextBox::ParseMarkup0x17f1c0
// -> ParseControlSymbol0x181120. Names are input identifiers, not key glyphs.
inline std::string_view controlForTextSymbol(std::string_view token){
    constexpr std::array<std::pair<std::string_view,std::string_view>,29> symbols{{
        {"UP","Up"},{"DN","Down"},{"LT","Left"},{"RT","Right"},
        {"A1","Attack1"},{"A2","Attack2"},{"A3","Attack3"},{"RU","Rush"},
        {"AL","AutoLock"},{"SL","Select"},{"CF","Confirm"},{"CT","ContinueText"},
        {"CN","Cancel"},{"ML","MenuLeft"},{"MR","MenuRight"},{"MU","MenuUp"},
        {"MD","MenuDown"},{"MX","MenuInfo"},{"SH","Shout"},{"AS","Assist"},
        {"AT","AdvancedTooltip"},{"I","Use"},{"G","Gift"},{"RL","Reload"},
        {"EM","Emote"},{"ST","Pause"},{"CX","Codex"},{"RF","ReportFeedback"},
        {"TS","TakeScreenshot"}
    }};
    for(const auto&[symbol,control]:symbols)if(symbol==token)return control;
    return {};
}
}
