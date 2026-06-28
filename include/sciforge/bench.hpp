/**\file bench.hpp
 * \brief Raw-sample benchmark collector + JSON emitter (the C++ side of sciforge.bench).
 *
 * The C++ peer of the Python python/sciforge/bench/ package: a header-only, dependency-free
 * collector that gathers **raw** per-operation times and emits them as the exact JSON the
 * Python reporter ingests (schema.run_to_json / load_run). It computes no statistics, no
 * units, no reductions — every median / minimum / CI / box-plot is derived downstream in
 * Python, so stats and formatting live in exactly one place.
 *
 * Infrastructure layer ① (like test/framework.hpp): **never shipped** in any library, consumed
 * from a sibling checkout via SCIFORGE_INCLUDE. The timing primitives mirror runner.py
 * (calibrate / batch_time / collect) so a C++ collector and a Python collector size their loops
 * the same way.
 *
 * Gate note: the MSVC branch of \ref sciforge::bench::do_not_optimize is NOT exercised in CI
 * (the benches are out of gate, and CI builds with clang/gcc). It is a deliberate, best-effort
 * fallback for that toolchain; the GCC/Clang inline-asm barrier is the one the suite proves.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sciforge::bench {

  /*!
   * \brief Compiler barrier that keeps \p value (and the work producing it) from being
   *        optimized away — the benchmark equivalent of "observe this result".
   * \tparam T    Value type (scalar or class — the address form below handles both).
   * \param[in] value The value to mark as observed.
   */
  template <class T>
  void do_not_optimize(const T& value)
  {
#if defined(__GNUC__) || defined(__clang__)
    // Take the address ("g" with &value) so it works for class types (std::string, …), not
    // just scalars; the "memory" clobber forbids reordering the producing work across it.
    asm volatile ("" : : "g" (&value) : "memory");
#else
    // Best-effort portable fallback (e.g. MSVC): force a volatile read of the object's bytes
    // and a compiler-only fence. Not exercised in CI — see the file-level gate note.
    const volatile char& sink = *reinterpret_cast<const volatile char*>(&value);
    (void) sink;
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
  }

  namespace detail {

    //! \brief Invokes \p thunk once, observing its result if it returns one (void thunks just run).
    template <class Thunk>
    void run_thunk(Thunk&& thunk)
    {
      if constexpr (std::is_void_v<std::invoke_result_t<Thunk&>>) {
        thunk();
      }
      else {
        do_not_optimize(thunk());
      }
    }
  } // namespace detail

  /*!
   * \brief Sizes an inner batch so one batch lasts about \p batch_target seconds — the mirror
   *        of runner.calibrate. Warms \p thunk once (discarded), then times one call.
   * \tparam Thunk        Nullary callable.
   * \param[in] thunk        The operation to size for.
   * \param[in] batch_target Target batch duration in seconds (default 0.005, as in Python).
   * \return max(1, batch_target / one-call-seconds) — at least 1.
   */
  template <class Thunk>
  int calibrate(Thunk&& thunk,
                double  batch_target = 0.005)
  {
    detail::run_thunk(thunk); // warm up: discard first-call effects
    const auto                                start   = std::chrono::steady_clock::now();
    detail::run_thunk(thunk);
    const std::chrono::duration<double>       elapsed = std::chrono::steady_clock::now() - start;
    const double                              once    = std::max(elapsed.count(), 1e-9);
    return std::max(1, static_cast<int>(batch_target / once));
  }

  /*!
   * \brief Per-call time of \p thunk, averaged over an inner loop of \p n calls
   *        (the mirror of runner.batch_time).
   * \tparam Thunk Nullary callable.
   * \param[in] thunk The operation to time.
   * \param[in] n     Inner loop count (>= 1).
   * \return Elapsed seconds divided by \p n.
   */
  template <class Thunk>
  double batch_time(Thunk&& thunk,
                    int     n)
  {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      detail::run_thunk(thunk);
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count() / n;
  }

  /*!
   * \brief Collects \p samples raw per-operation times (seconds) for \p thunk — the mirror of
   *        runner.collect (there is no GC to disable on the C++ side).
   * \tparam Thunk Nullary callable.
   * \param[in] thunk        The operation to measure.
   * \param[in] samples      Number of raw samples to gather (default 40, as in Python).
   * \param[in] inner        Inner batch size; 0 (the default) auto-calibrates to \p batch_target.
   * \param[in] batch_target Target batch duration in seconds when auto-calibrating.
   * \return The raw samples, in seconds — no statistics computed.
   */
  template <class Thunk>
  std::vector<double> collect(Thunk&& thunk,
                              int     samples      = 40,
                              int     inner        = 0,
                              double  batch_target = 0.005)
  {
    const int           n = (inner > 0) ? inner : calibrate(thunk, batch_target);
    std::vector<double> raw;
    raw.reserve(static_cast<std::size_t>(std::max(0, samples)));
    for (int i = 0; i < samples; ++i) {
      raw.push_back(batch_time(thunk, n));
    }
    return raw;
  }

  // --- a tiny, comma-safe JSON emitter (the values of object pairs are pre-rendered JSON) ----

  //! \brief Renders \p value as a JSON string (quotes + escapes ", \\ and newlines).
  inline std::string json_string(const std::string& value)
  {
    std::string out = "\"";
    for (const char c : value) {
      if ((c == '"') || (c == '\\')) {
        out += '\\';
        out += c;
      }
      else if (c == '\n') {
        out += "\\n";
      }
      else {
        out += c;
      }
    }
    out += '"';
    return out;
  }

  //! \brief Renders \p value as a JSON number with %.6g (the round-trip precision contract).
  inline std::string json_number(double value)
  {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
  }

  //! \brief Joins pre-rendered JSON \p items with commas.
  inline std::string json_join(const std::vector<std::string>& items)
  {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (i != 0) {
        out += ',';
      }
      out += items[i];
    }
    return out;
  }

  //! \brief Renders \p values as a JSON array of numbers.
  inline std::string json_array(const std::vector<double>& values)
  {
    std::vector<std::string> items;
    items.reserve(values.size());
    for (const double value : values) {
      items.push_back(json_number(value));
    }
    return "[" + json_join(items) + "]";
  }

  //! \brief Renders \p fields as a JSON object; each pair's value is already JSON.
  inline std::string json_object(const std::vector<std::pair<std::string, std::string>>& fields)
  {
    std::vector<std::string> items;
    items.reserve(fields.size());
    for (const auto& [key, value] : fields) {
      items.push_back(json_string(key) + ":" + value);
    }
    return "{" + json_join(items) + "}";
  }

  /*!
   * \brief Emits one case in the schema.Case shape: name / unit / samples, an optional result,
   *        then the domain fields as **siblings** (mirroring _case_to_json: obj.update(extra)).
   * \param[in] name        Case name.
   * \param[in] unit        Sample unit (canonically "s").
   * \param[in] samples     Raw samples.
   * \param[in] domain      Domain fields as key -> pre-rendered-JSON pairs (siblings, not nested).
   * \param[in] result_json The result value as pre-rendered JSON; omitted when empty.
   * \return The case object as a JSON string.
   */
  inline std::string emit_case(std::string_view                                        name,
                               std::string_view                                        unit,
                               const std::vector<double>&                              samples,
                               const std::vector<std::pair<std::string, std::string>>& domain,
                               std::string_view                                        result_json = {})
  {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("name", json_string(std::string(name)));
    fields.emplace_back("unit", json_string(std::string(unit)));
    fields.emplace_back("samples", json_array(samples));
    if (!result_json.empty()) {
      fields.emplace_back("result", std::string(result_json));
    }
    for (const auto& field : domain) {
      fields.push_back(field); // domain fields are first-class siblings of name/unit/samples
    }
    return json_object(fields);
  }

  /*!
   * \brief Emits a Run: {"meta": <meta_json>, "cases": [ ... ]} (the schema.run_to_json shape).
   * \param[in] meta_json The meta object as pre-rendered JSON.
   * \param[in] cases     The already-emitted case objects.
   * \return The run object as a JSON string.
   */
  inline std::string emit_run(std::string_view                meta_json,
                              const std::vector<std::string>& cases)
  {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("meta", std::string(meta_json));
    fields.emplace_back("cases", "[" + json_join(cases) + "]");
    return json_object(fields);
  }
} // namespace sciforge::bench
