/*!
 * \file lintdemo.hpp
 * \brief A tiny MISRA-clean, uncrustify-clean header — the fixture the lint-cpp
 *        self-test runs the shared config against.
 */
#pragma once

namespace lintdemo {

  //! \brief A deliberately trivial, lint-clean function.
  [[nodiscard]] inline int answer() noexcept
  {
    return 0;
  }
} // namespace lintdemo
