/******************************************************************************
* templatinum [trigger]                                          trigger.hpp
*
* Condition-monitoring trigger with compile-time fire policies (C++).
*   A trigger bridges external state (filesystem, clock, network, a value
* crossing a threshold) into the templatinum pipeline and djinterp event
* system. Unlike djinterp's event_handler (which dispatches events that
* are explicitly fired at known call sites), a trigger autonomously
* monitors a condition and fires when that condition is met.
*
*   The trigger base is a CRTP template parameterized on the derived
* class, an optional payload type, and a fire policy. Fire policies are
* tag types with static methods — the compiler eliminates all policy
* logic that does not apply, achieving zero overhead for unused policies.
*
*   Derived classes implement arm_impl() and disarm_impl() for their
* detection mechanism, and call fire() (or fire(_payload)) when their
* condition is met. The base handles policy enforcement, callback
* dispatch, and bookkeeping.
*
* FIRE POLICIES:
*   repeating_policy   — fires every time (default; zero state)
*   one_shot_policy    — fires once then auto-disarms
*   edge_policy        — fires only on false→true transition
*   debounced_policy   — suppresses re-fires within a cooldown
*
* COMPONENTS:
*   templatinum::repeating_policy / one_shot_policy /
*     edge_policy / debounced_policy
*     - compile-time fire policy tag types
*
*   templatinum::trigger_context<_Payload>
*     - metadata carried to callbacks on fire
*
*   templatinum::callback_id
*     - opaque handle for registered callbacks
*
*   templatinum::tmpl_trigger<_Derived, _Payload, _Policy>
*     - CRTP trigger base
*
* FEATURE DEPENDENCIES:
*   D_ENV_CPP_FEATURE_LANG_RVALUE_REFERENCES   - move semantics
*   D_ENV_CPP_FEATURE_LANG_ALIAS_TEMPLATES     - using aliases
*   D_ENV_CPP_FEATURE_LANG_LAMBDAS             - lambda closures
*
* PORTABLE ACROSS:
*   C++11, C++14, C++17, C++20, C++23, C++26
*
* path:      \inc\templatinum\trigger.hpp
* author(s): Samuel 'teer' Neal-Blim                          date: 2026.03.26
******************************************************************************/

#ifndef TEMPLATINUM_TRIGGER_
#define TEMPLATINUM_TRIGGER_ 1

// require the C++ framework header
#ifndef DJINTERP_
    #error "trigger.hpp requires djinterp.h first"
#endif

#ifndef __cplusplus
    #error "trigger.hpp requires C++ compilation mode"
#endif

#if !D_ENV_LANG_IS_CPP11_OR_HIGHER
    #error "trigger.hpp requires C++11 or higher"
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>


NS_DJINTERP
NS_TEMPLATINUM


///////////////////////////////////////////////////////////////////////////////
///             I.    FIRE POLICIES                                         ///
///////////////////////////////////////////////////////////////////////////////

// repeating_policy
//   policy: fires every time fire() is called. No state, no
// overhead. This is the default policy.
struct repeating_policy
{
    // state_type
    //   type: no state needed.
    struct state_type
    {
    };

    // should_fire
    //   function: always returns true.
    static bool should_fire(state_type&)
    {
        return true;
    };

    // post_fire
    //   function: no-op.
    static void post_fire(state_type&)
    {
        return;
    };
};

// one_shot_policy
//   policy: fires once, then all subsequent fire() calls are
// suppressed. The trigger auto-disarms after the first fire.
struct one_shot_policy
{
    // state_type
    //   type: tracks whether the trigger has fired.
    struct state_type
    {
        bool fired;

        state_type() : fired(false)
        {
        };
    };

    // should_fire
    //   function: returns true only if not yet fired.
    static bool should_fire(state_type& _s)
    {
        return (!_s.fired);
    };

    // post_fire
    //   function: marks as fired.
    static void post_fire(state_type& _s)
    {
        _s.fired = true;

        return;
    };
};

// edge_policy
//   policy: fires only on a false→true transition of the
// monitored condition. The derived class must call
// set_condition(bool) before fire() to update the condition
// state. Repeated fire() calls while the condition remains
// true are suppressed.
struct edge_policy
{
    // state_type
    //   type: tracks the previous and current condition.
    struct state_type
    {
        bool previous;
        bool current;

        state_type()
            : previous(false)
            , current(false)
        {
        };
    };

    // should_fire
    //   function: returns true only on false→true transition.
    static bool should_fire(state_type& _s)
    {
        return ( (!_s.previous) && (_s.current) );
    };

    // post_fire
    //   function: records the transition.
    static void post_fire(state_type& _s)
    {
        _s.previous = _s.current;

        return;
    };
};

// debounced_policy
//   policy: suppresses re-fires that occur within a cooldown
// window of the last successful fire. The cooldown duration
// is configured via set_cooldown() on the trigger.
struct debounced_policy
{
    using clock_type    = std::chrono::steady_clock;
    using time_point    = clock_type::time_point;
    using duration_type = clock_type::duration;

    // state_type
    //   type: tracks the last fire time and cooldown.
    struct state_type
    {
        time_point    last_fire;
        duration_type cooldown;

        state_type()
            : last_fire(time_point::min())
            , cooldown(duration_type::zero())
        {
        };
    };

    // should_fire
    //   function: returns true if the cooldown has elapsed
    // since the last fire.
    static bool should_fire(state_type& _s)
    {
        if (_s.cooldown == duration_type::zero())
        {
            return true;
        }

        auto now = clock_type::now();

        return ((now - _s.last_fire) >= _s.cooldown);
    };

    // post_fire
    //   function: records the fire time.
    static void post_fire(state_type& _s)
    {
        _s.last_fire = clock_type::now();

        return;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             II.   TRIGGER CONTEXT                                       ///
///////////////////////////////////////////////////////////////////////////////

// trigger_context
//   struct: metadata carried to callbacks when a trigger fires.
// Parameterized on _Payload — when _Payload is not void, the
// context additionally carries a typed payload value.
//
//   source_id identifies the trigger that fired (a human-readable
// string like "file_watcher:/src/main.cpp" or
// "threshold:queue_size"). The timestamp records when the fire
// occurred.
template<typename _Payload>
struct trigger_context
{
    using payload_type = _Payload;

    std::string source_id;

    std::chrono::steady_clock::time_point timestamp;

    _Payload payload;

    trigger_context()
        : source_id()
        , timestamp(
              std::chrono::steady_clock::now())
        , payload()
    {
    };

    trigger_context(std::string _source,
                    _Payload    _data)
        : source_id(std::move(_source))
        , timestamp(
              std::chrono::steady_clock::now())
        , payload(std::move(_data))
    {
    };
};

// trigger_context<void>
//   struct: specialization for triggers that carry no payload.
template<>
struct trigger_context<void>
{
    using payload_type = void;

    std::string source_id;

    std::chrono::steady_clock::time_point timestamp;

    trigger_context()
        : source_id()
        , timestamp(
              std::chrono::steady_clock::now())
    {
    };

    explicit trigger_context(std::string _source)
        : source_id(std::move(_source))
        , timestamp(
              std::chrono::steady_clock::now())
    {
    };
};


///////////////////////////////////////////////////////////////////////////////
///             III.  CALLBACK IDENTIFICATION                               ///
///////////////////////////////////////////////////////////////////////////////

// callback_id
//   struct: opaque handle for registered trigger callbacks.
// Follows the listener_id / route_id / node_id pattern.
struct callback_id
{
    std::uint64_t value;

    bool operator==(const callback_id& _other) const
    {
        return (value == _other.value);
    };

    bool operator!=(const callback_id& _other) const
    {
        return (value != _other.value);
    };

    // is_valid
    //   returns true if this id refers to a real callback.
    bool is_valid() const
    {
        return (value != 0);
    };

    // null
    //   returns an invalid callback_id sentinel.
    static callback_id null()
    {
        callback_id id;
        id.value = 0;

        return id;
    };
};


///////////////////////////////////////////////////////////////////////////////
///             IV.   TRIGGER EVENTS                                        ///
///////////////////////////////////////////////////////////////////////////////

// trigger event tags for observability.

// on_trigger_fired
//   event: fired after a trigger's callbacks are dispatched.
// Carries the source_id string.
D_EVENT(on_trigger_fired, std::string);

// on_trigger_armed
//   event: fired when a trigger is armed.
D_EVENT(on_trigger_armed, std::string);

// on_trigger_disarmed
//   event: fired when a trigger is disarmed.
D_EVENT(on_trigger_disarmed, std::string);


///////////////////////////////////////////////////////////////////////////////
///             V.    INTERNAL CALLBACK STORAGE                             ///
///////////////////////////////////////////////////////////////////////////////

NS_INTERNAL

    // callback_entry
    //   struct: a registered callback with its id and enabled
    // state.
    template<typename _Context>
    struct callback_entry
    {
        callback_id                          id;
        std::function<void(const _Context&)> fn;
        bool                                 enabled;
    };

NS_END  // internal


///////////////////////////////////////////////////////////////////////////////
///             VI.   TRIGGER BASE CLASS (CRTP)                             ///
///////////////////////////////////////////////////////////////////////////////

// tmpl_trigger
//   class: CRTP base for all templatinum triggers. Provides
// callback management, fire policy enforcement, and lifecycle
// (arm/disarm). Derived classes implement the detection
// mechanism and call fire() when their condition is met.
//
//   _Derived must provide:
//     void arm_impl()       — start monitoring
//     void disarm_impl()    — stop monitoring
//
//   _Derived calls (inherited protected):
//     void fire()                         — no-payload fire
//     void fire(const _Payload& _data)    — payload fire
//     void fire(trigger_context<_Payload>) — full context
//
//   _Policy selects the fire behavior at compile time:
//     repeating_policy  — fire every time (default)
//     one_shot_policy   — fire once then auto-disarm
//     edge_policy       — fire on false→true transition
//     debounced_policy  — suppress within cooldown window
//
// usage:
//   class my_trigger
//       : public tmpl_trigger<my_trigger, void>
//   {
//   public:
//       void arm_impl()   { /* start watching */ };
//       void disarm_impl(){ /* stop watching */ };
//       void check()
//       {
//           if (condition_met())
//           {
//               fire();
//           }
//       };
//   };
template<typename _Derived,
         typename _Payload = void,
         typename _Policy  = repeating_policy>
class tmpl_trigger
{
public:
    using payload_type = _Payload;
    using policy_type  = _Policy;
    using context_type = trigger_context<_Payload>;

    using callback_fn =
        std::function<void(const context_type&)>;

    // ---- construction ----

    tmpl_trigger()
        : m_callbacks()
        , m_next_cb_id(1)
        , m_armed(false)
        , m_source_id()
        , m_fire_count(0)
        , m_policy_state()
    {
    };

    explicit tmpl_trigger(std::string _source_id)
        : m_callbacks()
        , m_next_cb_id(1)
        , m_armed(false)
        , m_source_id(std::move(_source_id))
        , m_fire_count(0)
        , m_policy_state()
    {
    };


    // =========================================================
    // LIFECYCLE
    // =========================================================

    // arm
    //   method: activates the trigger. Dispatches to
    // _Derived::arm_impl() via CRTP.
    void arm()
    {
        if (!m_armed)
        {
            derived().arm_impl();
            m_armed = true;
        }

        return;
    };

    // disarm
    //   method: deactivates the trigger. Dispatches to
    // _Derived::disarm_impl() via CRTP.
    void disarm()
    {
        if (m_armed)
        {
            derived().disarm_impl();
            m_armed = false;
        }

        return;
    };

    // is_armed
    //   method: returns true if the trigger is currently
    // active.
    bool is_armed() const
    {
        return m_armed;
    };

    // set_source_id
    //   method: sets the source identifier string that is
    // included in trigger_context on each fire.
    void set_source_id(const std::string& _id)
    {
        m_source_id = _id;

        return;
    };

    // source_id
    //   method: returns the current source identifier.
    const std::string& source_id() const
    {
        return m_source_id;
    };


    // =========================================================
    // CALLBACK MANAGEMENT
    // =========================================================

    // on_fire
    //   method: registers a callback that is invoked each
    // time the trigger fires (subject to policy). Returns
    // a callback_id for later removal or enable/disable.
    callback_id on_fire(callback_fn _fn)
    {
        callback_id cid;
        cid.value = m_next_cb_id++;

        internal::callback_entry<context_type> entry;
        entry.id      = cid;
        entry.fn      = std::move(_fn);
        entry.enabled = true;

        m_callbacks.push_back(std::move(entry));

        return cid;
    };

    // remove_callback
    //   method: removes a registered callback. Returns true
    // if the callback was found and removed.
    bool remove_callback(callback_id _id)
    {
        for (auto it = m_callbacks.begin();
             it != m_callbacks.end();
             ++it)
        {
            if (it->id == _id)
            {
                m_callbacks.erase(it);

                return true;
            }
        }

        return false;
    };

    // enable_callback
    //   method: enables a previously disabled callback.
    bool enable_callback(callback_id _id)
    {
        for (auto& cb : m_callbacks)
        {
            if (cb.id == _id)
            {
                cb.enabled = true;

                return true;
            }
        }

        return false;
    };

    // disable_callback
    //   method: disables a callback without removing it.
    bool disable_callback(callback_id _id)
    {
        for (auto& cb : m_callbacks)
        {
            if (cb.id == _id)
            {
                cb.enabled = false;

                return true;
            }
        }

        return false;
    };

    // callback_count
    //   method: returns the number of registered callbacks.
    std::size_t callback_count() const
    {
        return m_callbacks.size();
    };

    // clear_callbacks
    //   method: removes all registered callbacks.
    void clear_callbacks()
    {
        m_callbacks.clear();

        return;
    };


    // =========================================================
    // EDGE POLICY: CONDITION CONTROL
    // =========================================================
    // These methods are only available when _Policy is
    // edge_policy. SFINAE removes them from overload
    // resolution for other policies.

    // set_condition
    //   method: updates the edge condition state. Call this
    // before fire() to indicate the current condition value.
    // fire() will only dispatch if the condition transitions
    // from false to true.
    template<typename _P = _Policy>
    typename std::enable_if<
        std::is_same<_P, edge_policy>::value
    >::type
    set_condition(bool _active)
    {
        m_policy_state.current = _active;

        return;
    };


    // =========================================================
    // DEBOUNCED POLICY: COOLDOWN CONTROL
    // =========================================================
    // These methods are only available when _Policy is
    // debounced_policy.

    // set_cooldown
    //   method: sets the minimum duration between successive
    // fires. Fires that occur within the cooldown window are
    // suppressed.
    template<typename _P = _Policy>
    typename std::enable_if<
        std::is_same<_P, debounced_policy>::value
    >::type
    set_cooldown(
        debounced_policy::duration_type _duration)
    {
        m_policy_state.cooldown = _duration;

        return;
    };

    // set_cooldown_ms
    //   method: convenience overload accepting milliseconds.
    template<typename _P = _Policy>
    typename std::enable_if<
        std::is_same<_P, debounced_policy>::value
    >::type
    set_cooldown_ms(std::size_t _ms)
    {
        m_policy_state.cooldown =
            std::chrono::milliseconds(_ms);

        return;
    };


    // =========================================================
    // ONE-SHOT POLICY: RESET
    // =========================================================
    // Available only for one_shot_policy.

    // reset
    //   method: resets the one-shot state, allowing the
    // trigger to fire again.
    template<typename _P = _Policy>
    typename std::enable_if<
        std::is_same<_P, one_shot_policy>::value
    >::type
    reset()
    {
        m_policy_state.fired = false;

        return;
    };


    // =========================================================
    // INTROSPECTION
    // =========================================================

    // fire_count
    //   method: returns the total number of times the trigger
    // has successfully fired (passed policy check and
    // dispatched callbacks).
    std::size_t fire_count() const
    {
        return m_fire_count;
    };

    // reset_fire_count
    //   method: resets the fire counter to zero.
    void reset_fire_count()
    {
        m_fire_count = 0;

        return;
    };

protected:

    // =========================================================
    // FIRE METHODS (called by derived classes)
    // =========================================================

    // fire
    //   method: attempts to fire the trigger. The fire policy
    // is consulted; if should_fire() returns false, the fire
    // is suppressed. Otherwise, all enabled callbacks are
    // invoked with a trigger_context.
    void fire()
    {
        fire_with_context(build_context());

        return;
    };

    // fire (payload)
    //   method: fires with a typed payload. Only available
    // when _Payload is not void.
    template<typename _P = _Payload>
    typename std::enable_if<
        !std::is_void<_P>::value
    >::type
    fire(const _Payload& _data)
    {
        fire_with_context(
            build_context_with(_data));

        return;
    };

    // fire_ctx
    //   method: fires with a fully constructed context.
    // Useful when the derived class wants to customize the
    // context beyond source_id and payload.
    void fire_ctx(context_type _ctx)
    {
        fire_with_context(std::move(_ctx));

        return;
    };

private:

    // =========================================================
    // INTERNAL: CRTP ACCESS
    // =========================================================

    _Derived& derived()
    {
        return static_cast<_Derived&>(*this);
    };

    const _Derived& derived() const
    {
        return static_cast<
            const _Derived&>(*this);
    };

    // =========================================================
    // INTERNAL: CONTEXT CONSTRUCTION
    // =========================================================

    // build_context (void payload specialization)
    template<typename _P = _Payload>
    typename std::enable_if<
        std::is_void<_P>::value,
        context_type
    >::type
    build_context()
    {
        return context_type(m_source_id);
    };

    // build_context (typed payload — default-constructed)
    template<typename _P = _Payload>
    typename std::enable_if<
        !std::is_void<_P>::value,
        context_type
    >::type
    build_context()
    {
        return context_type(m_source_id, _Payload());
    };

    // build_context_with (typed payload — provided)
    context_type
    build_context_with(const _Payload& _data)
    {
        return context_type(m_source_id, _data);
    };

    // =========================================================
    // INTERNAL: FIRE DISPATCH
    // =========================================================

    // fire_with_context
    //   method: the central fire path. Checks policy, then
    // dispatches to all enabled callbacks.
    void fire_with_context(context_type _ctx)
    {
        // policy gate
        if (!_Policy::should_fire(m_policy_state))
        {
            return;
        }

        // dispatch to all enabled callbacks
        for (auto& cb : m_callbacks)
        {
            if (cb.enabled)
            {
                cb.fn(_ctx);
            }
        }

        // post-fire bookkeeping
        _Policy::post_fire(m_policy_state);
        ++m_fire_count;

        // one-shot: auto-disarm after first fire
        auto_disarm_if_one_shot();

        return;
    };

    // auto_disarm_if_one_shot
    //   method: disarms the trigger if using one_shot_policy
    // and the policy has been satisfied. SFINAE ensures this
    // is a no-op for other policies.
    template<typename _P = _Policy>
    typename std::enable_if<
        std::is_same<_P, one_shot_policy>::value
    >::type
    auto_disarm_if_one_shot()
    {
        if (m_armed)
        {
            disarm();
        }

        return;
    };

    template<typename _P = _Policy>
    typename std::enable_if<
        !std::is_same<_P, one_shot_policy>::value
    >::type
    auto_disarm_if_one_shot()
    {
        return;
    };

    // ---- members ----

    std::vector<
        internal::callback_entry<context_type>>
            m_callbacks;

    std::uint64_t   m_next_cb_id;
    bool            m_armed;
    std::string     m_source_id;
    std::size_t     m_fire_count;

    typename _Policy::state_type m_policy_state;
};


NS_END  // templatinum
NS_END  // djinterp


#endif  // TEMPLATINUM_TRIGGER_
