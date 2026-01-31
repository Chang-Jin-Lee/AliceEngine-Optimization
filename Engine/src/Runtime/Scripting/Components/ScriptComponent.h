#pragma once

#include <memory>
#include <string>

namespace Alice
{
    class IScript;

    /// ???뷀떚?곗뿉 遺숇뒗 ?⑥씪 ?ㅽ겕由쏀듃 而댄룷?뚰듃?낅땲??
    /// - scriptName ? ?⑺넗由?由ы뵆?됱뀡???대쫫?낅땲??
    /// - instance ???ㅼ젣 ?ㅽ뻾?섎뒗 ?ㅽ겕由쏀듃 媛앹껜?낅땲??
    struct ScriptComponent
    {
        std::string                scriptName;
        std::unique_ptr<IScript>   instance;
        bool enabled { true };
        bool awoken  { false };
        bool started { false };
        bool wasEnabled { true };

        // .meta 湲곕낯媛믪쓣 ??踰덈쭔 二쇱엯?섍린 ?꾪븳 ?뚮옒洹몄엯?덈떎.
        bool defaultsApplied { false };
    };
}
