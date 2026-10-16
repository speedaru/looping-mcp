// SQL-style names for Looping attribute types.
//
// Mapping verified with looping_file_examples/all_sql_types.loo (see docs/format.md):
//   VARCHAR / NVARCHAR / VARBINARY   text, variable, ASCII / Unicode / binary
//   CHAR / NCHAR / BINARY            text, fixed
//   TEXT / NTEXT / BLOB              text, large object
//   TINYINT SMALLINT INT BIGINT      numeric, integer of 8/16/32/64 bits
//   DECIMAL FLOAT DOUBLE MONEY COUNTER
//   DATE TIME DATETIME  BOOLEAN  XML  GEOGRAPHY  GEOMETRY
#pragma once

#include <string>

#include "loo/objects.hpp"

namespace loo {

// Sets the type-related fields of an attribute from a name such as "INT" or "VARCHAR(50)".
// Case-insensitive. Throws Error for unknown names. Text types (VARCHAR, CHAR, ... but not TEXT/BLOB) take an
// optional length 1..65535; without one the attribute keeps its stored length (50 for a new attribute).
void applyType(Param& p, const std::string& sqlType);

// The SQL-style name of an attribute's type, e.g. "BIGINT" or "VARCHAR(50)".
std::string formatType(const Param& p);

// All accepted type names (for error messages and tool descriptions).
const char* supportedTypeNames();

}  // namespace loo
