#include "crypto/SignInKey.h"

#include <QRegularExpression>
#include <QStringConverter>

#include <sodium.h>

#include <array>
#include <limits>

namespace mini::crypto {

namespace {

// Word list: EFF Short Wordlist #1 (https://www.eff.org/dice), first 1024
// entries, CC BY 3.0. Generated once and embedded so the file format is
// stable across builds.
const char *const kWords[1024] = {
    "acid", "acorn", "acre", "acts", "afar", "affix", "aged", "agent",
    "agile", "aging", "agony", "ahead", "aide", "aids", "aim", "ajar",
    "alarm", "alias", "alibi", "alien", "alike", "alive", "aloe", "aloft",
    "aloha", "alone", "amend", "amino", "ample", "amuse", "angel", "anger",
    "angle", "ankle", "apple", "april", "apron", "aqua", "area", "arena",
    "argue", "arise", "armed", "armor", "army", "aroma", "array", "arson",
    "art", "ashen", "ashes", "atlas", "atom", "attic", "audio", "avert",
    "avoid", "awake", "award", "awoke", "axis", "bacon", "badge", "bagel",
    "baggy", "baked", "baker", "balmy", "banjo", "barge", "barn", "bash",
    "basil", "bask", "batch", "bath", "baton", "bats", "blade", "blank",
    "blast", "blaze", "bleak", "blend", "bless", "blimp", "blink", "bloat",
    "blob", "blog", "blot", "blunt", "blurt", "blush", "boast", "boat",
    "body", "boil", "bok", "bolt", "boned", "boney", "bonus", "bony",
    "book", "booth", "boots", "boss", "botch", "both", "boxer", "breed",
    "bribe", "brick", "bride", "brim", "bring", "brink", "brisk", "broad",
    "broil", "broke", "brook", "broom", "brush", "buck", "bud", "buggy",
    "bulge", "bulk", "bully", "bunch", "bunny", "bunt", "bush", "bust",
    "busy", "buzz", "cable", "cache", "cadet", "cage", "cake", "calm",
    "cameo", "canal", "candy", "cane", "canon", "cape", "card", "cargo",
    "carol", "carry", "carve", "case", "cash", "cause", "cedar", "chain",
    "chair", "chant", "chaos", "charm", "chase", "cheek", "cheer", "chef",
    "chess", "chest", "chew", "chief", "chili", "chill", "chip", "chomp",
    "chop", "chow", "chuck", "chump", "chunk", "churn", "chute", "cider",
    "cinch", "city", "civic", "civil", "clad", "claim", "clamp", "clap",
    "clash", "clasp", "class", "claw", "clay", "clean", "clear", "cleat",
    "cleft", "clerk", "click", "cling", "clink", "clip", "cloak", "clock",
    "clone", "cloth", "cloud", "clump", "coach", "coast", "coat", "cod",
    "coil", "coke", "cola", "cold", "colt", "coma", "come", "comic",
    "comma", "cone", "cope", "copy", "coral", "cork", "cost", "cot",
    "couch", "cough", "cover", "cozy", "craft", "cramp", "crane", "crank",
    "crate", "crave", "crawl", "crazy", "creme", "crepe", "crept", "crib",
    "cried", "crisp", "crook", "crop", "cross", "crowd", "crown", "crumb",
    "crush", "crust", "cub", "cult", "cupid", "cure", "curl", "curry",
    "curse", "curve", "curvy", "cushy", "cut", "cycle", "dab", "dad",
    "daily", "dairy", "daisy", "dance", "dandy", "darn", "dart", "dash",
    "data", "date", "dawn", "deaf", "deal", "dean", "debit", "debt",
    "debug", "decaf", "decal", "decay", "deck", "decor", "decoy", "deed",
    "delay", "denim", "dense", "dent", "depth", "derby", "desk", "dial",
    "diary", "dice", "dig", "dill", "dime", "dimly", "diner", "dingy",
    "disco", "dish", "disk", "ditch", "ditzy", "dizzy", "dock", "dodge",
    "doing", "doll", "dome", "donor", "donut", "dose", "dot", "dove",
    "down", "dowry", "doze", "drab", "drama", "drank", "draw", "dress",
    "dried", "drift", "drill", "drive", "drone", "droop", "drove", "drown",
    "drum", "dry", "duck", "duct", "dude", "dug", "duke", "duo",
    "dusk", "dust", "duty", "dwarf", "dwell", "eagle", "early", "earth",
    "easel", "east", "eaten", "eats", "ebay", "ebony", "ebook", "echo",
    "edge", "eel", "eject", "elbow", "elder", "elf", "elk", "elm",
    "elope", "elude", "elves", "email", "emit", "empty", "emu", "enter",
    "entry", "envoy", "equal", "erase", "error", "erupt", "essay", "etch",
    "evade", "even", "evict", "evil", "evoke", "exact", "exit", "fable",
    "faced", "fact", "fade", "fall", "false", "fancy", "fang", "fax",
    "feast", "feed", "femur", "fence", "fend", "ferry", "fetal", "fetch",
    "fever", "fiber", "fifth", "fifty", "film", "filth", "final", "finch",
    "fit", "five", "flag", "flaky", "flame", "flap", "flask", "fled",
    "flick", "fling", "flint", "flip", "flirt", "float", "flock", "flop",
    "floss", "flyer", "foam", "foe", "fog", "foil", "folic", "folk",
    "food", "fool", "found", "fox", "foyer", "frail", "frame", "fray",
    "fresh", "fried", "frill", "frisk", "from", "front", "frost", "froth",
    "frown", "froze", "fruit", "gag", "gains", "gala", "game", "gap",
    "gas", "gave", "gear", "gecko", "geek", "gem", "genre", "gift",
    "gig", "gills", "given", "giver", "glad", "glass", "glide", "gloss",
    "glove", "glow", "glue", "goal", "going", "golf", "gong", "good",
    "gooey", "goofy", "gore", "gown", "grab", "grain", "grant", "grape",
    "graph", "grasp", "grass", "grave", "gravy", "gray", "green", "greet",
    "grew", "grid", "grief", "grill", "grip", "grit", "groom", "grope",
    "growl", "grub", "grunt", "guide", "gulf", "gulp", "gummy", "guru",
    "gush", "gut", "guy", "habit", "half", "halo", "halt", "happy",
    "harm", "hash", "hasty", "hatch", "hate", "haven", "hazel", "hazy",
    "heap", "heat", "heave", "hedge", "hefty", "help", "herbs", "hers",
    "hub", "hug", "hula", "hull", "human", "humid", "hump", "hung",
    "hunk", "hunt", "hurry", "hurt", "hush", "hut", "ice", "icing",
    "icon", "icy", "igloo", "image", "ion", "iron", "islam", "issue",
    "item", "ivory", "ivy", "jab", "jam", "jaws", "jazz", "jeep",
    "jelly", "jet", "jiffy", "job", "jog", "jolly", "jolt", "jot",
    "joy", "judge", "juice", "juicy", "july", "jumbo", "jump", "junky",
    "juror", "jury", "keep", "keg", "kept", "kick", "kilt", "king",
    "kite", "kitty", "kiwi", "knee", "knelt", "koala", "kung", "ladle",
    "lady", "lair", "lake", "lance", "land", "lapel", "large", "lash",
    "lasso", "last", "latch", "late", "lazy", "left", "legal", "lemon",
    "lend", "lens", "lent", "level", "lever", "lid", "life", "lift",
    "lilac", "lily", "limb", "limes", "line", "lint", "lion", "lip",
    "list", "lived", "liver", "lunar", "lunch", "lung", "lurch", "lure",
    "lurk", "lying", "lyric", "mace", "maker", "malt", "mama", "mango",
    "manor", "many", "map", "march", "mardi", "marry", "mash", "match",
    "mate", "math", "moan", "mocha", "moist", "mold", "mom", "moody",
    "mop", "morse", "most", "motor", "motto", "mount", "mouse", "mousy",
    "mouth", "move", "movie", "mower", "mud", "mug", "mulch", "mule",
    "mull", "mumbo", "mummy", "mural", "muse", "music", "musky", "mute",
    "nacho", "nag", "nail", "name", "nanny", "nap", "navy", "near",
    "neat", "neon", "nerd", "nest", "net", "next", "niece", "ninth",
    "nutty", "oak", "oasis", "oat", "ocean", "oil", "old", "olive",
    "omen", "onion", "only", "ooze", "opal", "open", "opera", "opt",
    "otter", "ouch", "ounce", "outer", "oval", "oven", "owl", "ozone",
    "pace", "pagan", "pager", "palm", "panda", "panic", "pants", "panty",
    "paper", "park", "party", "pasta", "patch", "path", "patio", "payer",
    "pecan", "penny", "pep", "perch", "perky", "perm", "pest", "petal",
    "petri", "petty", "photo", "plank", "plant", "plaza", "plead", "plot",
    "plow", "pluck", "plug", "plus", "poach", "pod", "poem", "poet",
    "pogo", "point", "poise", "poker", "polar", "polio", "polka", "polo",
    "pond", "pony", "poppy", "pork", "poser", "pouch", "pound", "pout",
    "power", "prank", "press", "print", "prior", "prism", "prize", "probe",
    "prong", "proof", "props", "prude", "prune", "pry", "pug", "pull",
    "pulp", "pulse", "puma", "punch", "punk", "pupil", "puppy", "purr",
    "purse", "push", "putt", "quack", "quake", "query", "quiet", "quill",
    "quilt", "quit", "quota", "quote", "rabid", "race", "rack", "radar",
    "radio", "raft", "rage", "raid", "rail", "rake", "rally", "ramp",
    "ranch", "range", "rank", "rant", "rash", "raven", "reach", "react",
    "ream", "rebel", "recap", "relax", "relay", "relic", "remix", "repay",
    "repel", "reply", "rerun", "reset", "rhyme", "rice", "rich", "ride",
    "rigid", "rigor", "rinse", "riot", "ripen", "rise", "risk", "ritzy",
    "rival", "river", "roast", "robe", "robin", "rock", "rogue", "roman",
    "romp", "rope", "rover", "royal", "ruby", "rug", "ruin", "rule",
    "runny", "rush", "rust", "rut", "sadly", "sage", "said", "saint",
    "salad", "salon", "salsa", "salt", "same", "sandy", "santa", "satin",
    "sauna", "saved", "savor", "sax", "say", "scale", "scam", "scan",
    "scare", "scarf", "scary", "scoff", "scold", "scoop", "scoot", "scope",
    "score", "scorn", "scout", "scowl", "scrap", "scrub", "scuba", "scuff",
    "sect", "sedan", "self", "send", "sepia", "serve", "set", "seven",
    "shack", "shade", "shady", "shaft", "shaky", "sham", "shape", "share",
    "sharp", "shed", "sheep", "sheet", "shelf", "shell", "shine", "shiny",
    "ship", "shirt", "shock", "shop", "shore", "shout", "shove", "shown",
    "showy", "shred", "shrug", "shun", "shush", "shut", "shy", "sift",
    "silk", "silly", "silo", "sip", "siren", "sixth", "size", "skate",
    "skew", "skid", "skier", "skies", "skip", "skirt", "skit", "sky",
    "slab", "slack", "slain", "slam", "slang", "slash", "slate", "slaw",
    "sled", "sleek", "sleep", "sleet", "slept", "slice", "slick", "slimy",
    "sling", "slip", "slit", "slob", "slot", "slug", "slum", "slurp",
    "slush", "small", "smash", "smell", "smile", "smirk", "smog", "snack",
};

const QStringList &wordList()
{
    static const QStringList list = [] {
        QStringList words;
        words.reserve(1024);
        for (const char *word : kWords) {
            words.append(QString::fromLatin1(word));
        }
        return words;
    }();
    return list;
}

const QString &letterAlphabet()
{
    static const QString alphabet = [] {
        QString letters;
        for (QChar c = QLatin1Char('a'); c <= QLatin1Char('z'); c = QChar(c.unicode() + 1)) {
            letters.append(c);
        }
        for (QChar c = QLatin1Char('A'); c <= QLatin1Char('Z'); c = QChar(c.unicode() + 1)) {
            letters.append(c);
        }
        for (char32_t c = 0x0430; c <= 0x044F; ++c) { // а-я
            letters.append(QChar(c));
        }
        for (char32_t c = 0x0410; c <= 0x042F; ++c) { // А-Я
            letters.append(QChar(c));
        }
        for (char32_t c = 0x03B1; c <= 0x03C9; ++c) { // α-ω (includes ς)
            letters.append(QChar(c));
        }
        for (char32_t c = 0x0391; c <= 0x03A9; ++c) { // Α-Ω
            letters.append(QChar(c));
        }
        return letters;
    }();
    return alphabet;
}

const QString &symbolAlphabet()
{
    static const QString symbols = QStringLiteral("!@#$%^&*()-_=+[]{};:,.<>/?");
    return symbols;
}

quint32 randomBelow(quint32 bound)
{
    // All pools used here are powers of two or smaller; modulo bias is
    // avoided by rejecting the tail above the largest multiple of `bound`.
    const quint32 limit = std::numeric_limits<quint32>::max()
        - (std::numeric_limits<quint32>::max() % bound);
    quint32 value = 0;
    do {
        randombytes_buf(&value, sizeof(value));
    } while (value >= limit);
    return value % bound;
}

QStringList pickDistinct(const QString &pool, int count)
{
    QStringList items;
    items.reserve(count);
    for (const QChar c : pool) {
        items.append(QString(c));
    }
    // Fisher-Yates partial shuffle.
    for (int i = 0; i < count; ++i) {
        const quint32 j = i + randomBelow(static_cast<quint32>(items.size() - i));
        items.swapItemsAt(i, static_cast<int>(j));
    }
    items.erase(items.begin() + count, items.end());
    return items;
}

QString base32(const QByteArray &digest)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    QString out;
    quint32 buffer = 0;
    int bits = 0;
    for (const char byte : digest) {
        buffer = (buffer << 8) | static_cast<unsigned char>(byte);
        bits += 8;
        while (bits >= 5) {
            out.append(QLatin1Char(alphabet[(buffer >> (bits - 5)) & 0x1F]));
            bits -= 5;
        }
    }
    if (bits > 0) {
        out.append(QLatin1Char(alphabet[(buffer << (5 - bits)) & 0x1F]));
    }
    return out;
}

QStringList splitTokens(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return text.split(whitespace, Qt::SkipEmptyParts);
}

bool isControlOrFormat(QChar c)
{
    const ushort code = c.unicode();
    if (code <= 0x001F || (code >= 0x007F && code <= 0x009F)) {
        return true;
    }
    return c.category() == QChar::Other_Format;
}

} // namespace

int SignInKey::wordCount()
{
    return 1024;
}

bool SignInKey::isWordToken(const QString &token)
{
    return wordList().contains(token);
}

bool SignInKey::isLetterToken(const QString &token)
{
    return token.size() == 1 && letterAlphabet().contains(token.front());
}

bool SignInKey::isSymbolToken(const QString &token)
{
    return token.size() == 1 && symbolAlphabet().contains(token.front());
}

QString SignInKey::checksumFor(const QString &normalized)
{
    const QByteArray input = normalized.toUtf8();
    std::array<unsigned char, 8> digest{};
    crypto_generichash(digest.data(), digest.size(),
                       reinterpret_cast<const unsigned char *>(input.constData()),
                       static_cast<unsigned long long>(input.size()), nullptr, 0);
    return base32(QByteArray(reinterpret_cast<const char *>(digest.data()),
                             static_cast<int>(digest.size())));
}

SignInMaterial SignInKey::generate()
{
    if (sodium_init() < 0) {
        return {};
    }

    // Fisher-Yates over the full word list, then take the first five.
    QVector<int> indices(1024);
    for (int i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    for (int i = 0; i < 5; ++i) {
        const quint32 j = i + randomBelow(static_cast<quint32>(indices.size() - i));
        indices.swapItemsAt(i, static_cast<int>(j));
    }

    QStringList tokens;
    tokens.reserve(20);
    for (int i = 0; i < 5; ++i) {
        tokens.append(wordList().at(indices.at(i)));
    }
    tokens.append(pickDistinct(letterAlphabet(), 10));
    tokens.append(pickDistinct(symbolAlphabet(), 5));

    SignInMaterial material;
    material.parsed.tokens = tokens;
    material.parsed.normalized = tokens.join(QLatin1Char(' '));
    material.parsed.check = checksumFor(material.parsed.normalized);

    QByteArray file;
    file += "MINI-SIGNIN-1\n";
    file += material.parsed.normalized.toUtf8();
    file += "\nCHECK ";
    file += material.parsed.check.toUtf8();
    file += '\n';
    material.fileBytes = file;
    return material;
}

Result<NormalizedSignIn> SignInKey::normalize(const QByteArray &fileContents)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(fileContents);
    if (decoder.hasError()) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.encoding"));
    }

    // Strip a UTF-8 BOM and normalize every line ending to \n.
    if (!text.isEmpty() && text.front() == QChar(0xFEFF)) {
        text.remove(0, 1);
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines.append(trimmed);
        }
    }
    if (lines.size() < 2 || lines.first() != QStringLiteral("MINI-SIGNIN-1")) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.header"));
    }

    const QString checkLine = lines.takeLast();
    if (!checkLine.startsWith(QStringLiteral("CHECK"))) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.check_missing"));
    }
    QString checkValue = checkLine.mid(5).trimmed();
    if (checkValue.startsWith(QLatin1Char(':'))) {
        checkValue = checkValue.mid(1).trimmed();
    }
    static const QRegularExpression checkPattern(QStringLiteral("^[A-Z2-7]{13}$"));
    if (!checkPattern.match(checkValue).hasMatch()) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.check_format"));
    }

    const QString tokenText = lines.mid(1).join(QLatin1Char(' '));
    const QStringList tokens = tokenText.split(whitespace, Qt::SkipEmptyParts);
    if (tokens.size() != 20) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.tokens"));
    }
    for (const QString &token : tokens) {
        for (const QChar c : token) {
            if (isControlOrFormat(c)) {
                return Result<NormalizedSignIn>::error(QStringLiteral("signin.token_alphabet"));
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (!isWordToken(tokens.at(i))) {
            return Result<NormalizedSignIn>::error(QStringLiteral("signin.token_alphabet"));
        }
    }
    for (int i = 5; i < 15; ++i) {
        if (!isLetterToken(tokens.at(i))) {
            return Result<NormalizedSignIn>::error(QStringLiteral("signin.token_alphabet"));
        }
    }
    for (int i = 15; i < 20; ++i) {
        if (!isSymbolToken(tokens.at(i))) {
            return Result<NormalizedSignIn>::error(QStringLiteral("signin.token_alphabet"));
        }
    }

    NormalizedSignIn parsed;
    parsed.tokens = tokens;
    parsed.normalized = tokens.join(QLatin1Char(' '));
    parsed.check = checkValue;
    if (!verifyChecksum(parsed)) {
        return Result<NormalizedSignIn>::error(QStringLiteral("signin.check_mismatch"));
    }
    return Result<NormalizedSignIn>::ok(parsed);
}

bool SignInKey::verifyChecksum(const NormalizedSignIn &parsed)
{
    const QString expected = checksumFor(parsed.normalized);
    const QByteArray actualBytes = parsed.check.toLatin1();
    const QByteArray expectedBytes = expected.toLatin1();
    if (actualBytes.size() != expectedBytes.size()) {
        return false;
    }
    return sodium_memcmp(actualBytes.constData(), expectedBytes.constData(),
                         static_cast<unsigned long long>(actualBytes.size())) == 0;
}

} // namespace mini::crypto
