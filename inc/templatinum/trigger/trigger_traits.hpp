/******************************************************************************
* templatinum [trigger]                                    trigger_traits.hpp
*
* Trigger type traits:
*   Compile-time structural detection for types that satisfy the trigger
* interface. All detection is via void_t / decltype SFINAE — tagless,
* no inheritance, structural only.
*
* COMPONENTS:
*   templatinum::trigger_traits<_T>
*     - has_arm            : arm()
*     - has_disarm         : disarm()
*     - has_is_armed       : is_armed() const → bool
*     - has_on_fire        : on_fire(function) → callback_id
*     - has_remove_callback: remove_callback(callback_id) → bool
*     - has_callback_count : callback_count() const → size_t
*     - has_fire_count     : fire_count() const → size_t
*     - has_source_id      : source_id() const → string
*     - has_set_condition   : set_condition(bool) (edge only)
*     - has_set_cooldown   : set_cooldown(...) (debounced only)
*     - has_reset          : reset() (one_shot only)
*     - is_valid           : minimum trigger requirements
*     - is_full_trigger    : complete trigger interface
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES    - using aliases
*   D_ENV_CPP_FEATURE_LANG_VARIABLE_TEMPLATES - _v shortcuts
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\trigger_traits.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.26
******************************************************************************/

#ifndef TEMPLATINUM_TRIGGER_TRAITS_
#define TEMPLATINUM_TRIGGER_TRAITS_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "trigger_traits.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "trigger_traits.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "trigger_traits.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <type_traits>

// forward declarations
NS_DJINTERP
NS_TEMPLATINUM
    struct callback_id;
NS_END
NS_END


NS_DJINTERP
NS_TEMPLATINUM


// =========================================================================
// I.   INTERNAL DETECTION PRIMITIVES
// =========================================================================

NS_INTERNAL

    // ---- lifecycle detection ----

    // has_trig_arm
    //   trait: detects arm().
    template<typename _T,
             typename = void>
    struct has_trig_arm
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_arm<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().arm())>>
    {
        static constexpr bool value = true;
    };

    // has_trig_disarm
    //   trait: detects disarm().
    template<typename _T,
             typename = void>
    struct has_trig_disarm
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_disarm<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().disarm())>>
    {
        static constexpr bool value = true;
    };

    // has_trig_is_armed
    //   trait: detects is_armed() const → bool.
    template<typename _T,
             typename = void>
    struct has_trig_is_armed
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_is_armed<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .is_armed())>>
    {
        static constexpr bool value = true;
    };

    // ---- callback detection ----

    // has_trig_on_fire
    //   trait: detects on_fire(function) → callback_id.
    // Uses a loose check: detects the method name exists
    // and returns callback_id.
    template<typename _T,
             typename = void>
    struct has_trig_on_fire
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_on_fire<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().on_fire(
                std::declval<
                    std::function<void(int)>>()))>>
    {
        // note: the int argument is a dummy; we are
        // checking that on_fire accepts some callable.
        // the actual signature is
        // on_fire(function<void(const context_type&)>)
        // but context_type depends on the payload
        // parameter which we do not know here.
        static constexpr bool value = false;
    };

    // fallback: detect on_fire by address
    template<typename _T,
             typename = void>
    struct has_trig_on_fire_name
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_on_fire_name<_T,
        djinterp::void_t<
            decltype(sizeof(&_T::on_fire))>>
    {
        static constexpr bool value = true;
    };

    // has_trig_remove_callback
    //   trait: detects remove_callback(callback_id) → bool.
    template<typename _T,
             typename = void>
    struct has_trig_remove_callback
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_remove_callback<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .remove_callback(
                    std::declval<callback_id>()))>>
    {
        static constexpr bool value = true;
    };

    // has_trig_callback_count
    //   trait: detects callback_count() const → size_t.
    template<typename _T,
             typename = void>
    struct has_trig_callback_count
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_callback_count<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .callback_count())>>
    {
        static constexpr bool value = true;
    };

    // ---- introspection detection ----

    // has_trig_fire_count
    //   trait: detects fire_count() const → size_t.
    template<typename _T,
             typename = void>
    struct has_trig_fire_count
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_fire_count<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .fire_count())>>
    {
        static constexpr bool value = true;
    };

    // has_trig_source_id
    //   trait: detects source_id() const → string.
    template<typename _T,
             typename = void>
    struct has_trig_source_id
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_source_id<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .source_id())>>
    {
        static constexpr bool value = true;
    };

    // ---- policy-specific detection ----

    // has_trig_set_condition
    //   trait: detects set_condition(bool) (edge_policy).
    template<typename _T,
             typename = void>
    struct has_trig_set_condition
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_set_condition<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .set_condition(
                    std::declval<bool>()))>>
    {
        static constexpr bool value = true;
    };

    // has_trig_set_cooldown
    //   trait: detects set_cooldown_ms(size_t)
    // (debounced_policy).
    template<typename _T,
             typename = void>
    struct has_trig_set_cooldown
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_set_cooldown<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .set_cooldown_ms(
                    std::declval<std::size_t>()))>>
    {
        static constexpr bool value = true;
    };

    // has_trig_reset
    //   trait: detects reset() (one_shot_policy).
    template<typename _T,
             typename = void>
    struct has_trig_reset
    {
        static constexpr bool value = false;
    };

    template<typename _T>
    struct has_trig_reset<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().reset())>>
    {
        static constexpr bool value = true;
    };

NS_END  // internal


// =========================================================================
// II.  TRIGGER TRAITS
// =========================================================================

// trigger_traits
//   trait: compile-time structural detection for types that
// satisfy the trigger interface. Validates lifecycle, callback
// management, introspection, and policy-specific methods.
// Structural check only — no inheritance requirement.
template<typename _T>
struct trigger_traits
{
private:
    using clean_type = typename std::remove_cv<
                           typename std::remove_reference<
                               _T>::type>::type;

public:
    // ---- lifecycle detection ----

    // has_arm
    //   constant: true if _T has arm().
    static constexpr bool has_arm =
        internal::has_trig_arm<clean_type>::value;

    // has_disarm
    //   constant: true if _T has disarm().
    static constexpr bool has_disarm =
        internal::has_trig_disarm<clean_type>::value;

    // has_is_armed
    //   constant: true if _T has
    // is_armed() const → bool.
    static constexpr bool has_is_armed =
        internal::has_trig_is_armed<
            clean_type>::value;

    // ---- callback detection ----

    // has_on_fire
    //   constant: true if _T has an on_fire method.
    static constexpr bool has_on_fire =
        internal::has_trig_on_fire_name<
            clean_type>::value;

    // has_remove_callback
    //   constant: true if _T has
    // remove_callback(callback_id) → bool.
    static constexpr bool has_remove_callback =
        internal::has_trig_remove_callback<
            clean_type>::value;

    // has_callback_count
    //   constant: true if _T has
    // callback_count() const → size_t.
    static constexpr bool has_callback_count =
        internal::has_trig_callback_count<
            clean_type>::value;

    // ---- introspection detection ----

    // has_fire_count
    //   constant: true if _T has
    // fire_count() const → size_t.
    static constexpr bool has_fire_count =
        internal::has_trig_fire_count<
            clean_type>::value;

    // has_source_id
    //   constant: true if _T has
    // source_id() const → string.
    static constexpr bool has_source_id =
        internal::has_trig_source_id<
            clean_type>::value;

    // ---- policy-specific detection ----

    // has_set_condition
    //   constant: true if _T has set_condition(bool).
    // Present only for edge_policy triggers.
    static constexpr bool has_set_condition =
        internal::has_trig_set_condition<
            clean_type>::value;

    // has_set_cooldown
    //   constant: true if _T has set_cooldown_ms(size_t).
    // Present only for debounced_policy triggers.
    static constexpr bool has_set_cooldown =
        internal::has_trig_set_cooldown<
            clean_type>::value;

    // has_reset
    //   constant: true if _T has reset().
    // Present only for one_shot_policy triggers.
    static constexpr bool has_reset =
        internal::has_trig_reset<clean_type>::value;

    // ---- composite ----

    // is_valid
    //   constant: true if _T satisfies the minimum trigger
    // requirements: arm, disarm, is_armed.
    static constexpr bool is_valid =
        ( has_arm       &&
          has_disarm    &&
          has_is_armed );

    // is_full_trigger
    //   constant: true if _T provides the complete base
    // trigger interface (excluding policy-specific methods).
    static constexpr bool is_full_trigger =
        ( has_arm              &&
          has_disarm           &&
          has_is_armed         &&
          has_on_fire          &&
          has_remove_callback  &&
          has_callback_count   &&
          has_fire_count       &&
          has_source_id );

    // is_edge_trigger
    //   constant: true if _T is a valid trigger with edge
    // policy support (set_condition).
    static constexpr bool is_edge_trigger =
        ( is_valid &&
          has_set_condition );

    // is_debounced_trigger
    //   constant: true if _T is a valid trigger with
    // debounce support (set_cooldown).
    static constexpr bool is_debounced_trigger =
        ( is_valid &&
          has_set_cooldown );

    // is_one_shot_trigger
    //   constant: true if _T is a valid trigger with
    // one-shot support (reset).
    static constexpr bool is_one_shot_trigger =
        ( is_valid &&
          has_reset );
};


// =========================================================================
// III. CONVENIENCE ALIASES
// =========================================================================

#if D_ENV_CPP_FEATURE_LANG_VARIABLE_TEMPLATES

    // is_valid_trigger_v
    //   constant: shorthand for
    // trigger_traits<_T>::is_valid.
    template<typename _T>
    constexpr bool is_valid_trigger_v =
        trigger_traits<_T>::is_valid;

    // is_full_trigger_v
    //   constant: shorthand for
    // trigger_traits<_T>::is_full_trigger.
    template<typename _T>
    constexpr bool is_full_trigger_v =
        trigger_traits<_T>::is_full_trigger;

    // is_edge_trigger_v
    //   constant: shorthand for
    // trigger_traits<_T>::is_edge_trigger.
    template<typename _T>
    constexpr bool is_edge_trigger_v =
        trigger_traits<_T>::is_edge_trigger;

    // is_debounced_trigger_v
    //   constant: shorthand for
    // trigger_traits<_T>::is_debounced_trigger.
    template<typename _T>
    constexpr bool is_debounced_trigger_v =
        trigger_traits<_T>::is_debounced_trigger;

    // is_one_shot_trigger_v
    //   constant: shorthand for
    // trigger_traits<_T>::is_one_shot_trigger.
    template<typename _T>
    constexpr bool is_one_shot_trigger_v =
        trigger_traits<_T>::is_one_shot_trigger;

#endif  // D_ENV_CPP_FEATURE_LANG_VARIABLE_TEMPLATES


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_TRIGGER_TRAITS_
