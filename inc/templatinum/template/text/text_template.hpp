/******************************************************************************
* templatinum [template]                                  text_template.hpp
*
* Marker-aware text templating engine (C++).
*   Typed specialization of tmpl_template<string, string> providing
* configurable prefix/suffix markers (e.g. "{{" / "}}", "%" / "%",
* "${" / "}"), bindings that map keys to plain string values, nested
* templates for recursive expansion, user-supplied functions for
* computed content, or list bindings that iterate a sub-template over
* a counted sequence with per-item callbacks.
*
*   C++ port of the C text_template.h module with the following
* changes from the C version:
*   - std::string replaces char*/size_t pairs throughout
*   - std::unordered_map replaces the manually-grown binding array
*   - std::function replaces raw function pointers
*   - std::variant (C++17) or tagged union replaces the C union
*   - Inherits from tmpl_template via CRTP, so transform_impl()
*     performs render() with the input as the format string
*   - No virtual dispatch; all resolution is compile-time
*
*   List bindings enable iteration without adding loop syntax to
* templates. A key is bound to a sub-template, count, and per-item
* callback. During rendering, the renderer loops `count` times:
* clears the sub-template, injects auto-keys (_index, _number,
* _count, _is_first, _is_last), invokes the callback to bind
* domain-specific keys, renders the sub-template, and appends the
* result. An optional separator string is inserted between items.
*
* COMPONENTS:
*   templatinum::marker_config
*     - prefix/suffix marker strings
*
*   templatinum::list_options
*     - separator, empty_text, number formatting
*
*   templatinum::text_template_error
*     - error codes for rendering failures
*
*   templatinum::render_result
*     - result type carrying either a rendered string or an error
*
*   templatinum::tmpl_text_template
*     - marker-aware text template with bindings and rendering
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_RVALUE_REFERENCES  - move semantics
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES    - using aliases
*   D_ENV_CPP_FEATURE_LANG_LAMBDAS            - lambda callbacks
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\text_template.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.25
******************************************************************************/

#ifndef TEMPLATINUM_TEXT_TEMPLATE_
#define TEMPLATINUM_TEXT_TEMPLATE_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "text_template.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "text_template.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "text_template.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "template.hpp"
#include "text_template_traits.hpp"


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    CONSTANTS AND DEFAULTS                                ///
///////////////////////////////////////////////////////////////////////////////

// D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH
//   constant: initial maximum recursion depth for nested template
// expansion. Shared across all nesting mechanisms: nested template
// bindings, function bindings, and list bindings.
#ifndef D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH
    #define D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH 16
#endif

// D_TEXT_TEMPLATE_CPP_DEFAULT_PREFIX
//   constant: default prefix marker string.
#ifndef D_TEXT_TEMPLATE_CPP_DEFAULT_PREFIX
    #define D_TEXT_TEMPLATE_CPP_DEFAULT_PREFIX "%"
#endif

// D_TEXT_TEMPLATE_CPP_DEFAULT_SUFFIX
//   constant: default suffix marker string.
#ifndef D_TEXT_TEMPLATE_CPP_DEFAULT_SUFFIX
    #define D_TEXT_TEMPLATE_CPP_DEFAULT_SUFFIX "%"
#endif


///////////////////////////////////////////////////////////////////////////////
///             II.   ENUMERATIONS                                         ///
///////////////////////////////////////////////////////////////////////////////

// text_template_error
//   enum: error codes returned by rendering operations.
enum class text_template_error
{
    success = 0,
    null_param,
    key_not_found,
    nesting_too_deep,
    invalid_marker,
    cycle_detected,
    function_failed,
    list_callback_failed,
    list_item_render_failed
};


///////////////////////////////////////////////////////////////////////////////
///             III.  SUPPORTING STRUCTURES                                 ///
///////////////////////////////////////////////////////////////////////////////

// marker_config
//   struct: prefix and suffix marker strings used to delimit
// specifier keys within a template string. For example,
// prefix="{{" and suffix="}}" would match "{{name}}".
struct marker_config
{
    std::string prefix;
    std::string suffix;

    marker_config()
        : prefix(D_TEXT_TEMPLATE_CPP_DEFAULT_PREFIX)
        , suffix(D_TEXT_TEMPLATE_CPP_DEFAULT_SUFFIX)
    {
    };

    marker_config(std::string _prefix,
                  std::string _suffix)
        : prefix(std::move(_prefix))
        , suffix(std::move(_suffix))
    {
    };
};

// list_options
//   struct: configuration for list binding rendering behavior.
// All fields have sensible defaults when default-constructed.
//
// defaults:
//   - no separator between items
//   - empty string when count is 0
//   - _number starts at 1 (1-based)
//   - _number zero-padded to digit width of count
//   - pad character is '0'
struct list_options
{
    // string inserted between consecutive rendered items;
    // empty means no separator
    std::string separator;

    // string rendered when the item count is 0; allows
    // displaying "(no items)" or similar placeholder text
    std::string empty_text;

    // minimum width for the _number auto-key; 0 means
    // auto-detect from the digit width of count
    std::size_t number_pad_width;

    // character used for padding _number; default is '0'
    char        number_pad_char;

    // when true, _number starts at 0 instead of 1;
    // _index is always 0-based regardless
    bool        number_from_zero;

    list_options()
        : separator()
        , empty_text()
        , number_pad_width(0)
        , number_pad_char('0')
        , number_from_zero(false)
    {
    };
};

// render_result
//   struct: result type for rendering operations. Carries either
// a successfully rendered string or an error code. Provides a
// lightweight alternative to exceptions for error reporting.
struct render_result
{
    std::string          output;
    text_template_error  error;

    // is_ok
    //   method: returns true if rendering succeeded.
    bool is_ok() const
    {
        return error == text_template_error::success;
    };

    // is_error
    //   method: returns true if rendering failed.
    bool is_error() const
    {
        return error != text_template_error::success;
    };

    // value_or
    //   method: returns the rendered output on success, or
    // the provided fallback on error.
    const std::string& value_or(
        const std::string& _fallback) const
    {
        if (is_ok())
        {
            return output;
        }

        return _fallback;
    };

    // ok (factory)
    //   function: creates a successful render_result.
    static render_result ok(std::string _output)
    {
        render_result r;

        r.output = std::move(_output);
        r.error  = text_template_error::success;

        return r;
    };

    // fail (factory)
    //   function: creates a failed render_result.
    static render_result fail(text_template_error _error)
    {
        render_result r;

        r.output = std::string();
        r.error  = _error;

        return r;
    };

private:
    render_result()
        : output()
        , error(text_template_error::success)
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             IV.   BINDING TYPES                                         ///
///////////////////////////////////////////////////////////////////////////////

// forward declaration
class tmpl_text_template;

// binding_type
//   enum: the type of value a binding maps to.
enum class binding_type
{
    string_value,
    template_ref,
    function_value,
    list_value
};

// list_bind_fn
//   type: per-item callback for list bindings. Invoked once per
// item to bind domain-specific keys onto the item template.
// Returns true to continue iteration, false to abort.
//
// parameters:
//   _index         : 0-based item index
//   _count         : total number of items
//   _item_template : the sub-template to bind keys onto
using list_bind_fn = std::function<bool(
    std::size_t          _index,
    std::size_t          _count,
    tmpl_text_template&  _item_template)>;

NS_INTERNAL

    // list_binding_data
    //   struct: data payload for a list binding. Holds the
    // sub-template pointer, iteration count, per-item
    // callback, and rendering options.
    struct list_binding_data
    {
        tmpl_text_template* item_template;
        std::size_t         count;
        list_bind_fn        bind_fn;
        list_options        options;

        list_binding_data()
            : item_template(nullptr)
            , count(0)
            , bind_fn()
            , options()
        {
        };
    };

    // binding_value
    //   struct: tagged storage for a single binding's value.
    // Uses the binding_type tag to determine which member is
    // active. In C++17, this could be a std::variant; this
    // manual tagged union provides C++11 compatibility.
    struct binding_value
    {
        binding_type type;

        // string binding: the replacement text
        std::string string_val;

        // template binding: non-owning pointer to another
        // text template (caller manages lifetime)
        tmpl_text_template* template_ptr;

        // function binding: callable that produces the
        // replacement string at render time
        std::function<std::string()> function_val;

        // list binding: sub-template, count, callback,
        // options
        list_binding_data list_val;

        binding_value()
            : type(binding_type::string_value)
            , string_val()
            , template_ptr(nullptr)
            , function_val()
            , list_val()
        {
        };
    };

NS_END  // internal


///////////////////////////////////////////////////////////////////////////////
///             V.    TEXT TEMPLATE CLASS                                   ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_text_template
//   class: marker-aware text template with configurable delimiters
// and a dynamic binding map. Inherits from tmpl_template<string,
// string> so that transform() performs render() with the input as
// the format string, enabling seamless use in pipelines and
// composition chains.
//
//   Bindings map key names to one of four value types:
//   - string: plain text replacement
//   - template: recursive expansion of a nested template
//   - function: computed replacement via a callable
//   - list: iterated rendering of a sub-template
//
// usage:
//   tmpl_text_template t("{{", "}}");
//   t.bind_string("name", "world");
//   auto result = t.render("Hello, {{name}}!");
//   // result.output == "Hello, world!"
class tmpl_text_template
    : public tmpl_template<tmpl_text_template,
                         std::string,
                         std::string>
{
public:
    using input_type         = std::string;
    using output_type        = std::string;
    using marker_config_type = marker_config;

    // ---- construction ----

    // default constructor
    //   constructs with default markers ("%" / "%").
    tmpl_text_template()
        : m_markers()
        , m_bindings()
        , m_max_depth(
              D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH)
    {
    };

    // marker constructor
    //   constructs with the specified prefix and suffix.
    tmpl_text_template(std::string _prefix,
                       std::string _suffix)
        : m_markers(std::move(_prefix),
                    std::move(_suffix))
        , m_bindings()
        , m_max_depth(
              D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH)
    {
    };

    // marker_config constructor
    //   constructs from an existing marker_config.
    explicit tmpl_text_template(marker_config _config)
        : m_markers(std::move(_config))
        , m_bindings()
        , m_max_depth(
              D_TEXT_TEMPLATE_CPP_DEFAULT_NESTING_DEPTH)
    {
    };



    // =========================================================
    // MARKER CONFIGURATION
    // =========================================================

    // set_markers
    //   method: replaces the current prefix and suffix markers.
    void set_markers(const std::string& _prefix,
                     const std::string& _suffix)
    {
        m_markers.prefix = _prefix;
        m_markers.suffix = _suffix;

        return;
    };

    // get_prefix
    //   method: returns the current prefix marker.
    const std::string& get_prefix() const
    {
        return m_markers.prefix;
    };

    // get_suffix
    //   method: returns the current suffix marker.
    const std::string& get_suffix() const
    {
        return m_markers.suffix;
    };

    // markers
    //   method: returns a const reference to the full marker
    // configuration.
    const marker_config& markers() const
    {
        return m_markers;
    };


    // =========================================================
    // NESTING DEPTH CONFIGURATION
    // =========================================================

    // set_max_depth
    //   method: sets the maximum recursion depth for nested
    // expansion.
    void set_max_depth(std::size_t _depth)
    {
        m_max_depth = _depth;

        return;
    };

    // max_depth
    //   method: returns the current maximum nesting depth.
    std::size_t max_depth() const
    {
        return m_max_depth;
    };


    // =========================================================
    // BINDING MANAGEMENT
    // =========================================================

    // bind_string
    //   method: binds a key to a plain string replacement
    // value. Overwrites any existing binding for the same key.
    void bind_string(const std::string& _key,
                     const std::string& _value)
    {
        internal::binding_value bv;

        bv.type       = binding_type::string_value;
        bv.string_val = _value;

        m_bindings[_key] = std::move(bv);

        return;
    };

    // bind_template
    //   method: binds a key to another text template for
    // recursive expansion. The nested template is not owned;
    // the caller must ensure it outlives this template or at
    // least any render() call.
    void bind_template(const std::string& _key,
                       tmpl_text_template& _nested)
    {
        internal::binding_value bv;

        bv.type         = binding_type::template_ref;
        bv.template_ptr = &_nested;

        m_bindings[_key] = std::move(bv);

        return;
    };

    // bind_function
    //   method: binds a key to a callable that produces the
    // replacement string at render time. The callable is
    // invoked each time the key is encountered during
    // rendering, enabling dynamic/computed content.
    void bind_function(const std::string& _key,
                       std::function<std::string()> _fn)
    {
        internal::binding_value bv;

        bv.type         = binding_type::function_value;
        bv.function_val = std::move(_fn);

        m_bindings[_key] = std::move(bv);

        return;
    };

    // bind_list
    //   method: binds a key to a list iteration. During
    // rendering, the item template is rendered _count times
    // with per-item bindings supplied by _bind_fn.
    //
    //   The item template is not owned; the caller must ensure
    // it outlives any render() call. The bind callback is
    // invoked for each item to populate domain-specific keys;
    // auto-keys (_index, _number, _count, _is_first, _is_last)
    // are injected before each callback invocation.
    void bind_list(const std::string&   _key,
                   tmpl_text_template&  _item_template,
                   std::size_t          _count,
                   list_bind_fn         _bind_fn,
                   const list_options&  _options =
                       list_options())
    {
        internal::binding_value bv;

        bv.type                      = binding_type::list_value;
        bv.list_val.item_template    = &_item_template;
        bv.list_val.count            = _count;
        bv.list_val.bind_fn          = std::move(_bind_fn);
        bv.list_val.options          = _options;

        m_bindings[_key] = std::move(bv);

        return;
    };

    // unbind
    //   method: removes the binding for the given key. Returns
    // true if the key was found and removed, false if it was
    // not present.
    bool unbind(const std::string& _key)
    {
        return m_bindings.erase(_key) > 0;
    };

    // clear
    //   method: removes all bindings. Marker configuration
    // and nesting depth are not affected.
    void clear()
    {
        m_bindings.clear();

        return;
    };


    // =========================================================
    // BINDING QUERIES
    // =========================================================

    // has_binding
    //   method: returns true if a binding exists for the
    // given key.
    bool has_binding(const std::string& _key) const
    {
        return m_bindings.count(_key) > 0;
    };

    // binding_count
    //   method: returns the number of currently registered
    // bindings.
    std::size_t binding_count() const
    {
        return m_bindings.size();
    };

    // binding_type_of
    //   method: returns the binding type for the given key.
    // Undefined behavior if the key is not bound; call
    // has_binding() first.
    binding_type binding_type_of(
        const std::string& _key) const
    {
        return m_bindings.at(_key).type;
    };


    // =========================================================
    // RENDERING
    // =========================================================

    // render
    //   method: performs marker-aware substitution on the
    // format string, replacing all occurrences of
    // prefix+key+suffix with the bound values. Returns a
    // render_result carrying the output string or an error.
    //
    //   Nested template bindings, function bindings, and list
    // bindings are expanded recursively up to max_depth().
    render_result render(const std::string& _format)
    {
        return render_impl(_format, 0);
    };

    // render_or
    //   method: convenience wrapper that returns the rendered
    // string on success, or the fallback string on error.
    std::string render_or(const std::string& _format,
                          const std::string& _fallback)
    {
        render_result r = render(_format);

        return r.value_or(_fallback);
    };


    // =========================================================
    // tmpl_template INTERFACE
    // =========================================================

    // transform_impl
    //   method: CRTP implementation for tmpl_template.
    // Treats the input string as a format string and performs
    // render(). On error, returns the input unchanged.
    std::string transform_impl(
        const std::string& _input)
    {
        render_result r = render(_input);

        if (r.is_ok())
        {
            return std::move(r.output);
        }

        return _input;
    };

private:

    // =========================================================
    // INTERNAL RENDERING
    // =========================================================

    // render_impl
    //   method: recursive rendering implementation with depth
    // tracking.
    render_result render_impl(const std::string& _format,
                              std::size_t        _depth)
    {
        if (_depth > m_max_depth)
        {
            return render_result::fail(
                text_template_error::nesting_too_deep);
        }

        if ( (m_markers.prefix.empty()) ||
             (m_markers.suffix.empty()) )
        {
            return render_result::fail(
                text_template_error::invalid_marker);
        }

        std::string result;

        result.reserve(_format.size());

        std::size_t pos = 0;

        while (pos < _format.size())
        {
            // find next prefix marker
            std::size_t prefix_pos =
                _format.find(m_markers.prefix, pos);

            if (prefix_pos == std::string::npos)
            {
                // no more markers; append remainder
                result.append(_format, pos,
                              _format.size() - pos);
                break;
            }

            // append text before the marker
            result.append(_format, pos,
                          prefix_pos - pos);

            // find the matching suffix
            std::size_t key_start =
                prefix_pos + m_markers.prefix.size();
            std::size_t suffix_pos =
                _format.find(m_markers.suffix, key_start);

            if (suffix_pos == std::string::npos)
            {
                // unmatched prefix; append it literally
                result.append(m_markers.prefix);
                pos = key_start;
                continue;
            }

            // extract the key
            std::string key = _format.substr(
                key_start, suffix_pos - key_start);

            // look up the binding
            auto it = m_bindings.find(key);

            if (it == m_bindings.end())
            {
                // unbound key; reproduce the marker
                // literally so upstream templates or
                // later passes can resolve it
                result.append(m_markers.prefix);
                result.append(key);
                result.append(m_markers.suffix);
            }
            else
            {
                // resolve the binding
                render_result resolved =
                    resolve_binding(it->second,
                                   _depth);

                if (resolved.is_error())
                {
                    return resolved;
                }

                result.append(resolved.output);
            }

            pos = suffix_pos + m_markers.suffix.size();
        }

        return render_result::ok(std::move(result));
    };

    // resolve_binding
    //   method: resolves a single binding value to its string
    // representation. Handles all four binding types.
    render_result resolve_binding(
        const internal::binding_value& _bv,
        std::size_t                    _depth)
    {
        switch (_bv.type)
        {
            case binding_type::string_value:
            {
                return render_result::ok(_bv.string_val);
            }

            case binding_type::template_ref:
            {
                if (!_bv.template_ptr)
                {
                    return render_result::fail(
                        text_template_error::
                            null_param);
                }

                // render the nested template against
                // an empty format (the nested template
                // is its own format source — it should
                // have been rendered with its own format
                // string by the caller, or this binding
                // represents a "component" whose render
                // is triggered here)
                return _bv.template_ptr->render_impl(
                    _bv.template_ptr->m_last_format,
                    _depth + 1);
            }

            case binding_type::function_value:
            {
                if (!_bv.function_val)
                {
                    return render_result::fail(
                        text_template_error::
                            function_failed);
                }

                std::string fn_result =
                    _bv.function_val();

                // re-render the function output in case
                // it contains markers
                return render_impl(fn_result,
                                   _depth + 1);
            }

            case binding_type::list_value:
            {
                return resolve_list_binding(
                    _bv.list_val, _depth);
            }
        }

        return render_result::fail(
            text_template_error::key_not_found);
    };

    // resolve_list_binding
    //   method: renders a list binding by iterating the
    // sub-template _count times with per-item callback
    // invocations.
    render_result resolve_list_binding(
        const internal::list_binding_data& _list,
        std::size_t                        _depth)
    {
        if (!_list.item_template)
        {
            return render_result::fail(
                text_template_error::null_param);
        }

        if (_list.count == 0)
        {
            return render_result::ok(
                _list.options.empty_text);
        }

        std::string result;

        // compute number padding width
        std::size_t pad_width =
            _list.options.number_pad_width;

        if (pad_width == 0)
        {
            // auto-detect from digit width of count
            std::size_t n = _list.count;

            pad_width = 1;

            while (n >= 10)
            {
                n /= 10;
                ++pad_width;
            }
        }

        char pad_char = _list.options.number_pad_char;

        if (pad_char == '\0')
        {
            pad_char = '0';
        }

        for (std::size_t i = 0; i < _list.count; ++i)
        {
            // insert separator between items
            if ( (i > 0) &&
                 (!_list.options.separator.empty()) )
            {
                result.append(
                    _list.options.separator);
            }

            // clear prior bindings on the item template
            _list.item_template->clear();

            // inject auto-keys
            std::size_t number =
                _list.options.number_from_zero
                    ? i
                    : (i + 1);

            // format _number with padding
            std::string num_str =
                std::to_string(number);

            while (num_str.size() < pad_width)
            {
                num_str.insert(
                    num_str.begin(), pad_char);
            }

            _list.item_template->bind_string(
                "_index",
                std::to_string(i));
            _list.item_template->bind_string(
                "_number", num_str);
            _list.item_template->bind_string(
                "_count",
                std::to_string(_list.count));
            _list.item_template->bind_string(
                "_is_first",
                (i == 0) ? "true" : "false");
            _list.item_template->bind_string(
                "_is_last",
                (i == _list.count - 1)
                    ? "true" : "false");

            // invoke the per-item callback
            if (_list.bind_fn)
            {
                bool ok = _list.bind_fn(
                    i, _list.count,
                    *_list.item_template);

                if (!ok)
                {
                    return render_result::fail(
                        text_template_error::
                            list_callback_failed);
                }
            }

            // render the item template
            // (use an empty format string; the item
            // template's bindings define the content,
            // and the caller should have set its
            // format via set_format() or the template's
            // own structure)
            render_result item_result =
                _list.item_template->render_impl(
                    _list.item_template->m_last_format,
                    _depth + 1);

            if (item_result.is_error())
            {
                return render_result::fail(
                    text_template_error::
                        list_item_render_failed);
            }

            result.append(item_result.output);
        }

        return render_result::ok(std::move(result));
    };

    // ---- members ----

    marker_config m_markers;

    std::unordered_map<std::string,
                       internal::binding_value>
        m_bindings;

    std::size_t m_max_depth;

    // m_last_format: stored format string for nested
    // template and list bindings where the template is
    // rendered as a component; set by set_format().
    std::string m_last_format;

public:
    // set_format
    //   method: stores a format string on this template for
    // use when it is rendered as a nested template binding
    // or list item template. Not needed when calling
    // render() directly.
    void set_format(const std::string& _format)
    {
        m_last_format = _format;

        return;
    };

    // format
    //   method: returns the stored format string.
    const std::string& format() const
    {
        return m_last_format;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             VI.   ERROR STRING UTILITY                                  ///
///////////////////////////////////////////////////////////////////////////////

// text_template_error_string
//   function: returns a human-readable string for the given
// error code.
inline const char*
text_template_error_string(text_template_error _error)
{
    switch (_error)
    {
        case text_template_error::success:
            return "success";
        case text_template_error::null_param:
            return "null parameter";
        case text_template_error::key_not_found:
            return "key not found";
        case text_template_error::nesting_too_deep:
            return "nesting depth exceeded";
        case text_template_error::invalid_marker:
            return "invalid marker configuration";
        case text_template_error::cycle_detected:
            return "cycle detected in template graph";
        case text_template_error::function_failed:
            return "function binding failed";
        case text_template_error::list_callback_failed:
            return "list callback aborted iteration";
        case text_template_error::list_item_render_failed:
            return "list item render failed";
    }

    return "unknown error";
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_TEXT_TEMPLATE_
