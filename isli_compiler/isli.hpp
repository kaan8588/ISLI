#ifndef ISLI_HPP
#define ISLI_HPP

/**
 * @file isli.hpp
 * @brief Modern C++20 Binding Layer for the Isli Virtual Machine (Inspired by Sol2).
 * 
 * Provides an ergonomic, template-driven, RAII C++ interface on top of Isli's C API.
 * Features:
 *   - RAII State Management (`isli::state`)
 *   - Sol2-Style Proxy Indexing (`vm["key"] = val; int x = vm["key"];`)
 *   - Seamless C++ Function & Lambda Binding (`vm["add"] = [](int a, int b) { return a + b; };`)
 *   - Type Traits & Value Conversions
 */

#include "isli_api.hpp"
#include "value.hpp"
#include "object.hpp"

#include <string>
#include <string_view>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <array>
#include <iostream>

namespace isli {

namespace detail {

// ── Type Inspection & Stack Push/Get Traits ───────────────────────

template <typename T, typename = void>
struct type_traits;

template <>
struct type_traits<double> {
    static void push(IsliState* L, double val) { isli_pushnumber(L, val); }
    static double get(IsliState* L, int index) { return isli_tonumber(L, index); }
    static bool is(IsliState* L, int index) { return isli_isnumber(L, index); }
};

template <>
struct type_traits<float> {
    static void push(IsliState* L, float val) { isli_pushnumber(L, static_cast<double>(val)); }
    static float get(IsliState* L, int index) { return static_cast<float>(isli_tonumber(L, index)); }
    static bool is(IsliState* L, int index) { return isli_isnumber(L, index); }
};

template <>
struct type_traits<int> {
    static void push(IsliState* L, int val) { isli_pushnumber(L, static_cast<double>(val)); }
    static int get(IsliState* L, int index) { return static_cast<int>(isli_tonumber(L, index)); }
    static bool is(IsliState* L, int index) { return isli_isnumber(L, index); }
};

template <>
struct type_traits<long> {
    static void push(IsliState* L, long val) { isli_pushnumber(L, static_cast<double>(val)); }
    static long get(IsliState* L, int index) { return static_cast<long>(isli_tonumber(L, index)); }
    static bool is(IsliState* L, int index) { return isli_isnumber(L, index); }
};

template <>
struct type_traits<long long> {
    static void push(IsliState* L, long long val) { isli_pushnumber(L, static_cast<double>(val)); }
    static long long get(IsliState* L, int index) { return static_cast<long long>(isli_tonumber(L, index)); }
    static bool is(IsliState* L, int index) { return isli_isnumber(L, index); }
};

template <>
struct type_traits<bool> {
    static void push(IsliState* L, bool val) { isli_pushboolean(L, val); }
    static bool get(IsliState* L, int index) { return isli_toboolean(L, index); }
    static bool is(IsliState* L, int index) { return isli_isboolean(L, index); }
};

template <>
struct type_traits<const char*> {
    static void push(IsliState* L, const char* val) { isli_pushstring(L, val); }
    static const char* get(IsliState* L, int index) { return isli_tostring(L, index); }
    static bool is(IsliState* L, int index) { return isli_isstring(L, index); }
};

template <size_t N>
struct type_traits<char[N]> {
    static void push(IsliState* L, const char* val) { isli_pushstring(L, val); }
    static const char* get(IsliState* L, int index) { return isli_tostring(L, index); }
    static bool is(IsliState* L, int index) { return isli_isstring(L, index); }
};

template <>
struct type_traits<std::string> {
    static void push(IsliState* L, const std::string& val) { isli_pushstring(L, val.c_str()); }
    static std::string get(IsliState* L, int index) {
        size_t len = 0;
        const char* s = isli_tolstring(L, index, &len);
        return s ? std::string(s, len) : std::string();
    }
    static bool is(IsliState* L, int index) { return isli_isstring(L, index); }
};

template <>
struct type_traits<std::string_view> {
    static void push(IsliState* L, std::string_view val) {
        std::string s(val);
        isli_pushstring(L, s.c_str());
    }
    static std::string_view get(IsliState* L, int index) {
        size_t len = 0;
        const char* s = isli_tolstring(L, index, &len);
        return s ? std::string_view(s, len) : std::string_view();
    }
    static bool is(IsliState* L, int index) { return isli_isstring(L, index); }
};

// ── Low-Level Value Struct Conversions (for Native Function Invocation) ──

template <typename T>
struct value_traits;

template <>
struct value_traits<double> {
    static double from(Value v) { return IS_NUMBER(v) ? AS_NUMBER(v) : 0.0; }
    static Value to(double v) { return NUMBER_VAL(v); }
};

template <>
struct value_traits<float> {
    static float from(Value v) { return IS_NUMBER(v) ? static_cast<float>(AS_NUMBER(v)) : 0.0f; }
    static Value to(float v) { return NUMBER_VAL(static_cast<double>(v)); }
};

template <>
struct value_traits<int> {
    static int from(Value v) { return IS_NUMBER(v) ? static_cast<int>(AS_NUMBER(v)) : 0; }
    static Value to(int v) { return NUMBER_VAL(static_cast<double>(v)); }
};

template <>
struct value_traits<long> {
    static long from(Value v) { return IS_NUMBER(v) ? static_cast<long>(AS_NUMBER(v)) : 0; }
    static Value to(long v) { return NUMBER_VAL(static_cast<double>(v)); }
};

template <>
struct value_traits<long long> {
    static long long from(Value v) { return IS_NUMBER(v) ? static_cast<long long>(AS_NUMBER(v)) : 0; }
    static Value to(long long v) { return NUMBER_VAL(static_cast<double>(v)); }
};

template <>
struct value_traits<bool> {
    static bool from(Value v) { return IS_BOOL(v) ? AS_BOOL(v) : !IS_NIL(v); }
    static Value to(bool v) { return BOOL_VAL(v); }
};

template <>
struct value_traits<std::string> {
    static std::string from(Value v) { return IS_STRING(v) ? std::string(AS_CSTRING(v)) : std::string(); }
    static Value to(const std::string& v) {
        ObjString* s = copyString(v.c_str(), static_cast<int>(v.length()));
        return OBJ_VAL(s);
    }
};

template <>
struct value_traits<const char*> {
    static const char* from(Value v) { return IS_STRING(v) ? AS_CSTRING(v) : ""; }
    static Value to(const char* v) {
        ObjString* s = copyString(v, static_cast<int>(std::strlen(v)));
        return OBJ_VAL(s);
    }
};

template <>
struct value_traits<void> {
    static Value to() { return NIL_VAL; }
};

// ── Function Traits & Deduction ───────────────────────────────────

template <typename T>
struct function_traits;

// Free function
template <typename R, typename... Args>
struct function_traits<R(*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Const member function / Lambda operator()
template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Non-const member function
template <typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Functors and Lambdas
template <typename F>
struct function_traits : function_traits<decltype(&std::decay_t<F>::operator())> {};

template <typename T, typename = void>
struct is_callable : std::false_type {};

template <typename T>
struct is_callable<T, std::void_t<decltype(&std::decay_t<T>::operator())>> : std::true_type {};

template <typename R, typename... Args>
struct is_callable<R(*)(Args...)> : std::true_type {};

template <typename T>
inline constexpr bool is_callable_v = is_callable<T>::value;

// ── Global Dynamic Function Registry (512 Slots) ──────────────────

template <size_t N = 512>
struct FunctionRegistry {
    static inline std::function<Value(int, Value*)> slots[N];
    static inline size_t next_slot = 0;

    template <size_t I>
    static Value dispatcher(int argCount, Value* args) {
        if (slots[I]) {
            return slots[I](argCount, args);
        }
        return NIL_VAL;
    }

    template <size_t... I>
    static constexpr auto make_dispatch_table(std::index_sequence<I...>) {
        return std::array<IsliNativeFn, sizeof...(I)>{ (&dispatcher<I>)... };
    }

    static inline const auto dispatch_table = make_dispatch_table(std::make_index_sequence<N>{});

    static IsliNativeFn allocate(std::function<Value(int, Value*)> fn) {
        size_t idx = next_slot % N;
        next_slot++;
        slots[idx] = std::move(fn);
        return dispatch_table[idx];
    }
};

using DefaultRegistry = FunctionRegistry<512>;

template <typename F>
IsliNativeFn wrap_callable(F&& func) {
    using Traits = function_traits<std::decay_t<F>>;
    using Ret = typename Traits::return_type;
    constexpr size_t Arity = Traits::arity;

    auto invoker = [f = std::forward<F>(func)](int argCount, Value* args) -> Value {
        if (argCount < static_cast<int>(Arity)) {
            return NIL_VAL;
        }
        return [&]<size_t... I>(std::index_sequence<I...>) -> Value {
            if constexpr (std::is_void_v<Ret>) {
                f(detail::value_traits<std::tuple_element_t<I, typename Traits::args_tuple>>::from(args[I])...);
                return NIL_VAL;
            } else {
                auto res = f(detail::value_traits<std::tuple_element_t<I, typename Traits::args_tuple>>::from(args[I])...);
                return detail::value_traits<std::decay_t<Ret>>::to(res);
            }
        }(std::make_index_sequence<Arity>{});
    };

    return DefaultRegistry::allocate(std::move(invoker));
}

} // namespace detail

// ── Sol2-Style Proxy Reference ────────────────────────────────────

class reference {
private:
    IsliState* L = nullptr;
    std::string key;

public:
    reference(IsliState* L, std::string key) : L(L), key(std::move(key)) {}

    // Assignment: vm["key"] = value; OR vm["func"] = [](int a) { return a * 2; };
    template <typename T>
    reference& operator=(T&& val) {
        using DecayedT = std::decay_t<T>;
        if constexpr (std::is_same_v<DecayedT, IsliNativeFn>) {
            isli_setcfunction(L, key.c_str(), val);
        } else if constexpr (detail::is_callable_v<DecayedT>) {
            IsliNativeFn nativeFn = detail::wrap_callable(std::forward<T>(val));
            isli_setcfunction(L, key.c_str(), nativeFn);
        } else {
            detail::type_traits<DecayedT>::push(L, std::forward<T>(val));
            isli_setglobal(L, key.c_str());
        }
        return *this;
    }

    // Implicit conversion: int x = vm["key"];
    template <typename T>
    operator T() const {
        return get<T>();
    }

    template <typename T>
    T get() const {
        if (!isli_getglobal(L, key.c_str())) {
            return T{};
        }
        T res = detail::type_traits<std::decay_t<T>>::get(L, -1);
        isli_pop(L, 1);
        return res;
    }

    template <typename T>
    bool is() const {
        if (!isli_getglobal(L, key.c_str())) {
            return false;
        }
        bool res = detail::type_traits<std::decay_t<T>>::is(L, -1);
        isli_pop(L, 1);
        return res;
    }

    bool valid() const {
        if (!isli_getglobal(L, key.c_str())) return false;
        bool isNil = isli_isnil(L, -1);
        isli_pop(L, 1);
        return !isNil;
    }
};

// ── Sol2-Style State (Main VM Encapsulation) ──────────────────────

class state {
private:
    IsliState* L = nullptr;
    bool owns_state = true;

public:
    state() {
        L = isli_open();
    }

    explicit state(IsliState* existing) : L(existing), owns_state(false) {}

    ~state() {
        if (owns_state && L != nullptr) {
            isli_close(L);
            L = nullptr;
        }
    }

    // Move semantics
    state(state&& other) noexcept : L(other.L), owns_state(other.owns_state) {
        other.L = nullptr;
        other.owns_state = false;
    }

    state& operator=(state&& other) noexcept {
        if (this != &other) {
            if (owns_state && L != nullptr) {
                isli_close(L);
            }
            L = other.L;
            owns_state = other.owns_state;
            other.L = nullptr;
            other.owns_state = false;
        }
        return *this;
    }

    // Disable copy semantics
    state(const state&) = delete;
    state& operator=(const state&) = delete;

    IsliState* lua_state() const noexcept { return L; }
    IsliState* isli_state() const noexcept { return L; }
    IsliState* raw_state() const noexcept { return L; }

    void open_libraries() {
        // Core standard libraries are initialized by default in isli_open()
    }

    // Execute script code directly
    bool script(const std::string& code) {
        return isli_dostring(L, code.c_str());
    }

    bool script_file(const std::string& filepath) {
        return isli_dofile(L, filepath.c_str());
    }

    bool do_string(const std::string& code) {
        return script(code);
    }

    bool do_file(const std::string& filepath) {
        return script_file(filepath);
    }

    // Proxy Indexing
    reference operator[](const std::string& key) {
        return reference(L, key);
    }

    reference operator[](const char* key) {
        return reference(L, std::string(key));
    }

    // Explicit binding methods (matching Sol2)
    template <typename F>
    void set_function(const std::string& name, F&& func) {
        (*this)[name] = std::forward<F>(func);
    }

    template <typename T>
    T get(const std::string& name) const {
        if (!isli_getglobal(L, name.c_str())) {
            return T{};
        }
        T res = detail::type_traits<std::decay_t<T>>::get(L, -1);
        isli_pop(L, 1);
        return res;
    }

    template <typename T>
    void set(const std::string& name, T&& val) {
        (*this)[name] = std::forward<T>(val);
    }
};

} // namespace isli

#endif // ISLI_HPP
