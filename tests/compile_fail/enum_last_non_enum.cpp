/**
 * @file enum_last_non_enum.cpp
 * @brief Must NOT compile: `detail::EnumLast` names the last enumerator of an enumeration, so an
 * integer type does not satisfy its constraint (handbook KB-CPP20-005, -026).
 *
 * EXPECT: Enumeration
 */
#include "as5047u_detail.hpp"

int main() {
  const as5047u::detail::EnumLast<int>* last = nullptr;
  return last == nullptr ? 0 : 1;
}
