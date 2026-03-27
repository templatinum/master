/******************************************************************************
* templatinum [template]                                     code_template.hpp
*
* Language-agnostic parsed code structures and template base (C++).
*   Defines the intermediate representation for parsed source code: symbols,
* dependencies, comments, diagnostics, and locations. These structures are
* the lingua franca between the parser (which produces them) and downstream
* consumers (wiki, dependency graph, formatter, upload beam).
*
*   The CRTP base `code_template<_Derived>` maps
* `code_source -> code_parse_result`. Concrete language-specific templates
* (e.g., cpp_code_template for C++ via libclang) inherit from this base
* and implement `transform_impl()`.
*
* COMPONENTS:
*   templatinum::code_source - input: file path or raw source text
*   templatinum::code_location - file + line + column + offset
*   templatinum::code_symbol_kind / code_access - classification enumerations
*   templatinum::code_comment - doc-comment text associated with a symbol
*   templatinum::code_symbol - a parsed symbol (class, function, variable, etc.)
*   templatinum::code_dependency - an include/inheritance/usage edge
*   templatinum::code_diagnostic - a warning, error, or note from parsing
*   templatinum::code_parse_result - aggregate output of a parse operation
*   templatinum::code_template<_Derived> - CRTP base: code_source -> code_parse_result
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* 
* path:      /inc/templatinum/code/code_template.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.26
******************************************************************************/

#ifndef TEMPLATINUM_CODE_TEMPLATE_
#define TEMPLATINUM_CODE_TEMPLATE_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "code_template.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "code_template.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "code_template.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "template.hpp"


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    INPUT: CODE SOURCE                                    ///
///////////////////////////////////////////////////////////////////////////////

// code_source_kind
//   enum: how the source is provided to the parser.
enum class code_source_kind
{
    // source is a file on disk; `path` is populated
    file_path,

    // source is an in-memory string; `text` is populated
    raw_text
};

// code_source
//   struct: input to a code template. Carries either a file
// path or raw source text, plus an optional language hint.
struct code_source
{
    code_source_kind kind;

    // populated when kind == file_path
    std::string path;

    // populated when kind == raw_text; `path` may still
    // be set as a virtual filename for diagnostics
    std::string text;

    // optional language hint (e.g., "c++", "c", "objc")
    // — the parser may use this to select a mode
    std::string language;

    // ---- factories ----

    // from_file
    //   factory: creates a code_source from a file path.
    static code_source from_file(std::string _path)
    {
        code_source src;

        src.kind = code_source_kind::file_path;
        src.path = std::move(_path);

        return src;
    };

    // from_text
    //   factory: creates a code_source from raw text.
    // _virtual_path is used for diagnostics and location
    // reporting.
    static code_source from_text(
        std::string _text,
        std::string _virtual_path = "<memory>")
    {
        code_source src;

        src.kind = code_source_kind::raw_text;
        src.text = std::move(_text);
        src.path = std::move(_virtual_path);

        return src;
    };

private:
    code_source()
        : kind(code_source_kind::file_path)
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   LOCATION                                              ///
///////////////////////////////////////////////////////////////////////////////

// code_location
//   struct: a position within a source file.
struct code_location
{
    // file path (may be empty for generated locations)
    std::string file;

    // 1-based line number
    unsigned int line;

    // 1-based column number
    unsigned int column;

    // 0-based byte offset from start of file
    unsigned int offset;

    code_location()
        : file()
        , line(0)
        , column(0)
        , offset(0)
    {
    };

    code_location(std::string  _file,
                  unsigned int _line,
                  unsigned int _col,
                  unsigned int _offset = 0)
        : file(std::move(_file))
        , line(_line)
        , column(_col)
        , offset(_offset)
    {
    };
};

// code_range
//   struct: a contiguous span within a source file.
struct code_range
{
    code_location start;
    code_location end;
};


///////////////////////////////////////////////////////////////////////////////
///             III.  SYMBOL CLASSIFICATION                                 ///
///////////////////////////////////////////////////////////////////////////////

// code_symbol_kind
//   enum: the kind of symbol extracted from parsed code.
// Language-agnostic — concrete parsers map their language-
// specific node types to these categories.
enum class code_symbol_kind
{
    // structural
    namespace_decl,
    class_decl,
    struct_decl,
    union_decl,
    enum_decl,
    enum_constant,

    // callable
    function_decl,
    method_decl,
    constructor,
    destructor,
    operator_decl,

    // data
    variable_decl,
    field_decl,
    parameter_decl,

    // type
    typedef_decl,
    type_alias,
    template_decl,
    template_param,

    // preprocessor
    macro_definition,
    macro_expansion,
    include_directive,

    // other
    concept_decl,
    static_assert_decl,
    friend_decl,
    using_decl,
    unknown
};

// code_access
//   enum: access specifier for class/struct members.
enum class code_access
{
    public_access,
    protected_access,
    private_access,
    none
};

// code_storage
//   enum: storage class / linkage.
enum class code_storage
{
    none,
    static_storage,
    extern_storage,
    inline_storage,
    constexpr_storage,
    thread_local_storage
};


///////////////////////////////////////////////////////////////////////////////
///             IV.   COMMENT                                               ///
///////////////////////////////////////////////////////////////////////////////

// code_comment
//   struct: a documentation comment associated with a symbol.
// Carries the raw text and the style (line // or block /**/).
struct code_comment
{
    // raw comment text (including delimiters)
    std::string raw;

    // cleaned text (delimiters and leading * stripped)
    std::string text;

    // true if this is a Doxygen/Javadoc-style doc comment
    bool is_doc;

    // location of the comment in the source
    code_range range;

    code_comment()
        : raw()
        , text()
        , is_doc(false)
        , range()
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             V.    SYMBOL                                                ///
///////////////////////////////////////////////////////////////////////////////

// code_symbol
//   struct: a single parsed symbol extracted from source code.
// Carries identification (name, qualified name), classification
// (kind, access), signature, location, associated comment, and
// child symbols (for nested declarations).
struct code_symbol
{
    // unqualified name (e.g., "my_function")
    std::string name;

    // fully qualified name (e.g., "ns::cls::my_function")
    std::string qualified_name;

    // kind of symbol
    code_symbol_kind kind;

    // access specifier (for class members)
    code_access access;

    // storage class
    code_storage storage;

    // the full declaration signature as written in source
    // (e.g., "void my_function(int _x, const string& _y)")
    std::string signature;

    // return type (for functions/methods; empty otherwise)
    std::string return_type;

    // source location of the declaration
    code_range location;

    // associated documentation comment (may be empty)
    code_comment comment;

    // template parameters (for template declarations)
    std::vector<std::string> template_params;

    // base classes (for class/struct declarations)
    std::vector<std::string> bases;

    // child symbols (nested declarations: methods inside
    // a class, enumerators inside an enum, etc.)
    std::vector<code_symbol> children;

    // language-specific flags
    bool is_virtual;
    bool is_static;
    bool is_constexpr;
    bool is_inline;
    bool is_noexcept;
    bool is_deleted;
    bool is_defaulted;
    bool is_pure_virtual;
    bool is_override;
    bool is_final;
    bool is_deprecated;

    code_symbol()
        : name()
        , qualified_name()
        , kind(code_symbol_kind::unknown)
        , access(code_access::none)
        , storage(code_storage::none)
        , signature()
        , return_type()
        , location()
        , comment()
        , template_params()
        , bases()
        , children()
        , is_virtual(false)
        , is_static(false)
        , is_constexpr(false)
        , is_inline(false)
        , is_noexcept(false)
        , is_deleted(false)
        , is_defaulted(false)
        , is_pure_virtual(false)
        , is_override(false)
        , is_final(false)
        , is_deprecated(false)
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             VI.   DEPENDENCY                                            ///
///////////////////////////////////////////////////////////////////////////////

// code_dependency_kind
//   enum: the type of dependency edge between two entities.
enum class code_dependency_kind
{
    // #include / import
    include,

    // class inheritance (: public base)
    inheritance,

    // class composition (member of type)
    composition,

    // function/method uses a type or calls a function
    usage,

    // friend declaration
    friend_of,

    // template instantiation
    template_instantiation
};

// code_dependency
//   struct: a directed edge between two entities in the code.
struct code_dependency
{
    // the entity that depends on target
    // (file path for includes; qualified name for symbols)
    std::string source;

    // the entity being depended upon
    std::string target;

    // the kind of dependency
    code_dependency_kind kind;

    // location of the dependency in source code
    code_location location;

    // for includes: true if it uses <> (system), false
    // for "" (local)
    bool is_system;

    code_dependency()
        : source()
        , target()
        , kind(code_dependency_kind::include)
        , location()
        , is_system(false)
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             VII.  DIAGNOSTIC                                            ///
///////////////////////////////////////////////////////////////////////////////

// code_diagnostic_severity
//   enum: severity level of a parse diagnostic.
enum class code_diagnostic_severity
{
    ignored,
    note,
    warning,
    error,
    fatal
};

// code_diagnostic
//   struct: a warning, error, or note produced during parsing.
struct code_diagnostic
{
    code_diagnostic_severity severity;
    std::string              message;
    code_location            location;

    code_diagnostic()
        : severity(code_diagnostic_severity::note)
        , message()
        , location()
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             VIII. PARSE RESULT                                          ///
///////////////////////////////////////////////////////////////////////////////

// code_parse_result
//   struct: the aggregate output of a code parsing operation.
// Carries all extracted symbols, dependencies, and diagnostics,
// plus metadata about the parse itself.
struct code_parse_result
{
    // the source that was parsed (echoed back for provenance)
    code_source source;

    // top-level symbols extracted from the source
    std::vector<code_symbol> symbols;

    // dependency edges extracted from the source
    std::vector<code_dependency> dependencies;

    // diagnostics produced during parsing
    std::vector<code_diagnostic> diagnostics;

    // true if parsing completed without fatal errors
    bool success;

    // total number of symbols (including nested children)
    std::size_t total_symbol_count;

    code_parse_result()
        : source(code_source::from_text(""))
        , symbols()
        , dependencies()
        , diagnostics()
        , success(false)
        , total_symbol_count(0)
    {
    };

    // has_errors
    //   method: returns true if any diagnostic has error
    // or fatal severity.
    bool has_errors() const
    {
        for (const auto& diag : diagnostics)
        {
            if ( (diag.severity ==
                      code_diagnostic_severity::error) ||
                 (diag.severity ==
                      code_diagnostic_severity::fatal) )
            {
                return true;
            }
        }

        return false;
    };

    // error_count
    //   method: returns the number of error and fatal
    // diagnostics.
    std::size_t error_count() const
    {
        std::size_t count = 0;

        for (const auto& diag : diagnostics)
        {
            if ( (diag.severity ==
                      code_diagnostic_severity::error) ||
                 (diag.severity ==
                      code_diagnostic_severity::fatal) )
            {
                ++count;
            }
        }

        return count;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             IX.   CODE TEMPLATE BASE (CRTP)                             ///
///////////////////////////////////////////////////////////////////////////////

// code_template
//   class: CRTP base for language-specific code parsers.
// Maps code_source -> code_parse_result. Derived classes
// implement transform_impl() using their language-specific
// parsing machinery.
//
// usage:
//   class my_parser
//       : public code_template<my_parser>
//   {
//   public:
//       code_parse_result transform_impl(
//           const code_source& _src)
//       {
//           // parse and return result
//       };
//   };
template<typename _Derived>
class code_template
    : public tmpl_template<_Derived,
                           code_source,
                           code_parse_result>
{
public:
    using input_type  = code_source;
    using output_type = code_parse_result;

    // parse
    //   method: convenience alias for transform().
    code_parse_result parse(const code_source& _src)
    {
        return this->transform(_src);
    };

    // parse_file
    //   method: convenience wrapper that creates a
    // code_source from a file path and parses it.
    code_parse_result parse_file(
        const std::string& _path)
    {
        return parse(code_source::from_file(_path));
    };

    // parse_text
    //   method: convenience wrapper that creates a
    // code_source from raw text and parses it.
    code_parse_result parse_text(
        const std::string& _text,
        const std::string& _virtual_path = "<memory>")
    {
        return parse(
            code_source::from_text(_text,
                                   _virtual_path));
    };
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_CODE_TEMPLATE_
