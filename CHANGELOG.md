# Event Machine Change Log

This is the Event Machine (EM) change log.  
The implementation of EM on top of Open Data Plane (ODP) is called "em-odp".

- See:
  - `include/event_machine.h` for the umbrella public API used by applications
  - `README.md` for usage and compilation instructions.
  - `LICENSE` for licensing information.

## EM Version Numbering

The EM version numbering scheme reflects the used EM API version:

```text
maj.min.impl(-fix)
    maj.min = EM API version supported
              maj = major API version number, reserved for big changes
             .min = minor API version, incremented when the API changes
      .impl = Implementation number supporting EM API version maj.min
              Updated for implementations with no API changes, starts at .0
       -fix = Fix number, added for bugfixes on a certain implementation
              version maj.min.impl (-0 is not added, first fix is -1)
```

- Examples
  - **EM v4.1.0**:

    ```text
        maj.min = 4.1: supports EM API v4.1
          .impl =  .0: first implementation supporting API 4.1 (starts at .0)
           -fix = n/a, no fix
    ```

  - **EM v3.8.0-1**:

    ```text
        maj.min = 3.8: supports EM API v3.8
          .impl =  .0: first implementation supporting API 3.8 (starts at .0)
           -fix =  -1: first bugfix for 3.8.0
    ```

## Event Machine (EM) on ODP v4.4.0

Requires ODP API v1.48.0 or later.
Tested up to ODP API v1.51.0.

The headline features are generic event vectors and aggregator queues.
Event vectors can now hold any event type, not just packets, and the new
aggregator queues use them to bundle many small events into a single
vector automatically, cutting per-event scheduling overhead. Alongside
these, queue creation is unified into a single `em_queue_create_param()`
API, and the timer API gains priorities, ring-timer flow control and
frequency-based periodic timers.  
The sections below are organised by feature; start with the upgrade notes for
the breaking changes.

### Upgrade notes (breaking changes)

Most of v4.4.0 is additive and backward compatible. The exceptions to be
aware of before upgrading:

- **Minimum ODP API raised v1.47.0 → v1.48.0.** ODP 1.48 introduced the
  event-vector and queue-aggregation primitives EM now builds on; building
  against older ODP versions fails the configure check.
- **Config file version bumped "1.1.0" → "1.2.0".** An existing `em-odp.conf`
  must be updated — see the Configuration section for the new `vector` section
  and `queue.num_aggr` option.
- **`em_queue_type_t` renumbered.** The new `EM_QUEUE_TYPE_AGGR = 5` was
  inserted, shifting `EM_QUEUE_TYPE_LOCAL` (5 → 6) and `EM_QUEUE_TYPE_OUTPUT`
  (6 → 7). Symbolic names are unchanged; only code relying on the raw integer
  values is affected.
- **Event-group send to an unscheduled queue is now rejected**
  (`EM_ERR_BAD_CONTEXT` at `EM_CHECK_LEVEL > 1`; previously accepted
  silently) — see the Event groups section.

The `EM_QUEUE_TYPE_PARALLEL_ORDERED` → `EM_QUEUE_TYPE_ORDERED` rename keeps a
backward-compatible alias (see Queue type rename and renumbering).

### Configuration

Two distinct things change here: the **EM config file** (`config/em-odp.conf`)
gains new options, and the **runtime config-query API** gains matching fields.
They are listed separately below — "config file" always refers to
`config/em-odp.conf`, not to API-level configuration.

#### EM config file (`config/em-odp.conf`)

- Format version bumped to **"1.2.0"** — an existing file must be updated.
- New top-level `vector` section selects the event-vector backend via
  `vector.backend`. The choice is instance-wide and either-or; an EM
  instance uses *event* vectors or *packet* vectors, never both:
  - `"event"` (new default): Uses ODP event vectors that can hold any event
    type; required for aggregator queues. Needs a newer ODP with
    event-vector support. This differs from earlier EM, which only ever
    used packet vectors.
  - `"packet"` (legacy, backward compatible): Uses ODP packet vectors only,
    matching pre-v4.4.0 behaviour; aggregation is not available. Works on
    older ODP versions.
- New option `queue.num_aggr` (default 128): total maximum number of
  aggregator queues in the EM instance, shared between all scheduled and
  unscheduled parent queues. Only meaningful when
  `vector.backend = "event"`; interpreted as 0 otherwise.

#### Runtime config-query API

(see `include/event_machine/api/event_machine_init.h`)

- `em_cfgfile_opts_t` gains a `queue.num_aggr` field, a `vector.backend`
  enum (`EM_VECTOR_BACKEND_PACKET` / `EM_VECTOR_BACKEND_EVENT`) and a
  `vector.backend_str` string, exposing the config-file options above
  through the runtime config-query APIs.

#### Under the hood

- `em_init()` validates the configured `vector.backend`: if `"event"` is
  selected but the underlying ODP build has no event-vector pool support,
  init fails immediately with a descriptive error.

### Generic event vectors

(see `include/event_machine/api/event_machine_event.h`)

An event vector is a single event that carries a table of other events. Until
now a vector could hold only packets; with the `"event"` backend selected in the
EM config file, it can hold events of any type, or a mix. New constants and APIs
make the vector's content type explicit.

#### New API

- New major event type constant `EM_EVENT_TYPE_ANY` indicates an event vector
  whose table may contain events of mixed types.
- `em_event_type_t em_event_vector_type(em_event_t vector)` — returns the event
  type of the events stored in the vector's event-table; `EM_EVENT_TYPE_ANY`
  for a mixed-type vector, or a specific type when all entries share one type.
- `em_status_t em_event_vector_type_set(em_event_t vector, em_event_type_t type)`
  — sets the type tag on the vector event-table; useful when the application
  fills a vector manually and wants to record what it holds.
- `em_event_vector_info_t` gains an `event_type` field, set to the event type
  common to all events in the vector or to `EM_EVENT_TYPE_ANY` for a mix.

#### Under the hood

- Added an ODP event-vector backend: pool creation, event alloc/free/send and
  event-table access use either the ODP event-vector or ODP packet-vector
  implementation depending on the `vector.backend` config setting.
- Reorganized event inline functions into a dedicated `em_event_inline.h`
  header (moved from `em_event.h`) and pool inlines into a new
  `em_pool_inline.h`, improving the compile-time dependency structure.

### Queues

(see `include/event_machine/api/event_machine_queue.h`)

#### Aggregator queues

Bundle many small events into a single vector automatically, cutting
per-event scheduling overhead.

- New queue type `EM_QUEUE_TYPE_AGGR`: an aggregator is a lightweight
  child queue attached to a parent scheduled or unscheduled queue.
  Events sent to an aggregator are collected and bundled into a vector
  event before being enqueued on the parent, reducing per-event
  scheduling overhead. Aggregator queue handles are returned by the
  query APIs and passed to `em_send()`/`em_send_multi()` like any
  other queue.
- Aggregator queues are not created directly: they are created and
  configured as part of their parent queue by `em_queue_create_param()`
  — set `em_queue_param_t::num_aggr` and point
  `em_queue_param_t::aggr_conf` at a matching `em_queue_aggr_conf_t`
  array (pool, max timeout, max vector size, event type) initialized with
  `em_queue_aggr_conf_init()`. See Unified queue creation below.
- A vector event (major type `EM_EVENT_TYPE_VECTOR`) must never be
  sent to an aggregator queue — aggregating a vector into a vector is
  not allowed (no vectors within vectors). A vector may still be sent
  directly to the aggregators' parent queue.
- New compile-time constant `EM_QUEUE_MAX_AGGR` (default 4) sets the
  upper bound on aggregator queues per parent queue. The effective
  per-queue limit is `min(EM_QUEUE_MAX_AGGR, ODP aggregator capability)`.
- New queue aggregator query APIs:
  - `int em_queue_aggr_max_num(void)`:  
    returns the configured maximum total number of aggregator queues
    in this EM instance (set via `queue.num_aggr` in `em-odp.conf`).
  - `uint32_t em_queue_aggr_num(em_queue_t parent_queue)`:  
    returns the number of aggregator queues attached to
    `parent_queue`.
  - `em_queue_t em_queue_aggr(em_queue_t parent_queue, uint32_t aggr_index)`:  
    returns the aggregator queue handle at the given index.
  - `uint32_t em_queue_aggr_list(em_queue_t parent_queue,
                                 em_queue_t aggr_list[],
                                 uint32_t max, uint32_t *num_aggr)`:  
    writes up to `max` aggregator queue handles for `parent_queue`
    into `aggr_list[]` and optionally writes the total count into
    `*num_aggr`.
  - `uint32_t em_queue_aggr_info(em_queue_t parent_queue,
                                 em_queue_aggr_info_t aggr_info[],
                                 uint32_t max, uint32_t *num_aggr)`:  
    fills `aggr_info[]` with `em_queue_aggr_info_t` structs holding the
    configuration details (handle, pool, timeout, max size, event
    type) of each aggregator attached to `parent_queue`.

#### Unified queue creation

One struct and one function now replace the four older queue create variants
(the static and/or aggregator combinations).

- `em_queue_t em_queue_create_param(const char *name,
                                    const em_queue_param_t *param)`:  
  unified queue creation function. Replaces the need for separate
  `em_queue_create()`, `em_queue_create_static()`,
  `em_queue_create_ag()` and `em_queue_create_static_ag()` calls. All
  queue options — type, priority, queue group, atomic group, flags,
  output config and aggregator config — are expressed through a
  single `em_queue_param_t` struct.
- `void em_queue_param_init(em_queue_param_t *param)`:  
  must be called before filling in an `em_queue_param_t` to
  initialize all fields to safe defaults and set the internal
  compatibility check field.
- `em_queue_aggr_conf_t` — per-aggregator configuration struct
  holding the vector pool, maximum aggregation timeout (ns), maximum
  vector size, and the expected event type of aggregated events.
- `void em_queue_aggr_conf_init(uint32_t num_aggr,
                                em_queue_aggr_conf_t aggr_conf[])`:  
  initializes a `num_aggr`-sized `aggr_conf[]` array of
  `em_queue_aggr_conf_t` to defaults. Must be called before setting
  fields, analogous to `em_queue_param_init()`.

#### Queue type rename and renumbering

- `EM_QUEUE_TYPE_PARALLEL_ORDERED` renamed to `EM_QUEUE_TYPE_ORDERED`,
  matching ODP's `ODP_SCHED_SYNC_ORDERED` naming and the way the type
  is commonly referred to. The old name is preserved as a
  backwards-compatible alias, so existing code continues to compile
  unchanged.
- The new `EM_QUEUE_TYPE_AGGR = 5` was inserted into the
  `em_queue_type_t` enumeration, shifting the numeric values of the
  subsequent constants: `EM_QUEUE_TYPE_LOCAL` (5 -> 6) and
  `EM_QUEUE_TYPE_OUTPUT` (6 -> 7). The symbolic names are unchanged;
  only code relying on the raw integer values is affected.

#### Under the hood

- Fixed off-by-one and count leak in output queue setup that could leave
  output queues in an inconsistent state.
- Fixed: unscheduled queue enqueue errors are now always reported.
  `queue_unsched_enqueue()` previously gated the `odp_queue_enq()` return
  value on `EM_CHECK_LEVEL > 0`, silently returning `EM_OK` on a real
  enqueue failure at check level 0.
- Improved error diagnostics for queue deletion: `queue_delete()` returns
  detailed error context to all callers, giving more informative log
  messages on failure.

### Timers

(see `include/event_machine/api/event_machine_timer.h`)

#### Timer priority

Serve timeouts from important timers ahead of less important ones.

- `em_timer_attr_t` gains a `uint16_t priority` field. Timeouts from
  a higher-priority timer are served before those from a
  lower-priority timer; the scheduling algorithm is
  implementation-defined. Valid values are `0` (lowest, default) up
  to `em_timer_capability_t::max_priority` for one-shot timers, or
  `em_timer_capability_t::ring.max_priority` for periodic ring timers.
- `em_timer_capability_t` gains `uint16_t max_priority` (one-shot
  timers) and `uint16_t ring.max_priority` (ring timers) to let
  applications query the supported priority range before creating
  a timer.

#### Ring timer flow control

Limit how many timeout events may pile up on a periodic ring timer's
target queue.

- `em_timer_attr_t` gains a `uint32_t max_pending_events` field for
  periodic ring timers. A non-zero value asks the implementation to
  keep at most that many timeout events pending on the target queue
  at any time. The default value `0` disables flow control.
- `em_timer_capability_t::ring` gains `uint32_t min_pending_events`
  and `uint32_t max_pending_events` so applications can query the
  supported range. `ring.max_pending_events == 0` means flow control
  is not supported by the underlying ODP implementation; in that
  case `em_timer_attr_t::max_pending_events` must be left at `0`.

#### Frequency-based periodic ring timer

Request the timeout rate directly in Hz instead of as a base-rate
multiplier.

- New alternative periodic ring timer type
  (ODP `ODP_TIMER_TYPE_PERIODIC_FREQ`) whose per-timeout rate is
  expressed directly as a frequency in hertz instead of a base-rate
  multiplier. New `em_timer_flag_t` value `EM_TIMER_FLAG_RING_FREQ`
  (set automatically by the init function below).
- New APIs:
  - `em_status_t em_timer_ring_freq_attr_init(em_timer_attr_t *ring_attr,
    em_timer_clksrc_t clk_src, em_fract_u64_t *freq_hz, uint32_t num,
    uint64_t res_ns)`:  
    initialize an `em_timer_attr_t` from an ascending array of
    constraining frequencies.
  - `em_status_t em_timer_ring_freq_capability(em_timer_ring_freq_param_t *ring)`:  
    check a frequency combination before creating the timer; on
    `EM_ERR_NOT_SUPPORTED` the `freq_hz` array is updated in-place with
    the closest supported values.
  - `em_timer_t em_timer_ring_freq_create(const em_timer_attr_t *ring_attr)`:  
    create and start the frequency-based periodic ring timer.
  - `em_status_t em_tmo_set_periodic_ring_freq(em_tmo_t tmo,
    em_timer_tick_t start_abs, em_fract_u64_t freq_hz)`:  
    activate a periodic timeout at the requested frequency (within the
    constraining range given at create time).
- New type `em_timer_ring_freq_param_t` and a new `freqparam` field in
  `em_timer_attr_t` carrying `{clk_src, freq_hz[], num, res_ns}`.
- `em_timer_capability_t::ring` gains a `support` bitfield
  (`base_mul`, `freq`) indicating which periodic ring types are
  available, plus `em_fract_u64_t min_freq_hz` / `max_freq_hz`. The
  existing `min_base_hz` / `max_base_hz` are now valid only when
  `support.base_mul` is set.
- For a frequency-based ring timer, `em_timer_attr()` returns
  `freqparam.freq_hz` pointing to read-only implementation-internal
  storage valid until `em_timer_delete()`; it must not be modified or
  freed.

#### Under the hood

- `em_tmo_ack()` now retries when ODP returns `ODP_TIMER_BUSY` while
  re-arming a periodic timeout, instead of treating it as fatal — the
  same start tick is retried until it succeeds or `EM_TIMER_ACK_TRIES` is
  exhausted (requires ODP API >= v1.50).

### Error scopes

(see `include/event_machine/api/event_machine_error.h`)

- New API error scopes added for the new event-vector and queue APIs:
  - `EM_ESCOPE_EVENT_VECTOR_TYPE`, `EM_ESCOPE_EVENT_VECTOR_TYPE_SET`
  - `EM_ESCOPE_QUEUE_CREATE_PARAM`, `EM_ESCOPE_QUEUE_PARAM_INIT`
  - `EM_ESCOPE_QUEUE_AGGR_CONF_INIT`, `EM_ESCOPE_QUEUE_AGGR`,
    `EM_ESCOPE_QUEUE_AGGR_NUM`, `EM_ESCOPE_QUEUE_AGGR_LIST`,
    `EM_ESCOPE_QUEUE_AGGR_INFO`
- New API error scopes for the frequency-based ring timer APIs:
  - `EM_ESCOPE_TIMER_RING_FREQ_CREATE`,
    `EM_ESCOPE_TMO_SET_PERIODIC_RING_FREQ`
- Subsequent error scope numerical values shifted accordingly.

### Event groups

(see `include/event_machine/api/event_machine_event_group.h`)

- `em_send_group()` and `em_send_group_multi()` now reject sending an
  event tagged to an event group to an unscheduled queue, returning
  `EM_ERR_BAD_CONTEXT` (checked at `EM_CHECK_LEVEL > 1`). An event
  group's count is decremented only when an EO receive function
  returns; events on an unscheduled queue are dequeued directly by
  the application and never reach an EO, so the count would never
  reach zero. `em_send_group()` previously accepted this silently.
- Sending with `EM_EVENT_GROUP_UNDEF` to an unscheduled queue stays
  allowed and behaves like `em_send()` / `em_send_multi()`
  (`em_send_group_multi()` previously rejected even this case).

### Dispatcher

(see `include/event_machine/api/event_machine_dispatcher.h`)

#### Callback (un)registration timing

- `em_dispatch_register_enter_cb()`, `em_dispatch_register_exit_cb()`,
  `em_dispatch_unregister_enter_cb()` and
  `em_dispatch_unregister_exit_cb()` now document that a
  (un)registration takes effect at scheduler-burst granularity, not
  per event. Events from a burst already pulled from the scheduler
  before the call may bypass a newly registered callback or still
  invoke a just-unregistered one; the new state is guaranteed to be in
  effect from the next scheduler burst onwards on each core.
- For unregister, the existing caveat still holds: another core may
  still be executing the callback when the function returns, so care
  must be taken before freeing anything it may use.
- No API signature change; this reflects the dispatch burst
  optimizations that read the callback table once per scheduler burst.

#### Under the hood

- Dispatch fast path for the common no-callback case: whether any
  dispatch enter/exit callbacks are registered is now checked once at the
  start of each scheduler burst instead of per event, avoiding
  per-event/per-batch re-reads of the callback table.

### Other implementation changes

#### Input validation hardening

- Pool statistics: `em_pool_subpool_stats()`,
  `em_pool_subpool_stats_selected()` and `em_pool_subpool_stats_reset()` now
  reject out-of-range subpool indices, including negative ones. Only the upper
  bound was checked before, so a negative index in the caller-supplied
  `subpools[]` could cause an out-of-bounds read of the pool's subpool table
  (the `_selected()` variant even dereferenced before the check). Invalid
  indices are now rejected as `EM_ERR_BAD_ARG`.
- EM CLI `em_pool_stats --subpools`: subpool ids from the command line are
  validated, rejecting negative or out-of-range values instead of forwarding
  them to the pool-statistics APIs.
- EM CLI `top -t <time>`: the update-rate argument is validated; non-numeric,
  out-of-range and non-finite (NaN/Inf) values are rejected instead of
  yielding a zero or overflowed update interval.

#### Internal cleanup

- Removed the monolithic `em_include.h`; every `src/*.[ch]` file now
  carries the includes it actually uses (IWYU), which lets `clangd`
  and other tooling analyze files in isolation. No functional change.

#### Build

- Event-chaining output hooks: build the weak default implementations of
  `event_send_device()` / `event_send_device_multi()` in a dedicated
  translation unit (`src/em_chaining_default.c`) compiled without LTO.
  Under static + LTO builds, GCC <= 13 could bind an application's strong
  link-time override to the library's weak default instead of the override;
  keeping the defaults out of the LTO IR makes the override resolve reliably
  across toolchains and link modes (GCC 14+ was already unaffected).

#### Performance

- Event header initialization is batched for homogeneous bursts (events
  of the same type/pool), reducing per-event work on the receive path.
- Atomic group event dispatch now has much lower per-event overhead.
  An atomic group ties several queues together so that only one core
  processes their events at any one time. Previously every event from
  the group was pushed through an internal step to enforce this, even
  when a single core was handling the group with no contention. Now
  that core processes the received events directly, using the internal
  path only when another core is working the same group at the same
  time. This speeds up the common case, and helps most when the
  scheduler delivers one event at a time (as some hardware-assisted
  schedulers do). Atomic groups that use only normal priorities also
  skip some extra per-event work. Event order and the one-core-at-a-time
  guarantee are unchanged.

#### Programs

- `loop_united`: add a `-g NUM` option to enable atomic groups with
  `NUM` queues per group. Atomic groups are enabled solely via `-g`;
  the queue-type option `-q` stays atomic/parallel, matching the
  `em_queue_type_t` API surface.
- `programs/common` pktio: log the NIC MAC address right after creating
  an ODP pktio (useful e.g. when the device is bound to `vfio-pci` and no
  kernel netdev is available to query the MAC).
- `programs/common` pktio: packet-input vector setup is now
  vector-backend aware — event-vector (aggregator) mode is configured
  when `vector.backend = "event"` and packet-vector mode otherwise — and the
  pool/timeout setup picks capabilities from the correct (event-vector vs packet-vector) ODP capability.
- `dyn_cores`: flush stdio/stderr (and other streams) before forking so
  buffered data is not emitted by both parent and child.
- `timer_test_ring`: add a `-F` / `--freq-mode` option that exercises
  the new frequency-based periodic ring timer API
  (`em_timer_ring_freq_create()` etc.) instead of the base-multiplier
  variant.

## Event Machine (EM) on ODP v4.3.0

Requires ODP API v1.47.0 or later.
Tested up to ODP API v1.50.0.

### EM v4.3.0 - Summary

**Backward incompatible changes:**

- Queue
  - Remove compile-time static queue macros from the public API:  
    `EM_MAX_STATIC_QUEUES`, `EM_QUEUE_STATIC_MIN`, `EM_QUEUE_STATIC_MAX`,
    `EM_QUEUE_STATIC_NUM`.  
    Static queue handles are now configured at runtime and constructed
    via new queue APIs (see backward compatible changes below).
- Timer
  - `em_tmo_set_periodic_ring()`: remove the `tmo_ev` parameter.  
    ODP now manages ring timeout events internally — the application no longer
    provides or recycles them.
  - `em_tmo_ack()`: can no longer be used with periodic ring timeouts.
    Use the new `em_tmo_ring_ack()` instead.
- Config file
  - Replace runtime config option `queue.max_num` with `queue.num_static`
    and `queue.num_dynamic` for finer control over queue allocation.
  - Config file version bumped from "1.0.0" to "1.1.0".

**Backward compatible changes:**

- Init, conf
  - Add APIs to retrieve and print EM configuration options at runtime:  
    `em_conf_opts()`, `em_conf_opts_print()`,  
    `em_conf_local_opts()`, `em_conf_local_opts_print()`,  
    `em_cfgfile_opts()`, `em_cfgfile_opts_print()`
  - Add new public data structures: `em_cfgfile_opts_t`, `em_startup_pool_conf_t`.
- Queue
  - Add APIs to query queue limits and construct static queue handles:  
    `em_queue_internal_max_num()`, `em_queue_static_max_num()`,
    `em_queue_dynamic_max_num()`, `em_queue_static_handle()`
- Timer
  - Add `em_tmo_ring_ack()` for acknowledging periodic ring timeout events.
  - Add new error code `EM_ERR_BUSY`, returned when a timeout cannot be
    activated due to temporary resource limitations (transient, retryable).
- Error
  - Add new API error scopes: `EM_ESCOPE_CONF_OPTS`,
    `EM_ESCOPE_CONF_LOCAL_OPTS`, `EM_ESCOPE_CFGFILE_OPTS`,
    `EM_ESCOPE_QUEUE_STATIC_HANDLE`, `EM_ESCOPE_TMO_RING_ACK`.
    Subsequent error scope numerical values shifted accordingly.

### EM API v4.3 - Details

**Backward incompatible API changes:**

1. Queue
   - Static queue handle macros removed  
     (see `include/event_machine/api/event_machine_config.h.in` and
      `include/event_machine/api/event_machine_queue.h`)

     The following compile-time macros were removed from the public API:  
     `EM_MAX_STATIC_QUEUES`, `EM_QUEUE_STATIC_MIN`, `EM_QUEUE_STATIC_MAX`,
     `EM_QUEUE_STATIC_NUM`.

     Static queue handles are now runtime-config driven. Use the new APIs
     to construct handles and query limits:
     - `em_queue_static_handle(static_offset)`
     - `em_queue_static_max_num()`

     Queue counts are configured via the runtime config file:
     - `queue.num_static`
     - `queue.num_dynamic`

2. Timer (see `include/event_machine/api/event_machine_timer.h`)
   - `em_tmo_set_periodic_ring()` signature changed:

     Previously:

     ```c
     em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo,
                                          em_timer_tick_t start_abs,
                                          uint64_t multiplier,
                                          em_event_t tmo_ev);
     ```

     EM API v4.3 onwards:

     ```c
     em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo,
                                          em_timer_tick_t start_abs,
                                          uint64_t multiplier);
     ```

     The `tmo_ev` parameter has been removed. ODP now allocates and manages
     ring timeout events internally. The application must not allocate,
     provide, or free ring timeout events. This also means that the old
     pattern of passing a previously received ring event back via `tmo_ev`
     to reuse it during reactivation is no longer supported or needed.

   - `em_tmo_ack()` can no longer be used for periodic ring timeouts.
     Calling `em_tmo_ack()` with a ring timer event will return an error.
     Use the new `em_tmo_ring_ack()` function instead (see backward compatible
     API changes below).

**Backward compatible API changes:**

1. Init (see `include/event_machine/api/event_machine_init.h`)  
   New APIs to retrieve and print EM configuration options at runtime:
   - Global EM init options (given to `em_init()`):
     - `em_conf_opts(em_conf_t *conf_opts)`
     - `em_conf_opts_print(void)`

   - Core-local init options (given to `em_init_local()`/used by
     `em_init_core()`):
     - `em_conf_local_opts(em_conf_local_t *conf_local_opts)`
     - `em_conf_local_opts_print(void)`

   - EM config-file options (from `em-odp.conf` and `EM_CONFIG_FILE`):
     - `em_cfgfile_opts(em_cfgfile_opts_t *cfgfile_opts)`
     - `em_cfgfile_opts_print(void)`

   New public data structures:
   - `em_cfgfile_opts_t` — contains all config-file options.
   - `em_startup_pool_conf_t` — startup pool configuration entry.

2. Queue (see `include/event_machine/api/event_machine_queue.h`)  
   New APIs:
   - `em_queue_static_handle(uint16_t static_offset)`:
     construct a static queue handle from a given static offset.  
     Note: This function only constructs a static queue handle based on the
           provided offset. It does not verify whether the corresponding static
           queue has been created. The returned value is a handle, which may or
           may not refer to an existing queue.
   - `em_queue_internal_max_num(void)`:
     get the maximum number of internal EM queues reserved by EM.
   - `em_queue_static_max_num(void)`:
     get the configured maximum number of static queues.
   - `em_queue_dynamic_max_num(void)`:
     get the configured maximum number of dynamic queues.

   Updated documentation:
   - `em_queue_max_num()`: clarified that it returns the maximum number of
     queues that *can* be created, not the current count.
   - `em_queue_create_static()` and `em_queue_create_static_ag()`: updated
     documentation to describe static queue handles in terms of
     `em_queue_static_handle()` and the `queue.num_static` config option
     (replacing references to removed `EM_QUEUE_STATIC_MIN/MAX` macros).

3. Error (see `include/event_machine/api/event_machine_error.h`)
   - New error code: `EM_ERR_BUSY` — resource is temporarily busy.
   - New API error scopes added:
     `EM_ESCOPE_CONF_OPTS`, `EM_ESCOPE_CONF_LOCAL_OPTS`,
     `EM_ESCOPE_CFGFILE_OPTS`, `EM_ESCOPE_QUEUE_STATIC_HANDLE`,
     `EM_ESCOPE_TMO_RING_ACK`.
   - Subsequent error scope numerical values shifted accordingly.

4. Timer (see `include/event_machine/api/event_machine_timer.h`)
   - New API: `em_tmo_ring_ack(em_tmo_t tmo, em_event_t tmo_ev)`  
     A dedicated acknowledgment function for periodic ring timeout events.
     All received ring timeout events must be acknowledged with this function.  
     The received event must be passed back and must not be freed or modified
     by the application (ODP manages ring timeout events internally).

     Return values:
     - `EM_OK`: success, event taken.
     - `EM_ERR_CANCELED`: last timeout event for a cancelled timeout;
        the timeout can now be deleted.

     Unlike `em_tmo_ack()`, ring acknowledgment does not support
     skip/noskip behavior or next-event substitution since ODP manages
     the timeout event lifecycle.

     Migration from the previous API when using periodic ring timeouts:
     Replace `em_tmo_ack(tmo, event)` with `em_tmo_ring_ack(tmo, event)` in
     ring timer receive functions. The return value semantics are the same:
     `EM_OK` on success, `EM_ERR_CANCELED` on the last event after cancel.

   - New error code: `EM_ERR_BUSY`  
     Returned by `em_tmo_set_abs()`, `em_tmo_set_rel()`, `em_tmo_set_periodic()`
     and `em_tmo_set_periodic_ring()` when the timeout could not be activated
     due to temporary resource limitations. This is a transient condition —
     retrying with the same or updated arguments will likely succeed.  
     The EM error handler is NOT called for `EM_ERR_BUSY` and the event
     is not consumed (ownership remains with the caller).

   - New error scope: `EM_ESCOPE_TMO_RING_ACK`.

   - Updated ring timer documentation: ring timeout events "do not carry
     any user data" and users "must not allocate or free these events".

### EM v4.3.0 - Implementation changes

- Config file (see `config/em-odp.conf`)
  - The runtime config option `queue.max_num` has been replaced by separate
    options `queue.num_static` and `queue.num_dynamic`, giving users finer
    control over the number of static and dynamic EM queues.
  - Config file version bumped from "1.0.0" to "1.1.0".

- Init / Term
  - Improve support for repeated local init/term cycles in a running instance:
    fix invalid second init/term side effects and process-mode corner cases.
  - Destroy ODP schedule groups created for EM queue groups during EM
    termination to avoid stale-group leaks across repeated init/term cycles.

- Configure, build
  - Add ODP API version checks at both configure-time and runtime.
    Minimum supported ODP API version: v1.47.0.
    - Configure-time: a new m4 macro `EM_CHECK_ODP` enforces the minimum ODP
      version via `pkg-config`. The configure step will fail if the installed
      ODP is too old.
    - Runtime: `em_init()` compares the runtime ODP API version (from
      `odp_version_api_str()`) and the ODP header version against the EM
      required minimum. An error is printed to stderr if either is too old.
  - Move ODP minimum version defines from compiler CPPFLAGS into the
    autotools-generated `config.h`.
  - Remove legacy conditional ODP-version code paths from setup and timer
    modules.

- Timer
  - Allow ring timer base frequencies below 1 Hz.
  - Support for the new ODP >= 1.50 periodic timer API
    (`odp_timer_periodic_alloc()`) via compile-time detection.  
    Both old and new ODP API code paths are maintained for backward
    compatibility with older ODP versions.
    Key internal changes with the new ODP API:
    - Ring timer ODP allocation deferred from `em_tmo_create()` to
      `em_tmo_set_periodic_ring()` since the new ODP API requires the
      frequency multiplier at allocation time.
    - ODP manages the ring timeout event pool lifecycle internally;
      the shared EM-managed timeout event pool is no longer created.
    - Event header initialization uses an ODP user-area init callback.
    - The ODP periodic timer type `ODP_TIMER_TYPE_PERIODIC_BASE_MUL`
      replaces the older `ODP_TIMER_TYPE_PERIODIC`.
  - `em_tmo_ack()` now rejects periodic ring timeout events at the default
    check level (`EM_CHECK_LEVEL > 0`), not only at elevated debug level.

- EM CLI
  - Add commands for printing runtime options:
    `em_cfgfile_opts` and `em_conf_opts`.

- Bug fixes
  - Fix unchecked priority conversion return value in extension module.
  - Add missing `EM_ESCOPE_PACKET_CONCAT` entry to the error scope API
    name table.
  - Check `odp_queue_destroy()` return value in example programs.

- Internal cleanup and improvements
  - Remove dead code from init, event, and macro modules.
  - Optimize cache line usage for shared-memory atomic counters.
  - Add documentation and input validation to the internal object pool
    (objpool).
  - Use internal functions instead of public API calls inside the hook
    module.

- Programs
  - l2fwd: add `--concat` option to use `em_packet_concat()` in the receive
    function with selectable concatenation modes.
  - timer_test_ring: adapt to the new periodic ring timer API — uses
    `em_tmo_ring_ack()` instead of `em_tmo_ack()`, drops the removed `tmo_ev`
    parameter from `em_tmo_set_periodic_ring()` calls, and removes the
    `-E`/`--reuse-event` option (ODP manages ring timeout events internally).

- Tested against ODP API v1.50.0 (e.g., odp-linux v1.50.0.0).

## Event Machine (EM) on ODP v4.2.0

### EM API v4.2 - Summary

Backward incompatible API changes:

- Deprecated APIs
  - Deprecation of `env_atomic*`

Backward compatible API changes:

- Types
  - Move EM API types to module specific headers.
- Error
  - Change EM error scope numerical values.
- Config
  - Generate the API file `event_machine_config.h` based on options.
  - Add define guards to numerical EM configuration defines in
    `event_machine_config.h`/`.in`.
- Packet
  - Add `em_packet_concat()` to concatenate two packet events.

### EM API v4.2 - Details

**Backward incompatible API changes**:

1. Deprecated APIs (see `include/event_machine/api/event_machine_deprecated.h`/`.in`)

   Deprecation of `env_atomic*`  
   (see `include/event_machine/platform/env/environment.h`)  
   The `env_atomic` APIs hard-code the expensive atomic 'sequentially consistent'
   memory order, limiting flexibility and efficiency for applications that may
   prefer other memory orders. Applications should migrate from `env_atomic`
   APIs to e.g. ODP atomic APIs, C11 atomics or GCC atomic built-ins which allow
   additional 'relaxed', 'acquire', 'release' etc. memory order variants.

   Examples of deprecated types and APIs:  
   `env_atomic32_t`, `env_atomic64_t`,  
   `env_atomic32/64_set()`, `env_atomic32/64_get()`,  
   `env_atomic32/64_add()`, `env_atomic32/64_sub()` etc.

   Similar to other deprecated APIs, if the user wants to continue using them,
   the configure script needs to be given the `--enable-deprecated` option  
   (default: disabled):

   ```bash
   $> configure ... --enable-deprecated ...
   ```

**Backward compatible API changes**:

1. Types  
   Move EM API types to module specific headers.  
   Move EM types from `include/event_machine/api/event_machine_types.h` to
   EM module specific headers:
   - `include/event_machine/api/event_machine_event.h`
   - `include/event_machine/api/event_machine_queue.h`
   - `include/event_machine/api/event_machine_eo.h`
   - `include/event_machine/api/event_machine_scheduler.h`
   - `include/event_machine/api/event_machine_error.h`

2. Error (see `include/event_machine/api/event_machine_error.h`)  
   Change EM error scope numerical values. Remove gaps in the error scope range.

3. Config (see `include/event_machine/api/event_machine_config.h`/`.in`)

   - Generate the API file `event_machine_config.h` based on options.  
     Use the configure script options `--enable-...` to
     generate `event_machine_config.h` from `event_machine_config.h.in`.  
     See section *"Configuration"* in *"EM v4.2.0 - Implementation changes"*.

   - Add define guards to numerical EM configuration defines in
     `event_machine_config.h`/`.in`:

     ```c
     #ifndef EM_XXX
     #define EM_XXX value
     #endif
     ```
  
     These define guards allow overriding the default values from the build
     system or from a higher level config file.  
     Note that many of the `#define` options in `event_machine_config.h`/`.in`
     are preferrably set via configure script options
     (`--enable`/`disable-[option]`).

4. Packet (see `include/event_machine/api/event_machine_packet.h`)

   - Add `em_packet_concat()` to concatenate two packet events.
     Concatenates `second_pkt` to the end of `first_pkt` and updates packet
     metadata. On success, `first_pkt` receives a new handle and
     `second_pkt` becomes invalid.  

     ```c
     int em_packet_concat(em_event_t *first_pkt /*in,out*/, em_event_t second_pkt);
     ```

### EM v4.2.0 - Implementation changes

- Configuration
  - Run time config file options sorted.  
    Sort the EM config file (`config/em-odp.conf`) sections alphabetically.

  - Generate the API file `event_machine_config.h` based on options
    Use the configure script options `--enable-...` to
    generate `event_machine_config.h` from `event_machine_config.h.in`.
    This avoids passing the EM configuration defines via the compiler
    as `-DEM_...` flags, and instead puts them in a header file with
    the appropriate `#define` settings.  
    The generated `event_machine_config.h` is placed in the build tree
    under `include/event_machine/api/` and installed among the other EM API
    headers.

  - Improve the printouts for the EM runtime config file options that are
    disabled at build time via the EM_xxx_ENABLE macros and the
    configure-script (--enable/disable-...).  
    The printouts should clearly indicate when a feature is disabled at
    build time and thus the config file value (em-odp.conf) is ignored.

    Example:
    EM startup logging when EM is configured with 1) or 2) below.

    ```text
    1) em-odp/build $> ../configure --disable-esv --disable-sched-wait

       EM CONFIG FILE: ./em.conf
       EM dispatch config:
         …
         dispatch.sched_wait_ns: DISABLED (build time), using no-wait (0 ns)
         …
       EM ESV config:
         ESV is DISABLED (build time)
       …
    ````

    ```text
    2) em-odp/build $> ../configure --enable-esv --enable-sched-wait
    
       EM CONFIG FILE: ./em.conf
       EM dispatch config:
         …
         dispatch.sched_wait_ns: wait 10000ns (0.000010s)
         …
       EM ESV config:
         esv.enable: true(1)
         esv.store_state: true(1)
         esv.store_payload_first_u32: false(0)
         esv.prealloc_pools: true(1)
       …
    ````

  - Enable schedule-wait by default (`EM_SCHED_WAIT_ENABLE=1`)  
    Change the default value of `EM_SCHED_WAIT_ENABLE` from `0` to `1`,
    enabling the scheduler wait-for-event functionality by default.

    If the user wants to disable the schedule-wait functionality,
    it must explicitly be disabled by passing the `--disable-sched-wait`
    option to the `configure` script.

    The `dispatch.sched_wait_ns` value in `config/em-odp.conf` is not changed
    and remains at 10000ns by default, meaning that the scheduler will wait
    up to 10us for events before returning from `em_dispatch()` calls when no
    events are immediately available.

- Dispatch: Optimize single event dispatching  
  Optimize the common case of dispatching a single event by adding dedicated
  functions for that. This avoids the overhead of setting up arrays and loops
  for multiple events.

- Event group: counter update  
  Replace internal seq-cst atomic counters with a more relaxed acquire+release
  memory model for the event group atomic counters.  
  All counter fields that require atomic updates are combined into the same
  `egrp_counter_t` that packs them all into one 64-bit value.

- Queue group: configure cache stashing hints
  Configure cache stashing hints for event scheduling when creating an
  EM queue group.  
  Request cache stashing for:
  - first cache line of EM event payload data
  - EM event header
  - the EM queue element (first cache line that contains the most
    important data from a performance point of view)

  Depending on ODP implementation, these hints may or may not be used.

- Error
  - Symbolic names in error logs  
    When logging errors, EM also uses symbolic names for error codes
    and error scopes to aid debugging and readability.

    The old EM error logging format,  
    formatted by e.g. `em_error_format_string()` and EM's default error handler:

    ```text
    EM ERROR:0x00000001 ESCOPE:0xFF000404
    core:00 ecount:0(0) event_machine_eo.c:151 em_eo_create()
    Mandatory EO function pointer(s) NULL!
    ```

    The *new*, updated EM error logging format also prints the symbolic names
    for error codes and error scopes:

    ```text
    EM ERROR:0x00000001(EM_ERR_BAD_ARG)  ESCOPE:0xFF00001A(EM_ESCOPE_EO_CREATE)
    core:00 ecount:0(0) event_machine_eo.c:151 em_eo_create()
    Mandatory EO function pointer(s) NULL!
    ```

  - Log errors with one call to the log function  
    To avoid interleaved output when multiple threads log errors
    simultaneously, build the complete log message into a buffer first,
    then call the log function once.

- Fixes:
  - dispatch, fix: explicitly convert 0 ns to `ODP_SCHED_NO_WAIT`  
    If the `sched_wait_ns` value is 0 ns, then explicitly convert the `sched_wait`
    value to `ODP_SCHED_NO_WAIT` instead of calling
    `sched_wait = odp_schedule_wait_time(0 ns)`.
    This ensures that all odp targets use no wait value for the odp schedule
    call when a 0 ns wait is requested.

- Tested against ODP API v1.48.0 (e.g., odp-linux v1.48.0.0).

## Event Machine (EM) on ODP v4.1.0

### EM API v4.1 - Summary

- Pool: Get pool statistics options that are in use for a pool
  - `em_pool_stats_opt()` - retrieve the used pool statistics options
  - `em_pool_stats_opt_print()` - print used pool statistics options
  - `em_pool_stats_opt_print_all()` - print used pool statistics options
                                      for all pools
- Event: Support pass-through of non-EM events
  - New (major) EM event type: `EM_EVENT_TYPE_ODP` indicates a non-EM event,
    i.e. a native ODP-event not wrapped by EM. EM will pass all unknown ODP
    events through to the user and report the event type as `EM_EVENT_TYPE_ODP`.

### EM API v4.1 - Details

`EM_VERSION_API_MAJOR=4`, `EM_VERSION_API_MINOR=1`

1. Pool (see `include/event_machine/api/event_machine_pool.h`)
   - APIs to obtain the pool statistic counter options in use for a given pool:
     - `em_pool_stats_opt()`
     - `em_pool_stats_opt_print()`
     - `em_pool_stats_opt_print_all()`

     The new functions are used to retrieve/print the EM pool statistic
     counter options currently in use.

     The options in use for pool statistics might not be known by the application
     in e.g. the following cases:
     - no specific options were given during pool creation, instead relying on
       EM config file defaults (see em-odp.conf - `pool.statistics.{…}`).
     - the pool was created by EM at startup from the config file values in
       em-odp.conf - `startup_pools.{…}`
     - some of the requested pool statistics are not supported by the underlying
       ODP implementation and thus ignored by `em_pool_create()`.

     Before asking e.g. for a specific set of pool statistics counters with
     `em_pool_stats_selected()` or `em_pool_subpool_stats_selected()` the user
     needs to know whether the desired counters are supported and in use for the
     pool to avoid errors.

   - `em_pool_info()` now also retrieves the statistic options in use by a pool:
     In addition to the new API `em_pool_stats_opt()` (described above) also
     `em_pool_info()` has been extended to return the used pool statistics
     options (amongst other pool info).  
     A new field is added to the pool info struct: `em_pool_info_t::stats_opt`:

     ```c
     typedef struct {
             ...
             /** The statistic options actually in use by a pool */
             em_pool_stats_opt_t stats_opt;
             ...
     } em_pool_info_t;
     ```

2. Event (see `include/event_machine/api/event_machine_event.h`)
   - Non-EM Events
     New EM Event (major) type: `EM_EVENT_TYPE_ODP`  
     EM can pass through and offer limited functionality to ODP events of types
     that are not natively supported by EM.  
     Examples of these non-EM events are: ODP ML-completion, ODP DMA-completion,
     ODP TX packet completion, ODP IPSec-status etc.  
     The non-EM events originate from event pools that are not created by EM.
     Non-EM events don't have an EM event header and therefore, e.g., ESV is not
     used with them. These events can be sent to EM queues, dequeued from
     unscheduled EM queues and freed.  
     The EM event type of a non-EM event is reported as `EM_EVENT_TYPE_ODP`.  
     To use a non-EM event with ODP functions, the event must first be converted
     to an ODP event with `em_odp_event2odp()` (or `em_odp_events2odp()`).  
     Non-EM events received by a user EO-receive function can be handled by
     inspecting the event type given as a receive function argument and
     comparing it to `EM_EVENT_TYPE_ODP`, converting it to an ODP event and then
     processing it via ODP APIs.

     The new event type for non-EM events is:
     `em_event_type_t::EM_EVENT_TYPE_ODP`

     Example application usage in an EO-receive function:

     ```c
     void eo_receive(void *eo_ctx_, em_event_t event, em_event_type_t type,
                     em_queue_t queue, void *q_ctx)
     {
             em_event_type_t event_type = em_event_get_type(event);
             em_event_type_t major_type = em_event_type_major(event_type);

             if (major_type == EM_EVENT_TYPE_ODP) {
                     odp_event_t odp_event = em_odp_event2odp(event);
                     odp_event_type_t odp_type = odp_event_type(odp_event);

                     if (odp_type == ODP_EVENT_ML_COMPL) {
                             /* Process ODP ML completion with ODP APIs */
                     } else if (...) {
                             ...
                     }
             } else {
                     /* EM event */
                     ...
             }
             ...
             status = em_send(event, next_queue);
     }
     ```

### EM v4.1.0 - Implementation changes

- Atomic group: support `em_atomic_processing_end()`  
  Add support for the `em_atomic_processing_end()` function when dispatching
  events from queues in an atomic group. Only allow the atomic processing
  context to be dropped for the last event of a dispatch burst (or for the last
  set of events in the case of EO multircv).

- Dispatch: track the number of local events dispatched  
  The `em_dispatch...()` functions now take the number of local queue events into
  account when reporting the number of dispatched events.

- configure, build: Further EM debug print options  
  Add options to separately enable/disable base-EM and EM-timer debug prints:

  ```text
  --enable-debug-print-all   - enable/disable all EM debug prints
  --enable-debug-print       - alias for --enable-debug-print-all for
                               backwards compatibility
  --enable-debug-print-base  - enable/disable base EM debug prints
                               (overrides all-option)
  --enable-debug-print-timer - enable/disable timer debug prints
                               (overrides all-option)
  ```

- EM CLI: "top" command  
  A new EM Command Line Interface (EM CLI) command "top" added that shows the
  EM-core load percent (%).  
  The command registers EM idle-hooks when started via the CLI and unregisters
  them when stopped, i.e. no runtime overhead when unused.  
  Lock-free updates of data guarantees that EM-worker threads will never be
  blocked by a suspended CLI-thread.  
  Note: The EM CLI must be enabled from the EM config file before use and the
        configure option `--enable-idle-hooks` must be used.

  ```text
  $ telnet localhost 55555
  EM-ODP> top

  Top started. Type 'stop' or 's' and Enter key to end it
  Update every 1.000s (1000 ms)

  Number of EM cores: 16   Round: 1
  EM-core    %CPU
  0          51.40
  1          51.40
  2          51.41
  3          51.40
  4          51.40
  5          51.41
  6          51.40
  7          51.40
  8          51.43
  9          51.43
  10         51.42
  11         51.41
  12         51.40
  13         51.41
  14         51.40
  15         51.43

  Total:     822.56(max 1600)
  Average:   51.41

  EM-ODP> stop
  EM-ODP>
  ```

- Programs / examples
  New example programs / tests added:
  - `cli_top`: EM application for testing the EM CLI "top"-command.  
     (programs/example/cli_top/cli_top.c)  
    The application sets up a periodic timeout for every core and waits for a
    given time upon receiving an event. The wait time simulates how long a core
    handles an event and the periodic timeout sets the frequency at which a core
    receives events. The wait-time/timeout ratio can roughly represent the
    percentage a core is in an active state.  
    This application provides a reference for the CLI top command. For example,
    with `cli_top -w 200000 -t 1000000` the CLI top command should see %CPU at
    around 20 (200000/1000000 = 0.2 = 20% load).

  - `event_types`: Event types test application.  
     (programs/example/event_types/event_types.c)  
    Test sending both EM events and non-EM events (i.e. native ODP events not
    wrapped by EM) through various queue types.

  Replaced the following performance tests:  
  - `loop`, `loop_multircv`, `loop_refs`, `loop_vectors` and `pairs`  
     Removed the performance test applications listed above and replaced them
     with the `loop_united` program configurable with command line arguments.
     The `loop_united` application was added in EM v3.8.0  
     (programs/performance/loop_united.c):  
       "Combines the existing performance test programs pairs, loop,
        loop_multircv, loop_refs and loop_vectors into a single program that can
        run all the individual tests via command line options."

     ```text
     Usage: loop_united EM-ODP options -- APP SPECIFIC OPTIONS
       E.g. loop_united -c 0xfe -t -- -l l -e 10

      Event Machine performance test application that loops received events, in
      different modes, and measures the average cycles consumed during the loop.

     Note to get EM-ODP options:
       -h, --help   before/without -- option.

     APP specific options
       -l MODE, --loop=MODE
             Select application MODE / loop type (default: loop)
             l: loop - loop events back into the same queue
             r: refs - loop event references back into the same queue
             v: vectors - loop event vectors back into the same queue
             m: multircv - loop events using multi-event receive-fn
             p: pairs - loop events between pairs of EOs & queues
     ...
     ```

- Tested against ODP API 1.48.0 (e.g. odp-linux 1.48.0.0)

## Event Machine (EM) on ODP v4.0.0

### EM API v4.0 - Summary

- Core:
  - EM-core types added: worker, control and external (`em_core_type_t`)
  - `em_core_id()` returns `-1` for EM external threads/processes
  - `em_core_count()` returns the number of currently running cores (`uint32_t`)
  - `em_core_count_max()` returns the max EM core count allowed for the instance
  - New APIs to iterate over existing EM-cores
  - New define `EM_MAX_EXTTHRS`
- EO:
  - Create EOs with additional parameters: `em_eo_create_param()`
    The argument `em_eo_param_t *param` adds a few new EO-options:  
    `local_start_mode` and `local_stop_mode`.  
    The `em_eo_multircv_param_t` used by `em_eo_create_multircv()` gets the
    same new options.
- Core Mask:
  - Add APIs to get the first, last and next set bit in a given EM core mask.
- Init:
  - `em_conf_t` changes - config params changes to `em_init(em_conf_t *conf)`
  - New EM-core local init API: `em_init_local()` and configuration params
  - New EM-core local term API: `em_term_local()` and configuration params
- API getter-function renaming:
  - Remove `_get_` from EM API names for more concise naming
  - Backwards compatibility defines added.
- Queue:
  - `em_queue_agroup()` moved and renamed from `em_atomic_group_get()`
  - Backwards compatibility define added.
- Timer:
  - Rename the API function `em_timer_get_all()` to `em_timer_list()`
  - Backwards compatibility define added.
- Helper APIs:
  - `em_core_id_physical()` and `em_core_mask_physical()` reimplemented
    (and renamed)
  - Backwards compatibility defines added.

### EM API v4.0 - Details

`EM_VERSION_API_MAJOR=4`, `EM_VERSION_API_MINOR=0`

The majority of the changes in the EM API v4.0 is concerned with supporting
dynamic EM-cores, i.e. adding and/or removing EM-cores to/from an already
running EM instance.

1. Core (see `include/event_machine/api/event_machine_core.h`)

   - EM-core type (`em_core_type_t`) added.  
     The following core types are supported:
       `EM_CORE_TYPE_WORKER`, `EM_CORE_TYPE_CONTROL` and `EM_CORE_TYPE_EXTERNAL`.
     Each thread/process added to EM by calling em_init_local() is assigned one
     of these types.

     EM APIs:  
     - `em_core_type_t em_core_type(void);`
       - return the EM-core type of the calling thread

   - The EM-core id of an EM external thread/process is -1, which means that the
     return value of `em_core_id()` can be negative (`-1`) in EM API v4.0.

   - EM-core counts and ids  
     The type of EM core counts have been changed to `uint32_t` since they are
     always >= 0. EM-core ids are `int` since for external threads the id is -1.
     EM APIs:  
     - `uint32_t em_core_count(void);`
        - The number of EM-cores currently running within the EM instance.
        - The EM-core count is not constant, instead it varies as EM-cores
         are added or removed.
        - Return type changed form `int` to `uint32_t`
     - `uint32_t em_core_count_max(void);`
        - Maximum number of concurrently running EM-cores allowed for this
          EM instance.
     - `uint32_t em_core_type_count(em_core_type_t core_type);`
        - Get the count of EM-cores of the requested type.
     - `int em_core_id_first(em_core_type_t core_type, uint32_t *num /*out*/);`
        - Initialize EM-core id iteration and get the first EM-core id (see also
          `em_core_id_next()` below).
     - `int em_core_id_next(void);`
        - Continue iteration over EM-core ids and get the next
          EM-core id available
     - `uint32_t em_core_ids_available(em_core_type_t core_type,
                                       em_core_mask_t *core_mask /*out*/);`
       - Retrieve the core mask for all available EM-cores of the specified
         core type.

   - New define `EM_MAX_EXTTHRS`  
     (see `include/event_machine/api/event_machine_config.h`)  
     In addition to the EM define `EM_MAX_CORES` that sets the limit for EM worker
     and control cores, a new define for the max number of EM external threads or
     processes has been added:

     ```c
       /**
        * @def EM_MAX_EXTTHRS
        * Max number of EM external threads/processes supported,
        * i.e. EM external threads/processes of type EM_CORE_TYPE_EXTERNAL.
        */
       #define EM_MAX_EXTTHRS     32
     ```

2. Execution Object (EO)  
   (see `include/event_machine/api/event_machine_eo.h`)

   Create an Execution Object (EO) with parameters.
   Add the `em_eo_create_param()` API to allow creating EO's with more options.

   EM APIs:
   - `void em_eo_param_init(em_eo_param_t *param);`
      - Initialize parameters for Execution Object (EO) creation.
   - `em_eo_t em_eo_create_param(const char *name, const em_eo_param_t *param);`
     - Create an Execution Object (EO) with parameters

   New EO parameters have been added that control the EO's startup and tear down
   behavior - see `em_eo_param_t` and `em_eo_create_param()`.
   - `em_eo_start_local_mode_t`:  
     Execution Object (EO) local start function mode.  
     The mode determines how & when the EO's local start function is run.  
     An EO local start function, if provided, is run on all, at the time
     available, EM-cores (of type `EM_CORE_TYPE_WORKER` or `EM_CORE_TYPE_CONTROL`)
     as triggered by `em_eo_start/_sync()`.  
     Adding an EM-core after the EO has already been started will not have the EO
     local start function triggered by `em_eo_start/_sync()` for the new EM-core,
     but the user may still want to have it run to properly initialize core local
     data. This option controls whether the EO's local start function is run
     during EM-core init when adding a new EM-core.
   - em_eo_stop_local_mode_t:  
     Execution Object (EO) local stop function mode.  
     The mode determines how & when the EO's local stop function is run.  
     An EO local stop function, if provided, is run on all, at the time
     available EM-cores (of type `EM_CORE_TYPE_WORKER` or `EM_CORE_TYPE_CONTROL`)
     as triggered by `em_eo_stop/_sync()`.  
     Removing an EM-core before the EO has been stopped will not have the EO
     local stop function triggered by `em_eo_stop/_sync()`, but the user may still
     want to have it run to properly clean up EM-core local data. This option
     controls whether the EO's local stop function is run during EM-core
     termination when removing an EM-core.

     Example of using the new `em_eo_create_param()` and `local_start/stop_mode`:

     ```c
        em_eo_param_t param;
        em_eo_t eo;
        ...
        em_eo_param_init(&param); /* Set default values for all fields */
        param.start = my_start_fn;
        param.local_start = my_local_start_fn;
        /* Run the local start function at EM-core init: */
        param.local_start_mode = EM_EO_START_LOCAL_MODE_INIT_RUN;
        param.stop = my_stop_fn;
        param.local_stop = my_local_stop_fn;
        /* Run the local stop function for this EO during EM-core term: */
        param.local_stop_mode = EM_EO_STOP_LOCAL_MODE_TERM_RUN;
        param.receive = my_receive_fn;
        ...
        eo = em_eo_create_param("my-eo", &param);
        if (unlikely(eo == EM_EO_UNDEF))
                report_error();
     ```

   The same `local_start_mode` and `local_stop_mode` options have been added
   to multi-receive-EO creation parameters `em_eo_multircv_param_t`.

3. EM Core Mask  
   (see include/event_machine/platform/event_machine_core_mask.h)

   Add APIs to get the first, last and next set bit in a given EM core mask.

   EM APIs:
   - `int em_core_mask_first(const em_core_mask_t *mask);`
     - Return the index of the first set bit in the core mask.
   - `int em_core_mask_last(const em_core_mask_t *mask);`
     - Return the index of the last set bit in the core mask.
   - `int em_core_mask_next(const em_core_mask_t *mask, int idx);`
     - Return the index of the next set bit in the core mask, use with
       `em_core_mask_first()` to traverse a core mask.

4. Init  
   (see include/event_machine/platform/event_machine_init.h)

   `em_conf_t` changes - changes to config params to `em_init()`
   - `em_conf_t::core_count` - type and meaning changed:  
     Type changed from `int` to `uint32_t` to align with the other APIs that are
     concerned with EM core counts.  
     The meaning has been altered:  
     Previously:  the number of EM-cores to be started for the EM instance.  
     EM API v4.0: set the "Maximum number of concurrently running EM worker and
                  control cores allowed for this EM instance".  
     Additionally the user may create threads/processes of type
     `EM_CORE_TYPE_EXTERNAL` that are not included in this max count.
     The EM external threads/processes are instead limited by the define
     `EM_MAX_EXTTHRS`, see 1.  
     EM-core ids will be assigned from the range of 0 to `.core_count-1` during
     local initialization. Based on the options given to `em_init_local()`,
     EM-cores will either be assigned in startup/init order regardless of the
     actual physical CPU ids, or, based on the user provided value (id) in
     `em_conf_local_t::core_id`.  
     Note that the range may be discountiguous if the user removes EM-cores from
     the middle of the range (by calling `em_term_local()`) or if the user
     requests specific core-ids with gaps in between.  
     The largest value that can be used for `.core_count` is the compile-time
     constant `EM_MAX_CORES`.
   - `em_conf_t::phys_mask` - meaning changed:  
     Previously:  physical core mask, exactly listing physical CPUs to be used.  
     EM API v4.0: physical core mask of CPUs allowed for EM-cores.  
     Similarly to `.core_count`, the physical CPU mask is concerned with
     concurrently running EM-cores of the type `EM_CORE_TYPE_CONTROL` or
     `EM_CORE_TYPE_WORKER`. Additionally the user may create threads/processes of
     type `EM_CORE_TYPE_EXTERNAL` that are not included in this mask.
   - `em_conf_t::input{}` and `em_conf_t::output{}` - structs removed and relevant
     content moved to per EM-core config via `em_conf_local_t` given to
     `em_init_local()`. The previous `.input{}` and `.output{}` structs don't work as
     such in EM API v4.0 with dynamic cores - the masks were not relevant when
     allowing for EM-cores to change. Now the user callbacks `input_poll_fn` and
     `output_drain_fn` are set per EM-core when they are created giving the user
     more control of the setup.

   EM-core local init and termination APIs:  
   A new EM API, `em_init_local()`, for initializing an EM-core (worker, control)
   or EM external thread/process has been added.  
   Similarly, a new EM API for local termination, `em_term_local()`, has been
   added. The APIs take a config struct as parameter to allow for better user
   control when adding or removing EM-cores to/from the instance. These config
   struct must be initialized with `em_conf_local_init()` or
   `em_term_local_init()` respectively.

   EM APIs:  
   - `void em_conf_local_init(em_conf_local_t *conf_local);`
     - Initialize configuration parameters for em_init_local()
   - `em_status_t em_init_local(const em_conf_local_t *conf_local);`
     - Initialize the current thread/process as a worker, control or
       external EM-core.
   - `void em_term_local_init(em_term_local_t *term_local);`
     - Initialize configuration parameters for em_term_local()
   - `em_status_t em_term_local(const em_term_local_t *term_local);`
     - Local terminatation of a thread set up to run EM

   The older API `em_init_core()` is similar to calling the v4.0 API
   `em_init_local(em_conf_local_t::core_type=EM_CORE_TYPE_WORKER)`.  
   The old API `em_term_core()` can only be used to terminate an EM worker core,
   while `em_term_local()`, with options, can be used for all EM-core types.
   Prefer using the newer `em_init_local()`/`em_term_local()` sequence over the
   older `em_init_core()`/`em_term_core()`.

5. Renaming EM APIs with "_get" in the name  
   EM "getter" APIs have been renamed by removing the unnecessary `_get` from
   the name to streamline and make API naming more consise.  
   Backwards compatibility maintained by adding defines for the old API names.

   Example from the EM event API:

   ```c
     uint32_t em_event_size(em_event_t event);
     /* Backwards compatible naming ("get") */
     #define em_event_get_size em_event_size

     em_event_type_t em_event_type(em_event_t event);
     /* Backwards compatible naming ("get") */
     #define em_event_get_type em_event_type
     ...
   ```

6. Queue  
   (see `include/event_machine/api/event_machine_queue.h`)  
   The EM API function `em_queue_agroup()` has been moved from the atomic group
   API to the queue API. The function has also been renamed to `em_queue_group()`
   from the previous name `em_atomic_group_get()`.  
   Backwards compatibility maintained by adding a define for the old API name.

7. Timer  
   (see `include/event_machine/api/event_machine_timer.h`)  
   Rename the API function `em_timer_get_all()` to `em_timer_list()`
   to better reflect its purpose: returns a list of all timers.  
   Gets rid of `_get` naming in line with other APIs.  
   Backwards compatibility is maintained by keeping the old
   function name as a define/macro.

8. Helper  
   (see include/event_machine/helper/event_machine_helper.h)  

   The EM Helper APIs  
   - `em_core_id_physical()`
   - `em_core_mask_physical()`  
   have been reimplemented to work with dynamic EM-cores and might be much
   slower than before since they can't rely on a static EM-core to/from physical
   core/cpu mapping. The physical cpu used by an EM-core needs to be read from
   the `/proc/` filesystem each time. The following note has been added:
   `@note This may be a very slow operation and MUST NOT be used in the
    fast path!`

### EM v4.0.0 - Implementation changes

- EM dynamic core support  
  The majority of the changes in the EM v4.0.0 release is concerned with
  supporting dynamic EM-cores, i.e. adding and/or removing EM-cores to/from an
  already running EM instance. Prior to v4.0.0 it was assumed that the user
  started all EM-cores when the EM instance was created (or shortly thereafter)
  and that the same static EM-core configuration was used until the instance and
  all EM-cores were torn down. Dynamic core support changes that assumption:  
  The user can add and/or remove EM cores at will from a running system to
  rescale the EM instance with more or fewer cores. The user can also move a
  running EM-core from one physical core to another simply by changing the
  scheduling affinity (no EM APIs needed for moves).  
  As before, the user is responsible for creating the threads or processes to be
  used as EM-cores, initialize them as odp-threads and then call the EM
  local initialization APIs for each thread/process. Similarly, when removing an
  EM-core, the user initiates it by calling EM local termination APIs on the
  thread/process to be removed from EM.

  These changes mean:
  - The EM-core count (as returned by em_core_count()) is not constant anymore,
    instead it reflects the current EM-core count and might change at any time
    the user adds or removes EM-cores.
  - The EM-core range may not be contiguous anymore. Adding new EM-cores with
    `em_init_local()` called on a new thread allocates the next free EM-core id
    for the thread - EM-core ids are set based on startup order.
    Removing an EM-core from a running EM instance (by calling `em_term_local()`
    on the thread/process to be terminated) frees that EM-core id and leaves a
    potential hole in the core id range.  
    Example:

    ```text
     a) Startup: adding 4 EM-cores => EM-core ids: 0, 1, 2, 3  EM core count=4
     b) Removing EM-core 2         => EM-core ids: 0, 1, -, 3  EM core count=3
     c) Removing EM-core 1         => EM-core ids: 0, -, -, 3  EM core count=2
     d) Adding an EM-core          => EM-core ids: 0, 1, -, 3  EM core count=3
     e) Terminating the instance   => EM-core ids: -, -, -, -  EM core count=0
    ```

   Care must be taken to change existing code that iterates based on EM-core
   count since e.g. in case b) above, iterating over a core-count of '3'
   (i.e. 0, 1, 2) would access the non-existing EM-core id '2'. Use the new
   EM APIs `em_core_id_first()` and `em_core_id_next()` to iterate over all
   existing cores instead - using these in the example b) would return 0, 1, 3
   (correctly skipping the non-existent EM-core '2').

  - Each thread or process participating in the EM instance is assigned an
    EM-core type when it is initialized.  
    An EM-core can be a 'worker' or 'control' EM-core, otherwise it is an
    EM external thread/process. EM-cores, i.e. worker and control EM-cores, are
    assigned an EM-core id in the range `[0, EM_MAX_CORES]` (with possible gaps,
    as explained above), while EM external threads/processes have no EM-core id
    ('-1' is returned if asked for).  
    The type is set when initializing the thread/process to run EM with
    `em_init_local(em_conf_local_t::core_type).`  
    The EM-core types (em_core_type_t) are:  
    - `EM_CORE_TYPE_WORKER`:  
       Worker EM-core, has an EM-core ID and must call `em_dispatch...()`.
       An EM worker core is pinned to a single CPU by the user before calling
       `em_init_local()` and it runs the EM dispatch loop with no interrupts.
       Prior to EM v4.0.0, all EM-cores had this role.  
       Meant for real-time processing of events ("RT-core(s)").
    - `EM_CORE_TYPE_CONTROL`:  
        Control EM-core, has an EM-core ID and calls dispatch (at least
        occasionally).  
        An EM control core can e.g. function as a non-realtime EM-core that
        can participate in dispatching of events, but usually via carefully
        selected queues and queue groups to avoid disturbing the EM worker
        cores that can have real time workloads.  
        An EM control core need not be pinned to single CPU, but still runs the
        dispatch loop. Meant for non-real-time processing ("NRT-core(s)").
    - EM_CORE_TYPE_EXTERNAL:  
        External EM thread/process, no EM-core ID, no dispatching allowed.
        An EM external thread is not allowed to participate in event dispatching
        (i.e. it must not call any `em_dispatch...()` functions).  
        An EM external thread can create/delete EM resources, send events and
        poll unscheduled queues etc. but is not considered an EM-core (hence no
        bit in an EM coremask) and cannot receive events via scheduled queues
        since dispatching is forbidden.
        Meant for external control or monitoring of EM.

  - The EM-core id of an EM external thread/process is -1, which means that the
    return value of `em_core_id()` can be negative (`-1`).

  - EOs have start, local-start (optional), stop, local-stop (optional) and
    receive-functions associated with them.  
    An EO can be created, deleted, started (calls EO's start-function) and
    stopped (calls EO's stop-function) on any of the EM-core types, but the EO's
    local-start, local-stop and receive functions will only be run on EM-cores
    of type 'worker' and 'control' (they have an EM-core id and call dispatch).
  - For EM-cores, added after an EO has already been created and started, the
    user needs to decide whether it makes sense to run the EOs' local start
    functions "late". Sometimes an application may require that local start
    functions are always run on an EM-core, no matter when they are started,
    and other times running them may only make sense at startup.  
    Similarly, for removing an EM-core while EOs are running, the user needs to
    decide whether local-stop functions should, or should not, be run.  
    To cater for both needs, EM has made this user configurable in two ways:
    - Global options via the EM config file (see further down) that sets the
      behaviour for all EOs in the instance:
        `eo.start_local_fn_at_init = true/false`
        `eo.stop_local_fn_at_term = true/false`  
        and/or
    - The `em_eo_start_local_mode_t` and `em_eo_stop_local_mode_t` types used in
      the new EM EO API  
      `em_eo_create_param(..., em_eo_param_t::local_start_mode)`
      overrides the global setting from the config file for a specific EO
      created with this new API.

- Orphan Queue Group flush during EM-core termination.  
  The user can select to have EM flush events from queues in queue groups that
  would be left unserviced when the EM-core terminates. The new EM API
  `em_term_local()` takes as argument an `em_term_local_t` that has the options
  `.queue_group_flush: .in_use` and `.flush_orphans` that lets the user select
  the desired behaviour per EM-core or globally via the EM config file
  (see EM config file option `queue_group.term_local_flush_orphans` below).

- EM config file options - runtime config file `config/em-odp.conf`:
  - Config file version number bumped up to "1.0.0", see new options below.
  - Execution Object (EO) options added (new section in config file)
    - Option: `eo.start_local_fn_at_init = true/false (default=false)`:  
      Run the EO local start functions of already created and started EO:s
      when a new EM-core is added to the system (true/false).  
      The EO local start functions are only run on the newly added EM-core
      for EOs in the 'running' state (`EM_EO_STATE_RUNNING`).
    - Option: `eo.stop_local_fn_at_term = true/false (default=false)`:  
      Run the EO local stop functions of already created and started EO:s
      when an EM-core is removed from the system (true/false).
      The local stop functions are only run on the EM-core for EOs in the
      'running' state (`EM_EO_STATE_RUNNING`).
  - Queue Group option added
    - Option: `queue_group.term_local_flush_orphans = true/false (default=true)`:  
      Flush events from orphaned queue groups during EM-core local termination.
      During EM-core local termination: flush the scheduler from events from
      queues that belong to queue groups that only a single terminating EM-core
      is handling. This may be needed to avoid events being stuck in the
      scheduler when an EM-core is terminating and orphaned queue groups exist
      with queues that still have events in them.  
      Alternatively, the application may modify the orphan queue group(s) to have
      another EM-core handle the events, or create a new EM-core after EM-core
      termination on another cpu - then no flushing is needed (use `false`).

- Helper APIs
  - `em_core_id_physical()`  
  - `em_core_mask_physical()`  
    have been reimplemented to work with dynamic EM-cores and might be much
    slower than before since thay can't rely on a static EM-core <-> physical
    core/cpu mapping. The physical cpu used by an EM-core needs to be read from
    the `/proc/` filesystem each time.  
    The following note has been added: `@note This may be a very slow operation
    and MUST NOT be used in the fast path!`

- Programs  
  New EM dynamic core example application added: `dyn_cores`  
  Simple interactive EM dynamic core tester. Can be used to verify the ability
  to dynamically add and remove EM-cores within an EM-instance or between
  different EM instances. Acts as a frontend and forks EM "applications" based
  on configuration.

- Fixes:
  - atomic group: fix multicore issue in dispatch  
    Fix an issue where the atomic group dispatch function was not
    properly synchronized across multiple cores. This could lead to
    events not being dispatched immediately since the lock and the
    event counters were not updated atomically.  
    Use a combined lock-and-count approach to ensure atomic updates.

- Tested against ODP API 1.47.0 (e.g. odp-linux 1.47.0.0)

## Event Machine (EM) on ODP v3.8.0-1

- Fixes:
  - event: fix `em_event_clone_part()`  
    In `em_event_clone_part()`, when cloning an event allocated not from an
    EM-pool, but directly from an odp-pool, the event header and the EM user area
    was not copied correctly.
    When using `odp_packet_copy_part()` to clone the event, the ODP user area is
    not copied resulting in an invalid EM event header and EM user area.
    This is now fixed.

## Event Machine (EM) on ODP v3.8.0

### EM API v3.8 - Summary

- Timer: change the `em_tmo_delete()` API function and
         enhance the timeout cancel and delete logic.  
  NOTE: Both an API and functional change, requires changes to
        user code logic!
- Init: check that `em_conf_init()` has been called before `em_init()`

### EM API v3.8 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=8`

1. Timer: change the `em_tmo_delete()` API function and
          enhance the timeout cancel and delete logic.  
   (see `include/event_machine/api/event_machine_timer.h`)

   NOTE: Both an API and functional change, requires changes to user code logic!

   Remove the possibility to delete an active timeout.  
   The API function `em_tmo_delete()` does not return the timeout event anymore.
   Only an inactive timeout can be deleted. The timeout must be successfully
   canceled, or the timeout must be expired and the corresponding timeout event
   received, before deletion. The user must be prepared to receive the last
   timeout event if cancelling the timeout fails (e.g. too late) and only after
   that delete the timeout.  
   A periodic or a periodic ring timeout can be deleted after a successful
   cancel, or if cancel fails with EM_ERR_TOONEAR when `em_tmo_ack()` has returned
   `EM_ERR_CANCELED` - this indicates that the acknowledged timeout is canceled
   and that it was the last timeout event coming for that periodic timeout.
   The change is needed to make the API more robust and reliable.

2. Init: check that `em_conf_init()` has been called before `em_init()`  
   (see include/event_machine/platform/event_machine_init.h)

   Verify that the user has called `em_conf_init(conf)` to initialize the
   EM configuration with default values before calling `em_init(conf)`,
   otherwise an error is reported an `em_init()` returns failure.

   ```c
   em_conf_t conf;
   em_conf_init(&conf); /* Mandatory! */
   conf. ... = ...;     /* Set needed EM options */
   ...
   ret = em_init(&conf); /* Initialize EM once */
   ...
   ret = em_init_core(); /* Initialize each EM-core separately */
   ```

### EM v3.8.0 - Implementation changes

- Fixes:
  - timer: fix proper handling of the return values of `odp_timer_cancel()`  
    The `odp_timer_cancel()` return values changed in ODP v1.41.1.  
    The return value separates failure from expired timeout.

  - event, fix: update check for setting vector evtype  
    In `em_event_set_type()`, when setting a new event type for a vector event:
    The new major event type is only used if `EM_CHECK_LEVEL >= 1`, thus move the
    variable declaration inside the if-statment.  
    Avoids potential compilation warnings with `EM_CHECK_LEVEL = 0`.

- Programs:
  - New performance test program added: `programs/performance/loop_united.c`  
    `loop_united` combines the existing performance test programs `pairs`, `loop`,
    `loop_multircv`, `loop_refs` and `loop_vectors` into a single program that
    can run all the individual tests via command line options.

## Event Machine (EM) on ODP v3.7.0

### EM API v3.7 - Summary

- Timer: `em_timer_get_all()` update
- Helper APIs: `em_version_print()`, `em_info_print()`

### EM API v3.7 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=7`

1. Timer, fix: `em_timer_get_all()` always returns the actual number of timers.  
   (see `include/event_machine/api/event_machine_timer.h`)

   The API `em_timer_get_all(*tmr_list, max)` should, according to the API spec,
   always return the actual number of timers regardless of the value of 'max'.
   The prev implementation returned a value from 0 to 'max' even if there was
   more timers - this is now fixed and the API documentation has been updated.

   Calling `num_timers = em_timer_get_all(NULL, 0);` would now always return the
   number of timers.

   When you also need a list of the timer handles:

   ```c
   num_timers = em_timer_get_all(tmr_list, max);
   if (num_timers > max) {
           /* more timers exist than could be filled into 'tmr_list' */
   }
   ```

2. Helper: Print EM information  
   (see include/event_machine/helper/event_machine_helper.h)

   Two new helper APIs were added to enable printing EM version information
   as well as various information about the running EM instance.

   Helper APIs:  
   - `void em_version_print(void)` - Print EM related version information
   - `void em_info_print(void)`    - Print miscellaneous EM information

   Both em_init() and em_version_print() will output version information:

   ```text
     ===========================================================
     EM version information: em-odp
     ===========================================================
     EM API version:         3.7
     EM version:             3.7.0, 64 bit (EM_CHECK_LEVEL:3, EM_ESV_ENABLE:1)
     EM build info:          v3.7.0-18-g7ea320af 2024-08-21 14:36
     ODP API version:        1.45.0
     ODP impl name:          odp-linux
     ODP impl details:       odp-linux 1.45.0-0 (v1.45.0) 1.45.0.0
     CPU model:              Intel(R) Xeon(R) Gold 6212U CPU
     CPU arch:               x86
     CPU ISA version:        Unknown
      SW ISA version (ODP):  x86_64
      SW ISA version (EM):   x86_64
     ...
   ```

   The helper API `em_info_print()` prints various data about the running
   EM instance that previously were part of the em_init() printouts (removed
   from `em_init()` to reduce startup clutter, printed now only on demand).

### EM v3.7.0 - Implementation changes

- Startup printouts changed  
  The EM startup printouts / logging has been changed to reduce startup clutter
  and give more control to the user about what is being printed and when.
  EM will print the version information at startup (with the same content as
  produced by the new helper API `em_version_print()`) but further printouts
  with info about the running instance has been moved into another new helper
  API `em_info_print()`, which the user can now call on demand
  (as is done by the EM example applications, see `programs/common/cm_setup.c`).

- configure, build: disable strict-aliasing (use `-fno-strict-aliasing`)

  Don't allow the compiler to assume the strictest aliasing rules.  
  Unless turned off, the `-fstrict-aliasing` option is enabled at
  optimization levels `-O2`, `-O3`, `-Os`.

  EM contains some type casting that might violate C strict aliasing rules.

## Event Machine (EM) on ODP v3.6.0

### EM API v3.6 - Summary

- Packet event APIs: new API module for manipulation of packet events
- Timer API: moved from add-ons to the base EM API
- Event: `em_event_pointer_and_size()`
- Deprecation macros/defines

### EM API v3.6 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=6`

1. Packet event APIs  
   (see `include/event_machine/api/event_machine_packet.h`)

   - Add EM APIs for basic manipulation of packet events (i.e. events allocated
     from EM pools created with event type `EM_EVENT_TYPE_PACKET`).
     More advanced packet manipulation should be done via ODP APIs by first
     converting the EM event to an ODP packet
     (see `include/event_machine/platform/event_machine_odp_ext.h`).  
     Packet events contain a certain amount of headroom before the start of the
     actual packet data. The headroom allows new data, e.g. additional protocol
     headers, to be inserted before the start of the existing packet data. The
     amount of headroom available for newly allocated packets can be configured
     globally via the EM config file or, specifically for a pool via the
     `em_pool_cfg_t` struct passed to `em_pool_create()`.
     Similarly, the packet may contain a certain amount of tailroom that can be
     used to extend the payload with additional data. The tailroom is located
     after the end of the packet payload until the end of the packet buffer.
     Pushing out the head/tail via APIs is required to include head- or tailroom
     into the packet data.  

     EM APIs:  
      - `void *em_packet_pointer(em_event_t pktev);`
      - `uint32_t em_packet_size(em_event_t pktev);`
      - `void *em_packet_pointer_and_size(em_event_t pktev, uint32_t *size /*out*/);`
      - `void *em_packet_resize(em_event_t pktev, uint32_t size);`
      - `uint32_t em_packet_headroom(em_event_t pktev);`
      - `uint32_t em_packet_tailroom(em_event_t pktev);`
      - `void *em_packet_push_head(em_event_t pktev, uint32_t len);`
      - `void *em_packet_pull_head(em_event_t pktev, uint32_t len);`
      - `void *em_packet_push_tail(em_event_t pktev, uint32_t len);`
      - `void *em_packet_pull_tail(em_event_t pktev, uint32_t len);`
      - `em_status_t em_packet_reset(em_event_t pktev, uint32_t size);`

     Note: All EM packet-APIs require that the event is an actual packet - not a
           buffer, vector or timeout etc.!  
           Consider using `EM_CHECK_LEVEL=3` in testing to catch illegal
           event types etc (`../configure --enable-check-level=3`).

2. Timer  
   (see `include/event_machine/api/event_machine_timer.h`)

   - Move the EM timer API from the add-on API to the base EM API.
     The EM timer API was earlier considered and add-on to the base EM API and
     thus located in the add-ons API directory
     (old: include/event_machine/add-ons/event_machine_timer.h).  
     The new location for the timer APIs is in the normal EM API include folder:  
     include/event_machine/api/event_machine_timer.h.
     Using the timer does not require the user to manually include the add-ons
     header files, instead including only event_machine.h is enough.  
     To preserve backwards compatibility, the old EM timer API file has been left
     in the add-ons directory but the file has no content - all content is now in
     the new header file.

     The EM Timer version number defines have been left in the API for now but
     are updated with the actual EM API version instead.
     These timer version defines will be removed in a later release:

     ```c
     /** Deprecated
      *  Major EM Timer API version. Marks possibly backwards incompatible changes.
      *  EM timer is now part of EM API. Use EM_API_VERSION_MAJOR instead.
      */
     #define EM_TIMER_API_VERSION_MAJOR  EM_API_VERSION_MAJOR
     /** Deprecated
      *  Minor EM Timer API version. Marks possibly backwards incompatible changes.
      *  EM Timer is now part of EM API. Use EM_API_VERSION_MINOR instead.
      */
     #define EM_TIMER_API_VERSION_MINOR  EM_API_VERSION_MINOR
     ```

3. Event  
   (see `include/event_machine/api/event_machine_event.h`)

   - Add the new EM API `em_event_pointer_and_size()` that combines the APIs
     `em_event_pointer()` and `em_event_get_size()` into one call to reduce API
     overhead. The user often needs both the event data pointer as well as the
     size when processing the event.  

     EM API:  
      - `void *em_event_pointer_and_size(em_event_t event, uint32_t *size /*out*/);`
  
   - Update the API documentation for `em_alloc()`, `em_alloc_multi()`,
     `em_event_pointer()` and `em_event_set_type()` to more clearly state the
     intended usage and the suitable event types.

4. Deprecation macros  
   (see `include/event_machine/api/event_machine_deprecated.h.in`
    or the generated .h file)

   - `The EM_DEPRECATED_API` define was added into the API. Its value can be
     used to check whether deprecated EM API definitions are enabled or disabled.  
     Deprecated APIs are disabled by default.  
     The deprecated APIs are meant to be removed completely in a later
     API version but can be accessed by the user for a while by setting the
     `EM_DEPRECATED_API` define.  
   - The `EM_DEPRECATE()` macro enforces deprecation of API definitions.  
     When deprecated APIs are disabled, the macro renames the API definitions so
     that the application cannot use them any more.  
     No EM APIs have yet been marked as "deprecated" by these macros / defines,
     but will be in coming releases.

5. Misc.
   - api, escope: add hex printf-format specifier `PRIxESCOPE`  
     Add the `PRIxESCOPE` printf format specifier to easier print.
     EM error scopes as hexadecimal values (instead of decimal).

   - api, init, doc: pin to unique core before `em_init_core()`  
     Update the `em_init_core()` documentation:  
     "Note, the thread or process must be pinned to a unique physical core before
      running `em_init_core()`."

### EM v3.6.0 - Implementation changes

- Add support for using ODP API 1.45  
  Update EM to be able to run against ODP API v1.45.

- Deprecated EM API enable/disable configuration option  
  Add a new configure-script option that enables/disables decprecated EM APIs.  
  Old EM APIs can be deprecated in new EM releases through the use of
  deprecation macros and defines found in event_machine_deprecated.h(.in).  
  The deprecation of an API will cause any use of it to fail the compilation.  
  The user may, however, decide to enable the deprecated APIs (by using
  `--enable-deprecated`) during a transition period until the deprecated API
  usage has been replaced.

  - `configure.ac` - new configure option:  
    `--enable-deprecated` - enable deprecated EM API definitions `[default=disabled]`

- Event State Verification (ESV) changes  
  - Add ESV compile-time vs. runtime configuration checks.  
    Verifies that ESV was activated also during
    compile time (with configure option: `--enable_esv`)
    when enabling it during runtime (via the EM config file: `esv.enable = true`).  
    Warn the user otherwise.  
    The user might not notice that the run time config file option has no effect
    if ESV has been disabled during compilation.  
    Example:

    ```text
      build> ../configure --disable-esv …  (set the define EM_ESV_ENABLE to 0)
        vs.
      <em-odp>/config/em-odp.conf:
          esv: {
                  # Runtime ESV enable/disable option.
                  # Allows the user to disable ESV without recompilation when
                  # 'configure --enable-esv'.
                  enable = true
                  …
          }
    ```

    will show the following warning at startup:

    ```text
      …
      EM ESV config: (EM_ESV_ENABLE=0)
        ESV disabled
        esv.enable: true(1)
        WARNING: ESV disabled (build-time) - config file option IGNORED!
      …
    ```

    Note that the warning is only printed, no other action is taken.

- EM Termination
  - Move functionality from em_term_core() to em_term().
    Now em_term_core() only flushes events stashed locally on the core
    and em_term() flushes the rest of the events at the end.
    Additionally, em_term() joins all available queue groups to be able to
    flush all events from the scheduler before terminating.

- Programs
  - New performance test programs added
    - `programs/performance/queues_output.c`  
      Event Machine performance test for output queues.
    - `programs/performance/loop_vectors.c`  
      Event Machine performance test for vector events

## Event Machine (EM) on ODP v3.5.0

### EM API v3.5 - Summary

- Queue: replace `EM_MAX_QUEUES` with config file option `queue.max_num` and
         `em_queue_get_max_num()`
- Queue conversions: EM-queue <-> {device-id, queue-id}
- Event, pool: `em_event_get_pool_subpool()`
- EM-handle to unsigned integer conversion functions.
- EM device-ID: get the current device-id with `em_device_id()`

### EM API v3.5 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=5`

1. Queue  
   (see `include/event_machine/api/event_machine_queue.h` for documentation)

   - Replace the EM API define ~~`EM_MAX_QUEUES`~~ with a runtime config file
     option `queue.max_num` (see `config/em-odp.conf`)  
     In addition, add a new define `EM_MAX_OUTPUT_QUEUES` and a function that
     returns the current setting for the maximum number of EM queues:  
     EM API:  
      - Remove ~~`EM_MAX_QUEUES`~~
      - `#define EM_MAX_OUTPUT_QUEUES ...`
      - `int em_queue_get_max_num(void);`

   - Conversions between EM queue and device-id, queue-id  
     Extract the device-id and/or queue-id from an EM queue handle.  
     EM API:  
     - `uint16_t em_queue_get_device_id(em_queue_t queue);`
     - `uint16_t em_queue_get_qid(em_queue_t queue);`
     - `void em_queue_get_ids(em_queue_t queue, uint16_t *device_id, uint16_t *qid);`

     Also add a function that let's the user construct a raw
     EM queue handle that may, or may not, refer to a real queue.  
     This is for event-chaining / BIP use cases:  
     EM API:  
     - `em_queue_t em_queue_handle_raw(uint16_t device_id, uint16_t qid);`

2. Event  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   - Add the `em_event_get_pool_subpool()` function  
     Add a new event API function that returns both the EM event-pool and the
     subpool the event was allocated from.  
     EM API:  
     - `em_pool_t em_event_get_pool_subpool(em_event_t event, int *subpool /out/);`

3. EM Handle
   - EM-handle to unsigned integer conversion functions.  
     Diagnostic conversion-to-'unsigned integer' functions for various EM handles
     added.  
     Event, EO, queue group, event group, atomic group, pool, timer and tmo
     handles can be converted to uint64_t:s.  
     EM API:  
     - `uint64_t em_atomic_group_to_u64(em_atomic_group_t atomic_group);`
     - `uint64_t em_eo_to_u64(em_eo_t eo);`
     - `uint64_t em_event_group_to_u64(em_event_group_t event_group);`
     - `uint64_t em_event_to_u64(em_event_t event);`
     - `uint64_t em_pool_to_u64(em_pool_t pool);`
     - `uint64_t em_queue_group_to_u64(em_queue_group_t queue_group);`
     - `uint64_t em_timer_to_u64(em_timer_t timer);`
     - `uint64_t em_tmo_to_u64(em_tmo_t tmo);`

     The queue handle can be converted to an `uint32_t`. The queue conversion
     is different since it reflects the fact that the queue handle consists
     of a 16-bit device-id and a 16-bit queue-id.  
     EM API:  
      - `uint32_t em_queue_to_u32(em_queue_t queue);`

4. EM device-ID  
   (see include/event_machine/platform/event_machine_init.h for documentation)

   - Add a function that returns the EM device-id for the current instance.
     This is a convenience function that returns the EM device-id given by the
     user to `em_init()` via the `em_conf_t::device_id` field.
     The function should only be called after a successful EM initialization.  
     EM API:  
     - `uint16_t em_device_id(void);`

### EM v3.5.0 - Implementation changes

- EM config file options - runtime config file `config/em-odp.conf`:
  New option to limit the maximum number of EM queues that can be created

  ```text
  queue: {
        # The maximum number of EM queues that can be created.
        #
        # EM will allocate memory at startup to support 'max_num' queues.
        # The number must be large enough to cover the EM internal queues as
        # well as all the static and dynamic queues created by the application.
        # The smallest acceptable 'max_num' value depends on the values of
        # EM_MAX_CORES, EM_QUEUE_STATIC_MIN and EM_QUEUE_STATIC_MAX.
        # The smallest acceptable value allows creating one dynamic EM queue.
        # The largest 'max_num' depends on ODP capability.
        # A larger value will consume more memory.
        max_num = 1024
        ...
  }
  ```

  The `queue.max_num` option replaces the removed EM API define `EM_MAX_QUEUES`

  Config file version number bumped up to "0.0.20"

- Add support for using ODP API 1.43  
  Update EM to be able to run against ODP API v1.43.  
  Maintain compatibility with v1.42.  

- Fixes:
  - Event: fix `em_free_multi()` when freeing many timer events  
    In `remove_active_timers()`: if the loop removes the j:th event
    from the array, and the `j+1`:th event also happens to be an `ODP_EVENT_TIMEOUT`,
    then the function missed removing that following timeout.  
    This is now fixed and the function is refactored.  
    Both `em_free()` and `em_free_multi()` are improved to check for these tmos only
    when periodic ring timers have actually been created - if no ring timers ever
    created then no checks need to be done.

  - Dispatch: error logging should not access `NULL` `q_elem`  
    The error logging in local-queue dispatch should not access the
    `q_elem` if it is `NULL`.

  - configure: prevent `-Wstringop-overflow` warnings by gcc when LTO is enabled  
    GCC can generate false errors about object sizes, especially when LTO is used.
    As a workaround, make `-Wstringop-overflow` not generate errors as part of
    `-Werror` when GCC version 10 or later is used.  
    Add `-Wno-error=stringop-overflow` both to `CFLAGS` and `CXXFLAGS`.

- Programs:
  - Programs, common: add cmd line options to dispatch with options

    Add new test program command line options that enables the use
    of `em_dispatch_duration()` instead of `em_dispatch()`, see b) below.

    EM dispatch options (optional):

    ```text
    a) using em_dispatch() - default: uses em_dispatch() unless options
                                      in b) are given.
      -r, --dispatch-rounds <arg> Number of dispatch rounds (default: 0=forever)
    b) using em_dispatch_duration() - combinations of these options possible.
      --duration-rounds <arg> Dispatch for the given number of rounds.
      --duration-ns <arg> Dispatch for the given time in nanoseconds.
      --duration-events <arg> Dispatch until the given number of events
                                        have been handled.
      --duration-noevents-rounds <arg> Dispatch until no events have been
                                       received for the given number of rounds.
      --duration-noevents-ns <arg> Dispatch until no events have been received
                                   for the given time in nanoseconds.
      --burst-size <arg> The max number of events the dispatcher will request
                         in one burst from the scheduler.
                         (default: EM_SCHED_MULTI_MAX_BURST=%d)
      --wait-ns <arg> Scheduler wait-for-events timeout in nanoseconds.
                      The scheduler will wait for events, if no immediately
                      available, for 'wait_ns' per dispatch round.
                      (default: 0, do not wait)
      --sched-pause Pause the scheduler on the calling core when exiting the
                    EM dispatch function. If enabled, will also resume the
                    scheduling when entering dispatch (default: don't pause).
    ```

## Event Machine (EM) on ODP v3.4.0

### EM API v3.4 - Summary

- Dispatch APIs: `em_dispatch_duration()`/`_ns()`/`_rounds()`/`_events()` and
                 `em_dispatch_opt_init()`  
  Dispatch APIs with duration and option arguments for more control of the
  dispatch loop.
- Pool statistics: new option `em_pool_stats_opt_t::core_cache_available`

### EM API v3.4 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=4`

1. Dispatch  
   (see `include/event_machine/api/event_machine_dispatcher.h` for documentation)

   - Dispatch APIs with duration and option arguments for more control of the
     dispatch loop.

     EM API:

     ```c
     void em_dispatch_opt_init(em_dispatch_opt_t *opt);

     em_status_t em_dispatch_duration(const em_dispatch_duration_t *duration,
                                      const em_dispatch_opt_t *opt,`  
                                      em_dispatch_results_t *results /*out*/);
     ```

     and the simplified versions of em_dispatch_duration():

     ```c
     em_status_t em_dispatch_ns(uint64_t ns,
                                const em_dispatch_opt_t *opt,
                                em_dispatch_results_t *results /*out*/);
     em_status_t em_dispatch_events(uint64_t events,
                                    const em_dispatch_opt_t *opt,
                                    em_dispatch_results_t *results /*out*/);
     em_status_t em_dispatch_rounds(uint64_t rounds,
                                    const em_dispatch_opt_t *opt,
                                    em_dispatch_results_t *results /*out*/);
     ```

     These APIs gives the user more control over the dispatching of events.  
     The user can select to dispatch for a certain number of rounds,
     a certain amount of time or until a certain amount of events have been
     dispatched.  
     Options to exit the dispatching when no events are available are also added.  
     Further, options to control the event burst-size, the scheduler
     wait-for-events and the pausing of scheduling on return are added.

2. Pool  
   (see include/event_machine/platform/event_machine_pool.h for documentation)

   - Add a new (special) field to the pool-statistics selection options
     in the EM API:
     `em_pool_stats_opt_t::core_cache_available`

     This new, and a bit special, EM event pool statistics option enables
     applications to read the number of events/bufs available in the
     core/thread-local pool-cache of the underlying ODP pool(s), but do so with
     ODP APIs only (e.g. odp_pool_stats()).  
     The EM pool statistics APIs do not support fetching `.core_cache_available`
     directly because EM has no reliable way of mapping ODP threads to EM cores in
     all possible ODP-thread setups. Thus EM allows for this statistics to be setup
     for an EM pool but the reading must be done by the application using ODP APIs
     for the ODP pools that correspond to a certain EM pool. The application must
     then itself figure out which ODP thread maps to which EM core and which are
     "extra" ODP control or other theads.

     Example usage:  
     A) Create an EM pool with `core_cache_available`-statistics enabled:

        ```c
        em_pool_cfg_t pool_cfg;

        em_pool_cfg_init(&pool_cfg);
        ...
        pool_cfg.stats_opt.in_use = true;
        pool_cfg.stats_opt.opt.core_cache_available = 1;
        ...
        em_pool_t pool_em = em_pool_create("my-pool", EM_POOL_UNDEF, &pool_cfg);
        ```

     B) To read the cached-per-thread statistics we need ODP APIs:

        ```c
        odp_pool_t odp_pools[EM_MAX_SUBPOOLS];

        /* Get the nbr of subpools (= ODP pools) in the EM pool */
        int num = em_pool_get_num_subpools(pool_em);

        if (num > 0) {
                /*
                 * Get the ODP pools used as subpools in the EM event pool.
                 * Use an EM <-> ODP conversion functions to get the ODP pools.
                 */
                int ret = em_odp_pool2odp(pool_em, odp_pools/*out*/, num);

                if (ret > 0) {
                        for (int i = 0; i < ret; i++) {
                                odp_pool_stats_t stats;

                                stats.thread.first = x;
                                stats.thread.last = y;
                                err = odp_pool_stats(odp_pools[i], &stats /*in/out*/);
                                /*
                                 * stats.thread.cache_available[...] now contains
                                 * cached-per-thread stats for odp_pools[i]
                                 */
                                ...
                        }
                }
     }
     ```

### EM v3.4.0 - Implementation changes

- Dispatch configuration options  
  `em-odp.conf` (runtime config file):  
  Add a new EM runtime config file option:
    `dispatch.sched_pause = true / false`  
  that controls whether the em_dispatch() API-function pauses the EM core
  from scheduling when it returns.

  Note: the option only affects the `em_dispatch()` API,
        not `em_dispatch_duration()`/`_ns()`/`_rounds()`/`_events()`.

  ```text
  dispatch: {
        ...
        # Pause scheduling for an EM-core returning from em_dispatch().
        # The core is paused from participation in global scheduling and EM
        # empties all locally pre-scheduled events, as well as dispatches them,
        # before returning from the em_dispatch() function.
        # The scheduler should be paused on a core that is leaving the
        # dispatch / scheduling loop for a longer time to avoid locking atomic
        # or ordered contexts (thus blocking other cores from handling events
        # from those same contexts / queues).
        # Only affects the em_dispatch() function.
        sched_pause = false
  }
  ```

  Bump the `em-odp.conf` file version to 0.0.19

- Info: detect additional ARM v8 and v9 ISAs.  
  Update the SW ISA detection for EM with further ARM v8 and v9 ISAs:  
  ARMv8.8-A, ARMv8.9-A, and ARMv9.3-A.
  Based on ODP's sysinfo.  
  Detection of these ISAs require ODP API v1.42.1 or later.

- Fixes:
  - Timer: fix last ring timer delete bug  
    Events for ring timeout indication is provided by timer from a common pool.
    This pool was deleted when last ring timer was deleted to save memory,
    but application may still have event(s) from that pool, create new ring timer
    and then re-use those events. That lead to corrupted event. Fixed by not
    deleting the shared pool until application termination.

  - Scheduler: `em_ordered_processing_end()` corrected.  
    Allow ending the ordered context only if the currect scheduling context type
    is 'ordered', not if the current queue type is 'ordered'.

  - Dispatch, ESV: set core's (internal) current queue info before event init.  
    Set the core's current queue before event-init in dispatch, since the init
    might notice early ESV errors and the error printout is then able to print
    the queue and EO information correctly.

## Event Machine (EM) on ODP v3.3.0

### EM API v3.3 - Summary

- Timer ring event type changed to `EM_EVENT_TYPE_TIMER_IND`
- Timer APIs: `em_tmo_get_timer()` - get timer from timeout
- Event APIs: `em_event_clone_part()` - partially clone an event
- Pool APIs: Selected Pool Statistics - `em_pool_stats_selected()`,
                                        `em_pool_subpool_stats_selected()` etc.

### EM API v3.3 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=3`

1. Timer  
   (see `include/event_machine/api/event_machine_timer.h` for documentation)

   - Timer Ring
     Add a new event type `EM_EVENT_TYPE_TIMER_IND` that will be used by the
     timer ring (instead of `EM_EVENT_TYPE_TIMER`).  
     Keep the `EM_EVENT_TYPE_TIMER` available for application use, as it might
     have been used by the applications prior to the API v3.2 update.

   - Update the specification to state that only the major event types
     `EM_EVENT_TYPE_SW`, `EM_EVENT_TYPE_PACKET` and `EM_EVENT_TYPE_TIMER` can be
     used as a timeout indication. Additionally, the type `EM_EVENT_TYPE_TIMER_IND`
     is used by the timer ring.

   - Get timer from timeout:  
     EM API:  
     - `em_timer_t em_tmo_get_timer(em_tmo_t tmo);`  
        Get the related timer handle from a timeout handle.

2. Event  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   - Partial Event Clone:  
     Add a new EM API to be able to partially clone an event (and its user area).  
     EM API:

     ```c
     em_event_t em_event_clone_part(em_event_t event, em_pool_t pool/*or EM_POOL_UNDEF*/,
                                    uint32_t offset, uint32_t len, bool clone_uarea);
     ```

3. Pool  
   (see include/event_machine/platform/event_machine_pool.h for documentation)

   - Selected Pool Statistics
     Read/print only a selected subset of the pool statistic counters  
     EM APIs:

     ```c
     em_status_t em_pool_stats_selected(em_pool_t pool,
                                        em_pool_stats_selected_t *pool_stats/*out*/,
                                        const em_pool_stats_opt_t *opt);
     void em_pool_stats_selected_print(em_pool_t pool, const em_pool_stats_opt_t *opt);
     int em_pool_subpool_stats_selected(em_pool_t pool, const int subpools[], int num_subpools,
                                        em_pool_subpool_stats_selected_t subpool_stats[]/*out*/,
                                        const em_pool_stats_opt_t *opt);
     void em_pool_subpool_stats_selected_print(em_pool_t pool, const int subpools[],
                                               int num_subpools, const em_pool_stats_opt_t *opt);
     ```

   - Get the number of subpools in an EM event pool  
     EM API:  
     `int em_pool_get_num_subpools(em_pool_t pool);`

### EM v3.3.0 - Implementation changes

- Event State Verification (ESV) changes  
  - Add basic ESV state checks for timer-ring events. Since the same event might
    be processed on multiple cores, only a limited set of ESV checks can be done.  
    For now only verify timer-ring event alloc and free.

- Dispatch configuration options  
  - Add the ability to configure the wait-for-events timeout passed
    to the ODP scheduler during EM dispatch.

  - Config file (`em-odp.conf`) version number bumped up to "0.0.18"

  - New configure option - `configure.ac`:  
    Enable the EM dispatcher/scheduler to start waiting for events if none are
    immediately available. Waiting for events, rather than busy-waiting, might
    save power.

    ```text
    --enable-sched-wait   EM calls odp_schedule_multi(..., sched_wait_ns, ...),
                          where 'sched_wait_ns' is taken from
                          'dispatch.sched_wait_ns = value' in the
                          EM config file (config/em-odp.conf).
                          Set 'EM_SCHED_WAIT_ENABLE=1'
    --disable--sched-wait EM calls odp_schedule_multi_no_wait().
                          Set 'EM_SCHED_WAIT_ENABLE=0'
     no option given Use '#define EM_SCHED_WAIT_ENABLE 0|1' from source code
     ```

  - Runtime config file (`em-odp.conf`) option: `dispatch.sched_wait_ns`:  
    Runtime schedule-wait option in nanoseconds.  
    Allows the user to configure the time (ns) the scheduler should wait
    for events before returning (when no events are immediately available).  
    Only usable if the define `EM_SCHED_WAIT_ENABLE` has been set to '1'
    either via  
    1) `configure --enable-sched-wait` or  
    2) `#define EM_SCHED_WAIT_ENABLE` `0` ==> `1`  
       (include/event_machine/platform/event_machine_config.h)  
    Note that using option 1), i.e. via `configure`, overrides the value
    in `event_machine_config.h`.  
    See `configure.ac`, `config/em-odp.conf` and `event_machine_config.h`  

    ```text
    em-odp.conf:

    dispatch {
            ...
            sched_wait_ns = 0:  no waiting for events, scheduler returns
                                immediately even if no events are available
                                (converted to ODP_SCHED_NO_WAIT)
            sched_wait_ns = -1: waits indefinitly for events
                                (converted to ODP_SCHED_WAIT)
            sched_wait_ns > 0:  waits `sched_wait_ns` for events
                                (converted to odp_schedule_wait_time(sched_wait_ns))
            ...
    }
    ```

    Note that the `sched_wait_ns` may be rounded up as specified by the
    ODP spec, configuration and implementation.

## Event Machine (EM) on ODP v3.2.0

### EM API v3.2 - Summary

- Timer ring
- Timeout type
- Error codes

### EM API v3.2 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=2`

1. Timer: timer version updated to v2.3  
   (see `include/event_machine/api/event_machine_timer.h` for documentation)

   - Timer Ring  
     EM APIs:

     ```c
     em_status_t em_timer_ring_attr_init(em_timer_attr_t *ring_attr,
                                         em_timer_clksrc_t clk_src, uint64_t base_hz,
                                         uint64_t max_mul, uint64_t res_ns);
     em_status_t em_timer_ring_capability(em_timer_ring_param_t *ring);
     em_timer_t em_timer_ring_create(const em_timer_attr_t *ring_attr);
     em_status_t em_tmo_set_periodic_ring(em_tmo_t tmo, em_timer_tick_t start_abs,
                                          uint64_t multiplier, em_event_t tmo_ev);
     ```

     An alternative periodic ring timer. As it uses a different abstraction, it is
     created and started via separate ring specific APIs. It has three main
     differences to the regular periodic timeouts:  
     1. Only a pre-defined read-only event type can be used and is provided
        by the timer (`EM_EVENT_TYPE_TIMER_IND`).
     2. Flow control is not supported. Some implementations may have it,
        but the specification does not guarantee any so the user needs to be
        prepared to see the same event enqueued multiple times if handling of
        the received timeouts is not fast enough
     3. Only a limited set of periods are supported per timer (base rate or an
        integer multiple thereof).
     Ring timers can be abstracted as a clock face ticking the pointer forward.
     One cycle around is the base rate (minimum rate). The same timeout can be
     inserted to multiple locations evenly spread within the clock face thus
     multiplying the base rate. Starting offset can be adjusted only up to
     one timeout period.  
     Depending on platform, this mode may provide better integration with HW and
     thus less runtime overhead. However, as it exposes potential queue overflows
     and race hazards (race avoidable by using an atomic queue as target) the
     regular periodic timer is recommended as default.

     Example usage:

     ```c
          em_status_t stat;
          em_timer_ring_attr_t attr;

          stat = em_timer_ring_attr_init(&attr, EM_TIMER_CLKSRC_DEFAULT,
                                         1000 /*Hz*/, 8, 0);
          if (stat != EM_OK) {
                  /* given arguments not supported */
          }

          /* optional: set frational frequency, e.g 1000 + 1/3 = 1000.333... Hz */
          attr.ringparam.base_hz.numer = 1;
          attr.ringparam.base_hz.denom = 3;
          if (em_timer_ring_capability(&attr.ringparam) == EM_ERR_NOT_SUPPORTED) {
                  /* given (modified) ring params not supported by the timer */
          }

          em_timer_t tmr = em_timer_ring_create(&attr);
          if (tmr == EM_TIMER_UNDEF) {
                  /* can't create timer, handle error here or via error handler */
          }

          em_queue_t dst_queue = MY_QUEUE; /* prefer atomic queue to avoid races */
          em_tmo_t tmo = em_tmo_create(tmr, EM_TMO_FLAG_PERIODIC, dst_queue);
          if (tmo == EM_TMO_UNDEF) {
                  /* can't create timeout */
          }

          uint64_t multiplier = 1;
          stat = em_tmo_set_periodic_ring(tmo, 0, multiplier,
                                          EM_EVENT_UNDEF /*event reuse only*/);
          if (stat != EM_OK) {
                  /* Could not start timeouts */
          }
     ```

     After the setup above, the timer ring will start sending periodic
     timeouts into the given queue.  
     EM will dispatch these events normally and call the EO's receive function
     with the timeout event:

     ```c
          /* EO receive function */
          void my_eo_receive(..., event, event_type = EM_EVENT_TYPE_TIMER_IND,
                             queue = MY_QUEUE, ...)
          {
                  em_tmo_t tmo = EM_TMO_UNDEF;
                  em_tmo_type_t tmo_type = em_tmo_get_type(event, &tmo /*out*/,
                                                           false);
                  if (tmo_type == EM_TMO_TYPE_PERIODIC) {
                          if (event_type == EM_EVENT_TYPE_TIMER_IND) {
                                  /* from timer ring, event read only*/
                          } else {
                                  /* from "normal" periodic timer,
                                     event can be modified or swapped in ack */
                          }

                          em_status_t stat = em_tmo_ack(tmo, event);

                          if (stat == EM_OK) {
                                  /* ack successful, event consumed by timer */
                                  do_work();
                                  return;
                          }
                          if (stat == EM_ERR_CANCELED) {
                                  /* Last periodic event */
                                  /* timeout was canceled, event can be freed */
                          } else {
                                  /* other error */
                          }
                  }
                  ...
          }
     ```

   - Timout user argument / ptr (can be used with the ring timer)  
     EM APIs:

     ```c
     em_tmo_t em_tmo_create_arg(em_timer_t tmr, em_tmo_flag_t flags,
                                em_queue_t queue, em_tmo_args_t *args);
     void *em_tmo_get_userptr(em_event_t event, em_tmo_t *tmo);
     ```

     Attach a user pointer to timer ring timeouts.

   - Timeout type  
     EM API:

     ```c
     em_tmo_type_t em_tmo_get_type(em_event_t event, em_tmo_t *tmo, bool reset);
     ```

     where the return value of type `em_tmo_type_t` is a new type that will
     indicate whether the event is a timeout (`EM_TMO_TYPE_ONESHOT` or
     `EM_TMO_TYPE_PERIODIC`) or another type of event (`EM_TMO_TYPE_NONE`).

     ```c
     typedef enum em_tmo_type_t {
          EM_TMO_TYPE_NONE     = 0, /**< unknown or not timer-related event   */
          EM_TMO_TYPE_ONESHOT  = 1, /**< event is oneshot timeout indication  */
          EM_TMO_TYPE_PERIODIC = 2, /**< event is periodic timeout indication */
     } em_tmo_type_t;
     ```

   - Specify that only the major event types `EM_EVENT_TYPE_SW`,
     `EM_EVENT_TYPE_PACKET` and `EM_EVENT_TYPE_TIMER` can be used as
     timeout indication.

   - Convert a valid EM timer to an ODP timer_pool.  
     EM extension API:  

     ```c
     odp_timer_pool_t em_odp_timer2odp(em_timer_t tmr);
     ```

     This is for debugging and using potential future extra odp apis.

2. Error
   - Error codes  
     Reorganize the EM error codes better and add the new error codes
     `EM_ERR_NOT_CREATED`, `EM_ERR_NOT_SUPPORTED` and `EM_ERR_BAD_TYPE`.

   - Error handler arguments, documentation fix  
     Remove mention that all API function arguments would be passed
     to the error handler - they are not.

3. Separate config define for the event burst size from EM local queues
   (`EM_QUEUE_TYPE_LOCAL`)

   ```c
   #define EM_QUEUE_LOCAL_MULTI_MAX_BURST  32
   ```

   The maximum number of events from a local queue (`EM_QUEUE_TYPE_LOCAL`) to
   dispatch in one burst. Allows different burst sizes from scheduled queues vs.
   local queues.

4. Misc.
   - Extension API: Rename `pkt_enqueue()` to `em_odp_pkt_enqueue()`.
   - Remove the deprecated EM API template files.
   - Event: removed `const` from the `em_free_multi()` arg `events[]`  
     Changed `void em_free_multi(const em_event_t events[], int num);`
     to `void em_free_multi(em_event_t events[], int num);`
     since EM might need to filter out events that can not / should not
     be freed, e.g. active timer ring events.

### EM v3.2.0 - Implementation changes

- EM config file options - `config/em-odp.conf`:
  - Config file version number bumped up to "0.0.17",
  - New `timer:{}` options in the config file.

- Event State Verification (ESV) changes
  - Combine ESV operations for events/packets from e.g. pktio for potential
    performance gains.  
    Combine 'mark allocated', 'mark sent' and 'mark em2usr for dispatch' into a
    single ESV operation (with a single atomic counter update).

- Performance optimizations
  - Reduce the sizes of commonly used (in fast path) data structures for
    potential caching gains (queue, pool, atomic group).
  - Adjust fast-path API error checking according to the used `EM_CHECK_LEVEL`.
  - Dispatch: check output queue buffering only with ordered schedule context.

- Misc.
  - daemon-eo: remove the unnecessary daemon-eo
  - event: replace `odp_packet_user_ptr/set()` with `odp_packet_user_flag/set()`
    to detect whether the event header has been initialized or not.

- Performance test applications added
  - `em-odp/programs/performance/bench_event.c`
    - Measure event related API-function performance (cycles), single-core
  - `em-odp/programs/performance/pool_perf.c`
    - Performance test tool for memory pool performance.
      Measures time consumed during calls to allocate and free buffers or
      packets. Command line arguments can be used to try different aspects.
  - `em-odp/programs/performance/timer_test_ring.c`
    - Timer ring test for alternative periodic timeouts.
      The purpose of this tool is to manually test periodic ring timer
      functionality. At least two EM timers are created. One standard timer for
      a heartbeat driving test states. The second timer (or many) is periodic
      ring for testing.

## Event Machine (EM) on ODP v3.1.0

### EM API v3.1 - Summary

- Pool Statistics
- Timer updates
- Event Vectors

### EM API v3.1 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=1`

1. Pool Statistics  
   (see include/event_machine/platform/event_machine_pool.h for documentation)

   EM APIs:

   ```c
   em_status_t em_pool_stats(em_pool_t pool, em_pool_stats_t *pool_stats/*out*/);
   em_status_t em_pool_stats_reset(em_pool_t pool);
   void em_pool_stats_print(em_pool_t pool);

   int em_pool_subpool_stats(em_pool_t pool, const int subpools[], int num_subpools,
                             em_pool_subpool_stats_t subpool_stats[]/*out*/);
   em_status_t em_pool_subpool_stats_reset(em_pool_t pool, const int subpools[],
                                           int num_subpools);
   void em_pool_subpool_stats_print(em_pool_t pool, const int subpools[],
                                    int num_subpools);
   ```

   New EM event pool statistics APIs and configuration allow the applications to
   choose which pool statistic counters to collect and retrieve.  
   Configuration of pool statistics is done globally via the EM config file
   (`config/em-odp.conf`: `pool.statistics`) or per pool through the updated
   `em_pool_cfg_t::stats_opt` given to `em_pool_create()`.  
   The new APIs allow the user to retrieve pool statistics on an EM event pool
   level or, more fine grained, on a subpool level.  
   The statistics 'reset'-APIs clear some of the counters, see the details in
   the API function documentation.  
   Additionally, 'print'-APIs have been added for easy printout of statistics.

   Changes to `em_pool_create()` are needed to configure pool-statistics on a
   per-pool basis (thus overriding the EM config file settings for the pool):
     `em_pool_t em_pool_create(..., const em_pool_cfg_t *pool_cfg);`
   The new `em_pool_cfg_t::stats_opt{...}` fields controls which statistics are
   gathered.  
   The existing em_pool_cfg_init() API can hide the `pool_cfg` changes if the
   user does not need updated pool statistics (note: changes to the config file
   are still needed).

   The 'pool info' APIs are unmodified and produce the same output as before,
   event though the statistics configuration in the config file or in pool
   creation has changed. See the APIs `em_pool_info()`, `em_pool_info_print()`,
   `em_pool_info_print_all()`). To fully mimic the previous behaviour of the
   'pool info' APIs with statistics in use, enable the pool statistics options
   `.available = true` and `.cache_available = true` via
   `em_pool_cfg_t::stats_opt` or the EM config file.

2. Timer  
   (see `include/event_machine/api/event_machine_timer.h` for documentation)

   - `em_tmo_set_rel`/`abs`/`periodic()`:
     - Clarification of return value on error:  
       When the timeout tick value given to `em_tmo_set_...()` is in the past or
       is too close to current time, the error code `EM_ERR_TOONEAR` is returned.
       In this case EM will not call the error handler, instead it's up to the
       application to decide whether to retry with an updated time or treat the
       condition as an error. The given event is not consumed on error, the
       ownership remains with the caller.
     - Starting a periodic tmo could previously be done with `em_tmo_set_rel()`,
       this has been obsoleted and cannot be used any more.  
       The API `em_tmo_set_periodic()` must now be used instead.

   - `em_tmo_cancel()`:
      - The same return value change applies also to `em_tmo_cancel()`:
        if attempted too late, em_tmo_cancel() also now returns `EM_ERR_TOONEAR`
        and does not call the error handler.  
        Additionally, a more strict state check has been applied and the API
        will fail if e.g. attempted twice.

3. Event Vectors  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   Update documentation for em_event_vector_size_set():  
   Explicitly mention that a vector's event-table must be updated properly with
   events before setting the vector size. EM always assumes that there are as
   many valid events in the event-table as indicated by `size`:

   ```text
   The events must be stored into the vector before setting the size,
   i.e. first add/remove events to/from the vector's event-table (within
   max-size limits) and only after that set the size.
   ```

### EM v3.1.0 - Implementation changes

- EM config file options - `config/em-odp.conf`:
  - Config file version number bumped up to "0.0.16",
  - New `pool.statistics:{}` options in the config file.

- Event State Verification (ESV) changes
  - Fix: evgen wrap handling in `esv_usr2em`/`_multi()` during a 'revert'
  - Use larger `EVGEN_INIT` value to protect from over/underflows

    ```text
      Evgen values:  0x1000 - 0xEFFF
      Event handles: 0x1000 xxxx xxxx xxxx - 0xefff xxxx xxxx xxxx,
                     where x…x = ptr value)
    ```

  - Fix: Reset ESV-counters on the next alloc after using a reference.  
    Event references do not use the ESV evgen counter but it is still updated by
    ESV operations. Thus it needs to be reset for the next alloc of the same
    event or it will contain invalid values.

- Event changes  
  Highlighting EM internal changes related to the event header since it may
  affect users extending or accessing EM internals.

  - Placement of the internal event header has been changed for odp_buffers.
    Now the event header always resides in the ODP buf/pkt/vector user area
    followed by the EM event user area (if any).

    ```c
     Prev:  event_hdr_t *ev_hdr = odp_buffer_addr(odp_buf);
     Curr:  event_hdr_t *ev_hdr = odp_buffer_user_area(odp_buf);
    ```

     If changes have been made to EM internals in your project then this needs
     to be updated!

  - The internal event header size is reduced to fit within one cache line for
    potential caching and performance gains.

## Event Machine (EM) on ODP v3.0.0

### EM API v3.0 - Details

`EM_VERSION_API_MAJOR=3`, `EM_VERSION_API_MINOR=0`

1. Event References  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   EM APIs:

   ```c
   em_event_t em_event_ref(em_event_t event);
   bool em_event_has_ref(em_event_t event);
   ```

   Normally, each event is associated with one event handle (`em_event_t`) - each
   event allocation produces a new event (and associated payload data) that can
   be processed, sent or freed. When the user EO has allocated or received an
   event from a queue, the event payload data may be read and written as needed
   by the application.
   An exception to the above described scenario happens when using event
   references.
   An event reference is an additional event handle referring to an existing
   event. New references are created with the `em_event_ref()` API call. The
   intent of using multiple references is to avoid event copies.
   An event that has multiple references shares its data with the other
   reference handles and thus the (shared) data must not be modified.
   Reading event data from a reference is allowed. Writes to the event data must
   only be done when there is a single event handle left, i.e. when
   `em_event_has_ref(event)` returns `false`. Results are undefined if these
   restrictions are not observed.
   The event is freed when the last reference, including the original event,
   is freed.
   It is not allowed to use event references with event groups since assigning
   an event that has references to an event group would assign all the
   references to the event group resulting in undefined behaviour. E.g. using
   `em_send_group()`/`em_send_group_multi()` to send a reference is wrong.

2. Event Vectors  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   EM APIs:

   ```c
   void em_event_vector_free(em_event_t vector_event);
   uint32_t em_event_vector_tbl(em_event_t vector_event,
                                em_event_t **event_tbl/*out*/);
   uint32_t em_event_vector_size(em_event_t vector_event);
   void em_event_vector_size_set(em_event_t vector_event, uint32_t size);
   uint32_t em_event_vector_max_size(em_event_t vector_event);
   em_status_t em_event_vector_info(em_event_t vector_event,
                                    em_event_vector_info_t *vector_info/*out*/);
   ```

   Note: Event Vectors always have the major part of their event type set to
         `EM_EVENT_TYPE_VECTOR`.

   Vector events contain a table of events.  
   All events in the event-table must be of major type `EM_EVENT_TYPE_PACKET`.
   Storing events of another type into the event-table is an error and leads to
   undefined behaviour.  
   Event vector pools are created with em_pool_create() with the pool event-type
   set to `EM_EVENT_TYPE_VECTOR`. Event vectors can then be allocated from vector
   pools by calling `em_alloc(..., vector_pool)`.  
   To free the vector event along with all events it contains, use `em_free()` or
   `em_free_multi()`.  
   To free the vector event only, not the events it contains,
   use `em_event_vector_free()`.

   - Extension APIs for 'em-odp' (needed to set up pktio to use vectors)  
     (see include/event_machine/platform/event_machine_odp_ext.h)

     - Get the ODP schedule group that corresponds to the given EM queue group:

       ```c
       odp_schedule_group_t em_odp_qgrp2odp(em_queue_group_t queue_group);
       ```

     - Map the given scheduled ODP pktin event queues to new EM queues:

       ```c
       int em_odp_pktin_event_queues2em(const odp_queue_t odp_pktin_evqueues[/*num*/],
                                        em_queue_t queues[/*out:num*/], int num);
       ```

3. Vector Pools  
   (see include/event_machine/platform/event_machine_pool.h for documentation)

   EM pools for Event Vectors can now be created via the em_pool_create() API.  
   The pool config struct is extended to allow `.event_type = EM_EVENT_TYPE_VECTOR`.

   Changes to pool creation via configuration struct `em_pool_cfg_t`:
   - em_pool_cfg_t::event_type:  
     The event type determines the pool type to create, now it supports vectors.  
     Using `EM_EVENT_TYPE_VECTOR` here creates subpools of type `ODP_POOL_VECTOR`.  
     This kind of EM pool can ONLY be used for creating event vectors.
   - `em_pool_cfg_t::subpool[].size`:  
     (when `.event_type = EM_EVENT_TYPE_VECTOR`)  
     Max number of events in a vector from the subpool, i.e.
     'number of `em_event_t`:s in the vector's `event-table[]`'.  
     EM does not initialize the vector.  
     Note! The meaning of `.size` is slightly different for "normal" event pools
           vs. vector pools.

4. Vector allocation and freeing  
   (see `include/event_machine/api/event_machine_event.h` for documentation)

   Event vectors can be allocated from vector pools using the existing
   APIs `em_alloc()` or `em_alloc_multi()` by providing
   `(major) type=EM_EVENT_TYPE_VECTOR` and `pool=vector-pool`:

   ```c
   em_event_t em_alloc(uint32_t size, em_event_type_t type, em_pool_t pool);
   int em_alloc_multi(em_event_t events[/*out*/], int num,
                      uint32_t size, em_event_type_t type, em_pool_t pool);
   ```

   Minor change to the alloc APIs for the type and meaning of the `size` arg:

   ```text
         1) Packet & sw-buf: event size in bytes (B), size > 0.  
   *new* 2) Vector: number of event handles that should fit into
                    the vector table of the event, size > 0.
   ```

   The change was needed when introducing EM vector-events into to API,
   here the event alloc-size indicates the number of events that should
   fit into the vector and not a byte-size, so `uint32_t` makes more sense
   and is also more in line with the types used during pool-creation.

   Freeing a vector can be done with `em_free`/`_multi()`, which frees the vector
   and all events it contains. Alternatively, use `em_event_vector_free()`, which
   only frees the vector and not the events in the contained event table.

5. Helper: EM debug timestamps  
   (see include/event_machine/helper/event_machine_debug.h for documentation)

   Helper APIs:

   ```c
   uint64_t em_debug_timestamp(em_debug_tsp_t tsp);
   ```

   Use `em_debug_timestamp()` to retrieve a 'per EM thread' dispatcher timestamp
   from EM internal timestamp-points (tsp) that can be compared to the current
   time to determine e.g. scheduling latency or other overheads.  
   Not for normal application use!  
   Disabled by default (returns `0`) and can be enabled via the configure option
   `--enable-debug-timestamps` or by changing the `EM_DEBUG_TIMESTAMP_ENABLE`
   define.  
   The type `em_debug_ts_t` defines the available internal EM timestamp points,
   see the type definition.

6. Other  
   - Event: rename functions for major and minor event types
   - Rename `em_get_type_major()` to `em_event_type_major()` and
            `em_get_type_minor()` to `em_event_type_major()`.  
   Create defines for the old names for backwards compatibility.

   - Event: `em_event_get_size()` - returns `uint32_t` instead of `size_t` to
                                    reflect changes in `em_alloc()` etc.
   - Hooks: `em_api_hook_alloc_t` - arg `size` changed to `uint32_t` instead of
                                    `size_t` to reflect changes in `em_alloc()`
                                    etc.

### EM v3.0.0 - Implementation changes

- EM debug timestamps
  - Enabled with new configure-script option '--enable-debug-timestamps':  
    (see `configure.ac`)

    ```text
    Override EM-define value 'EM_DEBUG_TIMESTAMP_ENABLE' to 0...2
    --enable-debug-timestamps=0...2   Set 'EM_DEBUG_TIMESTAMP_ENABLE' to the
                                      given value: 1=low overhead, 2=strict time
    --enable-debug-timestamps<=yes(=1) Set 'EM_DEBUG_TIMESTAMP_ENABLE' to 1
    --enable-debug-timestamps          Set 'EM_DEBUG_TIMESTAMP_ENABLE' to 1
    --disable-debug-timestamps or
      no option given                  Use 'EM_DEBUG_TIMESTAMP_ENABLE' value
                                       from source code (0...2, default 0)
    ```

- EM config file - `config/em-odp.conf`:
  - Config file version number bumped up to "0.0.15", see new options below.

  - EM config file option: `event_chaining.order_keep = false/true`  
    Option description:  
      Events sent with `em_send...()` to an event chaining queue will be
       passed to the user provided `event_send_device`/`_multi()` functions.  
       EM can ensure that calls to these functions are:  
       1) made in order (order_keep = true)  
          or  
       2) make no extra effort to keep the order (order_keep = false),  
          e.g. if the user (or packet output) handles ordering or does not
          care about it.

     Note: Only meaningful when used together with (parallel-)ordered queues.
           Order is implicitly guaranteed when sent during an atomic context and
           never guaranteed when sent during a parallel context.

     Only set to `true` if queues of type `EM_QUEUE_TYPE_PARALLEL_ORDERED`
     are used with event chaining, no benefit otherwise. Using `false` lets EM
     save on some resources.

     Note! Currently only `event_chaining.order_keep = false` is supported.  
           Trying `true` gives the following printout:  
           "`Config option event_chaining.order_keep: true(1) currently not supported`"

- Env additions
  - Return difference of cpu cycles (cycles2 - cycles1):

    ```c
    uint64_t env_cycles_diff(uint64_t cycles2, uint64_t cycles1)
    ```

- Event State Verification (ESV) changes  
  The introduction of Event References has caused ESV to change a bit.
  The ESV counters are always modified together atomically as one `uint64_t`,
  as before. One of the ESV-counters, `free_cnt`, has been replaced by `ref_cnt`
  that stands for 'reference count', i.e. how many references exist for a
  certain event. A newly allocated event with no further references has
  `ref_cnt=1`. When one additional reference is created, the reference count
  increases to `ref_cnt=2` etc. Freeing an event, or a reference, decreases the
  reference count. A freed event (back into the event pool) should have
  `ref_cnt=0`.  
  The `send_cnt` works as before, sending the event (or e.g. giving it to the
  timer) increases the `send_cnt` while dispatching or dequeueing decreases the
  `send_cnt`. Also the `evgen` count works as before but has been restricted
  to "normal" events only, i.e. `evgen` will not be taken into account for
  references.

  1) "Normal" ESV Error format:    (double-free error)

     ```text
     EM ERROR:0x80000010  ESCOPE:0xFF000603
     core:00 ecount:0(0) em_event_state.c:350 esv_error()
     ESV: Event:0x27fffd9c7f000 state error -- counts:  send:0 ref:-1 evgen:2(3)
     Help: OK: 'send <= ref, both >=0' AND 'hdl evgen == evgen'. Err otherwise
       prev-state:em_free() core:00: EO:0xA-"myEO1" Q:0xB-"myQ1" u32[0]:(n/a)
     => err-state:em_free() core:01: EO:0xC-"myEO2" Q:0xD-"myQ2" u32[0]:0x00000000
     event:0x00027fffd9c7f000: ptr:0x7fffd9c7f000
     ```

  2) Reference ESV Error format:    ("too many frees" error, here two frees)

     ```text
     EM ERROR:0x80000010  ESCOPE:0xFF000610
     core:00 ecount:0(0) em_event_state.c:342 esv_error()
     ESV: RefEvent:0x27fffd9472000 state error -- counts:  send:0 ref:-1 (evgen:4 ignored for refs)
     Help: OK: 'send <= ref, both >=0'. Err otherwise
       prev-state:n/a (not valid for event references)
     => err-state:em_free() core:00: EO:0xC-"myEO2" Q:0xD-"myQ2" u32[0]:0x00000000
     event:0x00027fffd9472000: ptr:0x7fffd9472000
     ```

  3) Unmark ESV Error format:

     ```text
     EM ERROR:0x80000010  ESCOPE:0xFF000611
     core:00 ecount:0(0) em_event_state.c:363 esv_error()
     ESV: Event:0x27fffd9c7f100 state error - Invalid 'unmark'-API use
       prev-state:em_free()              core:00: EO:0xA-"myEO1" Q:0xB-"myQ1" u32[0]:(n/a)
     => err-state:em_event_unmark_free() core:00: EO:0xC-"myEO2" Q:0xD-"myQ2" u32[0]:0x00000000
     ``

## Event Machine (EM) on ODP v2.10.0

### EM API v2.10 - Summary

- EM Idle Hooks
- Startup pools (changes mostly related to the EM config file)

### EM API v2.10 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=10`

Backwards compatible with EM 2.9 API

1. EM Idle Hooks  
   (see include/event_machine/platform/event_machine_hooks.h)

   EM APIs:

   ```c
   em_status_t em_hooks_register_to_idle(em_idle_hook_to_idle_t func);
   em_status_t em_hooks_unregister_to_idle(em_idle_hook_to_idle_t func);
   em_status_t em_hooks_register_to_active(em_idle_hook_to_active_t func);
   em_status_t em_hooks_unregister_to_active(em_idle_hook_to_active_t func);
   em_status_t em_hooks_register_while_idle(em_idle_hook_while_idle_t func);
   em_status_t em_hooks_unregister_while_idle(em_idle_hook_while_idle_t func);
   ```

   Up to `EM_CALLBACKS_MAX` idle hooks of each type can be registered.

   Alternatively, the idle hooks can be provided as arguments to `em_init()`
   at start-up:  

   ```c
   em_init(em_conf_t::idle_hooks.to_idle_hook
                                .to_active_hook
                                .while_idle_hook);
   ```

   The arg `em_conf_t conf` of `em_init()` has new fields in
   `em_conf_t::idle_hooks{}`. The usage of `em_init()` and `em_conf_t` is backwards
   compatible with API v2.9 since `em_conf_init(&conf)` will init the new fields
   to `NULL`/`0`;

   EM Idle hook functions can be registered for tracking the idle state
   (ACTIVE/IDLE) of EM cores:
   'To_idle' hooks are called when the core state changes from ACTIVE to IDLE.
   'To_active' hooks are called when the core state changes from IDLE to ACTIVE.
   'While_idle' hooks are called when the core is already in the IDLE state and
   doesn't get any events from scheduled or local queues.

   Idle hook support is only available when EM_IDLE_HOOKS_ENABLE != 0  
   (see the configure `--enable-idle_hooks option`)

2. Startup Pools in config file  
   (see `config/em-odp.conf`)  
   Configure EM event-pools via the EM config file. These pools will be created
   during EM startup. The changes are mostly related to the EM config file but
   impacts how the `em_conf_t::default_pool_cfg` given to `em_init()` is used:
   - if the default pool configuration is also given in the config file
     through the `startup_pools` option, it will override the default pool
     configuration given via `em_conf_t::default_pool_cfg` to `em_init()`.

### EM v2.10.0 - Implementation changes

- EM Idle Hooks  
  (see include/event_machine/platform/event_machine_hooks.h for details)  
  The user can provide idle hooks to EM that will be run on each EM core when
  there are no events to process (`to_idle` and `while_idle` hooks).  
  Further, the `to_active` hook will be run, if provided, when the core again
  starts receiving events.  
  User idle hook functions can be provided via `em_conf_t::idle_hooks{...}`
  when calling `em_init()` or via hook register functions. EM will call the
  given hooks in the dispatcher depending on whether there are events to be
  processed by the core.  
  Note: the `EM_IDLE_HOOKS_ENABLE` value can be overridden by a command-line
        option to the `configure` script, e.g.:  
        `$build> ../configure ... --enable-idle-hooks`  
        The overridden value will be made available to the application via a
        pkgconfig set define.  
  Idle hooks available:
  - em_idle_hook_to_idle_t:  
    The `to_idle` hook will be called by the EM dispatcher when a core is
    entering the IDLE state i.e. when the core doesn't get any new events
    to process.
  - em_idle_hook_to_active_t:  
    The `to_active` hook will be called by the EM dispatcher when a core is
    entering the ACTIVE state i.e. when the core gets events after being idle.
  - em_idle_hook_while_idle_t:  
    The `while_idle` hook will be called by the EM dispatcher when a core is
    already in the IDLE state and stays in it i.e. the core doesn't get any
    events.  
  Register-functions:

  ```c
  em_status_t em_hooks_register_to_idle(em_idle_hook_to_idle_t func);
  em_status_t em_hooks_unregister_to_idle(em_idle_hook_to_idle_t func);
  em_status_t em_hooks_register_to_active(em_idle_hook_to_active_t func);
  em_status_t em_hooks_unregister_to_active(em_idle_hook_to_active_t func);
  em_status_t em_hooks_register_while_idle(em_idle_hook_while_idle_t func);
  em_status_t em_hooks_unregister_while_idle(em_idle_hook_while_idle_t func);
   ```

  Alternatively provide the idle-hook functions as arguments to `em_init()` at
  start-up:

  ```c
  em_init(em_conf_t::idle_hooks.to_idle_hook
                               .to_active_hook
                               .while_idle_hook);
  ```

- Startup pools  
  (see `config/em-odp.conf` for more documentation and details)  
  - EM config file option: `startup_pools:{}`
    Configure EM event-pools via the EM config file. These pools are created
    during EM startup and destroyed when EM terminates (if still available).
    When startup pools are used, EM will create them according to the
    configuration given in the EM config file during em_init().  
    Startup pools are configured via the EM config file option: `startup_pools`.
    The user should override the EM default config file (`config/em-odp.conf`) with
    a separate runtime configuration file containing the desired application pool
    configurations and provide it to EM via the `EM_CONFIG_FILE` env variable:  
    `$> EM_CONFIG_FILE=<path-to>/my-em.conf ./my-em-app [args]`  
    If no startup pools are given, only the default pool needs to be created. In
    this case, the default pool configuration is specified in the parameters of
    `em_init()`, more specifically, in the struct `em_conf_t::default_pool_cfg`.

    Note that if the default pool is configured via the config file, it overrides
    the default pool configuration given to `em_init()`.
    The priority regarding the default pool configuration is as follows:

    ```text
    +--------------+      +-------------+      +-----------------------------+
    | Runtime.conf |  >   | em-odp.conf |   >  | em_conf_t::default_pool_cfg |
    +--------------+      +-------------+      +-----------------------------+
    ```

    The default pool configuration specified in runtime configuration file
    overrides the one given in the default configuration file (`em-odp.conf`),
    which overrides the one passed as a parameter to `em_init()`.

    Note that the `startup_pools`-options are pool specific configurations given
    directly to `em_pool_create(name, pool, pool_cfg)`, thus overriding the global
    config file settings such as `pool.align_offset`, `pool.user_area_size` and
    `pool.pkt_headroom` that might be set in the config file.

- Core: support odp-thread counts larger than 256

- ESV: reduce the number of ESV state transitions for output from EM  
  Event Chaining output and output via EM output queues are both based on
  calling a user provided function triggered from `em_send()` - no need to
  toggle ESV state (between usr2em <-> em2usr) many times. Keep the ownership of
  the event with the user until the user provided callback.

- Queue: check that the EM queue context is valid  
  EM will verify that the used ODP queue context actually points to an
  EM queue-elem and not to something else.  
  Only enabled with `EM_CHECK_LEVEL=3` (configure option: `--enable-check-level=3`)

## Event Machine (EM) on ODP v2.9.0

### EM API v2.9 - Summary

- Minor API change: documentation clean up and a define value increase.

### EM API v2.9 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=9`

Backwards compatible with EM 2.8 API

1. API documentation clean-up.
2. The define `EM_MAX_QUEUES` increased from 960 to 1024.

### EM v2.9.0 - Implementation changes

- EM config file options - `config/em-odp.conf`:
  - Config file version number bumped up to "0.0.14", see new options below.

- EM pool statistics update  
  - EM Pool statistics to rely on ODP pool statistics.  
    EM does not have to maintain pool statistics since ODP provides the
    same (or extended) information. The EM API em_pool_info() remains the same,
    it now uses ODP pool statistics under the hood instead.

- Dispatch, init: enforce ctrl queue polling on first dispatch.  
  - The first call to `em_dispatch()` on each core will trigger a poll of the
    internal unscheduled ctrl queues.
  - See "Changed EM internal communication" in the release notes for v2.8.0
    below for more details on EM's unscheduled ctrl queues.

- EM Command Line Interface (EM CLI) update
  - Further EM CLI commands added, use the command "`help`" to see all available
    commands:  
    `em-odp.conf: cli.enable = true`

    ```text
    $> telnet localhost 55555
    EM-ODP> help
    Commands available:
      help                 Show available commands
      quit                 Disconnect
      logout               Disconnect
      exit                 Exit from current mode
      history              Show a list of previously run commands
      enable               Turn on privileged commands
      ...
      <odp-cmds>
      ...
      em_agrp_print        [a|i <ag id>|n <ag name>|h]
      em_eo_print          [a|i <eo id>|n <eo name>|h]
      em_egrp_print
      em_info_print        [a|p|c|h]
      em_pool_print        [a|i <pool id>|n <pool name>|h]
      em_queue_print       [a|c|h]
      em_qgrp_print        [a|i <qgrp id>|n <qgrp name>|h]
      em_core_print
    ```

  - See "EM Command Line Interface (EM CLI)" in the release notes for v2.8.0
    below for more details on usage.

- Rate limit EM's input-poll and output-drain callback functions.  
  - Config file options to control how often the user provided callback
    functions are called  
    (see `config/em-odp.conf` for full documentation):  
    - Core local interval for calling input poll and output drain functions
      (in dispatch rounds):
      `dispatch.poll_drain_interval = 1`
    - Core local interval for calling input poll and output drain functions
      (in nanoseconds):  
      `dispatch.poll_drain_interval_ns = 1000000L`
    - Default values currently selected so that functionality is unchanged
      compared to earlier releases.

- EM timer updated to use new ODP APIs
  - EM timer updated to internally use `odp_timer_start()` (instead of the now
    deprecated ~~`odp_timer_set_abs()`~~)

- Test files and scripts moved into the em-odp repo (robot-tests/, scripts/)

## Event Machine (EM) on ODP v2.8.0

### EM API v2.8 - Summary

- Small API change, a new define is added.

### EM API v2.8 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=8`

Backwards compatible with EM 2.7 API

1. Add a define for the base-name of the EM core-specific queue groups (one per
   EM-core):

   ```c
   #define EM_QUEUE_GROUP_CORE_BASE_NAME "core"
   ```

   The full queue group name for a single-core queue group is: `"core" + "%d"`,
   which gives `"core0"`, `"core1"`, `...` `"core99"`, `...`  
   EM earlier relied on these queue groups for internal core specific
   messaging and also allowed applications to use them. Currently EM
   does not internally need these groups but will create them based on
   an EM config file option for applications relying on their existence.

### EM v2.8.0 - Implementation changes

- EM config file options - config/em-odp.conf:
  - Config file version number bumped up to "0.0.13", see new options below.

- EM Queue Group start-up improvements:
  - The EM default queue group (`EM_QUEUE_GROUP_DEFAULT`) is now available at
    start-up after init, before all cores have run local init.
    This allows an application to create queues using the default queue group
    before all other EM cores have initialized.
  - If enabled in the EM config file, the single-core EM queue groups are also
    similarly available at startup.

- Concurrent sync-APIs
  - EM allows concurrent sync-APIs. Multiple concurrent calls to e.g.
    `em_eo_start_sync()`, `em_queue_group_modify_sync()` etc. are allowed.
    Previously only one sync-operation at a time was permitted, now there is
    no limit.  
    One restriction remains: concerned cores need to be dispatching
    (or processing other sync-API calls) for the operations to complete.  
    A core that has left the dispatch loop or is sleeping etc. will not allow
    the sync-API operation to complete.

- Changed EM internal communication:  
  The EM internal event communication previously used scheduled queues and
  single-core queue groups. This has now been replaced by unscheduled queues
  that each core polls periodically for internal control-events. The internal
  control operations and events are all triggered by EM APIs, e.g. sync-APIs.
  This change allows EM to support concurrent sync-APIs.  
  Adjust the EM control queue polling rate to a suitable level based on the
  needs of the application,  
  see options in the EM config file `config/em-odp.conf`:  
  `dispatch.poll_ctrl_interval = 100`  - rate limit EM control queue polling  
  `dispatch.poll_ctrl_interval_ns = 1000000L` - poll max every 1ms

  See the EM config file for additional documentation and details.  
  These options affect the timing of the EM control operations and the
  application may see changes in control operation duration and latency.  
  Tune the `dispatch.poll_ctrl_...` options to a preferred level of
  performance vs. latency (performance impact of polling is quite small).

- EM single-core queue groups disabled by default:  
  The EM single-core queue groups have been disabled by default since EM does
  not need them for internal communication anymore (see above).
  - EM single core queue group naming (if enabled and available):  
    `EM_QUEUE_GROUP_CORE_BASE_NAME + "%d"`: `"core0"`, `"core1"`, etc.
  Disabling saves on available queue groups especially when running with many
  cores (since EM would otherwise at startup create a queue group per core).

  Applications must override the default config if these single-core
  queue groups are needed,
  see option in the EM config file `config/em-odp.conf`:  
  - `queue_group.create_core_queue_groups = false(default)|true`

  See the EM config file for additional documentation and details.

- EM Command Line Interface (EM CLI)  
  The first version of the EM CLI has been integrated but is disabled by default.
  A change to the EM config file is needed in order to use it.  
  The EM CLI allows a developer to connect via telnet to a running EM instance
  in order to extract runtime information about the EM application.
  - Dependency: libcli needs to be availabe to use the EM CLI.  
    If libcli is unavailable at compile time then the CLI feature is left out.
  - An autoconf configure script option `--with-libcli-path=[install-dir]` can
    be used to set the path to libcli if not installed in the standard path:  
    `em-odp/build $> ../configure ... --with-libcli-path=[install-dir] ...`  
  - EM CLI options (see the EM config file for additional documentation and
    details), `config/em-odp.conf`:

    ```text
    cli.enable = false(default)|true
    cli.ip_addr = "127.0.0.1" # localhost
    cli.port = 55555
    ```

  Currently, only very basic commands are available - new ones will be added and
  existing ones extended & improved.  
  Note: existing commands might still be renamed and reworked.

## Event Machine (EM) on ODP v2.7.0

### EM API v2.7 - Summary

- New APIs for Version, Event User Area, Event User Area ID

### EM API v2.7 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=7`

Backwards compatible with EM 2.6 API

1. EM version number APIs:  
   (see `include/event_machine/api/event_machine_version.h.in`, which is used to
    create event_machine_version.h during package configuration/build)

   - Version number macros and define constants that can be used to identify the
     EM API and implementation versions at build time.  
     Can be used for conditional compilation:

    ```c
    #if EM_VERSION >= EM_VERSION_NUM(2.7.0.0)
            /* EM version is at least v2.7.0 (.fix=0) */
    #endif

    #if EM_VERSION_API >= EM_VERSION_API_NUM(2.7)
            /* EM API is at least v2.7 */
    #endif
    ```

   - Runtime API functions that can be used to check the EM API and
     implementation numbers at runtime.  
     The build-time and runtime versions numbers may differ e.g. if the
     application has been compiled against the header files of a certain
     EM release and later the EM shared library is updated without recompilation
     of the application.

     EM APIs:

     ```c
     const char *em_version_api_str(void);
     unsigned int em_version_api_num(void);
     const char *em_version_str(void);
     unsigned int em_version_num(void);
     ```

2. EM Event User Area:  
   (see `include/event_machine/api/event_machine_event.h`)  
   The event user area is a fixed sized area located within the event metadata
   (i.e. outside of the event payload) that can be used to store application
   specific event related data without the need to adjust the payload.  
   The event user area is configured during EM event pool creation and thus the
   size of the user area is set per pool.  
   Note that the user area content is not initialized by EM, neither em_alloc()
   nor em_free() will touch it and thus it might contain old user data set the
   last time the area was used during a previous allocation of the same event.  
   Since the user area is not part of the event payload, it will not be
   transmitted as part of a packet etc.

   - Get a pointer to the event user area, optionally along with its size:

     ```c
     void *em_event_uarea_get(em_event_t event, size_t *size/*out*/);
     ```

   - Pool config for the Event User Area.  
     (see include/event_machine/platform/event_machine_pool.h)  
     Pool config `em_pool_cfg_t::user_area.{.in_use, .size}` has been updated to
     include configuration options for the event user area.  
     The default event user area size can additionally be set via the
     EM config file `config/em-odp.conf`:

     ```text
     pool: {
        ...
        # Default event user area size in bytes for all events from all pools.
        #
        # This is a global setting that can be overridden per pool using
        # 'em_pool_cfg_t::user_area{}' with em_pool_create().
        #
        user_area_size = 0
        ...
     }
     ```

3. EM Event User Area ID:  
   (see `include/event_machine/api/event_machine_event.h`)  
   The event user area can be associated with an optional ID that e.g. can be
   used to identify the contents of the actual user area data. The ID is stored
   outside of the actual user area data and is available for use even if the
   user area size has been set to zero(0) for the pool the event was allocated
   from.

   - Get the event user area ID along with information if it has been set:

     ```c
     em_status_t em_event_uarea_id_get(em_event_t event, bool *isset /*out*/,
                                       uint16_t *id /*out*/);
     ```

   - Set the event user area ID:

     ```c
     em_status_t em_event_uarea_id_set(em_event_t event, uint16_t id);
     ```

   - Get the event user area information for a given event:

     ```c
     em_status_t em_event_uarea_info(em_event_t event,
                                     em_event_uarea_info_t *uarea_info /*out*/);
     ```

### EM v2.7.0 - Implementation changes

- EM config file options - config/em-odp.conf:
  - Config file version number bumped up to "0.0.10"
  - Change the default Queue priority mode:  
    `queue.priority.map_mode = 1`  
    `1`: map according to ODP runtime number of priorities
       (linear fit to full range of ODP priorities available)
  - The Event User Area default size can be set via the `user_area_size` option:  
    `config/em-odp.conf`:

    ```text
    pool: {
       ...
       # Default event user area size in bytes for all events from all pools.
       #
       # This is a global setting that can be overridden per pool using
       # 'em_pool_cfg_t::user_area{}' with em_pool_create().
       #
       user_area_size = 0
       ...
    }
    ```

- Tested against:

  ```text
  odp-linux - https://github.com/OpenDataPlane/odp
    Commit: 795d576fb46db723d0b5e11a20111bab574a5d0a [795d576]
    Date: Tuesday, 21 December 2021 11:42.06
    Commit Date: Monday, 27 December 2021 14:47.34
    linux-gen: sched: update random data table
  odp-dpdk - https://github.com/OpenDataPlane/odp-dpdk
    Commit: c3789c8e6ec34faee0b59080c1ab187cff1dc356 [c3789c8]
    Date: Friday, 17 December 2021 14:00.35
    Merge ODP v1.33.0.0
  ```

## Event Machine (EM) on ODP v2.6.0

### EM API v2.6 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=6`

Backwards compatible with EM 2.5 API

1. Timer:  
   (see em-odp/include/event_machine/api/event_machine_timer.h for details)
   - Add new timer clock source choices (`em_timer_clksrc_t`) to select the
     timer clock source in case multiple are supported:  
     `EM_TIMER_CLKSRC_0`, `EM_TIMER_CLKSRC_1`, ..., `EM_TIMER_CLKSRC_5`.  
     Backwards compatible macros (`EM_TIMER_CLKSRC_CPU`, `EM_TIMER_CLKSRC_EXT`) are
     provided for full compatibility with older code.  
     `EM_TIMER_CLKSRC_DEFAULT` is always available and is a fully portable
     definition.

2. Event:  
   (see em-odp/include/event_machine/api/event_machine_event.h for details)

   - Event cloning:

     ```c
     em_event_t em_event_clone(em_event_t event, em_pool_t pool);
     ```

     Allocate a new event with identical payload to the given event.
     The event metadata and internal headers will *not* be cloned.
     The optional `pool` argument is used to specify the event pool to allocate
     the cloned event from. Use `EM_POOL_UNDEF` to clone from the same pool as
     `event` was allocated from.  
     The event-type of `event` must be suitable for allocation from `pool`
     (e.g. `EM_EVENT_TYPE_PACKET` can not be allocated from a pool supporting
     only `EM_EVENT_TYPE_SW`)  
     The function returns the cloned event or `EM_EVENT_UNDEF` on error.

   - Event/Pool - Get the handle of the pool the event was allocated from:

     ```c
     em_pool_t em_event_get_pool(em_event_t event);
     ```

     Returns the EM event-pool the event was allocated from or `EM_POOL_UNDEF` if
     no EM pool is found. `EM_POOL_UNDEF` is returned also for a valid event that
     has been allocated from a pool external to EM (no error is reported).
     The EM event-pool for the given event can only be obtained if the event
     has been allocated from a pool created with `em_pool_create()`. For other
     pools, e.g. external (to EM) pktio pools, `EM_POOL_UNDEF` is returned.

   - Marking event(s) as "free":

     ```c
     void em_event_mark_free(em_event_t event);
     void em_event_mark_free_multi(const em_event_t events[], int num);
     ```

     Mark the event state as "free" and then rely on HW / mechanisms external
     to EM to actually free the event(s) back into the event pool.
     - `em_event_mark_free()`: Indicates a user-given promise to EM that the event
       will be freed back into the pool it was allocated from e.g. by HW or
       device drivers (external to EM). Calling `em_event_mark_free()` transfers
       event ownership away from the user, and thus the event must not be used or
       touched by the user anymore.  
       EM will, after this API-call, treat the event as "freed" and any further
       API operations or usage might lead to EM errors (depending on the
       error-check level), e.g. `em_send`/`free`/`tmo_set`/`ack(event)` etc. is
       forbidden after `em_event_mark_free(event)`.
     - `em_event_mark_free_multi()`: Similar to `em_event_mark_free()`, but
       allows the marking of multiple events as "free" with one function call.  
       Note: Registered API-callback hooks for `em_free`/`_multi()`
             (`em_api_hook_free_t`) will *NOT* be called.

   - Unmarking event(s) previously marked "free":

     ```c
     void em_event_unmark_free(em_event_t event);
     void em_event_unmark_free_multi(const em_event_t events[], int num);
     ```

     Unmark event(s) previously marked as "free" (i.e mark as "allocated" again).  
     Note: This is for recovery situations only and can potentially crash the
           application if used incorrectly! Unmarking the free-state of an event
           that has already been freed will lead to fatal error.
     - `em_event_unmark_free()`: Revert an event's "free" state, as set by
       `em_event_mark_free()` back to the state before the mark-free function call.
       Any further usage of the event after `em_event_mark_free()`, by EM or the
       user, will result in error when calling `em_event_unmark_free()` since the
       state has become unrecoverable.  
       => the only allowed EM API call after `em_event_mark_free()` (for a certain
          event) is `em_event_unmark_free()` when it is certain that the event, due
          to some external error, will not be freed otherwise and must be
          recovered back into the EM-domain so that calling em_free() by the user
          is possible.  
       Calling `em_event_unmark_free()` transfers event ownership back to the user
       again.  
       Note: Unmark-send and unmark-free are the only valid cases of using an
             event that the user no longer owns, all other such uses leads to
             fatal error.  
     - `em_event_unmark_free_multi()`: Unmark multiple events previously marked
       as "free". Similar to `em_event_unmark_free()`, but allows to do the
       "free"-unmarking of multiple events with one function call.

3. Queue:  
   (see em-odp/include/event_machine/api/event_machine_queue.h)
   - Number of queue priorities:

     ```c
     int em_queue_get_num_prio(int *num_runtime);
     ```

     The function returns the number of queue priorities available.
     Optionally, the amount of actual runtime priorities can be inquired.
     The valid queue priority range is from `0` (lowest priority) to
     `em_queue_get_num_prio() - 1`.  
     The runtime environment may provide a different amount of priority levels.
     EM priorities are in that case mapped to the runtime values depending on
     "priority map-mode" selected in the runtime configuration file
     (`config/em-odp.conf`).

4. Extension APIs for EM<->ODP conversions:  
   (see include/event_machine/platform/event_machine_odp_ext.h)
   - Convert between EM and ODP pools:

     ```c
     int em_odp_pool2odp(em_pool_t pool, odp_pool_t odp_pools[/out/], int num);
     em_pool_t em_odp_pool2em(odp_pool_t odp_pool);
     ```

     - `em_odp_pool2odp()`: Get the ODP pools used as subpools in a given EM event
       pool. An EM event pool consists of `1` to `EM_MAX_SUBPOOLS` subpools. Each
       subpool is an ODP pool. This function outputs the ODP pool handles of
       these subpools into a user-provided array and returns the number of
       handles written. The obtained ODP pools must not be deleted or altered
       outside of EM, e.g. these ODP pools must only be deleted as part of an
       EM event pool using `em_pool_delete()`.
       ODP pool handles obtained through this function can be used to
         - configure ODP pktio to use an ODP pool created via EM (allows for
           better ESV tracking)
         - print ODP-level pool statistics with ODP APIs etc.
       The function returns the number of ODP pools filled into `odp_pools[]`.

     - `em_odp_pool2em()`: Get the EM event pool that a given ODP pool belongs to.
       An EM event pool consists of `1` to `EM_MAX_SUBPOOLS` subpools. Each subpool
       is an ODP pool. This function returns the EM event pool that contains the
       given ODP pool as a subpool or `EM_POOL_UNDEF` if the ODP pool is not part
       of any EM event pool.

### EM v2.6.0 - Implementation changes

- New configure script option (see em-odp/configure.ac):  
  `--enable-debug-print` that enables EM debug printouts via the EM_DBG() macro:  
  `em-odp/build $> ../configure --prefix=… --enable-debug-print`  
  Currently if enabled, will print event header layout information at startup
  as well as EM timer runtime debug info.

- EM config file options - config/em-odp.conf:
  - Config file version bumped to "0.0.9" (user config files need to be updated!)
  - Options for setting EM Queue priority-levels:  
    1. Option `queue.priority.map_mode`:  
       Select the queue priority mapping mode, i.e how to map EM queue priorities
       to ODP scheduled queue priorities.  
       `0`: legacy simple mode, map EM prios to ODP min/default/max prio-levels only
            (0 is the default mode)  
       `1`: map according to the ODP runtime number of priorities
            (linear fit to full range of ODP prio-levels)  
       `2`: custom mapping
            (use `.custom_map` below)  
    2. Option `queue.priority.custom_map`:  
       Custom priority map (required when `queue.priority.map_mode = 2`)
       This array needs to have `EM_QUEUE_PRIO_NUM` entries (typically 8).
       The first entry is for the lowest priority (0). The value is ADDED to
       `odp_schedule_min_prio()` and then passed to ODP, i.e. values are offsets
       from `odp_schedule_min_prio()`. Values given here must be valid for ODP
       runtime configuration, i.e. value plus `odp_schedule_min_prio()` must
       not exceed `odp_schedule_max_prio()`.  
       Example of a custom priority mapping:

       ```text
          queue: {
                  priority: {
                          map_mode = 2
                          custom_map = [0, 0, 1, 3 ,4 ,6 ,7, 7]
                  }
          }
       ```

- EM SW ISA printout added at start-up:

  ```text
  ...
  ===========================================================
  EM Info on target: em-odp
  ===========================================================
  EM API version:         v2.6, 64 bit (EM_CHECK_LEVEL:3, EM_ESV_ENABLE:1)
  EM build info:          v2.6.0 2021-10-15 16:46
  ODP API version:        1.32.0
  ODP impl name:          odp-linux
  ODP impl details:       odp-linux 1.32.0-0 (v1.32.0) 1.32.0.0
  CPU model:              Intel(R) Xeon(R) CPU E5-2697 v3
  CPU arch:               x86     // ARM etc.
  CPU ISA version:        Unknown // ARMv8.2-A  # HW detected, reported by ODP
   SW ISA version (ODP):  x86_64  // ARMv8.1-A  # ODP SW config (compile time)
   SW ISA version (EM):   x86_64  // ARMv8.1-A  #  EM SW config (compile time)
  ```

  Arch specific `CFLAGS` should be passed to EM and ODP via configure.  
  Example for a made up ARMv8 SoC called "soc-x":  
  `odp-soc-x/build> ../configure … --with-platform=soc-x [default=linux-generic]`  
  `em-odp/build> ../configure … CFLAGS=”-O3 -march=armv8.2-a+… -mcpu=soc-x”`
  The whole chain of used SW libs/dependencies should preferably be compiled
  with the same `-march/-mcpu` options for best results:  
  `other-libs/build> ../configure … CFLAGS=”-O3 -march=armv8.2-a+… -mcpu=soc-x”`

- Program termination:  
  Flush the scheduler in `em_term_core()` on each EM-core.  
  Move the flushing of scheduled events from `em_term()` to `em_term_core()` so
  that all participating EM-cores do it.  
  See example usage of in `em-odp/programs/common/cm_setup.c`:  
  ('thread' in the description below refers to `odph_thread_t` that can be
   either linux processes or threads)
  `em_term()` has been moved from each EM-core's run-function to the common
  thread that created the EM-core threads. The common thread waits for the
  created EM-core threads to return/join.  
  After each EM-core has paused and flushed the scheduler from core-local events
  (by calling `em_term_local()`) they terminate by "joining" back into the
  common thread, which calls `em_term()` once to terminate EM (and ODP) before
  exiting the application.

- Fixes:
  - queue group: fix: `em_queue_group_modify_sync()` incorrectly removed the
                      calling core
  - ESV: fix: usr2em-revert transition to handle evgen wrap correctly  
    Event State Verification (ESV) incorrectly handled the revert of a usr-to-EM
    transition when the counter wrapped.

- Example programs (em-odp/programs)
  - Common Setup (cm_setup.c&h):
    - Move the flushing of scheduled events from em_term() to em_term_core().  
      Descibed above in "Program termination".
  - Packet-IO setup (cm_pktio.c&h)
    - Add command-line option to select whether to use an
      1) ODP-packet-pool: `-o, --pktpool-odp`  
         or an
      2) EM event-pool: `-e, --pktpool-em` (default if no option given)  
      as the main event pool to be used by packet-io for received packets.  
      Packet-IO is implemented via ODP so an EM event-pool needs to be converted
      to the corresponding ODP packet-pools with the new extension API
      `em_odp_pool2odp()`. Using an EM pool, instead of directly using an
      ODP pool, allows for more thorough ESV-checking also for packet-IO when
      em-odp.conf: esv.prealloc_pools = true.
  - Timer
  - Misc. performance and bug fixes in the example applications

- Tested against:

  ```text
  odp-linux - https://github.com/OpenDataPlane/odp
    Commit: 69f27643ec2287875048314d7975f0de4bcbbfa4 [69f2764]
    Date: Friday, 1 October 2021 18:12.09
    Commit Date: Tuesday, 12 October 2021 17:06.16
    configure.ac: move project to C standard revision 11
  odp-dpdk - https://github.com/OpenDataPlane/odp-dpdk
    Commit: 50c7b605b97474d26fcf600b4061968e3543b45b [50c7b60]
    Date: Monday, 11 October 2021 16:37.51
    Merge ODP v1.32.0.0
  ```

## Event Machine (EM) on ODP v2.5.0

### EM API v2.5 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=5`

Not backwards compatible with previous API version, API 2.5 contains modified
types and functions requiring small changes to older code.

1. Timer: timer version updated from v2.0 to v2.1,
   see em-odp/include/event_machine/api/event_machine_timer.h for details.
   - New Timer APIs for tick<->ns conversions:
     - `em_timer_tick_to_ns()`
     - `em_timer_ns_to_tick()`

   - New Error return codes for `em_tmo_set_abs()` and `em_tmo_set_periodic()`
     related to timing errors:

     ```text
     EM_OK          success (event taken)
     EM_ERR_TOONEAR failure, tick value is past or too close to current time
     EM_ERR_TOOFAR  failure, tick value exceeds timer capability (too far ahead)
     (other_codes)  other failure
     New Error return codes for em_tmo_ack():
     EM_OK            success
     EM_ERR_CANCELED  timer has been cancelled
     (other_codes)    failure
     ```

2. Event: New APIs introduced together with EM feature
          "Event State Verification" (ESV).
   - `em_event_mark_send(event, queue)`: Mark the event as "sent" (without
     calling em_send...()).  
     Indicates a user-given promise to EM that the event will later appear into
     'queue' by some means other than an explicit user call to `em_send...()`.  
     Calling `em_event_mark_send()` transfers event ownership away from the user,
     and thus the event must not be used or touched by the user anymore (the
     only exception is (hw) error recovery where the "sent" state can be
     cancelled by using `em_event_unmark_send()` - dangerous!).

   - `em_event_unmark_send(event)`: Unmark an event previously marked as "sent"
                                    (i.e mark as "unsent")  
     Note: This is for recovery situations only and can potentially crash the
           application if used incorrectly!  
     Revert an event's "sent" state, as set by `em_event_mark_send()`, back to
     the state before that function call.  
     Any further usage of the event after `em_event_mark_send()`, by EM or
     the user, will result in error when calling em_event_unmark_send() since
     the state has become unrecoverable => the only allowed EM API call after
     `em_event_mark_send()` is `em_event_unmark_send()` if it is certain that
     the event, due to some external error, will never be sent into EM again
     otherwise.  
     Calling `em_event_unmark_send()` transfers event ownership back to the user
     again.  
     Note2: This is the only valid case of using an event that the user no
            longer owns - all other such uses leads to fatal error.

3. Pool:
   - Possibility to set the event payload headroom of pkt-events during
     EM event pool creation (`em_pool_create()`) with the new config params:  
     `em_pool_cfg_t::pkt.headroom.in_use` & `.value`

4. Extension APIs for EM<->ODP conversions
   - Conversion functions between EM events to/from ODP events changed:

     ```c
     void em_odp_events2odp(const em_event_t events[],
                            odp_event_t odp_events[/*out*/], const int num)
     void em_odp_events2em(const odp_event_t odp_events[],
                           em_event_t events[/*out*/], const int num)
     ```

5. Misc
   - Improved C++ support: including EM API header files into a C++ project
     now avoids errors related to C vs. C++ typecasting.
   - API function pointer arguments should be `const` if object is not modified

### EM v2.5.0 - Implementation changes

- Error: don't allow error handlers to return fatal EM-errors.  
  Fatal EM reported errors must be dealt with in the error handler, since EM
  cannot continue running after a fatal error.  
  EM will now abort if an error handler returns a fatal error.

- Event State Verification (ESV)  
  ESV is an EM-internal feature that aims to catch invalid EM-event usage.
  The idea is to catch invalid/illegal EM event usage in the application and
  report it as a FATAL-error to immediately abort the application​.
  The event-state is tracked and updated every time the event is transferred
  between EM and the application, e.g. in dispatch, `em_send()`, `em_alloc()`,
  `em_free()` etc.​  
  Example scenarios that are detected:​ double-free, double-send, use-after-send,
  use-after-free etc.  
  Additionally, an event-generation-count (evgen) has been added into the
  EM event handle (em_event_t) to catch usage of events that are no longer
  “owned” by the application (EO).  
  The event handle will also change throughout the lifetime of the event.
  Using an out-of-date/obsolete event handle results in ESV error​.
  An event handle becomes invalid after it has been given to EM, e.g. reusing a
  stored event-handle after it has been sent will result in error, even if the
  same event would have been received again, because the event-handle has
  changed​.  
  ESV is (currently) disabled by default and can be taken into use via a
  configure-option​:

  ```text
  em-odp/build $> ../configure --prefix=… --enable-esv​
  em-odp/build $> make clean && make –j16 install​
  ```

  Using `--enable-esv` defines `EM_ESV_ENABLE` as `1`.

  ```text
  Example Scenario - ESV Error:
  a) EO-receives an event​
  b) em_send(event, …)​ - EO sends the event
  c) em_free(event)    - EO calls free for an already sent event
     ==> Fatal Error:

   EM ERROR:0x80000010  ESCOPE:0xFF000603  EO:0x5-"eo-a"​
   core:00 ecount:0(0) em_event_state.c:335 evstate_error()​
   Event:0x27f4a84c5d9c0 state error -- counts current(vs.expected): evgen:2(3) free:1(1) send:1(0)​
      prev-state:em_send() core:00: EO:0x5-"eo-a" Q:0x1d-"q-a" u32[0]:0x00000000​
   => new-state:em_free()  core:00: EO:0x5-"eo-a" Q:0x1d-"q-a" u32[0]:0x00000000​
      event:0x00027f4a84c5d9c0: ptr:0x7f4a84c5d9c0
  ```

  Note: ESV-errors are always reported with the same error-code:  
  `EM_FATAL(EM_ERR_EVENT_STATE) = 0x80000010`  
  `prev-state` shows the previous good state of the event. i.e. the
  previous place where EM observed the event in a valid state.
  `new-state` shows the current detected illegal state that
  caused the error.

  Using ESV has an impact on performance.
  Each event-state transition updates an atomic counter and stores further
  state data (EO, queue, core etc)​.
  The impact, of course, depends on the “number of state-transitions / second”​

- Event State Verification (ESV) runtime options - `config/em-odp.conf: esv:{...}`  
  Note: Options only considered if ESV is enabled at compile-time
        via the configure option `--enable-esv`, see `configure.ac`
  - Option `esv.enable`  
    Runtime ESV enable/disable option. Allows the user to disable ESV
    without recompilation when `configure --enable-esv` has been used.
  - Option `esv.store_payload_first_u32`  
    Store the first 32bits of the event payload during each valid
    event-state transition. This allows for a comparison of the payload
    content between the previous valid state and the invalid state.
    Note: The first 32bits of the payload will always be printed in the
          error log for the invalid state regardless of this setting.
    Enabling will impact performace somewhat.
  - Option `esv.prealloc_pools`  
    Preallocate all events in a pool during pool creation to set
    an initial ESV-state for each event that can be tracked over
    multiple allocs and frees.

- EM config file options - `config/em-odp.conf`:
  - Option `pool.pkt_headroom`:  
    Default minimum packet headroom in bytes for events allocated from
    EM-pools of type: `EM_EVENT_TYPE_PACKET`. Ignored for other pool types.

- Fixes:
  - timer: fix timer capability max_tmo values
  - event: fix: incorrect loop index when allocating multiple events
  - queue: fix `em_queue_get_index()` return on error
  - code style fixes (reported by code checkers)

## Event Machine (EM) on ODP v2.4.0

### EM API v2.4 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=4`

Not backwards compatible with previous API version, API 2.4 contains modified
types requiring small changes to older code.

1. Timer: timer version updated from v1.1 to v2.0  
   (see em-odp/include/event_machine/api/event_machine_timer.h for details)  

   New or updated timer types and APIs:
   - `em_timer_attr_t`: *updated* type that MUST be initialized with
                        `em_timer_attr_init()` before passing it to
                        `em_timer_create()`.  
     Note: timer resolution is given to `em_timer_create()` as either
           nanoseconds or Hertz:  
           `em_timer_attr_t::resparam.res_ns` or `.res_hz` (new option).  
           The user must choose which one to use by setting the other one to 0.

   - `em_timer_attr_init()`: Mandatory initialization of timer attributes.  
     Enforce the following sequence:

     ```c
        em_timer_attr_t attr;
        em_timer_attr_init(&attr); /* Now mandatory */
        ...
        em_timer_t tmr = em_timer_create(&attr);
     ```

     Timer-create will fail if attr-init has been omitted.

   - `em_timer_capability()` & `em_timer_capability_t`: Inquire timer capabilities.

   - `em_timer_res_capability()` & `em_timer_res_param_t`:
     Inquire timer capabilities for a specific resolution or maximum timeout.

   - `em_tmo_set_periodic()`: Allow an absolute start time for periodic timeouts
     with the new API `em_tmo_set_periodic(..., em_timer_tick_t start_abs, ...)`  
     Note: Prefer the new `em_tmo_set_periodic()` for activating/setting periodic
           timeouts over `em_tmo_set_rel()`.  
           Use `em_tmo_set_abs()` or `em_tmo_set_rel()` for oneshot timeouts.

   - `em_tmo_get_stats()` & `em_tmo_stats_t`: Get timeout statistics counters.  
     Currently meaningful for periodic timeouts only, 0 otherwise.

2. Pool:
   - em_pool_cfg_init(): pool-config initialization function.  
     Initialize EM-pool configuration parameters for `em_pool_create()`,
     i.e. set em_pool_cfg_t to default values for all fields.  
     After initialization, the user further needs to update the fields of
     `em_pool_cfg_t` with appropriate sizing information before calling
     em_pool_create().  
     Always initialize `pool_cfg` first with `em_pool_cfg_init(pool_cfg)` to
     ensure backwards compatibility with potentially added new options.

   - Possibility to configure the core-local event-cache per subpool via
     `em_pool_cfg_t::cache_size` during EM-pool creation.  
     The `cache_size` param is defined in `em_pool_cfg_t` as:

     ```text
     "Maximum number of locally cached subpool events per EM-core.
      Allocating or freeing events from a core-local event-cache
      can be faster than using the global event subpool. Cached
      events are only available on the local core and can reduce
      the number of globally free events in the subpool, thus set
      `num > EM-core-count * cache_size`.
      The actual used cache_size will be smaller than or equal to
      the requested value, depending on the implementation."
     ```  

3. EO: init: eo-multircv params must be initialized before creating EO.  
   Enforce the following sequence:

   ```c
        em_eo_multircv_param_t param;
        em_eo_multircv_param_init(&param); /* Now mandatory */
        eo = em_eo_create_multircv(..., param);
   ```

   EO-create will fail if param-init has been omitted.

4. Queue:
   - `em_queue_get_index()`: Get a unique index from a given EM queue handle.  
     Get a unique index corresponding to the given EM queue handle.
     Returns a unique index in the range `0...EM_MAX_QUEUES-1`.
     The same EM queue handle will always map to the same index.
     Only meaningful for queues created within the current EM instance.

5. Add missing const qualifiers into the API.  
   Some API calls missed const qualifiers on read-only data pointers.
   Add the missing `const` and fix the implementation accordingly.

### EM v2.4.0 - Implementation changes

- Parameter/attribute initialization function usage is now mandatory before
  calling "create"-functions for EO-multircv, Timer and Pool:
  - call em_eo_multircv_param_init() before em_eo_create_multircv()
  - call em_pool_cfg_init() before em_pool_create()
  - call em_timer_attr_init() before em_timer_create()

- Build:
  - configure: option --with-odp-lib=[name of used odp library] added.
    Add the `configure` script option --with-odp-lib=odp-library-name
    that allows the user to specify the name of the used ODP library.
    The library name defaults to "libodp-linux" if the option is omitted.
    E.g. configure em-odp to use odp-dpdk instead of odp-linux:

    ```bash
    cd em-odp/build
    ../configure ... [--with-odp-path=...] --with-odp-lib=libodp-dpdk
    make
    ```

  - pkg-config: update em-odp dependencies in the package config file.
    Add explicit dependency on ODP: "Requires: @libodp_name@" (where the ODP
    library name is taken from the configure option `--with-odp-lib=...` if given)
    (`em-odp/pkgconfig/libemodp.pc.in => <install-dir>/lib/pkgconfig/libemodp.pc`)
  - Support for `make dist` and `make distcheck`
    - `make dist`: create em-odp-2.4.0.tar.gz distribution
    - `make distcheck`: validate the distribution,
      i.e. use the .tar.gz: compile, install, clean etc.

- Fixes:
  - GCC 10 compilation fixed
  - pool: incorrect return value used when EM-pool creation failed
  - `em_eo_get_name()` return value used incorrectly
  - Refactoring EM example program startup

## Event Machine (EM) on ODP v2.3.0

### EM API v2.3 - Summary

- Event: multi-event alloc and free functions
- Hooks: use the same hook-type for all allocs and the same for all frees
- EO: multi-event EO-receive function
- Dispatch: one common enter-callback for multi- and single-receive funcs.
- Init: core masks for `input_poll_fn` and `output_drain_fn` added to `em_conf_t`
- Init: use `em_conf_init(em_conf_t *conf)` to init the EM config params.
- Event: APIs to get the event type(s) of multiple events

### EM API v2.3 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=3`

Not backwards compatible with previous API version, API 2.3 contains modified
types requiring small changes to older code (see 2, 4 and 5).

1. Event: multi-event alloc and free functions  
   APIs for allocating or freeing multiple events:
   - `em_alloc_multi()`: Allocate multiple events.  
     Similar to `em_alloc()`, but allows allocation of multiple events, with same
     properties, with one function call.  
     The `em_alloc_multi()` API function will try to allocate the requested number
     (`num`) of events but may fail to do so, e.g. if the pool has run out of
     events, and will return the actual number of events that were successfully
     allocated from the given pool.
   - `em_free_multi()`: Free multiple events.  
     Similar to `em_free()`, but allows freeing of multiple events with one
     function call.  
   See `include/event_machine/api/event_machine_event.h`:
   `em_alloc_multi()` and `em_free_multi()`

2. Hooks: use the same hook-type for all alloc():s and the same for all free():s  
   `em_api_hook_alloc_t`: API-callback hook for `em_alloc()` and `em_alloc_multi()`.
   `em_api_hook_free_t`:  API-callback hook for `em_free()` and `em_free_multi()`.
   Note! `em_api_hook_alloc_t` and `em_api_hook_free_t` *changed* in API 2.3
         to suit both single and multi alloc and frees.

3. EO: multi-event EO-receive function `em_receive_multi_func_t`:  
   Similar to the single-event receive function (`em_receive_func_t`), except
   that multiple events can be passed with one call to the EO receive function.
   A multi-event receive function is taken into use during EO creation with a
   call to `em_eo_create_multircv(...)`. The maximum number of events that the
   multi-event EO receive function is prepared to handle can be set with the
   option `em_eo_multircv_param_t::max_events` given to `em_eo_create_multircv()`.
   The EM dispatcher will split event batches larger than `max_events` into
   chunks of `max_events`.  
   Event group handling: All events passed by the EM dispatcher to the
   EO multi-event receive function belong to the same event group (or none)
   - a batch of events containing multiple event groups is split by the
     dispatcher into smaller chunks, each chunk belonging to the same event group
     (or none). The event group count is decremented by the number of events
     passed to the receive function when execution returns to the dispatcher.  
     See `include/event_machine/api/event_machine_eo.h`:  
     `em_receive_multi_func_t` and `em_eo_create_multircv()`.

4. Dispatch: one common enter-callback for multi- and single-receive funcs.  
   `em_dispatch_enter_func_t`: the dispatch enter callback, called before entering
   an EO-receive function, is changed so that one common callback type can be
   used for both single-event and multi-event EO-receive functions.  
   Note! `em_dispatch_enter_func_t` *changed* in EM API v2.3.  
   See `include/event_machine/api/event_machine_dispatcher.h`:  
   `em_dispatch_enter_func_t` and `em_dispatch_register_enter_cb()`

5. Init: core masks for `input_poll_fn` and `output_drain_fn` added to `em_conf_t`  
   Add the `input_poll_mask` and `output_drain_mask` fields to `em_conf_t`.
   These masks define the logical EM cores on which these functions
   are executed.  
   Note! New fields added to `em_conf_t` in API 2.3, user must initialize these
         added fields - use `em_conf_init()`, see 6.
   See `include/event_machine/platform/event_machine_init.h`:  
   `em_conf_t` and `em_conf_init()`

6. Init: use `em_conf_init(em_conf_t *conf)` to init the EM config params.  
   Using `em_conf_init()` provides better backwards compatibility since
   all options will be set to default values before use.
   All users should call `em_conf_init()` before calling `em_init()`!
   See `include/event_machine/platform/event_machine_init.h`:  
   `em_conf_t` and `em_conf_init()`

7. Event: APIs to get the event type(s) of multiple events  
   Two EM APIs added:  
   - Get the event types of multiple events:  
     `em_event_get_type_multi()`
   - Get the number of events that have the same event type:  
     `em_event_same_type_multi()`
   See `include/event_machine/api/event_machine_event.h`:  
   `em_event_get_type_multi()` and `em_event_same_type_multi()`

### EM v2.3.0 - Implementation changes

- Fix libemodp symbol visibility, only expose API symbols
- New example program: `timer_test_periodic`
  New packet-io programs: `loopback_local_multircv`, `loopback_multircv`
  New performance program: `loop_multircv`

## Event Machine (EM) on ODP v2.2.0

### EM API v2.2 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=2`

Backwards compatible with EM 2.1 API with one EXCEPTION:  
Introducing the new `em_pool_cfg_t::align_offset{}` option into the existing
`em_pool_conf_t` structure is potentially dangerous if these new fields are not
initialized properly. Initializing the whole `em_pool_cfg_t` to `0` with `memset`
before use is safe and equals API 2.1 behaviour.

1. Pool option to set the event payload alignment offset:  
   Add the new `em_pool_cfg_t::align_offset{}` option to be able to set the
   needed payload alignment offset per pool (during EM pool creation with
   `em_pool_create(..., pool_cfg)`).  
   Setting `align_offset.in_use = 0` disables the pool-specific alignment offset
   and equals API 2.1 behaviour.  
   See the documentation for `em_pool_cfg_t` for a thorough explanation.
2. Timer option to use an external timer clock source:  
   Add the `em_timer_clksrc_t::EM_TIMER_CLKSRC_EXT` option.
   Also add the `EM_TIMER_CLKSRC_CPU` option to explicitly request using the
   CPU clock as the timer clock source (behaviour is the same as using
   `EM_TIMER_CLKSRC_DEFAULT`).

### EM v2.2.0 - Implementation changes

- EM config file options - `config/em-odp.conf`
  - Option `pool.align_offset`:  
    (Replaced `pool.alloc_align` with `pool.align_offset`)

    Alignment offset in bytes for the event payload start address.  
    Set the event payload alignment offset for events allocated from
    any pool. This is a global setting concerning all pools.
    A similar, but pool-specific option, is `em_pool_cfg_t:align_offset{}`
    that overrides this global setting for a specific pool when given to
    `em_pool_create(..., pool_cfg)`.

    Use this option to globally adjust the payload layout so that a
    specific part of it can be placed at a needed alignment for e.g.
    HW access.

    The default EM event payload start address alignment is a power-of-two
    that is at minimum 32 bytes (i.e. 32 B, 64 B, 128 B etc. depending on
    e.g. target cache-line size).  
    The `align_offset` option can be used to fine-tune the start-address
    by a small offset to e.g. make room for a small SW header before the
    rest of the payload that might need a specific alignment for direct
    HW-access.  
    Example: setting `align_offset = 8` makes sure that the payload
    *after* 8 bytes will be aligned at minimum (2^x) 32 bytes for all
    pools that do not override this value.

    ```text
    start: base - align_offset
         |
         v
         +------------------------------------------+
         | <----- |  Event Payload                  |
         +------------------------------------------+
                  ^
                  |
                 base (min 32B aligned, power-of-two)
    ```

- New autoconf configure-script options (configure.ac):
  - `--without-programs`  
     Build the EM library without example programs
  - `--with-config-file=FILE`  
     Override the default EM configuration file (default:`config/em-odp.conf`)
     that is used during the configure & build phases to generate the built-in
     default values for the EM runtime-config (note that runtime config options
     can further be changed during startup by setting the `EM_CONFIG_FILE=file`
     variable).  
     The overriding file set with `--with-config-file=FILE` must include all
     EM configuration options (unlike the runtime file that only need to
     include the mandatory and changed options).  
  - `--enable-check-level=VAL(0…3)`  
     Set the compile-time `EM_CHECK_LEVEL`,  
     see `include/event_machine/platform/event_machine_config.h`

## Event Machine (EM) on ODP v2.1.2

- EM config file options - `config/em-odp.conf`  
  EM runtime configuration options added. The user can optionally override
  default config options by setting the environmental variable `EM_CONFIG_FILE`
  to point to an alternative config file with desired values, e.g.:  
  `$ sudo EM_CONFIG_FILE=./my-overrides.conf ./em-application -c 0xfe`  
  The configuration file values in `config/em-odp.conf` are the default values
  set for em-odp during the configure/build phase and the values defined there
  are used unless overridden via the optional `EM_CONFIG_FILE`. A user provided
  config file does not have to include all available options, only the mandatory
  options as well as the options that the user wants to change can be included
  (while the missing options will be replaced with the default build-time
   values).
  - Option `pool.statistics_enable`:  
    Enable EM pool usage statistics collection during runtime (`true/false`).  
    Pool usage statistics is updated e.g. during `em_alloc()` and `em_free()`,
    thus, if enabled, inducing some overhead. Pool information can be
    obtained by calling `em_pool_info()` and, if pool usage statistics is
    enabled, will return also pool usage statistics.
  - Option `pool.alloc_align`: (changed to `pool.align_offset` in v2.2.0)  
    Alloc alignment in bytes, default = `0` (no extra alignment requirement)  
    Set the exact alignment of the event payload start address (in bytes).
    The event payload address returned by em_event_pointer(event) will
    have the this alignment. Use power of two values only.
  - Option `queue.min_events_default`:  
    Default minimum number of events that a queue can hold.  
    This value will be used in queue creation (em_queue_create*()) if no
    other size information has been provided via the
    em_queue_conf_t::min_events parameter.  
    Setting `min_events_default = 0` will use the odp-implementation`s
    default values (might vary from one odp-implementation to another).
  - Option `event_chaining.num_order_queues`  
    Number of queues used for ordering event-chaining events.  
    Maintain the order of events sent to another device from within
    an EM ordered context (i.e. send from within the EO receive function
    when handling an event from a parallel-ordered queue).  
    An event-chaining queue has no 'real' queue associated with it, the
    queue-id simply indicates that events sent to the queue should be
    sent out of EM via a user specified function (and perhaps out towards
    another device). Sending events out of EM requires some intervention,
    especially sending during an ordered context needs to maintain the
    event order as determined by the context. To maintain event order for
    event-chaining queues, em-odp uses a set of 'real' queues for order
    tracking - the number of these queues is set by this option.  
    Note, 0 = no ordering.

- Event alloc size must be larger than zero, i.e. `em_alloc(sz > 0, ...)`

- Optimizations and performance improvements

## Event Machine (EM) on ODP v2.1.1

- Example programs support graceful `Ctrl-C` termination, see `em-odp/programs`.
  Pressing `Ctrl-C` (once) initiates graceful example program termination instead
  of abruptly killing it. A second press of `Ctrl-C` will always kill the program
  immediately.

- Added the `programs/performance/loop` example that simply loops received events
  back into the queue they originated from and measures the per core
  performance (cycles/core and millions-of-events/core).

- Optimizations and performance improvements

## Event Machine (EM) on ODP v2.1.0

### EM API v2.1 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=1`

Backwards compatible with EM 2.0 API with one EXCEPTION:
All fields of `em_conf_t` given to `em_init()` should be initialized properly
(includes the new `em_conf_t::api_hooks` field).

1. EO-start buffering of sent events changed to apply only to scheduled queues:  
   Events sent to scheduled queues from an EO-start function are buffered.
   The buffered events will be sent into the queues when the EO-start
   functions have returned - otherwise it would not be possible to send events
   to the EO's own queues as the EO is not yet in a started state. No buffering
   is done when sending to queues that are not scheduled.
   See `include/event_machine/api/event_machine_eo.h`

2. EM API-callback hooks:  
   EM API-callback hook functions can be registered for a selected set of
   EM APIs. The EM APIs in question are mostly fast path APIs, like `em_send()`,
   `em_alloc()` and `em_free()`. Control APIs generally do not need hook support.
   A registered user provided hook function will be called by EM each time the
   corresponding API is called.  
   API-callback hooks enables the user to gather statistics, trace program and
   event flow etc. API hooks should not change the state of the events etc.
   they receive as arguments, nor should they call the same API from within the
   hook to avoid hook recursion.  
   Hook support is only available when `EM_API_HOOKS_ENABLE != 0`.
   Currently hook functions can be registered/unregistered with:
     `em_hooks_register_alloc()` and `em_hooks_unregister_alloc()`
     `em_hooks_register_free()` and `em_hooks_unregister_free()`
     `em_hooks_register_send()` and `em_hooks_unregister_send()`  
   Hooks can also be registered during EM initialization by setting
   `em_conf_t::api_hooks` when calling `em_init()`.
   Note: `EM_API_HOOKS_ENABLE` must be set to `1` to enable API-hooks (default=`1`)  
   See `include/event_machine/platform/event_machine_hooks.h`

3. EM pool info and statistics:  
   `em_pool_info()` - Retrieve information about an EM event pool  
   `em_pool_info_print()` - print EM Pool information for a given event pool  
   `em_pool_info_print_all()` - print EM Pool information for all event pools  
   Note: Set EM config file: `pool.statistics_enable=true` to enable event pool
   statistics to ge gathered (default: `false`), otherwise only basic info is
   output omitting pool usage information.  
   See `include/event_machine/platform/event_machine_pool.h`

### EM v2.1.0 - Implementation changes

- the `EM_DEBUG` `#define` has been removed and the behaviour made default.
- EM Timer: check the timer attributes against odp-timer capabilities during
  timer creation.
- Detect packets from outside of EM and initialize them as events before further
  EM processing.

## Event Machine (EM) on ODP v2.0.0

### EM API v2.0 - Details

`EM_VERSION_API_MAJOR=2`, `EM_VERSION_API_MINOR=0`

 1. Introducing local queues for fast pipelining: `EM_QUEUE_TYPE_LOCAL`  
    Local queues are special virtual queues bypassing the scheduler for
    fast core-local pipelining without load balancing or atomic processing.
    Use `em_sched_context_type_current()` to determine the inherited scheduling
    context type (none, atomic or ordered) when handling events from a
    local queue.  
    See `event_machine_queue.h and event_machine_scheduler.h`
 2. Introducing output queues for system specific implementation of SW-HW
    interfaces, e.g. towards packet output or accelerators: `EM_QUEUE_TYPE_OUTPUT`  
    See `event_machine_queue.h`
 3. Add optional `conf` argument also to atomic-group queue creation for
    attributes and platform specific configuration.  
    See `event_machine_atomic_group.h`.
 4. Add optional `conf` argument to EO start for passing configuration.  
    See `event_machine_eo.h`.
 5. Add alternative synchronous (blocking) versions of "asynchronous
    API functions with notifications" to make it possible to do a simple robust
    startup & shutdown.  
    The synchronous API-functions do not return until the operation has been
    fully completed on all cores (the asynchronous APIs with notifications might
    return while the operation is still ongoing, thus notifications should be
    used to signal completion).  
    Synchronous APIs are named with the `_sync` ending:  
        - `em_eo_add_queue_sync()`  
        - `em_eo_remove_queue_sync()`,  
        - `em_eo_remove_queue_all_sync()`, set arg `delete_queues = true` to also
                                           delete all of the EO's queues.  
        - `em_eo_start_sync()`  
        - `em_eo_stop_sync()`  
        - `em_queue_group_create_sync()`  
        - `em_queue_group_delete_sync()`  
        - `em_queue_group_modify_sync()`  
    Note that the other cores must be running the dispatch loop when a core is
    calling a `_sync()` function, otherwise the `_sync()`-operation might never
    complete.  
    See `event_machine_eo.h` and `event_machine_queue_group.h`.
 6. Simplify queue and EO management - Queue enable and disable is removed
    and handled implicitly by EO-add-queue or EO-remove-queue calls.  
    See `event_machine_queue.h` and `event_machine_eo.h`.
 7. Events sent from within an EO-start function are buffered and sent
    after the start-operation has completed. Otherwise it would not be
    possible to reliably send events from the EO start-function to the
    EO's own queues.
 8. Add further `_find()` functions: atomic group, queue, EO and pool.
    Can be used to lookup objects by name.
 9. Add `em_queue_current()` and `em_eo_current()` to obtain the currently
    active queue or EO.
10. Add `em_eo_get_context(eo)` to return the EO context of an EO.
11. Add functions to iterate through all objects:
      - EOs & associated queues:
           - `em_eo_get_first()`, `em_eo_get_next()`
           - `em_eo_queue_get_first()`, `em_eo_queue_get_next()`
      - queue groups & associated queues:
           - `em_queue_group_get_first()`, `em_queue_group_get_next()`
           - `em_queue_group_queue_get_first()`, `em_queue_group_queue_get_next()`
      - atomic groups & associated queues:
          - `em_atomic_group_get_first()`, `em_atomic_group_get_next()`
          - `em_atomic_group_queue_get_first()`, `em_atomic_group_queue_get_next()`
      - queues:
          - `em_queue_get_first()`, `em_queue_get_next()`
      - event groups:
          - `em_event_group_get_first()`, `em_event_group_get_next()`
      - pools:
          - `em_pool_get_first()`, `em_pool_get_next()`

    See each header for details (`_get_first()`, `_get_next()`)

12. Slight EM header file reorganization.
    - EM init&term APIs moved into their own header file:  
      `include/event_machine/platform/event_machine_init.h`
    - EM Event Pool APIs moved into their own header file:  
      `include/event_machine/platform/event_machine_pool.h`
    - em_dispatch() moved into the 'dispatcher' API:  
      `include/event_machine/api/event_machine_dispatcher.h`
    - em_event_pointer() moved into the 'event' API:  
      `include/event_machine/api/event_machine_event.h`

This is a larger enhancement release with new or modified arguments requiring
changes to legacy code. All main features remain.

### EM v2.0.0 - Implementation changes

- Example/test application updated to use API v2.0.
- New examples for new API v2.0 features:
  - Local Queues:  
    `programs/example`: `queue/queue_types_local.c`  
    `programs/packet_io`: `loopback_local.c`, `multistage_local.c`
    `programs/performance`: `queues_local.c`
  - Output Queues:  
    All packet I/O examples have been updated to use output queues.
  - Sync APIs:  
    Example start-up has been modified to use _sync() versions of APIs.  
    etc.

## Event Machine (EM) on ODP v1.4.0

### EM API v1.4 - Details

`EM_VERSION_API_MAJOR=1`, `EM_VERSION_API_MINOR=4`

1. Add a queue configuration argument to `em_queue_create/_static(..., *conf)`.  
   The `conf` argument is optional (can be NULL) and can be used to pass extra
   attributes to the system specific implementation.  
   See `em_queue_conf_t` and the supported `conf->flags` in
   `include/event_machine/api/event_machine_types.h`:
   - `conf->min_events can` be used to request the 'minimum number of events' that
     a queue should be able to contain.
   - `conf->flags` can be used to request certain properties for the queue.  
     At the time of writing odp supports these flags only for unscheduled queues.
     An unscheduled queue can be created as 'single-producer & single-consumer'
     by setting the following flags:  
     `unsch_conf->flags = EM_QUEUE_FLAG_ENQ_NOT_MTSAFE;`  
     `unsch_conf->flags |= EM_QUEUE_FLAG_DEQ_NOT_MTSAFE;`  
     An unscheduled queue can be created as 'non-blocking & lock-free' by setting
     the following flag:  
     `unsch_conf->flags = EM_QUEUE_FLAG_NONBLOCKING_LF;`

2. Event Pool name added - event pools can be given a name during creation and
   that name can be used to find the event pool handle later.  
   The pool name given to `em_pool_create(name, ...)` can be used later on any
   core to obtain the event pool handle. `em_pool_find()` and `em_pool_get_name()`
   are new functions related to the added `name`.

## Event Machine (EM) on ODP v1.3.0

### EM API v1.3 - Details

`EM_VERSION_API_MAJOR=1`, `EM_VERSION_API_MINOR=3`

 1. Enhancement and clarification of ordered queue behavior. New clarified
    ordering concept is similar to what ODP currently defines. See queue
    documentation for details.
 2. Added add-ons - directory for extra APIs not part of the standard base
    definition. See README at add-ons/
 3. Clarification of event group behavior for exceptional situations. See
    event group documentation.
 4. New API to send multiple events (`em_send_multi()`, `em_send_group_multi()`)
    for potential performance gain.
 5. New API to receive multiple events from unscheduled queues.
 6. Event timer API. See `event_machine_timer.h`
 7. Added API to change event type (if compatible). See `event_machine_event.h`
 8. UNDEF - values of -1 (FFFFFFFF) deprecated. Implementation should use
    value 0 for any UNDEF. This makes it safer to initialize data to 0.

Backwards compatible.

### EM v1.3.0 - Implementation changes

- Support for EM API v1.3 (`em-odp/include`)
- Support for EM add-ons APIs, read about add-on APIs in
  em-odp/include/event_machine/add-ons/README.
- EM event timer API v1.0.
  See new event timer examples/tests in `em-odp/programs/example/timer/`

## Event Machine (EM) on ODP v1.2.3

- EO: `em_eo_start()`: if EO local-start functions are used: enable all the EO's
  queues only after the local-start funcs have been successfully run on all
  cores.

- EO: `em_eo_stop()`: the EO's global-stop function should be run only after all
  local-stop functions have completed on all cores. The EO's global-stop
  function can only be called after each core has seen and handled the
  stop-request, even if no local-stop functions are used, to ensure that no
  core is still processing EO-related code.

- EO: `em_eo_delete()` should remove all the remaining queues from the EO and
  delete them before deleting the actual EO.

- bugfix: queue group: incorrect check for zero mask during queue group delete.
  Calling `em_queue_group_delete()` could lead to an incorrectly reported error
  if the core mask was not modified to zero before deleting the queue group.
  Corrected.

- event group: add the `EM_EVENT_GROUP_SAFE_MODE` option (enabled by default).  
  Guards event groups in undefined and error situations.
  Excess and aborted group events don't belong to a valid group when received.
  Most event group APIs check if the core local event group has expired during
  the receive function. Impacts performance when event groups are used.
  Set `EM_EVENT_GROUP_SAFE_MODE` to `0` to disable.

- misc: code clean-ups.

## Event Machine (EM) on ODP v1.2.2

- ARM compilation and event alignment issues fixed

- EM packet pool size and packet pool buffer size defined in configuration
  parameters (`event_machine_config.h`)

- Build system changes
  - Installed header files added in library makefile
  - Shared object version information added
  - Unused package version number removed
  - Minor gitignore fix

## Event Machine (EM) on ODP v1.2.1

- `em_queue_group_get_name()` implemented

- Strict style checking taken in use
  - Source files modified accordingly

- ODP specific API extensions implemented for packet I/O
  - EM internal includes removed from packet I/O setup

- Requires ODP v1.10.0.0 / v1.10.1.0

## Event Machine (EM) on ODP v1.2.0

### EM API v1.2 - Details

`EM_VERSION_API_MAJOR=1`, `EM_VERSION_API_MINOR=2`

 1. Introduced a slightly modified header structure (headers by service group).
    Application only needs to include `event_machine.h`. Event group is now also
    part of the standard API.
 2. Added possibility to explicitly signal end of event group handling,
    see `em_event_group_processing_end()`
 3. Added possibility to assign a received event to an event group,
    see `em_event_group_assign()`
 4. Added recovery to event groups,
    see `em_event_group_abort()`, `em_event_group_get_notif()`
 5. Added a hint to the scheduler, see `em_preschedule()`
 6. Added common dispatcher callbacks for debug, statistics or new services,
    see `em_dispatch_register_enter_cb()`, `em_dispatch_unregister_enter_cb()`,
        `em_dispatch_register_exit_cb()`,  `em_dispatch_unregister_exit_cb()`
 7. Added APIs for unscheduled queues along with a new queue type,
    see `em_queue_dequeue()`, `EM_QUEUE_TYPE_UNSCHEDULED` (arg to `em_queue_create()`)
 8. Added API call to ask for the event size, see `em_event_get_size()`
 9. Added missing APIs to ask for the names of objects (like queue groups)
10. Clarified documentation

Backwards compatible.

### EM v1.2.0 - Implementation changes

- First EM implementation on top of ODP, providing the same example
  applications as OpenEM.

- Support for EM API v1.2
  - `em_queue_group_get_name()` not yet implemented, waiting for ODP support

- Requires ODP v1.9.0.0

## Event Machine (EM) on ODP v1.1.0

### EM API v1.1 - Details

`EM_VERSION_API_MAJOR=1`, `EM_VERSION_API_MINOR=1`

1. Atomic Group concept added (see `event_machine_atomic_group.h`)
2. Option to send notifications to an event group added.  
   See declaration of `em_notif_t`.
3. `em_event_group_is_ready()` added and
   clarified usage rules of `em_event_group_apply()`
4. Minor documentation clarifications

Backwards compatible with one EXCEPTION (item 2):  
Introducing a new input field to an existing structure is potentially
dangerous to existing code behavior as it is not initialized unless the
application initialized the structure with `0` using e.g. `memset()` with
`sizeof(em_notif_t)`.
`EM_EVENT_GROUP_UNDEF` or value `0` as `.egroup` should cause the legacy
behaviour in all implementations.

## Event Machine (EM) on ODP v1.0.0

### EM API v1.0 - Details

`EM_VERSION_API_MAJOR=1`, `EM_VERSION_API_MINOR=0`

Initial API
