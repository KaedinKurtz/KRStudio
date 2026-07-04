# KRStudio Multi-Layer Nodes — subgraphs and saved custom nodes (`.knode`)

Status: **design only** (no code yet). This doc pins the contracts so the implementation extends
the existing node system (`krs::nodes`, `NodeFactory`, `Node`, `evaluateGraphQuiet`) and the `.k*`
save family (see [SAVE_ARCHITECTURE.md](SAVE_ARCHITECTURE.md)) instead of inventing parallel rules.
Read it before writing subgraph code; the numbered CONTRACTs are the load-bearing part.

## 0. What exists today (the ground we build on)

- **`Node`** (`include/NodeHeaders/Node.hpp`): a backend compute unit. Owns `m_ports`
  (`std::vector<Port>`, each Input/Output with a rich `DataType{name,unit}`), `m_params`
  (`std::map<std::string,std::any>`, the tunable in-node-widget state), a `compute()` override, and
  the `process()` gate policy (Async / Sync / Triggered). `getInput<T>` reads a live connection
  packet, else the port's `literalValue` (the in-node widget's typed value), with numeric / vector /
  transform coercion. `setScene(Scene*)` injects the live ECS for bridge nodes.
- **`NodeFactory`** (singleton): maps a unique **string type-id** → `{NodeDescriptor, creation-fn}`.
  Every node registers itself from a static registrar at load (see `BridgeNodes.cpp`). This is the
  ONLY way a node type comes into existence, and it is what the "Add Node" menu enumerates.
- **`NodeDelegate`** wraps a backend `Node` for QtNodes; the visual graph is a
  `QtNodes::DataFlowGraphModel`. Ports are addressed **positionally** (nth-of-direction) at the
  QtNodes boundary — see `nthPortName` / `nthOutPacket` in `EvalEngine.cpp`.
- **`evaluateGraphQuiet(model)`** (`EvalEngine.cpp`): Kahn topo-sort over `allNodeIds()`; for each
  node in order calls `n->process()` then copies each output packet to the connected downstream
  input via `setInput`. **No QtNodes signals, no repaint — pure CPU, microseconds.** A cycle guard
  best-effort-processes any nodes left unreached. This is the evaluator a subgraph reuses verbatim.
- **`.k*` save family**: reusable definitions referenced by `{ref, id, contentHash}`, versioned
  `"format":"<family>/<major>"`, content-addressed, "recognize-as-different" on hash mismatch, part
  libraries as a search path. `.knode` joins this family.

A subgraph introduces **no new evaluation primitive** — it is a `Node` whose `compute()` runs a
nested `DataFlowGraphModel` through the *same* `evaluateGraphQuiet`. That is the whole idea.

---

## 1. Concept: the subgraph node = a graph wearing a node's clothes

A **subgraph node** is a single `Node` in an outer graph whose *internals are themselves a node
graph*. It has declared Input and Output ports like any node; each of those ports maps to an
**interior boundary node** — an `Input Proxy` (interior source) or an `Output Proxy` (interior
sink). Semantically it is a **function**: exposed inputs are parameters, exposed outputs are return
values, the interior is the body. Encapsulation + reuse; the outer graph neither sees nor can wire
the interior.

Because the interior is a graph and a subgraph node is a graph node, subgraphs **nest to arbitrary
depth** — a subgraph may contain subgraph nodes, each with its own interior. The runtime object is a
tree of `DataFlowGraphModel`s.

Two new built-in node types, registered in `NodeFactory` like everything else:

- **`subgraph/input`** — an interior **source**. One output port whose `{name, DataType}` becomes
  one exposed *input* on the parent. Its output value each eval is whatever the parent marshaled in.
  Carries `promoted.portName` + `promoted.order` (see §3) so port identity is stable.
- **`subgraph/output`** — an interior **sink**. One input port whose `{name, DataType}` becomes one
  exposed *output* on the parent. Its last-seen input value is marshaled out each eval.

The container node type itself:

- **`subgraph/container`** — a `Node` subclass (`SubgraphNode`) that OWNS an interior
  `DataFlowGraphModel` (+ the backend `Node`s inside it), rebuilds its own `m_ports` from the
  interior's `subgraph/input` and `subgraph/output` proxies (§3, ordered), and whose `compute()`
  evaluates the interior (§4). It reuses `Node::changePorts` / `reconfigurePorts` to mutate its
  exposed port set when a promote/unpromote happens, so QtNodes cleans up stale connections exactly
  as the Property node does today.

> **CONTRACT 1.1** A subgraph node's exposed port set is a *pure projection* of its interior proxy
> nodes: exactly one exposed Input per `subgraph/input`, one exposed Output per `subgraph/output`,
> in the proxies' declared `order`. Nothing else is exposable. There is no hidden side channel; the
> only data crossing the boundary crosses through a proxy.

> **CONTRACT 1.2** A subgraph node is indistinguishable from an ordinary node to the outer graph and
> to `evaluateGraphQuiet` — same `getPorts()`, same `process()`/`compute()` contract, same
> coercion. The outer evaluator never knows it stepped into a nested graph.

A **saved** subgraph (§2) additionally gets its OWN `NodeFactory` type-id
(`knode:<id>`) so an instance can be dropped from the Add-Node menu like any built-in — that is the
"custom node" feature.

---

## 2. The `.knode` file — a saved custom node

A `.knode` file is a versioned JSON document (`"format": "knode/1"`) capturing one subgraph
*definition* so it can be reused across scenes and shared. It obeys the SAVE_ARCHITECTURE reference
discipline verbatim: **relative `ref` + stable `id` + `contentHash`**, versioned format, unknown
major refused, recognize-as-different on hash mismatch.

```json
{ "format": "knode/1",
  "id": "6f1c…-uuid",                      // stable identity, minted at first Save (survives rename)
  "name": "PID + Clamp",
  "category": "Control/Custom",            // where it lands in the Add-Node menu
  "description": "PID controller with output saturation.",
  "revision": 3,                            // bumped each Save (human-facing; identity is id+hash)

  "exposed": {                              // the port façade, ORDERED — this is the node signature
    "inputs":  [ { "port": "sp",  "type": {"name":"double","unit":"unitless"}, "proxy": "n_in_sp"  },
                 { "port": "pv",  "type": {"name":"double","unit":"unitless"}, "proxy": "n_in_pv"  } ],
    "outputs": [ { "port": "u",   "type": {"name":"double","unit":"unitless"}, "proxy": "n_out_u"  } ] },

  "nodes": [                                // interior nodes (INCLUDING the proxies)
    { "nid": "n_in_sp", "type": "subgraph/input",  "params": {}, "geom": [ -320, -40 ] },
    { "nid": "n_in_pv", "type": "subgraph/input",  "params": {}, "geom": [ -320,  60 ] },
    { "nid": "n_err",   "type": "math/subtract",   "params": {}, "geom": [ -120,  10 ] },
    { "nid": "n_pid",   "type": "control/pid",
      "params": { "kp": 2.0, "ki": 0.5, "kd": 0.1 },             // in-node widget values (m_params)
      "literals": { "Setpoint": 0.0 },                          // unconnected input port literals
      "policy": { "update": "Asynchronous" },                   // Node::UpdatePolicy (+ triggerEdge)
      "geom": [ 60, 10 ] },
    { "nid": "n_clamp", "type": "math/clamp",
      "params": { "lo": -5.0, "hi": 5.0 }, "geom": [ 240, 10 ] },
    { "nid": "n_out_u", "type": "subgraph/output", "params": {}, "geom": [ 440, 10 ] } ],

  "nested": [                               // any INTERIOR subgraph instance references (see below)
    { "nid": "n_filter", "ref": "nodes/lowpass.knode", "id": "…", "contentHash": "…" } ],

  "connections": [                          // interior wiring, by (nid, port NAME) — never index
    { "from": ["n_in_sp","out"], "to": ["n_err","A"] },
    { "from": ["n_in_pv","out"], "to": ["n_err","B"] },
    { "from": ["n_err","result"],"to": ["n_pid","Error"] },
    { "from": ["n_pid","Output"],"to": ["n_clamp","in"] },
    { "from": ["n_clamp","out"], "to": ["n_out_u","in"] } ] }
```

Field discipline:

- **`nid`** — interior-local node id, unique within THIS file only. Connections and the `exposed`
  proxy map reference nodes by `nid`, never by array index (index is fragile across edits).
- **connections reference ports by NAME**, not positional index. Rationale: `.knode` is authored and
  round-tripped as data; a port-name reference survives a node type gaining/reordering ports far
  better than the positional `outPortIndex`/`inPortIndex` that QtNodes uses at runtime. The loader
  resolves name → current positional index when it materializes the interior (§4/§6).
- **`params`** persists `Node::m_params` (the in-node widget state). **`literals`** persists each
  unconnected input port's `literalValue`. **`policy`** persists `UpdatePolicy` + `TriggerEdge`.
  Together these three are the complete authored state of an interior node — matching what a scene
  save must already capture for a flat graph.
- **`geom`** is editor layout (x,y). Cosmetic; a missing/garbage `geom` never blocks a load
  (auto-layout fallback), consistent with the family's "cosmetic data is best-effort" stance.
- **`nested`** holds `{ref,id,contentHash}` for interior nodes that are THEMSELVES saved subgraphs.
  This is the recursion in the file format; it is the same reference triple as `.kscene`→`.krobot`.

> **CONTRACT 2.1** A `.knode` is a DEFINITION (library/commit layer, like `.krobot`/`.kmotor`), not
> an instance and not session state. Its `contentHash` is the sha1 of the file bytes. Instancing it
> in a scene stores `{ref, id, contentHash}`; on load a hash mismatch means "the custom node changed
> on disk" → **load the new content and report it** (never silent, never refuse for a mismatch
> alone). Same rule as every other cross-file link in the family.

> **CONTRACT 2.2** Instances carry NO interior override. A `.knode` instance's only scene-local
> state is (a) its outer wiring, (b) the literal/param values of ITS OWN exposed input ports (a
> subgraph node is still a node with in-node widgets on unconnected inputs). It does NOT store a
> patched copy of the interior — to change the interior you Save a new revision of the definition
> (new `contentHash`, same or new `id`). This keeps "shared definition, many instances" honest, and
> mirrors §2.1's family rule that definitions are never silently forked per-instance.

> **CONTRACT 2.3** `exposed.inputs`/`exposed.outputs` order IS the node's port order and thus part of
> its signature. Reordering them is a breaking signature change (outer connections are positional at
> the QtNodes layer); adding a NEW exposed port at the END is non-breaking. Treat `exposed` order the
> way you treat a function's parameter order.

---

## 3. Port promotion (interior proxy ↔ exposed port)

A `subgraph/input` proxy's single output declares one exposed *input*; a `subgraph/output` proxy's
single input declares one exposed *output*. The proxy stores:

- `promoted.portName` — the exposed port's name (defaults to the proxy's own port name; editable).
- `promoted.type` — the exposed `DataType`. Defaults to the interior wire's type; overridable to a
  wider unified type (e.g. `"number"`/`"vector"`) so coercion (Node.hpp) still applies across the
  boundary.
- `promoted.order` — position in the exposed list (§2.3).

`SubgraphNode` rebuilds `m_ports` by scanning its interior for these proxies in `order`. Promoting a
port = adding a proxy + wiring it; unpromoting = deleting the proxy. Both go through
`Node::changePorts(...)` so QtNodes brackets the port mutation and severs now-dangling outer
connections — the exact mechanism the Property node already uses to reshape its ports at runtime.

> **CONTRACT 3.1** Exposed-port NAME and TYPE are owned by the proxy, not the container. Two proxies
> may not share an exposed name within one direction (names disambiguate at the marshaling boundary,
> §4). The container's `m_ports` is regenerated from proxies; it is never hand-edited.

---

## 4. Evaluation — recursive quiet topo-sort, marshaled at the boundary

`SubgraphNode::compute()`:

1. **Marshal in.** For each exposed input port, read it with `getInput<PortDataPacket>`-equivalent
   (the connection packet, else the in-node literal) and `setInput` it onto the matching
   `subgraph/input` proxy inside the interior. Matching is by `promoted.portName`.
2. **Evaluate interior.** Call `krs::nodes::evaluateGraphQuiet(interiorModel)` — the SAME evaluator,
   one level down. It topo-sorts the interior, runs each `compute()`, propagates packets. The proxies
   are ordinary nodes to it: `subgraph/input` just re-emits the marshaled value; `subgraph/output`
   just latches its input.
3. **Marshal out.** For each exposed output port, read the matching `subgraph/output` proxy's latched
   input and `setOutput` it on the container so the OUTER `evaluateGraphQuiet` propagates it
   downstream.

Because step 2 is the identical function, nesting is automatic: a subgraph two levels deep is just a
container whose interior contains a container, evaluated depth-first by recursion through `compute()`.

> **CONTRACT 4.1 (determinism).** One outer eval = exactly one interior eval per subgraph instance,
> in outer topo order. The interior sees a consistent input snapshot (all inputs marshaled BEFORE the
> interior runs) and produces its outputs from exactly that snapshot. No interior node runs more or
> fewer times than a flattened-equivalent graph would. Given identical inputs + params, output is
> bit-identical run to run (the flat graph already guarantees this; the subgraph must not weaken it).

> **CONTRACT 4.2 (stateful interior).** A subgraph may legally contain stateful nodes (a PID
> integrator, a Triggered node, a low-pass filter holding `m_params`). Its interior model + backend
> nodes PERSIST across evals (they are owned by the `SubgraphNode`, not rebuilt per `compute()`), so
> that state accumulates exactly as it would in a flat graph. Marshaling copies VALUES across the
> boundary; it never resets interior node state.

> **CONTRACT 4.3 (cycle refusal across layers).** A subgraph node participates in the OUTER graph as
> a single vertex; the outer topo-sort's cycle guard (`EvalEngine.cpp`) is unchanged. The INTERIOR
> must itself be a DAG — its own `evaluateGraphQuiet` applies the same best-effort cycle guard. A
> connection from an exposed OUTPUT back to an exposed INPUT of the SAME instance in the outer graph
> is an outer-graph cycle and is caught by the outer guard exactly as today; it can never wedge the
> interior because in/out marshaling is one-shot per eval. **A subgraph may not (transitively)
> contain itself** — see §6/CONTRACT 6.2; that is checked at instantiation, not eval, so eval never
> needs to detect infinite nesting.

> **CONTRACT 4.4 (scene injection).** `SubgraphNode::setScene(s)` propagates `s` to every interior
> node before eval (bridge nodes like `SceneContextNode` need the live ECS). A subgraph is therefore
> a legal home for bridge/physics nodes; the interior reaches the same single ECS the outer graph
> does. No sandboxing of the registry — a subgraph is an organizational unit, not a security
> boundary.

---

## 5. The library angle — `.knode` in `krs::parts`

`.knode` lives on the **PartLibrary search path** exactly like `.kmotor` / `.kactuator`
(`<scene>/parts` → `<project>/parts` → `<user>/parts`, most-specific-wins, ids union in — see
SAVE_ARCHITECTURE §Parts). This is what turns a subgraph from a one-scene convenience into a shared,
reusable **node block**:

- A user builds a "PID + Clamp" block, a "sensor → filter → estimate" block, an "IK-with-limit-
  avoidance" block, Saves it as `.knode`, and it appears in every scene's Add-Node menu under its
  `category`.
- On library scan, each discovered `.knode` **registers a `NodeFactory` type**
  (`type-id = "knode:<id>"`, `NodeDescriptor{name, category, description}` straight from the file).
  Creating it instantiates a `SubgraphNode` that lazily loads + materializes the interior from the
  file (resolving any `nested` refs recursively). Thus custom nodes and built-ins share ONE
  registration path and ONE Add-Node menu.
- The Manufacturer/Parts panel lists `.knode` rows alongside motors/actuators; a row drags with the
  same `application/x-krstudio-asset` mime the node canvas already accepts, so dropping a custom node
  is drag-drop like dropping an actuator on a joint.
- **Repository tie-in.** `.knode` is content-addressed in `PartRepository` keyed by `(id, hash)` —
  de-dupes across scenes, a changed block is a NEW version, and the `RemoteRepository` HTTP contract
  (`GET /index`, `GET /part/<id>/<hash>`, `POST /part`, Bearer auth) carries `.knode` bytes unchanged
  once a live backend lands. Sharing a node block is `POST /part`; using someone's is a resolved
  `{ref,id,contentHash}`.

> **CONTRACT 5.1** A `.knode` in the library is a DEFINITION on the commit/library layer. Its
> discovered `NodeFactory` id is `"knode:"+id` (stable across renames of the file). Two files with
> the same `id` but different `contentHash` are the SAME node at DIFFERENT revisions — most-specific
> path wins for which revision the menu offers, but an already-placed instance keeps its
> scene-recorded `contentHash` and reports "changed on disk" if the resolved file differs (§2.1).

---

## 6. UI sketch (described, not specced)

- **Collapse selection into subgraph.** Select N nodes + their internal wiring → "Group into
  Subgraph". The tool moves those nodes into a fresh interior model, converts every wire that
  CROSSED the selection boundary into a proxy (incoming cut → `subgraph/input`; outgoing cut →
  `subgraph/output`), and drops a single `SubgraphNode` in their place with the outer wires
  reconnected to the new exposed ports. Inverse: **Expand/Ungroup** splices the interior back into
  the parent, deleting the proxies and reconnecting through them.
- **Double-click to enter.** Double-clicking a subgraph node pushes into its interior model (the
  canvas swaps to the interior `DataFlowGraphModel`). The proxies render as pinned left/right edge
  nodes so the boundary is visible.
- **Breadcrumb navigation.** A path bar — `Scene ▸ PID+Clamp ▸ Lowpass` — reflects the enter-stack;
  clicking a crumb pops back to that layer. Each crumb is one nested model.
- **Promote port.** Right-click an interior node's port → "Promote to Subgraph Input/Output" spawns
  the proxy, wires it, and (via `changePorts`) grows the container's exposed ports live. "Rename
  exposed port" edits `promoted.portName`; "Reorder" edits `promoted.order`.
- **Save as Custom Node.** On a subgraph node → "Save to Library…" writes the `.knode` (§2) into the
  chosen library folder and registers its `NodeFactory` type so it is immediately in the Add menu.
- **Edit-in-place vs. definition edit.** Entering a *library-instanced* subgraph opens it READ-ONLY
  with an "Edit Definition (new revision)" affordance — editing forks a new `contentHash` per §2.2,
  it never silently mutates the shared file underneath other instances.

> **CONTRACT 6.1** Collapse/expand is a pure refactor: the flattened graph before Collapse and after
> the equivalent Expand computes identical outputs for identical inputs (it only re-homes nodes +
> inserts pass-through proxies). This is directly gateable (§7, KG1).

> **CONTRACT 6.2 (recursion guard).** Instantiating a `.knode` resolves its `nested` refs
> transitively; if resolution reaches the definition currently being materialized (by `id`), refuse
> the instance with a report — a custom node may not contain itself at any depth. Checked at
> instantiation/load, never at eval (§4.3).

---

## 7. Phased build order (each phase independently gateable)

Each phase ships a headless free-function gate `bool runXxxGate()` per the house idiom
(`KRS_<NAME>_SELFTEST` → gate → `_Exit`), pure-CPU, with a NEG-CTRL that fails if the logic is
vacuous. All four evaluators reuse `evaluateGraphQuiet`, so gates build real interior models the way
`NodeGraphGate.cpp` builds flat ones.

**Phase A — Subgraph data model + recursive eval** (`KRS_SUBGRAPH_SELFTEST`, `runSubgraphGate`).
`SubgraphNode`, `subgraph/input|output` proxies, marshal-in/eval/marshal-out over the existing
`evaluateGraphQuiet`. GATE: build a subgraph that computes `(sp-pv)` and assert the exposed output
equals the flat-graph result (CONTRACT 4.1); nest one subgraph inside another and assert the 2-level
result (arbitrary depth); stateful-interior integrator accumulates across evals (4.2). NEG-CTRLs:
an unpromoted interior port produces NO exposed port (1.1); an interior cycle is caught, not wedged
(4.3); marshaling the WRONG type is rejected (coercion boundary), mirroring NodeGraphGate ND4.

**Phase B — `.knode` round-trip** (`KRS_KNODE_SELFTEST`, `runKnodeGate`). Write/read `knode/1`;
serialize a live subgraph (nodes, params, literals, policy, connections-by-name, exposed map) and
re-materialize it. GATE: save → load → evaluate, assert identical outputs (byte-stable params +
port-name resolution); unknown `format` major is REFUSED; a `nested` ref round-trips; NEG-CTRL: a
connection naming a nonexistent port fails loudly, not silently drops a wire; contentHash of a
byte-identical re-save is stable (2.1).

**Phase C — Library + factory integration** (`KRS_KNODELIB_SELFTEST`, `runKnodeLibGate`). Discover
`.knode` on the PartLibrary search path; register `"knode:<id>"` in `NodeFactory`; instance via the
factory. GATE: a `.knode` dropped in `<project>/parts` becomes creatable through
`NodeFactory::createNode("knode:"+id)` and evaluates; most-specific path wins on a same-id/diff-hash
collision and the loser is reported "changed on disk" (5.1); NEG-CTRL: a self-referential `nested`
chain is refused at instantiation (6.2), and an unknown-id create returns `nullptr` (matches
NodeGraphGate ND1's factory-audit rule).

**Phase D — UI (collapse / enter / breadcrumb / promote).** Wire the QtNodes canvas: collapse-
selection, double-click-enter with breadcrumb stack, promote-port via `changePorts`, Save-to-Library.
GATE (headless where possible): the collapse→expand refactor round-trips to an output-identical flat
graph (6.1) using the Phase-A evaluator; promote/unpromote grows/shrinks the exposed port set and
severs dangling outer wires (reuses the Property-node `reconfigurePorts` machinery). Visual polish
(edge-pinned proxies, crumb bar) is eyes-on, out of gate scope.

---

## 8. Open questions / v2 backlog

- **Durable interior node ids across revisions.** Today an instance re-materializes the whole
  interior from the definition; a v2 could carry sparse *instance overrides* on interior params
  (the scene-local override problem SAVE_ARCHITECTURE already flags for `.krobot`) — but only once
  interior nodes have ids stable across a definition edit. Deferred with the same rationale as
  durable BodyId.
- **Live/rate policy of the interior.** Whether a subgraph may declare its interior runs at a
  DIFFERENT eval rate than the outer loop (a sub-loop) — deferred; §4.1 fixes one-eval-per-eval for
  now to keep determinism trivial.
- **Typed generic ports** (a subgraph parameterized over the unified "number"/"vector" ids so one
  block serves float and vector wiring) — the coercion in `Node.hpp` already permits it at the
  boundary; a v2 would surface it in the promote UI.
- **Cross-subgraph shared state / interior singletons** — out of scope; a subgraph is an
  organizational unit over the one shared ECS (4.4), not a new state container.
