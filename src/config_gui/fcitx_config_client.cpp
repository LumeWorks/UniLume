// SPDX-License-Identifier: GPL-2.0-or-later

#include "fcitx_config_client.h"

#include "application_policy.h"
#include "dictionary_contract.h"
#include "keymap_contract.h"
#include "macro_contract.h"

#include <fcitxqtcontrollerproxy.h>
#include <fcitxqtdbustypes.h>

#include <fcitx-config/rawconfig.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QVariantMap>

#include <filesystem>
#include <fstream>
#include <map>

namespace unilume::config_gui {
namespace {

constexpr auto service = "org.fcitx.Fcitx5";
constexpr auto object_path = "/controller";
constexpr auto config_uri = "fcitx://config/inputmethod/unilume";

QDBusConnection registeredSessionBus()
{
    fcitx::registerFcitxQtDBusTypes();
    return QDBusConnection::sessionBus();
}

void decompose(fcitx::RawConfig &config, const QVariant &variant)
{
    QVariantMap map;
    if (variant.canConvert<QDBusArgument>()) {
        const QDBusArgument argument =
            qvariant_cast<QDBusArgument>(variant);
        argument >> map;
    } else if (variant.canConvert<QString>()) {
        config.setValue(variant.toString().toStdString());
        return;
    } else {
        map = variant.toMap();
    }
    for (auto item = map.constKeyValueBegin();
         item != map.constKeyValueEnd(); ++item) {
        decompose(*config.get(item->first.toStdString(), true),
                  item->second);
    }
}

QVariant compose(const fcitx::RawConfig &config)
{
    if (!config.hasSubItems()) {
        return QString::fromStdString(config.value());
    }
    QVariantMap map;
    if (!config.value().empty()) {
        map[QString()] = QString::fromStdString(config.value());
    }
    for (const std::string &name : config.subItems()) {
        map[QString::fromStdString(name)] =
            compose(*config.get(name));
    }
    return map;
}

std::map<std::string, std::string> valuesFromRaw(
    const fcitx::RawConfig &raw)
{
    std::map<std::string, std::string> values;
    for (const std::string_view key : allConfigKeys()) {
        if (const std::string *item =
                raw.valueByPath(std::string(key))) {
            values.emplace(key, *item);
        }
    }
    return values;
}

fcitx::RawConfig rawFromSettings(const Settings &settings)
{
    fcitx::RawConfig raw;
    for (const auto &[key, item] : settings.values) {
        raw[key] = item;
    }
    return raw;
}

bool readBounded(const std::string &path,
                 std::size_t limit,
                 std::string &text,
                 std::string &error)
{
    if (path.empty()) {
        text.clear();
        return true;
    }
    std::error_code filesystem_error;
    const std::uintmax_t size =
        std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size > limit) {
        error = filesystem_error
                    ? "inspect managed resource: " +
                          filesystem_error.message()
                    : "managed resource exceeds size limit";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "read managed resource failed";
        return false;
    }
    text.assign(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
        error = "managed resource changed while reading";
        return false;
    }
    return true;
}

} // namespace

class FcitxConfigClient::Implementation {
public:
    Implementation()
        : connection(registeredSessionBus()),
          proxy(service, object_path, connection)
    {
    }

    QDBusConnection connection;
    fcitx::FcitxQtControllerProxy proxy;
};

FcitxConfigClient::FcitxConfigClient()
    : implementation_(new Implementation)
{
}

FcitxConfigClient::~FcitxConfigClient()
{
    delete implementation_;
}

bool FcitxConfigClient::available(std::string *error) const
{
    if (!implementation_->connection.isConnected()) {
        if (error) {
            *error = "session D-Bus is unavailable";
        }
        return false;
    }
    QDBusConnectionInterface *interface =
        implementation_->connection.interface();
    if (!interface) {
        if (error) {
            *error = "session D-Bus has no connection interface";
        }
        return false;
    }
    const QDBusReply<bool> registered =
        interface->isServiceRegistered(service);
    if (!registered.isValid() || !registered.value()) {
        if (error) {
            *error = registered.isValid()
                         ? "Fcitx5 is not running"
                         : registered.error().message().toStdString();
        }
        return false;
    }
    return true;
}

bool FcitxConfigClient::load(Settings &settings,
                             std::string *error) const
{
    if (!available(error)) {
        return false;
    }
    fcitx::FcitxQtConfigTypeList description;
    const QDBusReply<QDBusVariant> reply =
        implementation_->proxy.GetConfig(config_uri, description);
    if (!reply.isValid()) {
        if (error) {
            *error = reply.error().message().toStdString();
        }
        return false;
    }
    fcitx::RawConfig raw;
    decompose(raw, reply.value().variant());
    settings = settingsFromValues(valuesFromRaw(raw));

    std::string load_error;
    if (!readBounded(value(settings, "MacroFile"),
                     macro::max_serialized_bytes,
                     settings.resources.macros, load_error) ||
        !readBounded(value(settings, "DictionaryFile"),
                     dictionary::max_serialized_bytes,
                     settings.resources.dictionary, load_error) ||
        !readBounded(value(settings, "KeymapFile"),
                     keymap::max_serialized_bytes,
                     settings.resources.keymap, load_error) ||
        !readBounded(value(settings, "ApplicationPolicyFile"),
                     policy::max_serialized_bytes,
                     settings.resources.application_policy, load_error)) {
        if (error) {
            *error = std::move(load_error);
        }
        return false;
    }
    const ValidationResult validation = validate(settings);
    if (!validation.ok()) {
        if (error) {
            *error = validation.errors.front().field + ": " +
                     validation.errors.front().message;
        }
        return false;
    }
    return true;
}

bool FcitxConfigClient::apply(const Settings &settings,
                              std::string *error) const
{
    const ValidationResult validation = validate(settings);
    if (!validation.ok()) {
        if (error) {
            *error = validation.errors.front().field + ": " +
                     validation.errors.front().message;
        }
        return false;
    }
    if (!available(error)) {
        return false;
    }
    QDBusPendingReply<> reply =
        implementation_->proxy.SetConfig(
            config_uri,
            QDBusVariant(compose(rawFromSettings(settings))));
    reply.waitForFinished();
    if (reply.isError()) {
        if (error) {
            *error = reply.error().message().toStdString();
        }
        return false;
    }
    Settings observed;
    if (!load(observed, error)) {
        return false;
    }
    if (observed != settings) {
        if (error) {
            *error = "Fcitx rejected or altered the configuration snapshot";
        }
        return false;
    }
    return true;
}

} // namespace unilume::config_gui
