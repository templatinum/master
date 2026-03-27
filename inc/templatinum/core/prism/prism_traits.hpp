/******************************************************************************
* templatinum [prism]                                      prism_traits.hpp
*
* Prism type traits:
*   Compile-time structural detection for split and merge prism types.
* Tagless, no inheritance, SFINAE only.
*
* COMPONENTS:
*   templatinum::split_prism_traits<_T>
*     - has_feed, has_output_count, has_clear
*     - is_valid
*
*   templatinum::merge_prism_traits<_T>
*     - has_push, has_flush, has_has_output,
*       has_buffered_count
*     - is_valid
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\prism_traits.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.27
******************************************************************************/

#ifndef TEMPLATINUM_PRISM_TRAITS_
#define TEMPLATINUM_PRISM_TRAITS_ 1

#ifndef DJINTERP_
    #error "prism_traits.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "prism_traits.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "prism_traits.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <type_traits>

NS_DJINTERP
NS_TEMPLATINUM
    struct route_id;
NS_END
NS_END


NS_DJINTERP
NS_TEMPLATINUM


// =========================================================================
// I.   SPLIT PRISM DETECTION
// =========================================================================

NS_INTERNAL

    // has_split_feed
    //   trait: detects a feed() method (any signature).
    template<typename _T, typename = void>
    struct has_split_feed
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_split_feed<_T,
        djinterp::void_t<
            decltype(sizeof(&_T::feed))>>
    { static constexpr bool value = true; };

    // has_split_output_count
    //   trait: detects output_count() const.
    template<typename _T, typename = void>
    struct has_split_output_count
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_split_output_count<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .output_count())>>
    { static constexpr bool value = true; };

    // has_split_remove_output
    //   trait: detects remove_output(route_id) → bool.
    template<typename _T, typename = void>
    struct has_split_remove_output
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_split_remove_output<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>()
                .remove_output(
                    std::declval<route_id>()))>>
    { static constexpr bool value = true; };

    // has_split_clear
    //   trait: detects clear().
    template<typename _T, typename = void>
    struct has_split_clear
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_split_clear<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().clear())>>
    { static constexpr bool value = true; };

NS_END  // internal


// split_prism_traits
//   trait: structural detection for split prisms.
template<typename _T>
struct split_prism_traits
{
private:
    using clean_type = typename std::remove_cv<
                           typename std::remove_reference<
                               _T>::type>::type;

public:
    static constexpr bool has_feed =
        internal::has_split_feed<
            clean_type>::value;

    static constexpr bool has_output_count =
        internal::has_split_output_count<
            clean_type>::value;

    static constexpr bool has_remove_output =
        internal::has_split_remove_output<
            clean_type>::value;

    static constexpr bool has_clear =
        internal::has_split_clear<
            clean_type>::value;

    // is_valid
    //   constant: minimum split prism: feed + output_count.
    static constexpr bool is_valid =
        ( has_feed         &&
          has_output_count );
};


// =========================================================================
// II.  MERGE PRISM DETECTION
// =========================================================================

NS_INTERNAL

    // has_merge_push
    //   trait: detects push() method.
    template<typename _T, typename = void>
    struct has_merge_push
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_merge_push<_T,
        djinterp::void_t<
            decltype(sizeof(&_T::push))>>
    { static constexpr bool value = true; };

    // has_merge_flush
    //   trait: detects flush().
    template<typename _T, typename = void>
    struct has_merge_flush
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_merge_flush<_T,
        djinterp::void_t<
            decltype(std::declval<_T&>().flush())>>
    { static constexpr bool value = true; };

    // has_merge_has_output
    //   trait: detects has_output() const → bool.
    template<typename _T, typename = void>
    struct has_merge_has_output
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_merge_has_output<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .has_output())>>
    { static constexpr bool value = true; };

    // has_merge_buffered_count
    //   trait: detects buffered_count() const.
    template<typename _T, typename = void>
    struct has_merge_buffered_count
    { static constexpr bool value = false; };

    template<typename _T>
    struct has_merge_buffered_count<_T,
        djinterp::void_t<
            decltype(std::declval<const _T&>()
                .buffered_count())>>
    { static constexpr bool value = true; };

NS_END  // internal


// merge_prism_traits
//   trait: structural detection for merge prisms.
template<typename _T>
struct merge_prism_traits
{
private:
    using clean_type = typename std::remove_cv<
                           typename std::remove_reference<
                               _T>::type>::type;

public:
    static constexpr bool has_push =
        internal::has_merge_push<
            clean_type>::value;

    static constexpr bool has_flush =
        internal::has_merge_flush<
            clean_type>::value;

    static constexpr bool has_has_output =
        internal::has_merge_has_output<
            clean_type>::value;

    static constexpr bool has_buffered_count =
        internal::has_merge_buffered_count<
            clean_type>::value;

    // is_valid
    //   constant: minimum merge prism: push + has_output.
    static constexpr bool is_valid =
        ( has_push       &&
          has_has_output );
};


// =========================================================================
// III. CONVENIENCE ALIASES
// =========================================================================

#if D_ENV_CPP_FEATURE_LANG_VARIABLE_TEMPLATES

    template<typename _T>
    constexpr bool is_valid_split_prism_v =
        split_prism_traits<_T>::is_valid;

    template<typename _T>
    constexpr bool is_valid_merge_prism_v =
        merge_prism_traits<_T>::is_valid;

#endif


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_PRISM_TRAITS_
