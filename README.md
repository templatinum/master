# templatinum

A template-driven framework built on **djinterp**.

At its core, templatinum is `source → template → sink`. A beam is
the full path connecting them — templates are stages on a beam,
prisms are template-less splitters/combiners between beams, and
triggers start, stop, toggle, or interrupt the flow. The beam IS
the pipeline.

```
simplest:   source ──→ template ──→ sink

typical:    source ──→ T₁ ──→ T₂ ──→ ... ──→ Tₙ ──→ sink

split:      source ──→ T₁ ──→ prism ──┬──→ beam B ──→ sink B
                                       └──→ beam C ──→ sink C

combine:    source A ──→ T₁ ──┐
                               ├──→ prism ──→ beam D ──→ sink
            source B ──→ T₂ ──┘
```

---

## Roadmap: Real-World Example

**Goal:** A desktop manager that watches a C++ codebase, auto-formats,
commits, publishes, and synchronizes with a wiki — with bidirectional
feedback.

### Phase 1 — Local File Watching and Parsing

Core loop: detect changes, parse C++ files, produce token streams.

1. **File watcher trigger** — monitors a directory tree for file
   modifications (filesystem events, polling fallback). Each change
   produces a `file_change` event carrying the path, diff, and timestamp.
2. **C++ parser template** — `cpp_code_template` takes a source file,
   uses libclang to tokenize and extract symbols (classes, functions,
   macros, comments, includes). Outputs a structured `code_parse_result`
   containing the symbol tree, dependency edges, and diagnostics.
3. **Formatting template** — consumes `code_parse_result`, applies style
   conventions (brace placement, spacing, naming), writes the formatted
   file back to disk. This is a self-contained `input → output` template
   with no prism needed.

*Deliverables:* `trigger` (filesystem), `template` (parser, formatter),
basic `pipeline` wiring.

### Phase 2 — Git Integration and Change Tracking

Route parsed changes into a commit queue.

1. **Change recorder** — each `file_change` is logged to a local change
   database (timestamp, file, diff summary, parse delta).
2. **Commit queue beam** — a beam targeting the local git repository.
   Accumulates changes until the user provides commit notes. The beam
   holds staged entries; a trigger (user input or batch timer) flushes
   them as a commit.
3. **User prompt trigger** — surfaces a TUI prompt for commit messages.
   Fires when the commit queue reaches a threshold or on manual request.

*Deliverables:* `beam` (git target), `trigger` (user input, threshold),
change database schema.

### Phase 3 — Prism: Splitting Parse Results

This is where the architecture fans out. A single `code_parse_result` must
simultaneously feed multiple downstream consumers.

1. **Parse prism** — receives `code_parse_result` and routes it into:
   - **formatted file beam** → writes formatted source back to disk
   - **documentation beam** → extracts doc-comments, symbol signatures,
     and usage examples; targets the wiki documentation database
   - **dependency beam** → extracts `#include` edges and class
     relationships; targets the dependency graph store
   - **upload beam** → packages the formatted file for the main site
2. **Prism routing logic** — each beam receives a filtered/transformed
   view of the same parse result. The prism applies per-beam templates
   to reshape the data before emission.

*Deliverables:* `prism` (parse splitter), multiple `beam` targets,
per-beam `template` specializations.

### Phase 4 — Wiki Synchronization

Bidirectional: code changes update the wiki, wiki edits suggest code
changes.

1. **Wiki updater pipeline** — documentation beam →
   `tmpl_wiki_template` → wiki API beam. The wiki template
   maps `code_parse_result` to a `wiki_output` containing
   MediaWiki-formatted pages (module overview, class pages,
   namespace pages, enum pages, function index). Incremental:
   only changed symbols trigger page regeneration. Default
   layouts can be overridden via `set_layout()` with custom
   `tmpl_text_template` instances.
2. **Wiki watcher trigger** — monitors wiki pages (polling or webhook)
   for edits to code-derived sections. When a user edits a function
   signature or doc-comment on the wiki, the trigger fires.
3. **Reverse beam (reflection)** — wiki changes are packaged as
   `suggestion` events and reflected back into the pipeline, targeting
   the desktop manager. The manager surfaces these as proposed code
   edits the user can accept, modify, or reject.

*Deliverables:* `tmpl_wiki_template`, reverse `beam`, `trigger`
(wiki watcher), suggestion UI in `manager`.

### Phase 5 — Dependency Graph

1. **Graph store** — accumulates dependency edges from the dependency
   beam. Maintained as an adjacency structure (directed graph of file →
   file and symbol → symbol edges).
2. **Graph update template** — consumes new edges, detects cycles,
   computes transitive closures, and emits a delta. The delta feeds both
   a visualization beam (for the wiki) and an alert trigger (circular
   dependency detected).
3. **Impact analysis** — given a changed file, the graph answers "what
   else might break?" This feeds back into the parse prism's routing
   decisions: high-impact changes may trigger additional validation
   beams.

*Deliverables:* graph data structure, graph update `template`,
impact query API, cycle-detection `trigger`.

### Phase 6 — Upload and Site Publication

1. **Site upload beam** — targets the main hosting site. Accepts
   formatted files, optionally bundles related changes, and pushes via
   the site's deployment mechanism (rsync, API, git push to deploy
   repo).
2. **Publication trigger** — fires on commit (automatic) or on manual
   request. May depend on CI status if a validation beam is wired in.

*Deliverables:* upload `beam`, publication `trigger`, site adapter.

### Phase 7 — Retro: Retroactive Template Propagation

When a template used to generate wiki pages or formatted files changes
(e.g., new style rules, updated doc-comment format), retro propagates
that change to all prior outputs.

1. **Retro registry** — each template that supports retro tracks its
   output instances (file paths, wiki page IDs, commit metadata).
2. **Retro trigger** — fires when a retro-enabled template is modified.
   Walks the instance registry and re-applies the template where
   compatible.
3. **Compatibility check** — not all instances can be retroactively
   updated (e.g., a committed git diff is immutable). The retro module
   skips incompatible instances and logs them.

*Deliverables:* `retro` module, instance registry, compatibility
predicate.

### Phase 8 — Server and Collaboration

1. **Server module** — centralizes the change database, dependency graph,
   wiki sync state, and retro registry. Multiple manager instances
   connect to a shared server for multi-user workflows.
2. **Remote triggers** — server-side triggers (CI webhooks, scheduled
   rebuilds, external API events) inject into client pipelines.
3. **Conflict resolution** — when two managers produce conflicting
   changes, the server mediates (lock, merge, or flag for human review).

*Deliverables:* `server` module, remote trigger protocol, conflict
resolution strategy.

### Phase 9 — Manager TUI/GUI

1. **Desktop manager** — TUI (and eventually GUI) that surfaces:
   - file watch status, pending commits, queue state
   - wiki suggestions awaiting review
   - dependency graph visualization
   - retro propagation status and logs
   - manual trigger controls (force parse, force commit, force upload)
2. **Notification triggers** — manager-level triggers for alerts
   (build failure, circular dependency, wiki conflict).

*Deliverables:* `manager` module, TUI layout, notification system.

---

## Module Reference

### I. Template

The foundational unit. A template is a typed transformation:
it accepts one or more inputs of type `_In`, applies a transformation,
and produces one or more outputs of type `_Out`.

**Design:**

The root class `tmpl_template<_Derived, _In, _Out>` is a CRTP base.
It provides the shared interface (operator(), transform_batch(),
operator>>) and dispatches the core transform operation to the derived
class at compile time via static_cast. No virtual dispatch is used
anywhere — all method resolution is compile-time.

For runtime polymorphism (storing heterogeneous templates in a
container, composing templates whose concrete types are not known
at compile time), `tmpl_erased_template<_In, _Out>` provides
type-erasure via std::function instead of virtual base pointers.

```
tmpl_template<_Derived, _In, _Out>   CRTP base
    │
    ├── tmpl_text_template           text/token substitution
    ├── code_template<D>             code_source → code_parse_result
    │       └── cpp_code_template    C++ via libclang
    ├── tmpl_wiki_template           code_parse_result → wiki_output
    ├── tmpl_format_template         token stream → formatted source
    ├── tmpl_doc_template            symbols → documentation pages
    ├── tmpl_identity_template       passthrough (useful for taps)
    ├── tmpl_fn_template             wraps any callable
    └── tmpl_erased_template         type-erased (std::function)
```

**Core interface:**

```cpp
template<typename _Derived,
         typename _In,
         typename _Out>
class tmpl_template
{
public:
    using input_type  = _In;
    using output_type = _Out;

    // dispatches to _Derived::transform_impl() via CRTP
    _Out transform(const _In& _input);

    // batch: iterates or dispatches to transform_batch_impl
    std::vector<_Out> transform_batch(
        const std::vector<_In>& _inputs);

    // callable interface for pipeline::map(), fn_builder
    _Out operator()(const _In& _input);

    // chain: compose with another template (T1 >> T2)
    template<typename _NextDerived, typename _NextOut>
    tmpl_composed_template<_Derived, _NextDerived>
    operator>>(tmpl_template<_NextDerived,
                             _Out, _NextOut>& _next);
};
```

**Derived classes must provide:**
- `_Out transform_impl(const _In& _input)` — the core operation

**Derived classes may optionally provide:**
- `std::vector<_Out> transform_batch_impl(...)` — batch-optimized
  path; detected via SFINAE and preferred when available

**Integration with djinterp:**
- `pipeline::map()` can accept a template's `transform` as its
  mapper function, so templates compose naturally with pipelines.
- `fn_builder` chains can wrap template transforms as steps.
- Templates are callables and satisfy `is_callable<T, const _In&>`.

**Template traits:**

Compile-time introspection following the djinterp trait pattern
(structural detection, no inheritance requirement):

```cpp
template<typename _T>
struct template_traits
{
    static constexpr bool has_transform   = /* detected */;
    static constexpr bool has_batch       = /* detected */;
    static constexpr bool has_input_type  = /* detected */;
    static constexpr bool has_output_type = /* detected */;
    static constexpr bool is_chainable    = /* detected */;
    static constexpr bool is_retro_aware  = /* detected */;
};
```

---

### II. Trigger

A trigger is a condition-driven event source. Unlike djinterp's
`event_handler` (which dispatches events that are explicitly fired
at known call sites), a trigger autonomously monitors a condition
and fires when that condition is met.

**Trigger taxonomy:**

```
tmpl_trigger<_Derived, _Payload, _Policy>   CRTP base
    │
    ├── tmpl_poll_trigger         periodic check (filesystem, URL)
    │       arm_impl / disarm_impl
    │
    ├── tmpl_watch_trigger        push-based (inotify, webhook)
    │       arm_impl / disarm_impl
    │
    ├── tmpl_time_trigger         date/time schedule (cron-like)
    │       arm_impl / disarm_impl
    │
    ├── tmpl_threshold_trigger    fires when value crosses boundary
    │       arm_impl / disarm_impl
    │
    └── tmpl_compound_trigger     boolean composition of sub-triggers
            AND / OR / NOT / SEQUENCE
```

**Fire policies (compile-time, zero overhead for unused policies):**

- **`repeating_policy`** — fires every time (default). No state,
  no overhead. `should_fire()` always returns true.
- **`one_shot_policy`** — fires once then auto-disarms. One bool
  of state. `reset()` re-enables firing.
- **`edge_policy`** — fires only on false→true transition of the
  monitored condition. Derived class calls `set_condition(bool)`
  before `fire()`. Suppresses repeated fires while condition is
  held true.
- **`debounced_policy`** — suppresses re-fires within a cooldown
  window. Cooldown configured via `set_cooldown_ms()`. Uses
  `steady_clock` for timing.

Policies are template parameters — the compiler eliminates all
logic for policies that are not selected. Policy-specific methods
(`set_condition`, `set_cooldown_ms`, `reset`) are SFINAE-gated
and only exist in the overload set for their respective policy.

**CRTP interface:**

```cpp
template<typename _Derived,
         typename _Payload = void,
         typename _Policy  = repeating_policy>
class tmpl_trigger
{
public:
    // lifecycle (CRTP → arm_impl / disarm_impl)
    void arm();
    void disarm();
    bool is_armed() const;

    // callback management
    callback_id on_fire(callback_fn _fn);
    bool remove_callback(callback_id _id);
    std::size_t callback_count() const;

    // introspection
    std::size_t fire_count() const;
    const std::string& source_id() const;

protected:
    // called by derived class when condition is met
    void fire();
    void fire(const _Payload& _data);
};
```

**Derived classes must provide:**
- `void arm_impl()` — start monitoring the condition
- `void disarm_impl()` — stop monitoring

**Derived classes call (inherited protected):**
- `fire()` — when the condition is met (no payload)
- `fire(const _Payload& _data)` — with typed payload

**`trigger_context<_Payload>`:** carries metadata on each fire —
`source_id` (string identifier), `timestamp` (steady_clock), and
an optional typed `payload`. The void specialization omits the
payload field entirely.

**Compound triggers:**

Built using djinterp's predicate combinators (`predicate_and`,
`predicate_or`, etc.) to express complex conditions:

```cpp
auto trigger = compound_trigger::all_of(
    file_watch("/src"),
    time_window("09:00", "17:00"),
    threshold_trigger(queue_size, ">", 5)
);
```

---

### III. Beam

A beam is the full path from source to sink. Templates are stages
on a beam; data enters via `inject()`, flows through each enabled
stage in order, and exits through the sink. **The beam IS the
pipeline.** There is no separate pipeline concept.

At its simplest, a beam is `source → template → sink`. At its most
complex, it is a chain of type-erased template stages with lifecycle
management, trigger binding, metadata context, and error handling.

Beams carry a `beam_context`: key-value metadata that flows with
the data and is accessible to all stages and the sink. This is the
pipeline-wide information that individual templates do not own
(provenance, routing history, configuration).

```cpp
class tmpl_beam
{
public:
    // stages (templates on the beam)
    template<typename _Tmpl>
    stage_id add_stage(_Tmpl& _tmpl);   // structural

    bool enable_stage(stage_id _id);
    bool disable_stage(stage_id _id);

    // sink (terminal receiver)
    template<typename _T, typename _Fn>
    void set_sink(_Fn&& _fn);
    bool has_sink() const;

    // lifecycle
    void start();
    void stop();
    void pause();
    void resume();
    bool is_running() const;

    // injection (feeds data into the beam)
    template<typename _T>
    bool inject(const _T& _data);

    // metadata
    beam_context& context();

    // error handling
    void on_error(
        std::function<void(stage_id, int)> _handler);

    // metrics
    beam_stats get_stats() const;
};
```

**Type-erased stages:**

Each template stage is captured into a `std::function` closure at
`add_stage()` time. Type compatibility between adjacent stages is
validated via per-type ID tokens (same pattern as djinterp's
event_table). Data flows through as `const void*` internally;
each stage casts to its input type, transforms, and produces
output for the next stage.

**Relationship to djinterp::pipeline:**

djinterp's `pipeline` is a data-processing chain (map/filter/fold
over elements). Templatinum's beam is an orchestration path: it
manages a heterogeneous chain of template stages with lifecycle,
not a homogeneous sequence of operations on a container.

---

### IV. Prism

A prism is a template-less beam splitter or combiner. It routes
data between beams without transforming it. If you need to reshape
data, put a template stage on the beam — prisms just route.

**Split prism** — one input, many output beams:

```
                    ┌──→ beam B (docs)
beam A ──→ prism ───┼──→ beam C (deps)
                    └──→ beam D (format)
```

**Merge prism** — many inputs, one output beam:

```
beam A ──┐
          ├──→ prism ──→ beam D
beam B ──┘
```

```cpp
template<typename _T>
class tmpl_split_prism
{
public:
    // add output beams (with optional predicates)
    route_id add_output(tmpl_beam& _beam);
    route_id add_output(tmpl_beam& _beam,
        std::function<bool(const _T&)> _predicate);

    // feed data into the prism
    std::size_t feed(const _T& _data);

    std::size_t output_count() const;
};

template<typename _T>
class tmpl_merge_prism
{
public:
    // set the downstream beam
    void set_output(tmpl_beam& _beam);

    // accept input from any source
    void push(const _T& _data);

    // buffered mode: flush on demand
    std::size_t flush();

    bool has_output() const;
};
```

A split prism is typically wired as a beam's sink:
`beam.set_sink<T>([&prism](const T& d){ prism.feed(d); })`.
A merge prism's push() is called by multiple sources; in
immediate mode it forwards to the output beam directly, in
buffered mode it accumulates and flushes on demand.

---

### V. Retro

Given a retro-enabled template `T` that produced instances
`I₁, I₂, ..., Iₙ`, any compatible modification to `T` is
retroactively applied to all tracked instances.

**This module will be implemented last.** The design supports two
complementary tracking strategies:

1. **Registry (push):** the template maintains references to all
   outputs it has produced. When the template changes, it walks
   the registry and re-applies. Best for outputs the template
   "owns" (generated wiki pages, formatted files).

2. **Declarative (pull):** instances declare their source template
   and version. On demand (or on a trigger), they query the template
   for updates and re-derive themselves. Best for distributed or
   independently-versioned outputs.

**Core types:**

```cpp
// retro_instance: metadata about a produced output
struct retro_instance
{
    instance_id     id;
    template_id     source_template;
    version_id      source_version;      // template version at creation
    std::string     output_location;     // file path, wiki page ID, etc.
    bool            is_mutable;          // false for immutable outputs
};

// retro_registry: tracks template → instance relationships
class tmpl_retro_registry
{
public:
    void register_instance(template_id _tmpl,
                           retro_instance _inst);
    void unregister(instance_id _id);

    std::vector<retro_instance>
    instances_for(template_id _tmpl) const;

    std::vector<retro_instance>
    stale_instances(template_id _tmpl) const;
};
```

**Compatibility predicate:**

Not all instances can be updated. A `retro_compat` predicate determines
whether an instance is eligible:

```cpp
template<typename _In, typename _Out>
using retro_compat_fn = std::function<bool(
    const retro_instance&  _inst,
    const tmpl_erased_template<_In, _Out>& _old_tmpl,
    const tmpl_erased_template<_In, _Out>& _new_tmpl
)>;
```

**Retro trigger:**

A specialized trigger that fires when a retro-enabled template's
definition changes. It walks the registry, filters by compatibility,
and re-applies the template to each eligible instance.

---

### VI. Server

Centralization and collaboration for multi-user workflows.

**Responsibilities:**

- **Shared state:** the change database, dependency graph, wiki sync
  state, and retro registry are hosted on the server. Multiple manager
  instances connect and operate on shared data.
- **Remote triggers:** server-side events (CI webhooks, scheduled
  rebuilds, external API events) are forwarded to connected clients
  as triggers.
- **Conflict resolution:** when two managers produce conflicting
  changes to the same file or wiki page, the server mediates.
  Strategies: lock-based, merge-based, or flag-for-human-review.
- **Beam relay:** beams targeting remote resources (wiki API, deploy
  site) can be routed through the server, which handles
  authentication, rate limiting, and retry logic.

**Design:** TBD — will be defined when local-only functionality is
stable. Expected to use a lightweight protocol (likely over TCP or
WebSocket) with djinterp's event system for message dispatch.

---

### VII. Manager

Desktop TUI (and eventually GUI) that surfaces the pipeline state
and provides manual controls.

**Core views:**

- **Watch status** — which files/directories are being monitored,
  recent changes, trigger activity
- **Commit queue** — pending changes, commit note entry, batch/single
  commit controls
- **Wiki sync** — pages updated, suggestions awaiting review,
  conflict flags
- **Dependency graph** — visualization of file and symbol dependencies,
  cycle alerts
- **Retro status** — which templates have pending propagations, logs
  of completed retro-applies
- **Pipeline monitor** — node status, beam activity, error log

**Controls:**

- Force parse (re-scan a file or directory)
- Force commit (flush the commit queue)
- Force upload (push to site)
- Accept/reject wiki suggestions
- Pause/resume individual triggers or the entire pipeline
- Retro: manually trigger propagation, review affected instances

**Design:** TBD — framework and toolkit selection deferred.

---

## djinterp Integration Map

Templatinum is built on djinterp's existing modules. Here is how each
djinterp component maps to templatinum's needs:

| djinterp module         | templatinum usage                             |
|-------------------------|-----------------------------------------------|
| `event_handler`         | trigger dispatch, beam lifecycle events        |
| `event_table`           | listener storage for trigger callbacks        |
| `listener_registry`     | bind/unbind triggers to beam stages           |
| `event_traits`          | `D_EVENT` macros for templatinum event tags    |
| `event_context`         | propagation control in trigger chains         |
| `pipeline`              | data transformations within template stages   |
| `fn_builder`            | fluent construction of template chains        |
| `compose` / `pipe`      | template composition (`T1 >> T2`)             |
| `filter` / `filter_builder` | predicate routing in prisms              |
| `predicate_and/or/not`  | compound trigger conditions                   |
| `functional` algorithms | `map`, `fold`, `group_by` inside templates    |
| `type_traits`           | `template_traits`, `beam_traits`, etc.        |

---

## File Layout (Planned)

```
inc/
└── templatinum/
    ├── templatinum.hpp            umbrella include
    ├── template.hpp               tmpl_template (CRTP base)
    ├── template_traits.hpp        template_traits
    ├── code_template.hpp          code_template, code_parse_result
    ├── cpp_code_template.hpp      cpp_code_template (libclang)
    ├── wiki_template.hpp          tmpl_wiki_template (MediaWiki)
    ├── text_template.hpp          tmpl_text_template
    ├── text_template_traits.hpp   text_template_traits
    ├── trigger.hpp                tmpl_trigger (CRTP), fire policies
    ├── trigger_traits.hpp         trigger_traits
    ├── trigger_poll.hpp           tmpl_poll_trigger
    ├── trigger_watch.hpp          tmpl_watch_trigger
    ├── trigger_time.hpp           tmpl_time_trigger
    ├── trigger_threshold.hpp      tmpl_threshold_trigger
    ├── trigger_compound.hpp       tmpl_compound_trigger
    ├── beam.hpp                   tmpl_beam (beam = pipeline)
    ├── beam_traits.hpp            beam_traits
    ├── prism.hpp                  tmpl_split_prism, tmpl_merge_prism
    ├── prism_traits.hpp           split/merge_prism_traits
    ├── retro.hpp                  tmpl_retro_registry, retro_instance
    ├── server.hpp                 (deferred)
    └── manager.hpp                (deferred)
```
