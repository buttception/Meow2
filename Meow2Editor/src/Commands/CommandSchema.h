#pragma once

/// @file
/// @brief Checks a command's parameters against its JSON Schema, and helpers to write those schemas.

#include <Meow2/Core/Result.h>

#include <initializer_list>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <utility>

namespace Meow2::Editor
{
/// @brief Checks `parameters` against a command's parameter schema.
///
/// The schemas are JSON Schema (https://json-schema.org), the format MCP tool definitions use, so the registry can
/// hand them to an MCP client unchanged (PR 6). Only the part commands need is checked:
/// - the schema is `{"type": "object", "properties": {...}, "required": [...]}`; a parameter that isn't in
///   `properties` is refused (a typo shouldn't be silently ignored);
/// - per property: `type` (`string`, `integer`, `number`, `boolean`, `object`, `array`; no `type` accepts any value),
///   `enum`, and `minimum` / `maximum` for numbers.
/// An `integer` must be a whole number; JSON has one number type, so `3.0` counts as an integer and `3.5` doesn't.
/// @param schema     The command's schema (CommandInfo::parameters). An empty object takes no parameters.
/// @param parameters The parameters to check.
/// @return Nothing, or ErrorCode::InvalidArgument naming the first parameter that's wrong and why.
[[nodiscard]] Result<void> validateParameters(const QJsonObject& schema, const QJsonObject& parameters);

/// @brief One property of a parameter schema, for objectSchema(). Plain data.
struct SchemaProperty
{
    QString name{};        ///< The parameter's name.
    QJsonObject schema{};  ///< Its JSON Schema, e.g. `{"type": "integer", "minimum": 0}`.
    bool required = false; ///< Whether it must be given.
};

/// @brief Builds an object schema from its properties, so commands don't spell out the JSON by hand.
/// @param properties The parameters, in the order a UI should show them.
/// @return `{"type": "object", "properties": {...}, "required": [...]}`.
[[nodiscard]] QJsonObject objectSchema(std::initializer_list<SchemaProperty> properties);

/// @brief A property schema with a type and a description.
/// @param type        A JSON Schema type name (`"string"`, `"integer"`, ...); empty for any value.
/// @param description What the parameter is, for people and AI agents reading the tool list.
/// @return `{"type": type, "description": description}`.
[[nodiscard]] QJsonObject propertySchema(const QString& type, const QString& description);
} // namespace Meow2::Editor
