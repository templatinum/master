/******************************************************************************
* templatinum [prism]                                              prism.hpp
*
* Template-less beam splitter and combiner (C++).
*   A prism is pure topology: it routes data between beams without
* transforming it. A split prism takes one typed input and routes it
* to multiple beams. A merge prism collects typed input from multiple
* sources and feeds it into one beam.
*
*   Prisms do not transform — if you need to reshape data between
* beams, put a template stage on the beam. Prisms just route.
*
* COMPONENTS:
*   templatinum::tmpl_split_prism<_T>
*     - one input → many beams (with optional predicates)
*
*   templatinum::tmpl_merge_prism<_T>
*     - many inputs → one beam (immediate or buffered)
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\prism.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.27
******************************************************************************/

#ifndef TEMPLATINUM_PRISM_
#define TEMPLATINUM_PRISM_ 1

#ifndef DJINTERP_
    #error "prism.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "prism.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "prism.hpp requires C++11 or higher"
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "beam.hpp"


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    ROUTE IDENTIFICATION                                  ///
///////////////////////////////////////////////////////////////////////////////

// route_id
//   struct: opaque handle for a split prism output route.
struct route_id
{
    std::uint64_t value;

    bool operator==(const route_id& _other) const
    {
        return (value == _other.value);
    };

    bool operator!=(const route_id& _other) const
    {
        return (value != _other.value);
    };

    bool is_valid() const
    {
        return (value != 0);
    };

    static route_id null()
    {
        route_id id;
        id.value = 0;

        return id;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   SPLIT PRISM                                           ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_split_prism
//   class: routes one typed input to multiple beams. Each
// output route can have an optional guard predicate. No
// transformation — data passes through unchanged.
//
//   A split prism can serve as a beam's sink: set it with
// beam.set_sink<_T>(split_fn) where split_fn calls
// prism.feed(). Or it can be fed manually.
//
// usage:
//   tmpl_split_prism<parse_result> splitter;
//
//   splitter.add_output(doc_beam);
//   splitter.add_output(dep_beam);
//   splitter.add_output(fmt_beam,
//       [](const parse_result& pr)
//       {
//           return pr.has_changes();
//       });
//
//   // wire as a beam's sink
//   main_beam.set_sink<parse_result>(
//       [&splitter](const parse_result& pr)
//       {
//           splitter.feed(pr);
//       });
template<typename _T>
class tmpl_split_prism
{
public:
    using value_type = _T;

    // ---- construction ----

    tmpl_split_prism()
        : m_routes()
        , m_next_id(1)
        , m_total_feeds(0)
        , m_total_dispatches(0)
    {
    };


    // =========================================================
    // OUTPUT ROUTES
    // =========================================================

    // add_output (beam)
    //   method: adds a beam as an output route. Data fed
    // into the prism is injected into the beam. The beam
    // must accept _T as input (first stage or sink type).
    route_id add_output(
        tmpl_beam& _beam,
        std::function<bool(const _T&)> _predicate =
            nullptr)
    {
        route_id rid;
        rid.value = m_next_id++;

        route_entry entry;
        entry.id      = rid;
        entry.enabled = true;

        auto* beam_ptr = &_beam;

        entry.deliver =
            [beam_ptr](const _T& _data)
        {
            beam_ptr->inject(_data);
        };

        if (_predicate)
        {
            entry.predicate = std::move(_predicate);
        }
        else
        {
            entry.predicate =
                [](const _T&) { return true; };
        }

        m_routes.push_back(std::move(entry));

        return rid;
    };

    // add_output (callable)
    //   method: adds any callable as an output route.
    // Useful for routing to prisms, loggers, or custom
    // receivers without a beam.
    template<typename _Fn>
    route_id add_output_fn(
        _Fn&& _fn,
        std::function<bool(const _T&)> _predicate =
            nullptr)
    {
        route_id rid;
        rid.value = m_next_id++;

        route_entry entry;
        entry.id      = rid;
        entry.enabled = true;
        entry.deliver =
            std::function<void(const _T&)>(
                std::forward<_Fn>(_fn));

        if (_predicate)
        {
            entry.predicate = std::move(_predicate);
        }
        else
        {
            entry.predicate =
                [](const _T&) { return true; };
        }

        m_routes.push_back(std::move(entry));

        return rid;
    };

    // remove_output
    //   method: removes a route. Returns true if found.
    bool remove_output(route_id _id)
    {
        for (auto it = m_routes.begin();
             it != m_routes.end();
             ++it)
        {
            if (it->id == _id)
            {
                m_routes.erase(it);

                return true;
            }
        }

        return false;
    };

    // enable_output / disable_output
    //   method: toggles a route.
    bool enable_output(route_id _id)
    {
        return set_enabled(_id, true);
    };

    bool disable_output(route_id _id)
    {
        return set_enabled(_id, false);
    };

    // output_count
    //   method: returns the number of output routes.
    std::size_t output_count() const
    {
        return m_routes.size();
    };

    // clear
    //   method: removes all output routes.
    void clear()
    {
        m_routes.clear();

        return;
    };


    // =========================================================
    // FEEDING
    // =========================================================

    // feed
    //   method: routes data to all enabled outputs whose
    // predicates pass. Returns the number of outputs that
    // received the data.
    std::size_t feed(const _T& _data)
    {
        ++m_total_feeds;

        std::size_t dispatched = 0;

        for (auto& route : m_routes)
        {
            if ( (route.enabled) &&
                 (route.predicate(_data)) )
            {
                route.deliver(_data);
                ++dispatched;
            }
        }

        m_total_dispatches += dispatched;

        return dispatched;
    };


    // =========================================================
    // INTROSPECTION
    // =========================================================

    std::size_t total_feeds() const
    {
        return m_total_feeds;
    };

    std::size_t total_dispatches() const
    {
        return m_total_dispatches;
    };

    void reset_stats()
    {
        m_total_feeds      = 0;
        m_total_dispatches = 0;

        return;
    };

private:

    // route_entry
    //   struct: a single output route.
    struct route_entry
    {
        route_id id;
        bool     enabled;

        std::function<void(const _T&)> deliver;
        std::function<bool(const _T&)> predicate;
    };

    bool set_enabled(route_id _id, bool _val)
    {
        for (auto& route : m_routes)
        {
            if (route.id == _id)
            {
                route.enabled = _val;

                return true;
            }
        }

        return false;
    };

    std::vector<route_entry> m_routes;
    std::uint64_t            m_next_id;
    std::size_t              m_total_feeds;
    std::size_t              m_total_dispatches;
};


///////////////////////////////////////////////////////////////////////////////
///             III.  MERGE PRISM                                           ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_merge_prism
//   class: collects typed input from multiple sources and
// feeds it into one beam. No transformation. Data can be
// forwarded immediately on each push, or buffered and
// flushed as a batch.
//
// usage:
//   tmpl_merge_prism<dep_edge> merger;
//   merger.set_output(graph_beam);
//
//   // sources push into the merger
//   merger.push(edge_from_file_a);
//   merger.push(edge_from_file_b);
template<typename _T>
class tmpl_merge_prism
{
public:
    using value_type = _T;

    // ---- construction ----

    tmpl_merge_prism()
        : m_output_fn()
        , m_buffer()
        , m_buffered(false)
        , m_total_pushes(0)
        , m_total_flushes(0)
    {
    };


    // =========================================================
    // OUTPUT
    // =========================================================

    // set_output (beam)
    //   method: sets the downstream beam. Each push (or
    // flush) injects into this beam.
    void set_output(tmpl_beam& _beam)
    {
        auto* beam_ptr = &_beam;

        m_output_fn =
            [beam_ptr](const _T& _data)
        {
            beam_ptr->inject(_data);
        };

        return;
    };

    // set_output_fn (callable)
    //   method: sets any callable as the output.
    template<typename _Fn>
    void set_output_fn(_Fn&& _fn)
    {
        m_output_fn =
            std::function<void(const _T&)>(
                std::forward<_Fn>(_fn));

        return;
    };

    // has_output
    //   method: returns true if an output is set.
    bool has_output() const
    {
        return static_cast<bool>(m_output_fn);
    };

    // clear_output
    //   method: removes the output.
    void clear_output()
    {
        m_output_fn = nullptr;

        return;
    };


    // =========================================================
    // BUFFERING MODE
    // =========================================================

    // set_buffered
    //   method: enables or disables buffering. When
    // buffered, push() accumulates data and flush()
    // delivers it. When immediate (default), push()
    // forwards to output directly.
    void set_buffered(bool _enable)
    {
        m_buffered = _enable;

        return;
    };

    // is_buffered
    //   method: returns true if buffering is enabled.
    bool is_buffered() const
    {
        return m_buffered;
    };


    // =========================================================
    // INPUT
    // =========================================================

    // push
    //   method: accepts input from any source. In immediate
    // mode, forwards to output. In buffered mode, stores for
    // later flush.
    void push(const _T& _data)
    {
        ++m_total_pushes;

        if (m_buffered)
        {
            m_buffer.push_back(_data);
        }
        else
        {
            if (m_output_fn)
            {
                m_output_fn(_data);
            }
        }

        return;
    };

    // push (move)
    void push(_T&& _data)
    {
        ++m_total_pushes;

        if (m_buffered)
        {
            m_buffer.push_back(std::move(_data));
        }
        else
        {
            if (m_output_fn)
            {
                m_output_fn(_data);
            }
        }

        return;
    };


    // =========================================================
    // FLUSHING
    // =========================================================

    // flush
    //   method: delivers all buffered data to the output
    // in order. Returns the number of items flushed.
    std::size_t flush()
    {
        if ( (!m_output_fn) || (m_buffer.empty()) )
        {
            return 0;
        }

        std::size_t count = m_buffer.size();

        for (auto& item : m_buffer)
        {
            m_output_fn(item);
        }

        m_buffer.clear();
        ++m_total_flushes;

        return count;
    };

    // discard
    //   method: clears the buffer without delivering.
    std::size_t discard()
    {
        std::size_t count = m_buffer.size();

        m_buffer.clear();

        return count;
    };


    // =========================================================
    // INTROSPECTION
    // =========================================================

    std::size_t buffered_count() const
    {
        return m_buffer.size();
    };

    std::size_t total_pushes() const
    {
        return m_total_pushes;
    };

    std::size_t total_flushes() const
    {
        return m_total_flushes;
    };

    void reset_stats()
    {
        m_total_pushes  = 0;
        m_total_flushes = 0;

        return;
    };

private:
    std::function<void(const _T&)> m_output_fn;
    std::vector<_T>                m_buffer;
    bool                           m_buffered;
    std::size_t                    m_total_pushes;
    std::size_t                    m_total_flushes;
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_PRISM_
