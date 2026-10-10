// A module that registers one function twice with keyword arguments: its import must fail with
// SystemError rather than let the second registration rewrite the first's names and defaults.
#include <sciforge/binding/convert.hpp>
#include <sciforge/binding/dispatch.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/module.hpp>

namespace sb = sciforge::binding;

namespace {
  long long add(long long x,
                long long n)
  {
    return x + n;
  }
} // namespace

SCIFORGE_MODULE(_conflict, "bindingdemo.conflict_error", m)
{
  m.def<&add>("add", "x, n=1 -> x + n", sb::arg("x"), sb::arg("n")  = 1LL);
  m.def<&add>("add2", "x, n=2 -> x + n", sb::arg("x"), sb::arg("n") = 2LL);
}
