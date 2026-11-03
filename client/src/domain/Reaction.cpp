#include "domain/Reaction.h"

namespace mini {

const QStringList &defaultReactions()
{
    static const QStringList reactions{
        QStringLiteral("\xF0\x9F\x91\x8D"), // thumbs up
        QStringLiteral("\xE2\x9D\xA4\xEF\xB8\x8F"), // heart
        QStringLiteral("\xF0\x9F\x94\xA5"), // fire
        QStringLiteral("\xF0\x9F\x98\x82"), // joy
        QStringLiteral("\xF0\x9F\x98\xAE"), // open mouth
        QStringLiteral("\xF0\x9F\x98\xA2"), // cry
        QStringLiteral("\xF0\x9F\x91\x8F"), // clap
        QStringLiteral("\xF0\x9F\x8E\x89"), // party
        QStringLiteral("\xF0\x9F\xA4\x94"), // thinking
        QStringLiteral("\xF0\x9F\x92\xAF"), // hundred
        QStringLiteral("\xF0\x9F\xA5\xB0"), // smiling hearts
        QStringLiteral("\xF0\x9F\x99\x8F"), // folded hands
    };
    return reactions;
}

} // namespace mini
