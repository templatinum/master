/******************************************************************************
* templatinum [beam]                                                beam.hpp
*
* Beam: the full path from source to sink (C++).
*   A beam is the fundamental execution unit in templatinum. At its simplest
* it is source → template → sink. At its most complex it is a chain of
* type-erased template stages with lifecycle management, trigger binding,
* and metadata context. A beam IS the pipeline.
*
*   Templates are stages ON a beam. Data enters via inject(), flows through
* each stage in order, and exits through the sink. Each stage is a template
* whose transform is type-erased into a closure at add_stage() time. Type
* compatibility between adjacent stages is validated at wire time via
* per-type ID tokens.
*
*   Beams carry a beam_context: metadata that flows with the data and is
* accessible to all stages and the sink. This is the "pipeline-wide info"
* that individual templates do not own.
*
*   Triggers act on the beam: start/stop/pause the flow, toggle stages,
* or inject data at arbitrary points.
*
* COMPONENTS:
*   templatinum::stage_id
*     - opaque handle for template stages on a beam
*
*   templatinum::beam_state
*     - enum: stopped, running, paused
*
*   templatinum::beam_context
*     - metadata flowing with the beam
*
*   templatinum::beam_stats
*     - snapshot of beam metrics
*
*   templatinum::tmpl_beam
*     - the beam class (non-templated; type-erased stages)
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_RVALUE_REFERENCES   - move semantics
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES     - using aliases
*   D_ENV_CPP_FEATURE_LANG_LAMBDAS             - lambda closures
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\beam.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.27
******************************************************************************/

#ifndef TEMPLATINUM_BEAM_
#define TEMPLATINUM_BEAM_ 1

#ifndef DJINTERP_
    #error "beam.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "beam.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "beam.hpp requires C++11 or higher"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "template_traits.hpp"


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    STAGE IDENTIFICATION                                  ///
///////////////////////////////////////////////////////////////////////////////

// stage_id
//   struct: opaque handle for a template stage on a beam.
// Value 0 is the null sentinel.
struct stage_id
{
    std::uint64_t value;

    bool operator==(const stage_id& _other) const
    {
        return (value == _other.value);
    };

    bool operator!=(const stage_id& _other) const
    {
        return (value != _other.value);
    };

    bool is_valid() const
    {
        return (value != 0);
    };

    static stage_id null()
    {
        stage_id id;
        id.value = 0;

        return id;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   BEAM STATE                                            ///
///////////////////////////////////////////////////////////////////////////////

// beam_state
//   enum: lifecycle state of the beam.
enum class beam_state
{
    stopped,
    running,
    paused
};


///////////////////////////////////////////////////////////////////////////////
///             III.  BEAM CONTEXT                                          ///
///////////////////////////////////////////////////////////////////////////////

// beam_context
//   struct: metadata that flows with the beam and is
// accessible to all stages and the sink. Carries pipeline-
// wide information that individual templates do not own.
struct beam_context
{
    // human-readable name for this beam
    std::string name;

    // arbitrary key-value metadata
    std::unordered_map<std::string, std::string>
        metadata;

    // provenance: the source identifier
    std::string source_id;

    beam_context()
        : name()
        , metadata()
        , source_id()
    {
    };

    // set
    //   method: sets a metadata key-value pair.
    void set(const std::string& _key,
             const std::string& _value)
    {
        metadata[_key] = _value;

        return;
    };

    // get
    //   method: retrieves a metadata value. Returns empty
    // string if the key is not found.
    std::string get(const std::string& _key) const
    {
        auto it = metadata.find(_key);

        if (it != metadata.end())
        {
            return it->second;
        }

        return std::string();
    };

    // has
    //   method: returns true if the key exists.
    bool has(const std::string& _key) const
    {
        return (metadata.count(_key) > 0);
    };
};


///////////////////////////////////////////////////////////////////////////////
///             IV.   BEAM STATISTICS                                       ///
///////////////////////////////////////////////////////////////////////////////

// beam_stats
//   struct: snapshot of beam metrics.
struct beam_stats
{
    std::size_t total_injected;
    std::size_t total_delivered;
    std::size_t total_dropped;
    std::size_t total_errors;
    std::size_t stage_count;
    std::size_t enabled_stages;
    beam_state  state;
};


///////////////////////////////////////////////////////////////////////////////
///             V.    BEAM EVENTS                                           ///
///////////////////////////////////////////////////////////////////////////////

D_EVENT(on_beam_injected, std::string);
D_EVENT(on_beam_delivered, std::string);
D_EVENT(on_beam_error, std::string, int);
D_EVENT_EMPTY(on_beam_started);
D_EVENT_EMPTY(on_beam_stopped);
D_EVENT_EMPTY(on_beam_paused);


///////////////////////////////////////////////////////////////////////////////
///             VI.   INTERNAL STAGE STORAGE                                ///
///////////////////////////////////////////////////////////////////////////////

NS_INTERNAL

    // type_token
    //   function: per-type unique identifier via static
    // address trick.
    template<typename _T>
    std::size_t type_token()
    {
        static const char anchor = '\0';

        return reinterpret_cast<std::size_t>(&anchor);
    };

    // stage_entry
    //   struct: a type-erased template stage on a beam.
    struct stage_entry
    {
        stage_id    id;
        std::string name;
        bool        enabled;

        // type identifiers for adjacent stage validation
        std::size_t input_type_id;
        std::size_t output_type_id;

        // type-erased process: receives const void* input,
        // returns a pointer to the output (held in the
        // captured closure's storage)
        std::function<const void*(const void*)>
            process_fn;
    };

NS_END  // internal


///////////////////////////////////////////////////////////////////////////////
///             VII.  BEAM CLASS                                            ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_beam
//   class: the full path from source to sink. Templates are
// stages on the beam; data flows from inject() through each
// enabled stage in order, then to the sink.
//
//   At its simplest: source → template → sink.
//   The beam is the pipeline.
//
// usage:
//   tmpl_beam beam("my_beam");
//
//   beam.add_stage(parser,    "parse");
//   beam.add_stage(formatter, "format");
//   beam.set_sink<string>(file_writer);
//
//   beam.start();
//   beam.inject<string>(source_text);
class tmpl_beam
{
public:

    // ---- construction ----

    tmpl_beam()
        : m_stages()
        , m_sink_fn()
        , m_sink_type_id(0)
        , m_next_id(1)
        , m_state(beam_state::stopped)
        , m_context()
        , m_total_injected(0)
        , m_total_delivered(0)
        , m_total_dropped(0)
        , m_total_errors(0)
        , m_error_handler()
    {
    };

    explicit tmpl_beam(const std::string& _name)
        : m_stages()
        , m_sink_fn()
        , m_sink_type_id(0)
        , m_next_id(1)
        , m_state(beam_state::stopped)
        , m_context()
        , m_total_injected(0)
        , m_total_delivered(0)
        , m_total_dropped(0)
        , m_total_errors(0)
        , m_error_handler()
    {
        m_context.name = _name;
    };

    // non-copyable (stages capture `this`)
    tmpl_beam(const tmpl_beam&)            = delete;
    tmpl_beam& operator=(const tmpl_beam&) = delete;
    tmpl_beam(tmpl_beam&&)                 = delete;
    tmpl_beam& operator=(tmpl_beam&&)      = delete;

    ~tmpl_beam() = default;


    // =========================================================
    // STAGE MANAGEMENT
    // =========================================================

    // add_stage
    //   method: adds a template as a processing stage on
    // the beam. The template's transform() is type-erased
    // into a closure. Type compatibility with the preceding
    // stage (or source type) is validated at wire time.
    //
    //   _Tmpl must satisfy template_traits::is_valid.
    template<typename _Tmpl>
    stage_id add_stage(_Tmpl& _tmpl,
                       const std::string& _name = "")
    {
        static_assert(
            template_traits<_Tmpl>::is_valid,
            "add_stage requires a type satisfying "
            "template_traits::is_valid.");

        using in_t  = typename _Tmpl::input_type;
        using out_t = typename _Tmpl::output_type;

        // type-check: if there are existing stages,
        // the new stage's input must match the last
        // stage's output
        if (!m_stages.empty())
        {
            auto& last = m_stages.back();

            if ( last.output_type_id !=
                 internal::type_token<in_t>() )
            {
                return stage_id::null();
            }
        }

        stage_id sid;
        sid.value = m_next_id++;

        internal::stage_entry entry;
        entry.id             = sid;
        entry.name           = _name;
        entry.enabled        = true;
        entry.input_type_id  =
            internal::type_token<in_t>();
        entry.output_type_id =
            internal::type_token<out_t>();

        auto* tmpl_ptr = &_tmpl;

        // capture a shared output buffer in the closure
        // so we can return a pointer to the result
        auto output_buf =
            std::make_shared<out_t>();

        entry.process_fn =
            [tmpl_ptr, output_buf]
            (const void* _raw) -> const void*
        {
            const auto& input =
                *static_cast<const in_t*>(_raw);
            *output_buf = tmpl_ptr->transform(input);

            return static_cast<const void*>(
                output_buf.get());
        };

        m_stages.push_back(std::move(entry));

        return sid;
    };

    // enable_stage
    //   method: enables a stage. Returns true if found.
    bool enable_stage(stage_id _id)
    {
        auto* entry = find_stage(_id);

        if (!entry)
        {
            return false;
        }

        entry->enabled = true;

        return true;
    };

    // disable_stage
    //   method: disables a stage. Disabled stages are
    // skipped — input passes through unchanged.
    // Returns true if found.
    bool disable_stage(stage_id _id)
    {
        auto* entry = find_stage(_id);

        if (!entry)
        {
            return false;
        }

        entry->enabled = false;

        return true;
    };

    // remove_stage
    //   method: removes a stage from the beam. Returns
    // true if found and removed.
    bool remove_stage(stage_id _id)
    {
        auto it = std::find_if(
            m_stages.begin(), m_stages.end(),
            [_id](const internal::stage_entry& _s)
            {
                return (_s.id == _id);
            });

        if (it == m_stages.end())
        {
            return false;
        }

        m_stages.erase(it);

        return true;
    };

    // stage_count
    //   method: returns the number of stages.
    std::size_t stage_count() const
    {
        return m_stages.size();
    };

    // clear_stages
    //   method: removes all stages.
    void clear_stages()
    {
        m_stages.clear();

        return;
    };


    // =========================================================
    // SINK
    // =========================================================

    // set_sink
    //   method: sets the terminal receiver for the beam.
    // The sink receives the output of the last stage (or
    // the raw injected data if there are no stages).
    //
    //   The callable is type-erased; _T must match the
    // output type of the last stage.
    template<typename _T,
             typename _Fn>
    void set_sink(_Fn&& _fn)
    {
        m_sink_type_id =
            internal::type_token<_T>();

        auto sink_impl =
            std::function<void(const _T&)>(
                std::forward<_Fn>(_fn));

        m_sink_fn =
            [sink_impl](const void* _raw)
        {
            sink_impl(
                *static_cast<const _T*>(_raw));
        };

        return;
    };

    // clear_sink
    //   method: removes the sink.
    void clear_sink()
    {
        m_sink_fn      = nullptr;
        m_sink_type_id = 0;

        return;
    };

    // has_sink
    //   method: returns true if a sink is set.
    bool has_sink() const
    {
        return static_cast<bool>(m_sink_fn);
    };


    // =========================================================
    // LIFECYCLE
    // =========================================================

    // start
    //   method: transitions the beam to running.
    void start()
    {
        m_state = beam_state::running;

        return;
    };

    // stop
    //   method: transitions the beam to stopped.
    void stop()
    {
        m_state = beam_state::stopped;

        return;
    };

    // pause
    //   method: transitions the beam to paused. While
    // paused, inject() calls are rejected.
    void pause()
    {
        m_state = beam_state::paused;

        return;
    };

    // resume
    //   method: transitions from paused to running.
    void resume()
    {
        if (m_state == beam_state::paused)
        {
            m_state = beam_state::running;
        }

        return;
    };

    // state
    //   method: returns the current lifecycle state.
    beam_state state() const
    {
        return m_state;
    };

    // is_running
    //   method: returns true if the beam is running.
    bool is_running() const
    {
        return (m_state == beam_state::running);
    };


    // =========================================================
    // INJECTION
    // =========================================================

    // inject
    //   method: feeds typed data into the beam. Data flows
    // through all enabled stages in order, then to the sink.
    //
    //   _T must match the input type of the first stage (or
    // the sink type if there are no stages). Type is checked
    // at runtime via type_token comparison.
    template<typename _T>
    bool inject(const _T& _data)
    {
        if (m_state != beam_state::running)
        {
            return false;
        }

        // type check against first stage or sink
        std::size_t expected = expected_input_type();

        if ( (expected != 0) &&
             (expected !=
                  internal::type_token<_T>()) )
        {
            ++m_total_errors;

            return false;
        }

        ++m_total_injected;

        // run through stages
        const void* current =
            static_cast<const void*>(&_data);

        for (auto& stage : m_stages)
        {
            if (!stage.enabled)
            {
                continue;
            }

            try
            {
                current = stage.process_fn(current);
            }
            catch (...)
            {
                ++m_total_errors;

                if (m_error_handler)
                {
                    m_error_handler(stage.id, -1);
                }

                return false;
            }
        }

        // deliver to sink
        if (m_sink_fn)
        {
            try
            {
                m_sink_fn(current);
                ++m_total_delivered;
            }
            catch (...)
            {
                ++m_total_errors;

                return false;
            }
        }
        else
        {
            ++m_total_dropped;
        }

        return true;
    };


    // =========================================================
    // CONTEXT
    // =========================================================

    // context
    //   method: returns a mutable reference to the beam
    // context (metadata).
    beam_context& context()
    {
        return m_context;
    };

    const beam_context& context() const
    {
        return m_context;
    };

    // name
    //   method: shortcut for context().name.
    const std::string& name() const
    {
        return m_context.name;
    };


    // =========================================================
    // ERROR HANDLING
    // =========================================================

    // on_error
    //   method: registers an error handler invoked when a
    // stage throws.
    void on_error(
        std::function<void(stage_id, int)> _handler)
    {
        m_error_handler = std::move(_handler);

        return;
    };


    // =========================================================
    // INTROSPECTION
    // =========================================================

    // stages
    //   method: returns the stage_ids of all stages.
    std::vector<stage_id> stages() const
    {
        std::vector<stage_id> result;

        result.reserve(m_stages.size());

        for (const auto& stage : m_stages)
        {
            result.push_back(stage.id);
        }

        return result;
    };

    // stage_name
    //   method: returns the name of a stage, or empty
    // string if not found.
    std::string stage_name(stage_id _id) const
    {
        auto* entry = find_stage_const(_id);

        if (!entry)
        {
            return std::string();
        }

        return entry->name;
    };

    // is_stage_enabled
    //   method: returns true if the stage exists and is
    // enabled.
    bool is_stage_enabled(stage_id _id) const
    {
        auto* entry = find_stage_const(_id);

        if (!entry)
        {
            return false;
        }

        return entry->enabled;
    };

    // get_stats
    //   method: returns a snapshot of beam metrics.
    beam_stats get_stats() const
    {
        beam_stats stats;

        stats.total_injected  = m_total_injected;
        stats.total_delivered = m_total_delivered;
        stats.total_dropped   = m_total_dropped;
        stats.total_errors    = m_total_errors;
        stats.stage_count     = m_stages.size();
        stats.state           = m_state;

        stats.enabled_stages = 0;

        for (const auto& stage : m_stages)
        {
            if (stage.enabled)
            {
                ++stats.enabled_stages;
            }
        }

        return stats;
    };

    // reset_stats
    //   method: resets all counters to zero.
    void reset_stats()
    {
        m_total_injected  = 0;
        m_total_delivered = 0;
        m_total_dropped   = 0;
        m_total_errors    = 0;

        return;
    };

private:

    // =========================================================
    // INTERNAL
    // =========================================================

    // expected_input_type
    //   method: returns the type_token of the expected input
    // for inject(). This is the first stage's input type, or
    // the sink type if there are no stages, or 0 if neither
    // is configured.
    std::size_t expected_input_type() const
    {
        if (!m_stages.empty())
        {
            return m_stages.front().input_type_id;
        }

        return m_sink_type_id;
    };

    // find_stage
    //   method: finds a stage by id.
    internal::stage_entry*
    find_stage(stage_id _id)
    {
        for (auto& stage : m_stages)
        {
            if (stage.id == _id)
            {
                return &stage;
            }
        }

        return nullptr;
    };

    const internal::stage_entry*
    find_stage_const(stage_id _id) const
    {
        for (const auto& stage : m_stages)
        {
            if (stage.id == _id)
            {
                return &stage;
            }
        }

        return nullptr;
    };

    // ---- members ----

    std::vector<internal::stage_entry> m_stages;
    std::function<void(const void*)>   m_sink_fn;
    std::size_t                        m_sink_type_id;
    std::uint64_t                      m_next_id;
    beam_state                         m_state;
    beam_context                       m_context;

    std::size_t m_total_injected;
    std::size_t m_total_delivered;
    std::size_t m_total_dropped;
    std::size_t m_total_errors;

    std::function<void(stage_id, int)> m_error_handler;
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_BEAM_
