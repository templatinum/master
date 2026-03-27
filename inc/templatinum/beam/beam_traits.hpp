/******************************************************************************
* templatinum [beam]                                        beam_traits.hpp
*
* Beam type traits:
*   Compile-time structural detection for types satisfying the beam
* interface (beam = pipeline). Tagless, no inheritance, SFINAE only.
*
* COMPONENTS:
*   templatinum::beam_traits<_T>
*     - has_start          : start()
*     - has_stop           : stop()
*     - has_pause          : pause()
*     - has_resume         : resume()
*     - has_is_running     : is_running() const → bool
*     - has_state          : state() const
*     - has_stage_count    : stage_count() const → size_t
*     - has_enable_stage   : enable_stage(stage_id) → bool
*     - has_disable_stage  : disable_stage(stage_id) → bool
*     - has_has_sink       : has_sink() const → bool
*     - has_get_stats      : get_stats() const
*     - has_on_error       : on_error(function)
*     - is_valid           : minimum beam requirements
*     - is_full_beam       : complete beam interface
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\beam_traits.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.27
******************************************************************************/

#ifndef TEMPLATINUM_BEAM_TRAITS_
#define TEMPLATINUM_BEAM_TRAITS_ 1

#ifndef DJINTERP_
    #error "beam_traits.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "beam_traits.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "beam_traits.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <type_traits>

NS_DJINTERP
NS_TEMPLATINUM
    struct stage_id;
NS_END
NS_END


NS_DJINTERP
NS_TEMPLATINUM


// =========================================================================
// I.   INTERNAL DETECTION PRIMITIVES
// =========================================================================

NS_INTERNAL

    // ---- lifecycle ----

    template<typename _T, typename = void>
    struct has_beam_start
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_start<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().start())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_stop
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_stop<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().stop())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_pause
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_pause<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().pause())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_resume
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_resume<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().resume())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_is_running
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_is_running<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .is_running())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_state
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_state<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .state())>>
    { static constexpr bool value = true; };

    // ---- stages ----

    template<typename _T, typename = void>
    struct has_beam_stage_count
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_stage_count<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .stage_count())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_enable_stage
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_enable_stage<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .enable_stage(
                    std::declval<stage_id>()))>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_disable_stage
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_disable_stage<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .disable_stage(
                    std::declval<stage_id>()))>>
    { static constexpr bool value = true; };

    // ---- sink ----

    template<typename _T, typename = void>
    struct has_beam_has_sink
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_has_sink<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .has_sink())>>
    { static constexpr bool value = true; };

    // ---- stats and error ----

    template<typename _T, typename = void>
    struct has_beam_get_stats
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_get_stats<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .get_stats())>>
    { static constexpr bool value = true; };

    template<typename _T, typename = void>
    struct has_beam_on_error
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_beam_on_error<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().on_error(
                std::declval<
                    std::function<
                        void(stage_id, int)>>()))>>
    { static constexpr bool value = true; };

NS_END  // internal


// =========================================================================
// II.  BEAM TRAITS
// =========================================================================

// beam_traits
//   trait: structural detection for the beam (= pipeline)
// interface.
template<typename _T>
struct beam_traits
{
private:
    using clean_type = typename std::remove_cv<
                           typename std::remove_reference<
                               _T>::type>::type;

public:
    static constexpr bool has_start =
        internal::has_beam_start<
            clean_type>::value;

    static constexpr bool has_stop =
        internal::has_beam_stop<
            clean_type>::value;

    static constexpr bool has_pause =
        internal::has_beam_pause<
            clean_type>::value;

    static constexpr bool has_resume =
        internal::has_beam_resume<
            clean_type>::value;

    static constexpr bool has_is_running =
        internal::has_beam_is_running<
            clean_type>::value;

    static constexpr bool has_state =
        internal::has_beam_state<
            clean_type>::value;

    static constexpr bool has_stage_count =
        internal::has_beam_stage_count<
            clean_type>::value;

    static constexpr bool has_enable_stage =
        internal::has_beam_enable_stage<
            clean_type>::value;

    static constexpr bool has_disable_stage =
        internal::has_beam_disable_stage<
            clean_type>::value;

    static constexpr bool has_has_sink =
        internal::has_beam_has_sink<
            clean_type>::value;

    static constexpr bool has_get_stats =
        internal::has_beam_get_stats<
            clean_type>::value;

    static constexpr bool has_on_error =
        internal::has_beam_on_error<
            clean_type>::value;

    // is_valid
    //   constant: minimum beam requirements: lifecycle
    // (start, stop) and stage management (stage_count).
    static constexpr bool is_valid =
        ( has_start       &&
          has_stop        &&
          has_stage_count );

    // is_full_beam
    //   constant: complete beam interface.
    static constexpr bool is_full_beam =
        ( has_start         &&
          has_stop          &&
          has_pause         &&
          has_resume        &&
          has_is_running    &&
          has_state         &&
          has_stage_count   &&
          has_enable_stage  &&
          has_disable_stage &&
          has_has_sink      &&
          has_get_stats     &&
          has_on_error );
};


// =========================================================================
// III. CONVENIENCE ALIASES
// =========================================================================

#if D_ENV_CPP_FEATURE_LANG_VARIABLE_TEMPLATES

    template<typename _T>
    constexpr bool is_valid_beam_v =
        beam_traits<_T>::is_valid;

    template<typename _T>
    constexpr bool is_full_beam_v =
        beam_traits<_T>::is_full_beam;

#endif


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_BEAM_TRAITS_
