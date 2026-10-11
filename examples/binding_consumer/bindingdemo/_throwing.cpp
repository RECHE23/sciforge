// A module whose registration throws: its import must fail with that exception as a Python error, rather than
// let the exception cross the extern "C" PyInit and end the process.
#include <stdexcept>

#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/module.hpp>

namespace {
  long long one()
  {
    return 1;
  }
} // namespace

SCIFORGE_MODULE(_throwing, "bindingdemo.throwing_error", m)
{
  m.def<&one>("one", "one() -> 1");
  throw std::runtime_error("registration failed");
}
