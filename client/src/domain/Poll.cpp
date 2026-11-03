#include "domain/Poll.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace mini {

namespace {

QJsonArray optionsToJson(const QStringList &options)
{
    QJsonArray array;
    for (const QString &option : options) {
        array.append(option);
    }
    return array;
}

QStringList optionsFromJson(const QJsonArray &array)
{
    QStringList options;
    for (const QJsonValue &value : array) {
        options.append(value.toString());
    }
    return options;
}

} // namespace

QByteArray encodePollBody(const PollBody &body)
{
    QJsonObject poll;
    poll.insert(QStringLiteral("id"), body.pollId);
    poll.insert(QStringLiteral("question"), body.question);
    poll.insert(QStringLiteral("options"), optionsToJson(body.options));
    poll.insert(QStringLiteral("anonymous"), body.anonymous);
    poll.insert(QStringLiteral("multiple"), body.multiple);
    poll.insert(QStringLiteral("quiz"), body.quiz);
    poll.insert(QStringLiteral("allow_change"), body.allowChange);
    poll.insert(QStringLiteral("correct"), body.correctOption);
    QJsonObject root;
    root.insert(QStringLiteral("v"), 1);
    root.insert(QStringLiteral("poll"), poll);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool decodePollBody(const QByteArray &bytes, PollBody *body)
{
    if (!bytes.startsWith('{') || !bytes.contains("\"poll\"")) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject poll = document.object().value(QStringLiteral("poll")).toObject();
    const QString id = poll.value(QStringLiteral("id")).toString();
    const QString question = poll.value(QStringLiteral("question")).toString();
    const QStringList options = optionsFromJson(poll.value(QStringLiteral("options")).toArray());
    if (id.isEmpty() || question.isEmpty() || options.size() < kPollMinOptions
        || options.size() > kPollMaxOptions) {
        return false;
    }
    PollBody decoded;
    decoded.pollId = id;
    decoded.question = question;
    decoded.options = options;
    decoded.anonymous = poll.value(QStringLiteral("anonymous")).toBool(true);
    decoded.multiple = poll.value(QStringLiteral("multiple")).toBool(false);
    decoded.quiz = poll.value(QStringLiteral("quiz")).toBool(false);
    decoded.allowChange = poll.value(QStringLiteral("allow_change")).toBool(false);
    decoded.correctOption = poll.value(QStringLiteral("correct")).toInt(-1);
    if (decoded.quiz && (decoded.correctOption < 0
                         || decoded.correctOption >= decoded.options.size())) {
        return false;
    }
    if (body) {
        *body = decoded;
    }
    return true;
}

} // namespace mini
