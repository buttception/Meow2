/// @file
/// @brief Parameter validation (CommandSchema.h).

#include "Commands/CommandSchema.h"

#include <cmath>
#include <format>
#include <QJsonValue>
#include <QStringList>
#include <string>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals; // u"..."_s: a QString built at compile time

namespace
{
/// @brief Whether `value` has the JSON Schema type `type`.
bool hasType(const QJsonValue& value, const QString& type)
{
    if (type == u"string")
    {
        return value.isString();
    }
    if (type == u"integer")
    {
        return value.isDouble() && std::trunc(value.toDouble()) == value.toDouble() && std::isfinite(value.toDouble());
    }
    if (type == u"number")
    {
        return value.isDouble();
    }
    if (type == u"boolean")
    {
        return value.isBool();
    }
    if (type == u"object")
    {
        return value.isObject();
    }
    if (type == u"array")
    {
        return value.isArray();
    }
    return false; // a schema with a type this checker doesn't know: refuse rather than guess
}

/// @brief An InvalidArgument error about one parameter.
Unexpected<Error> invalid(const QString& name, const std::string& why)
{
    return fail(ErrorCode::InvalidArgument, std::format("parameter '{}' {}", name.toStdString(), why));
}

/// @brief Checks one parameter's value against its property schema.
Result<void> validateProperty(const QString& name, const QJsonObject& property, const QJsonValue& value)
{
    const QString type = property.value(u"type").toString();
    if (!type.isEmpty() && !hasType(value, type))
    {
        return invalid(name, std::format("must be of type {}", type.toStdString()));
    }
    if (const QJsonValue options = property.value(u"enum"); options.isArray() && !options.toArray().contains(value))
    {
        QStringList names;
        for (const QJsonValue option : options.toArray())
        {
            names.push_back(option.toVariant().toString());
        }
        return invalid(name, std::format("must be one of: {}", names.join(u", ").toStdString()));
    }
    if (value.isDouble())
    {
        if (const QJsonValue minimum = property.value(u"minimum");
            minimum.isDouble() && value.toDouble() < minimum.toDouble())
        {
            return invalid(name, std::format("must be at least {}", minimum.toDouble()));
        }
        if (const QJsonValue maximum = property.value(u"maximum");
            maximum.isDouble() && value.toDouble() > maximum.toDouble())
        {
            return invalid(name, std::format("must be at most {}", maximum.toDouble()));
        }
    }
    return {};
}
} // namespace

Result<void> validateParameters(const QJsonObject& schema, const QJsonObject& parameters)
{
    const QJsonObject properties = schema.value(u"properties").toObject();
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
    {
        if (!properties.contains(it.key()))
        {
            return invalid(it.key(), "isn't a parameter of this command");
        }
        if (Result<void> checked = validateProperty(it.key(), properties.value(it.key()).toObject(), it.value());
            !checked)
        {
            return checked;
        }
    }
    for (const QJsonValue required : schema.value(u"required").toArray())
    {
        if (!parameters.contains(required.toString()))
        {
            return invalid(required.toString(), "is required");
        }
    }
    return {};
}

QJsonObject objectSchema(std::initializer_list<SchemaProperty> properties)
{
    QJsonObject propertyMap;
    QJsonArray required;
    for (const SchemaProperty& property : properties)
    {
        propertyMap.insert(property.name, property.schema);
        if (property.required)
        {
            required.append(property.name);
        }
    }
    QJsonObject schema{{u"type"_s, u"object"_s}, {u"properties"_s, propertyMap}};
    if (!required.isEmpty())
    {
        schema.insert(u"required", required);
    }
    return schema;
}

QJsonObject propertySchema(const QString& type, const QString& description)
{
    QJsonObject property{{u"description"_s, description}};
    if (!type.isEmpty())
    {
        property.insert(u"type", type);
    }
    return property;
}
} // namespace Meow2::Editor
