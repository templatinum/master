/******************************************************************************
* templatinum [template]                              cpp_code_template.hpp
*
* C++ code parser via libclang (C++).
*   Concrete code_template specialization that uses libclang to parse C++
* source files and produce a code_parse_result. The result contains all
* top-level and nested symbols (classes, functions, variables, macros,
* templates), dependency edges (#include, inheritance, composition),
* documentation comments, and diagnostics.
*
*   libclang is a C API; this module provides RAII wrappers for the
* CXIndex and CXTranslationUnit handles, and bridges the C visitor
* callback pattern to internal C++ methods. The clang headers are
* conditionally included behind D_TEMPLATINUM_HAS_LIBCLANG.
*
* COMPILE-TIME REQUIREMENTS:
*   - Define D_TEMPLATINUM_HAS_LIBCLANG=1 before including this header
*   - Link against libclang (typically -lclang)
*   - clang-c/Index.h must be on the include path
*
*   If D_TEMPLATINUM_HAS_LIBCLANG is not defined, this header provides
* only the clang_config structure (for configuration forwarding) and
* a stub cpp_code_template whose transform_impl() returns a failed
* parse result with a diagnostic explaining that libclang is not
* available.
*
* COMPONENTS:
*   templatinum::clang_config
*     - parse configuration (includes, defines, std, flags)
*
*   templatinum::cpp_code_template
*     - CRTP: code_source → code_parse_result via libclang
*
* INTERNAL COMPONENTS (libclang-enabled only):
*   templatinum::internal::clang_index_handle
*     - RAII wrapper for CXIndex
*
*   templatinum::internal::clang_tu_handle
*     - RAII wrapper for CXTranslationUnit
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_RVALUE_REFERENCES   - move semantics
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES     - using aliases
*   D_ENV_CPP_FEATURE_LANG_LAMBDAS             - lambda closures
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\cpp_code_template.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.26
******************************************************************************/

#ifndef TEMPLATINUM_CPP_CODE_TEMPLATE_
#define TEMPLATINUM_CPP_CODE_TEMPLATE_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "cpp_code_template.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "cpp_code_template.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "cpp_code_template.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "code_template.hpp"

#if defined(D_TEMPLATINUM_HAS_LIBCLANG) && \
    (D_TEMPLATINUM_HAS_LIBCLANG == 1)
    #include <clang-c/Index.h>
    #define D_TEMPLATINUM_LIBCLANG_ENABLED_ 1
#else
    #define D_TEMPLATINUM_LIBCLANG_ENABLED_ 0
#endif


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    PARSE CONFIGURATION                                   ///
///////////////////////////////////////////////////////////////////////////////

// clang_config
//   struct: configuration for the C++ parser. Controls include
// paths, preprocessor defines, language standard, and parse
// behavior flags. Available regardless of whether libclang is
// present (for configuration forwarding).
struct clang_config
{
    // user include paths (-I)
    std::vector<std::string> include_paths;

    // system include paths (-isystem)
    std::vector<std::string> system_include_paths;

    // preprocessor defines (-D); each entry is "NAME" or
    // "NAME=VALUE"
    std::vector<std::string> defines;

    // language standard (e.g., "c++11", "c++17", "c++20");
    // empty means compiler default
    std::string standard;

    // additional raw compiler flags passed to libclang
    std::vector<std::string> extra_flags;

    // ---- behavior flags ----

    // when true, skip function/method bodies during parsing;
    // produces symbols and signatures but not local variables
    // or nested expressions; significantly faster for large
    // codebases where only the interface is needed
    bool skip_function_bodies;

    // when true, include symbols from system headers in the
    // result; when false (default), only symbols from the
    // parsed file and user headers are included
    bool include_system_symbols;

    // when true, extract documentation comments and associate
    // them with their corresponding symbols
    bool extract_comments;

    // when true, extract #include dependency edges
    bool extract_dependencies;

    // when true, extract inheritance and composition edges
    // (requires full AST walk; ignored if
    // skip_function_bodies is true for composition edges)
    bool extract_relationships;

    clang_config()
        : include_paths()
        , system_include_paths()
        , defines()
        , standard()
        , extra_flags()
        , skip_function_bodies(false)
        , include_system_symbols(false)
        , extract_comments(true)
        , extract_dependencies(true)
        , extract_relationships(true)
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   LIBCLANG RAII WRAPPERS (ENABLED ONLY)                 ///
///////////////////////////////////////////////////////////////////////////////

#if D_TEMPLATINUM_LIBCLANG_ENABLED_

NS_INTERNAL

    // clang_index_handle
    //   class: RAII wrapper for CXIndex. Non-copyable, movable.
    class clang_index_handle
    {
    public:
        clang_index_handle()
            : m_index(clang_createIndex(0, 0))
        {
        };

        ~clang_index_handle()
        {
            if (m_index)
            {
                clang_disposeIndex(m_index);
            }
        };

        // non-copyable
        clang_index_handle(
            const clang_index_handle&) = delete;
        clang_index_handle& operator=(
            const clang_index_handle&) = delete;

        // movable
        clang_index_handle(
            clang_index_handle&& _other)
            : m_index(_other.m_index)
        {
            _other.m_index = nullptr;
        };

        clang_index_handle& operator=(
            clang_index_handle&& _other)
        {
            if (this != &_other)
            {
                if (m_index)
                {
                    clang_disposeIndex(m_index);
                }

                m_index        = _other.m_index;
                _other.m_index = nullptr;
            }

            return *this;
        };

        CXIndex get() const { return m_index; };

        bool is_valid() const
        {
            return (m_index != nullptr);
        };

    private:
        CXIndex m_index;
    };


    // clang_tu_handle
    //   class: RAII wrapper for CXTranslationUnit. Non-copyable,
    // movable.
    class clang_tu_handle
    {
    public:
        clang_tu_handle()
            : m_tu(nullptr)
        {
        };

        explicit clang_tu_handle(
            CXTranslationUnit _tu)
            : m_tu(_tu)
        {
        };

        ~clang_tu_handle()
        {
            if (m_tu)
            {
                clang_disposeTranslationUnit(m_tu);
            }
        };

        // non-copyable
        clang_tu_handle(
            const clang_tu_handle&) = delete;
        clang_tu_handle& operator=(
            const clang_tu_handle&) = delete;

        // movable
        clang_tu_handle(
            clang_tu_handle&& _other)
            : m_tu(_other.m_tu)
        {
            _other.m_tu = nullptr;
        };

        clang_tu_handle& operator=(
            clang_tu_handle&& _other)
        {
            if (this != &_other)
            {
                if (m_tu)
                {
                    clang_disposeTranslationUnit(
                        m_tu);
                }

                m_tu        = _other.m_tu;
                _other.m_tu = nullptr;
            }

            return *this;
        };

        CXTranslationUnit get() const
        {
            return m_tu;
        };

        bool is_valid() const
        {
            return (m_tu != nullptr);
        };

    private:
        CXTranslationUnit m_tu;
    };


    // clang_string_ref
    //   class: RAII wrapper for CXString. Converts to
    // std::string on extraction.
    class clang_string_ref
    {
    public:
        explicit clang_string_ref(CXString _str)
            : m_str(_str)
        {
        };

        ~clang_string_ref()
        {
            clang_disposeString(m_str);
        };

        clang_string_ref(
            const clang_string_ref&) = delete;
        clang_string_ref& operator=(
            const clang_string_ref&) = delete;

        // to_string
        //   method: extracts the CXString as a std::string.
        std::string to_string() const
        {
            const char* cstr =
                clang_getCString(m_str);

            if (cstr)
            {
                return std::string(cstr);
            }

            return std::string();
        };

    private:
        CXString m_str;
    };


    // ---- cursor kind mapping ----

    // map_cursor_kind
    //   function: maps a CXCursorKind to a code_symbol_kind.
    inline code_symbol_kind
    map_cursor_kind(CXCursorKind _kind)
    {
        switch (_kind)
        {
            // structural
            case CXCursor_Namespace:
                return code_symbol_kind::namespace_decl;
            case CXCursor_ClassDecl:
            case CXCursor_ClassTemplate:
                return code_symbol_kind::class_decl;
            case CXCursor_StructDecl:
                return code_symbol_kind::struct_decl;
            case CXCursor_UnionDecl:
                return code_symbol_kind::union_decl;
            case CXCursor_EnumDecl:
                return code_symbol_kind::enum_decl;
            case CXCursor_EnumConstantDecl:
                return code_symbol_kind::enum_constant;

            // callable
            case CXCursor_FunctionDecl:
            case CXCursor_FunctionTemplate:
                return code_symbol_kind::function_decl;
            case CXCursor_CXXMethod:
                return code_symbol_kind::method_decl;
            case CXCursor_Constructor:
                return code_symbol_kind::constructor;
            case CXCursor_Destructor:
                return code_symbol_kind::destructor;
            case CXCursor_ConversionFunction:
                return code_symbol_kind::operator_decl;

            // data
            case CXCursor_VarDecl:
                return code_symbol_kind::variable_decl;
            case CXCursor_FieldDecl:
                return code_symbol_kind::field_decl;
            case CXCursor_ParmDecl:
                return code_symbol_kind::parameter_decl;

            // type
            case CXCursor_TypedefDecl:
                return code_symbol_kind::typedef_decl;
            case CXCursor_TypeAliasDecl:
            case CXCursor_TypeAliasTemplateDecl:
                return code_symbol_kind::type_alias;
            case CXCursor_TemplateTypeParameter:
            case CXCursor_NonTypeTemplateParameter:
            case CXCursor_TemplateTemplateParameter:
                return code_symbol_kind::template_param;

            // preprocessor
            case CXCursor_MacroDefinition:
                return code_symbol_kind::
                    macro_definition;
            case CXCursor_MacroExpansion:
                return code_symbol_kind::
                    macro_expansion;
            case CXCursor_InclusionDirective:
                return code_symbol_kind::
                    include_directive;

            // other
            case CXCursor_UsingDeclaration:
            case CXCursor_UsingDirective:
                return code_symbol_kind::using_decl;
            case CXCursor_FriendDecl:
                return code_symbol_kind::friend_decl;
            case CXCursor_StaticAssert:
                return code_symbol_kind::
                    static_assert_decl;

            default:
                return code_symbol_kind::unknown;
        }
    };


    // map_access_specifier
    //   function: maps CX_CXXAccessSpecifier to code_access.
    inline code_access
    map_access_specifier(CX_CXXAccessSpecifier _acc)
    {
        switch (_acc)
        {
            case CX_CXXPublic:
                return code_access::public_access;
            case CX_CXXProtected:
                return code_access::protected_access;
            case CX_CXXPrivate:
                return code_access::private_access;
            default:
                return code_access::none;
        }
    };


    // map_diagnostic_severity
    //   function: maps CXDiagnosticSeverity to
    // code_diagnostic_severity.
    inline code_diagnostic_severity
    map_diagnostic_severity(
        CXDiagnosticSeverity _sev)
    {
        switch (_sev)
        {
            case CXDiagnostic_Ignored:
                return code_diagnostic_severity::
                    ignored;
            case CXDiagnostic_Note:
                return code_diagnostic_severity::note;
            case CXDiagnostic_Warning:
                return code_diagnostic_severity::
                    warning;
            case CXDiagnostic_Error:
                return code_diagnostic_severity::error;
            case CXDiagnostic_Fatal:
                return code_diagnostic_severity::fatal;
            default:
                return code_diagnostic_severity::note;
        }
    };


    // extract_location
    //   function: converts a CXSourceLocation to a
    // code_location.
    inline code_location
    extract_location(CXSourceLocation _loc)
    {
        CXFile       file;
        unsigned int line;
        unsigned int column;
        unsigned int offset;

        clang_getSpellingLocation(
            _loc, &file, &line, &column, &offset);

        code_location result;

        result.line   = line;
        result.column = column;
        result.offset = offset;

        if (file)
        {
            clang_string_ref fname(
                clang_getFileName(file));
            result.file = fname.to_string();
        }

        return result;
    };


    // extract_range
    //   function: converts a CXSourceRange to a code_range.
    inline code_range
    extract_range(CXSourceRange _range)
    {
        code_range result;

        result.start =
            extract_location(
                clang_getRangeStart(_range));
        result.end =
            extract_location(
                clang_getRangeEnd(_range));

        return result;
    };

NS_END  // internal

#endif  // D_TEMPLATINUM_LIBCLANG_ENABLED_


///////////////////////////////////////////////////////////////////////////////
///             III.  C++ CODE TEMPLATE                                     ///
///////////////////////////////////////////////////////////////////////////////

// cpp_code_template
//   class: C++ code parser via libclang. Inherits from
// code_template<cpp_code_template> and implements
// transform_impl() to parse C++ source into a
// code_parse_result.
//
//   When libclang is not available, transform_impl() returns
// a failed result with a diagnostic explaining the absence.
//
// usage:
//   clang_config cfg;
//   cfg.include_paths.push_back("/usr/local/include");
//   cfg.standard = "c++17";
//
//   cpp_code_template parser(cfg);
//   auto result = parser.parse_file("main.cpp");
//
//   for (const auto& sym : result.symbols)
//   {
//       // process symbols
//   }
class cpp_code_template
    : public code_template<cpp_code_template>
{
public:
    using input_type  = code_source;
    using output_type = code_parse_result;

    // ---- construction ----

    // default constructor
    cpp_code_template()
        : m_config()
    {
    };

    // config constructor
    explicit cpp_code_template(clang_config _config)
        : m_config(std::move(_config))
    {
    };

    // ---- configuration ----

    // config
    //   method: returns a mutable reference to the parse
    // configuration. Modifications take effect on the next
    // parse call.
    clang_config& config()
    {
        return m_config;
    };

    const clang_config& config() const
    {
        return m_config;
    };

    // set_config
    //   method: replaces the parse configuration.
    void set_config(clang_config _config)
    {
        m_config = std::move(_config);

        return;
    };


    // =========================================================
    // TRANSFORM IMPLEMENTATION
    // =========================================================

    // transform_impl
    //   method: parses the code_source using libclang and
    // returns a code_parse_result. When libclang is not
    // available, returns a failed result.
    code_parse_result transform_impl(
        const code_source& _src)
    {
#if D_TEMPLATINUM_LIBCLANG_ENABLED_
        return parse_with_clang(_src);
#else
        return make_unavailable_result(_src);
#endif
    };

private:

    // =========================================================
    // STUB (no libclang)
    // =========================================================

    // make_unavailable_result
    //   method: returns a failed parse result explaining that
    // libclang is not linked.
    code_parse_result make_unavailable_result(
        const code_source& _src)
    {
        code_parse_result result;

        result.source  = _src;
        result.success = false;

        code_diagnostic diag;
        diag.severity =
            code_diagnostic_severity::fatal;
        diag.message  =
            "libclang is not available; define "
            "D_TEMPLATINUM_HAS_LIBCLANG=1 and link "
            "against libclang to enable C++ parsing.";

        result.diagnostics.push_back(
            std::move(diag));

        return result;
    };


#if D_TEMPLATINUM_LIBCLANG_ENABLED_

    // =========================================================
    // LIBCLANG IMPLEMENTATION
    // =========================================================

    // parse_with_clang
    //   method: the real parse implementation. Creates an
    // index, builds command-line arguments from config,
    // parses the translation unit, walks the AST, and
    // extracts symbols/dependencies/diagnostics.
    code_parse_result parse_with_clang(
        const code_source& _src)
    {
        code_parse_result result;

        result.source = _src;

        // create index
        internal::clang_index_handle index;

        if (!index.is_valid())
        {
            code_diagnostic diag;
            diag.severity =
                code_diagnostic_severity::fatal;
            diag.message =
                "Failed to create CXIndex.";
            result.diagnostics.push_back(
                std::move(diag));

            return result;
        }

        // build command-line arguments
        std::vector<std::string> args =
            build_arguments();
        std::vector<const char*> c_args;

        c_args.reserve(args.size());

        for (const auto& arg : args)
        {
            c_args.push_back(arg.c_str());
        }

        // parse flags
        unsigned int flags =
            CXTranslationUnit_DetailedPreprocessingRecord;

        if (m_config.skip_function_bodies)
        {
            flags |=
                CXTranslationUnit_SkipFunctionBodies;
        }

        // parse the translation unit
        internal::clang_tu_handle tu =
            parse_source(index, _src, c_args, flags);

        if (!tu.is_valid())
        {
            code_diagnostic diag;
            diag.severity =
                code_diagnostic_severity::fatal;
            diag.message =
                "Failed to parse translation unit: "
                + _src.path;
            result.diagnostics.push_back(
                std::move(diag));

            return result;
        }

        // extract diagnostics
        extract_diagnostics(tu.get(), result);

        // walk the AST
        CXCursor root =
            clang_getTranslationUnitCursor(tu.get());

        walk_context ctx;
        ctx.result  = &result;
        ctx.config  = &m_config;
        ctx.tu      = tu.get();
        ctx.source_file = _src.path;

        clang_visitChildren(
            root,
            &cpp_code_template::visitor_callback,
            &ctx);

        // finalize
        result.success =
            !result.has_errors();
        result.total_symbol_count =
            count_symbols(result.symbols);

        return result;
    };

    // ---- argument building ----

    // build_arguments
    //   method: converts clang_config into a vector of
    // command-line argument strings.
    std::vector<std::string> build_arguments() const
    {
        std::vector<std::string> args;

        // language standard
        if (!m_config.standard.empty())
        {
            args.push_back(
                "-std=" + m_config.standard);
        }

        // include paths
        for (const auto& path :
                 m_config.include_paths)
        {
            args.push_back("-I" + path);
        }

        // system include paths
        for (const auto& path :
                 m_config.system_include_paths)
        {
            args.push_back("-isystem");
            args.push_back(path);
        }

        // defines
        for (const auto& def : m_config.defines)
        {
            args.push_back("-D" + def);
        }

        // extra flags
        for (const auto& flag :
                 m_config.extra_flags)
        {
            args.push_back(flag);
        }

        return args;
    };

    // ---- translation unit creation ----

    // parse_source
    //   method: creates a CXTranslationUnit from the
    // code_source.
    internal::clang_tu_handle parse_source(
        const internal::clang_index_handle& _index,
        const code_source&                  _src,
        const std::vector<const char*>&     _args,
        unsigned int                        _flags)
    {
        CXTranslationUnit tu = nullptr;

        if (_src.kind == code_source_kind::file_path)
        {
            tu = clang_parseTranslationUnit(
                _index.get(),
                _src.path.c_str(),
                _args.data(),
                static_cast<int>(_args.size()),
                nullptr,
                0,
                _flags);
        }
        else
        {
            // in-memory source via unsaved file
            CXUnsavedFile unsaved;

            unsaved.Filename = _src.path.c_str();
            unsaved.Contents = _src.text.c_str();
            unsaved.Length   = _src.text.size();

            tu = clang_parseTranslationUnit(
                _index.get(),
                _src.path.c_str(),
                _args.data(),
                static_cast<int>(_args.size()),
                &unsaved,
                1,
                _flags);
        }

        return internal::clang_tu_handle(tu);
    };

    // ---- diagnostic extraction ----

    // extract_diagnostics
    //   method: extracts all diagnostics from the
    // translation unit into the result.
    void extract_diagnostics(
        CXTranslationUnit  _tu,
        code_parse_result&  _result)
    {
        unsigned int count =
            clang_getNumDiagnostics(_tu);

        for (unsigned int i = 0; i < count; ++i)
        {
            CXDiagnostic diag =
                clang_getDiagnostic(_tu, i);

            code_diagnostic entry;

            entry.severity =
                internal::map_diagnostic_severity(
                    clang_getDiagnosticSeverity(diag));

            internal::clang_string_ref msg(
                clang_getDiagnosticSpelling(diag));
            entry.message = msg.to_string();

            entry.location =
                internal::extract_location(
                    clang_getDiagnosticLocation(diag));

            _result.diagnostics.push_back(
                std::move(entry));

            clang_disposeDiagnostic(diag);
        }

        return;
    };

    // ---- AST walking ----

    // walk_context
    //   struct: context passed through the libclang visitor
    // callback chain.
    struct walk_context
    {
        code_parse_result* result;
        const clang_config* config;
        CXTranslationUnit  tu;
        std::string         source_file;
    };

    // visitor_callback
    //   function: the C callback invoked by
    // clang_visitChildren. Maps cursor kinds to extraction
    // methods and populates the parse result.
    static CXChildVisitResult visitor_callback(
        CXCursor     _cursor,
        CXCursor     _parent,
        CXClientData _data)
    {
        (void)_parent;

        auto* ctx =
            static_cast<walk_context*>(_data);

        // skip symbols from system headers unless
        // configured otherwise
        if (!ctx->config->include_system_symbols)
        {
            CXSourceLocation loc =
                clang_getCursorLocation(_cursor);

            if (clang_Location_isInSystemHeader(loc))
            {
                return CXChildVisit_Continue;
            }
        }

        CXCursorKind kind =
            clang_getCursorKind(_cursor);

        // handle include directives as dependencies
        if ( (kind == CXCursor_InclusionDirective) &&
             (ctx->config->extract_dependencies) )
        {
            extract_include_dep(
                _cursor, ctx);

            return CXChildVisit_Continue;
        }

        // handle symbol declarations
        code_symbol_kind sym_kind =
            internal::map_cursor_kind(kind);

        if (sym_kind != code_symbol_kind::unknown)
        {
            code_symbol sym =
                extract_symbol(_cursor, ctx);

            ctx->result->symbols.push_back(
                std::move(sym));

            // do not recurse here; extract_symbol
            // handles children internally
            return CXChildVisit_Continue;
        }

        // recurse into unrecognized nodes
        return CXChildVisit_Recurse;
    };

    // extract_symbol
    //   function: extracts a code_symbol from a cursor.
    static code_symbol extract_symbol(
        CXCursor      _cursor,
        walk_context* _ctx)
    {
        code_symbol sym;

        // name
        internal::clang_string_ref name(
            clang_getCursorSpelling(_cursor));
        sym.name = name.to_string();

        // qualified name
        internal::clang_string_ref qname(
            clang_getCursorDisplayName(_cursor));
        sym.qualified_name = qname.to_string();

        // kind
        sym.kind = internal::map_cursor_kind(
            clang_getCursorKind(_cursor));

        // access
        sym.access = internal::map_access_specifier(
            clang_getCXXAccessSpecifier(_cursor));

        // location
        sym.location = internal::extract_range(
            clang_getCursorExtent(_cursor));

        // flags
        sym.is_virtual =
            (clang_CXXMethod_isVirtual(_cursor) != 0);
        sym.is_static =
            (clang_CXXMethod_isStatic(_cursor) != 0);
        sym.is_pure_virtual =
            (clang_CXXMethod_isPureVirtual(_cursor)
             != 0);

        // return type (for functions)
        CXType result_type =
            clang_getCursorResultType(_cursor);

        if (result_type.kind != CXType_Invalid)
        {
            internal::clang_string_ref rtype(
                clang_getTypeSpelling(result_type));
            sym.return_type = rtype.to_string();
        }

        // comment
        if (_ctx->config->extract_comments)
        {
            CXString raw_comment =
                clang_Cursor_getRawCommentText(
                    _cursor);
            const char* raw_cstr =
                clang_getCString(raw_comment);

            if (raw_cstr)
            {
                sym.comment.raw =
                    std::string(raw_cstr);
                sym.comment.is_doc = true;

                // brief
                internal::clang_string_ref brief(
                    clang_Cursor_getBriefCommentText(
                        _cursor));
                sym.comment.text =
                    brief.to_string();
            }

            clang_disposeString(raw_comment);
        }

        // inheritance edges
        if ( (_ctx->config->extract_relationships) &&
             ( (sym.kind ==
                    code_symbol_kind::class_decl) ||
               (sym.kind ==
                    code_symbol_kind::struct_decl) ) )
        {
            extract_bases(_cursor, sym, _ctx);
        }

        // recurse for children
        child_context child_ctx;
        child_ctx.parent_sym = &sym;
        child_ctx.walk       = _ctx;

        clang_visitChildren(
            _cursor,
            &cpp_code_template::child_visitor,
            &child_ctx);

        return sym;
    };

    // ---- child visitor ----

    // child_context
    //   struct: context for nested child visiting.
    struct child_context
    {
        code_symbol*  parent_sym;
        walk_context* walk;
    };

    // child_visitor
    //   function: visitor callback for children of a symbol.
    static CXChildVisitResult child_visitor(
        CXCursor     _cursor,
        CXCursor     _parent,
        CXClientData _data)
    {
        (void)_parent;

        auto* ctx =
            static_cast<child_context*>(_data);

        CXCursorKind kind =
            clang_getCursorKind(_cursor);
        code_symbol_kind sym_kind =
            internal::map_cursor_kind(kind);

        if (sym_kind != code_symbol_kind::unknown)
        {
            code_symbol child =
                extract_symbol(_cursor, ctx->walk);

            ctx->parent_sym->children.push_back(
                std::move(child));

            return CXChildVisit_Continue;
        }

        // handle base class specifiers
        if (kind == CXCursor_CXXBaseSpecifier)
        {
            internal::clang_string_ref base_name(
                clang_getCursorDisplayName(_cursor));

            ctx->parent_sym->bases.push_back(
                base_name.to_string());

            return CXChildVisit_Continue;
        }

        return CXChildVisit_Continue;
    };

    // ---- dependency extraction ----

    // extract_include_dep
    //   function: extracts an #include dependency from an
    // inclusion directive cursor.
    static void extract_include_dep(
        CXCursor      _cursor,
        walk_context* _ctx)
    {
        code_dependency dep;

        dep.kind     = code_dependency_kind::include;
        dep.source   = _ctx->source_file;
        dep.location = internal::extract_location(
            clang_getCursorLocation(_cursor));

        CXFile included =
            clang_getIncludedFile(_cursor);

        if (included)
        {
            internal::clang_string_ref fname(
                clang_getFileName(included));
            dep.target = fname.to_string();
        }
        else
        {
            internal::clang_string_ref name(
                clang_getCursorSpelling(_cursor));
            dep.target = name.to_string();
        }

        _ctx->result->dependencies.push_back(
            std::move(dep));

        return;
    };

    // extract_bases
    //   function: extracts inheritance dependency edges from
    // a class/struct cursor.
    static void extract_bases(
        CXCursor      _cursor,
        code_symbol&  _sym,
        walk_context* _ctx)
    {
        if (!_ctx->config->extract_relationships)
        {
            return;
        }

        // base specifiers are children; we extract them
        // as dependencies with kind == inheritance
        struct base_ctx
        {
            code_symbol*  sym;
            walk_context* walk;
        };

        base_ctx bctx;
        bctx.sym  = &_sym;
        bctx.walk = _ctx;

        clang_visitChildren(
            _cursor,
            [](CXCursor     _child,
               CXCursor     _p,
               CXClientData _d)
                -> CXChildVisitResult
            {
                (void)_p;

                if (clang_getCursorKind(_child) ==
                    CXCursor_CXXBaseSpecifier)
                {
                    auto* bc =
                        static_cast<base_ctx*>(_d);

                    internal::clang_string_ref name(
                        clang_getCursorDisplayName(
                            _child));

                    code_dependency dep;
                    dep.kind =
                        code_dependency_kind::
                            inheritance;
                    dep.source =
                        bc->sym->qualified_name;
                    dep.target = name.to_string();
                    dep.location =
                        internal::extract_location(
                            clang_getCursorLocation(
                                _child));

                    bc->walk->result->dependencies
                        .push_back(std::move(dep));
                }

                return CXChildVisit_Continue;
            },
            &bctx);

        return;
    };

    // ---- symbol counting ----

    // count_symbols
    //   function: recursively counts all symbols including
    // nested children.
    static std::size_t count_symbols(
        const std::vector<code_symbol>& _syms)
    {
        std::size_t count = _syms.size();

        for (const auto& sym : _syms)
        {
            count += count_symbols(sym.children);
        }

        return count;
    };

#endif  // D_TEMPLATINUM_LIBCLANG_ENABLED_


    // ---- members ----

    clang_config m_config;
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_CPP_CODE_TEMPLATE_
