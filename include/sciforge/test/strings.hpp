/**\file strings.hpp
 * \brief Small zero-dependency string builders shared by the C++ test suites.
 *
 * Two helpers that several tests re-implemented locally, hoisted here so every
 * consumer uses one definition: \ref test::cat concatenates views without a
 * chained operator+, and \ref test::bytes builds a std::string from raw byte
 * values (for crafting UTF-8 and malformed-input fixtures).
 */
#ifndef SCIFORGE_TEST_STRINGS_HPP
#define SCIFORGE_TEST_STRINGS_HPP

#include <initializer_list>
#include <string>
#include <string_view>

namespace test {

  /*!
   * \brief Concatenates \p parts into one std::string.
   *
   * Appends each view in turn, so there are no intermediate temporaries a chained
   * `operator+` would allocate. The result owns its bytes, so it is safe to view
   * into (a test lexeme, a pattern) after the call.
   *
   * \param[in] parts The views to join, in order.
   * \return The concatenation of \p parts.
   */
  inline std::string cat(std::initializer_list<std::string_view> parts)
  {
    std::string result;
    for (const std::string_view part : parts) {
      result.append(part);
    }
    return result;
  }

  /*!
   * \brief Builds a std::string from raw byte \p values.
   *
   * Each value is truncated to a char, so a test can spell an exact byte sequence —
   * including invalid UTF-8 — without escaping it in a string literal.
   *
   * \param[in] values The byte values (0–255; truncated to char).
   * \return A std::string of those bytes, in order.
   */
  inline std::string bytes(std::initializer_list<int> values)
  {
    std::string result;
    for (const int value : values) {
      result += static_cast<char>(value);
    }
    return result;
  }
} // namespace test

#endif // SCIFORGE_TEST_STRINGS_HPP
