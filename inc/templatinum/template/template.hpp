/******************************************************************************
* templatinum [template]                                         template.hpp
*
* CRTP template base and template composition (C++).
*   Provides the foundational transformation abstraction for the templatinum
* framework. A template is a typed transformation: it accepts input of type
* _In and produces output of type _Out. The CRTP base class provides
* operator(), transform_batch(), and composition via operator>>. Concrete
* templates inherit from the base with themselves as the _Derived parameter
* and implement transform_impl().
*
*   No virtual dispatch is used anywhere. All method resolution is performed
* at compile time via CRTP static dispatch and std::function type-erasure
* (for runtime-polymorphic composition). Structural detection is enforced
* via template_traits and static_assert.
*
*   Templates compose naturally via operator>>. Given templates A : X → Y
* and B : Y → Z, the expression (A >> B) produces a composed template
* C : X → Z. This composition is type-safe: mismatched intermediate types
* are caught at compile time.
*
*   Templates satisfy the callable interface required by djinterp's
* pipeline::map() and fn_builder, so they integrate directly into existing
* functional chains.
*
* COMPONENTS:
*   templatinum::tmpl_template<_Derived, _In, _Out>
*     - CRTP base; provides operator(), transform_batch(),
*       and chaining via operator>>
*
*   templatinum::tmpl_composed_template<_First, _Second>
*     - result of chaining two templates (A >> B); holds
*       references, fully typed at compile time
*
*   templatinum::tmpl_identity_template<_T>
*     - passthrough template; returns input unchanged
*
*   templatinum::tmpl_fn_template<_In, _Out>
*     - wraps any callable as a template
*
*   templatinum::tmpl_erased_template<_In, _Out>
*     - type-erased template via std::function; replaces
*       virtual base pointers for runtime polymorphism
*
*   templatinum::make_template / make_erased (factories)
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_RVALUE_REFERENCES   - move semantics
*   D_ENV_CPP_FEATURE_LANG_VARIADIC_TEMPLATES  - parameter packs
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES     - using aliases
*   D_ENV_CPP_FEATURE_LANG_LAMBDAS             - lambda callbacks
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\template.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.25
******************************************************************************/

#ifndef TEMPLATINUM_TEMPLATE_
#define TEMPLATINUM_TEMPLATE_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "template.hpp requires djinterp.h to be included first"
#endif

#ifndef __cplusplus
    #error "template.hpp can only be used in C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "template.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "template_traits.hpp"


NS_DJINTERP
NS_TEMPLATINUM


// forward declarations
template<typename _First,
         typename _Second>
class tmpl_composed_template;


///////////////////////////////////////////////////////////////////////////////
///             I.    TEMPLATE BASE CLASS (CRTP)                            ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_template
//   class: CRTP base for all templatinum templates. Provides the
// shared interface (operator(), transform_batch(), operator>>)
// and dispatches the core transform operation to the derived
// class at compile time via static_cast.
//
//   Derived classes must provide:
//     _Out transform_impl(const _In& _input)
//
//   Derived classes may optionally provide:
//     std::vector<_Out>
//     transform_batch_impl(const std::vector<_In>& _inputs)
//
//   If transform_batch_impl is not provided, the default
// implementation iterates and calls transform_impl() for each
// element.
//
// usage:
//   class my_template
//       : public tmpl_template<my_template, int, string>
//   {
//   public:
//       string transform_impl(const int& _input)
//       {
//           return std::to_string(_input);
//       };
//   };
template<typename _Derived,
         typename _In,
         typename _Out>
class tmpl_template
{
public:
    using input_type  = _In;
    using output_type = _Out;

    // ---- core transformation ----

    // transform
    //   method: dispatches to _Derived::transform_impl()
    // via CRTP static cast.
    _Out transform(const _In& _input)
    {
        return derived().transform_impl(_input);
    };

    // transform_batch
    //   method: applies transform to each element in a
    // collection. Dispatches to _Derived::transform_batch_impl
    // if available, otherwise iterates with transform_impl().
    std::vector<_Out>
    transform_batch(const std::vector<_In>& _inputs)
    {
        return transform_batch_dispatch(
            _inputs, nullptr);
    };

    // ---- callable interface ----

    // operator()
    //   method: delegates to transform(), enabling use as a
    // callable in pipeline::map(), fn_builder, and standard
    // algorithms.
    _Out operator()(const _In& _input)
    {
        return transform(_input);
    };

    // ---- composition ----

    // operator>>
    //   method: composes this template with _next, producing
    // a new template that applies this transform first, then
    // _next. Type-safe: _Out of this must match input_type
    // of _next.
    //
    // usage:
    //   auto composed = parser >> formatter;
    //   auto result   = composed.transform(source_file);
    template<typename _NextDerived,
             typename _NextOut>
    tmpl_composed_template<_Derived,
                           _NextDerived>
    operator>>(tmpl_template<_NextDerived,
                             _Out,
                             _NextOut>& _next)
    {
        return tmpl_composed_template<
            _Derived, _NextDerived>(
                derived(),
                static_cast<_NextDerived&>(_next));
    };

private:
    // ---- CRTP accessors ----

    _Derived& derived()
    {
        return static_cast<_Derived&>(*this);
    };

    const _Derived& derived() const
    {
        return static_cast<const _Derived&>(*this);
    };

    // ---- batch dispatch (SFINAE priority) ----

    // priority overload: _Derived has transform_batch_impl
    template<typename _D = _Derived>
    auto transform_batch_dispatch(
        const std::vector<_In>& _inputs,
        decltype(
            std::declval<_D>().transform_batch_impl(
                std::declval<
                    const std::vector<_In>&>()),
            nullptr)
    ) -> std::vector<_Out>
    {
        return derived().transform_batch_impl(_inputs);
    };

    // fallback: iterate with transform_impl
    std::vector<_Out>
    transform_batch_dispatch(
        const std::vector<_In>& _inputs,
        ...)
    {
        std::vector<_Out> results;

        results.reserve(_inputs.size());

        for (const auto& input : _inputs)
        {
            results.push_back(
                derived().transform_impl(input));
        }

        return results;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   COMPOSED TEMPLATE                                     ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_composed_template
//   class: the result of chaining two templates via operator>>.
// Both component types are fully known at compile time — no
// virtual dispatch or type-erasure. Given _First : A → B and
// _Second : B → C, this template maps A → C.
//
//   Composition is associative: (A >> B) >> C and A >> (B >> C)
// produce equivalent transformations.
template<typename _First,
         typename _Second>
class tmpl_composed_template
    : public tmpl_template<
          tmpl_composed_template<_First, _Second>,
          typename _First::input_type,
          typename _Second::output_type>
{
public:
    using input_type  = typename _First::input_type;
    using output_type = typename _Second::output_type;
    using mid_type    = typename _First::output_type;

    static_assert(
        std::is_same<
            typename _First::output_type,
            typename _Second::input_type>::value,
        "Composed template type mismatch: output_type "
        "of _First must match input_type of _Second.");

    // constructor
    //   constructs a composed template from two template
    // references. Both templates must outlive this composed
    // template.
    tmpl_composed_template(_First&  _first,
                           _Second& _second)
        : m_first(_first)
        , m_second(_second)
    {
    };

    // transform_impl
    //   method: applies the first template, then the second.
    output_type
    transform_impl(const input_type& _input)
    {
        return m_second.transform(
            m_first.transform(_input));
    };

    // transform_batch_impl
    //   method: batch-composes through both stages. Each
    // stage uses its own batch implementation if available.
    std::vector<output_type>
    transform_batch_impl(
        const std::vector<input_type>& _inputs)
    {
        std::vector<mid_type> intermediate =
            m_first.transform_batch(_inputs);

        return m_second.transform_batch(intermediate);
    };

    // ---- component access ----

    _First& first()               { return m_first; };
    const _First& first() const   { return m_first; };
    _Second& second()             { return m_second; };
    const _Second& second() const { return m_second; };

private:
    _First&  m_first;
    _Second& m_second;
};


///////////////////////////////////////////////////////////////////////////////
///             III.  IDENTITY TEMPLATE                                     ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_identity_template
//   class: passthrough template that returns its input unchanged.
// Useful as a no-op in composition chains, for tap/logging
// injection points, and as a default template argument.
template<typename _T>
class tmpl_identity_template
    : public tmpl_template<
          tmpl_identity_template<_T>, _T, _T>
{
public:
    using input_type  = _T;
    using output_type = _T;

    // transform_impl
    //   method: returns the input unchanged.
    _T transform_impl(const _T& _input)
    {
        return _input;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             IV.   FUNCTION TEMPLATE WRAPPER                             ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_fn_template
//   class: wraps any callable (lambda, function pointer, functor)
// as a templatinum template via CRTP. This bridges the gap between
// djinterp's functional utilities and templatinum's template
// system: any function that maps _In → _Out can be used as a
// template node in a pipeline.
//
// usage:
//   auto doubler = tmpl_fn_template<int, int>(
//       [](const int& x) { return x * 2; });
//
//   // or via factory:
//   auto doubler = make_template<int, int>(
//       [](const int& x) { return x * 2; });
template<typename _In,
         typename _Out>
class tmpl_fn_template
    : public tmpl_template<
          tmpl_fn_template<_In, _Out>, _In, _Out>
{
public:
    using input_type    = _In;
    using output_type   = _Out;
    using function_type = std::function<_Out(const _In&)>;

    // constructor (callable)
    //   constructs from any callable that accepts const _In&
    // and returns _Out.
    template<typename _Fn,
             typename = typename std::enable_if<
                 !std::is_same<
                     typename std::decay<_Fn>::type,
                     tmpl_fn_template>::value
             >::type>
    explicit tmpl_fn_template(_Fn&& _fn)
        : m_fn(std::forward<_Fn>(_fn))
    {
    };

    // transform_impl
    //   method: delegates to the wrapped callable.
    _Out transform_impl(const _In& _input)
    {
        return m_fn(_input);
    };

    // function
    //   method: returns a const reference to the underlying
    // std::function.
    const function_type& function() const
    {
        return m_fn;
    };

private:
    function_type m_fn;
};


///////////////////////////////////////////////////////////////////////////////
///             V.    TYPE-ERASED TEMPLATE                                  ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_erased_template
//   class: type-erased template that uses std::function
// internally instead of virtual dispatch. This replaces the
// role that virtual base pointers would play in runtime-
// polymorphic scenarios (e.g., storing heterogeneous templates
// in a container, or composing templates whose concrete types
// are not known at compile time).
//
//   Constructed from any type satisfying template_traits
// (has transform / input_type / output_type) via the
// factory functions below.
//
// usage:
//   tmpl_fn_template<int, int> doubler(...);
//   auto erased =
//       tmpl_erased_template<int, int>::from(doubler);
//   int result = erased.transform(21);  // 42
template<typename _In,
         typename _Out>
class tmpl_erased_template
    : public tmpl_template<
          tmpl_erased_template<_In, _Out>, _In, _Out>
{
public:
    using input_type    = _In;
    using output_type   = _Out;
    using function_type = std::function<_Out(const _In&)>;

    // constructor (from callable)
    //   constructs from any callable _In → _Out.
    template<typename _Fn,
             typename = typename std::enable_if<
                 !std::is_same<
                     typename std::decay<_Fn>::type,
                     tmpl_erased_template>::value
             >::type>
    explicit tmpl_erased_template(_Fn&& _fn)
        : m_fn(std::forward<_Fn>(_fn))
    {
    };

    // from (template)
    //   factory: captures a concrete template's transform
    // method. The source template must outlive this erased
    // wrapper (captured by pointer).
    template<typename _Tmpl>
    static tmpl_erased_template from(_Tmpl& _tmpl)
    {
        static_assert(
            template_traits<_Tmpl>::is_valid,
            "from() requires a type satisfying "
            "template_traits::is_valid.");

        auto* ptr = &_tmpl;

        return tmpl_erased_template(
            [ptr](const _In& _input) -> _Out
            {
                return ptr->transform(_input);
            });
    };

    // transform_impl
    //   method: delegates to the type-erased callable.
    _Out transform_impl(const _In& _input)
    {
        return m_fn(_input);
    };

    // function
    //   method: returns a const reference to the underlying
    // std::function.
    const function_type& function() const
    {
        return m_fn;
    };

private:
    function_type m_fn;
};


///////////////////////////////////////////////////////////////////////////////
///             VI.   CONVENIENCE FACTORIES                                 ///
///////////////////////////////////////////////////////////////////////////////

// make_template
//   function: creates a tmpl_fn_template from any callable.
template<typename _In,
         typename _Out,
         typename _Fn>
tmpl_fn_template<_In, _Out>
make_template(_Fn&& _fn)
{
    return tmpl_fn_template<_In, _Out>(
        std::forward<_Fn>(_fn));
}

// make_erased
//   function: creates a type-erased template from any callable.
template<typename _In,
         typename _Out,
         typename _Fn>
tmpl_erased_template<_In, _Out>
make_erased(_Fn&& _fn)
{
    return tmpl_erased_template<_In, _Out>(
        std::forward<_Fn>(_fn));
}

// compose_erased
//   function: composes two type-erased templates into a single
// type-erased template.
template<typename _In,
         typename _Mid,
         typename _Out>
tmpl_erased_template<_In, _Out>
compose_erased(
    tmpl_erased_template<_In, _Mid>  _first,
    tmpl_erased_template<_Mid, _Out> _second)
{
    return tmpl_erased_template<_In, _Out>(
        [first  = std::move(_first),
         second = std::move(_second)]
        (const _In& _input) mutable -> _Out
        {
            return second.transform(
                first.transform(_input));
        });
}


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_TEMPLATE_
