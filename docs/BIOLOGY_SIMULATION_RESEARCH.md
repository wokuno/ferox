# Biology And Simulation Research Notes

Date: 2026-04-29

These notes collect biology and simulation research directions for Ferox. The goal
is not to claim wet-lab fidelity, but to keep model ideas grounded in published
mechanisms and to separate active implementation work from research backlog.

## Executive Takeaways

- Ferox is best described as a lattice individual-based / cellular-agent
  hybrid: grid cells represent occupied territory, colonies are stateful agents,
  genomes parameterize behavior, and nutrients/toxins/signals are Eulerian
  fields.
- The biology backlog is already pointed in the right direction: frontier
  sectoring, mechanical pressure, EPS/biofilm transport, chemotaxis, quorum
  sensing, toxin competition, public goods, persistence, and HGT/plasmids are
  all supported by strong literature.
- The highest leverage simulation work is documentation and validation:
  ODD-style model docs, MIASE-style experiment metadata, calibrated pass-band
  provenance, exact-vs-statistical reproducibility policy, and sensitivity
  analysis.
- Several findings are already tracked by open GitHub issues. The main new
  untracked research lane is uncertainty/sensitivity analysis plus pass-band
  provenance for science benchmarks.

## Working Themes

1. Frontier-limited colony expansion and sectoring
2. Mechanical pressure, shoving, EPS, and biofilm morphology
3. Chemotaxis, quorum sensing, and reaction-diffusion fields
4. Competition, public goods, cheating, and toxin/contact warfare
5. Persistence, dormancy, and bet-hedging
6. Horizontal gene transfer and plasmid spread in spatial communities
7. Model documentation, validation, sensitivity analysis, and benchmark design

## Research Findings

### Mechanism Map

| Topic | Literature signal | Ferox mapping | Follow-up |
|---|---|---|---|
| Frontier expansion and sectoring | Expanding microbial fronts amplify drift and create lineage sectors. | Active frontier spread, frontier telemetry, `frontier_genetic_drift` scenario. | Add benchmark notes that distinguish mutation-driven frontier drift from physical-exclusion drift. See `#147`, `#164`, `#166`. |
| Shoving and mechanical pressure | Individual-based biofilm/colony models use local pushing or overlap relaxation to shape fronts. | Current pressure is a scalar crowding proxy, not a displacement solver. | Model pressure as local growth inhibition plus push/relax priority before any continuous mechanics. See `#164` and `#174`. |
| Biofilm and EPS | EPS changes mechanical stability, stress tolerance, resource capture, and transport. | `biofilm_strength`, EPS-like trait cost, and transport docs are present. | Reconcile docs that describe transport APIs not visible in code, then test EPS attenuation. See `#101`, `#163`, `#174`. |
| Chemotaxis | Bacteria can form patterns by sensing nutrient gradients and self-produced attractants. | Nutrient/toxin sensitivity and generic signal influence approximate taxis. | Split nutrient taxis from self-attractant taxis and document the abstraction. See `#165`. |
| Quorum sensing | Autoinducers accumulate with density and trigger thresholded state changes. | `signal_emission`, `signal_sensitivity`, `quorum_threshold`, and activation math exist. | Add hysteresis and quorum-gated trait expression instead of only signal/spread modulation. See `#105`. |
| Toxin/contact competition | Local antagonism, immunity, and non-transitive competition can preserve or collapse diversity. | Toxin production/resistance and border combat are strong hooks. | Add producer/resistant/sensitive archetypes and rock-paper-scissors coexistence tests. See `#166`, `#167`. |
| Public goods and cheating | Costly secretions benefit neighbors and can be exploited by non-producers. | Social-trait costs exist; explicit benefit sharing and cheater metrics are missing. | Add producer fraction, benefit radius, and spatial assortment metrics. See `#167`. |
| Persistence and bet hedging | Persisters are slow-growing or growth-arrested states that can resume after stress. | `is_persister`, dormancy, stress, and activity factors exist, but stale code references absent genome fields. | Move from colony-wide switching to subpopulation stochastic switching. See `#168`, `#174`. |
| HGT and plasmids | Conjugative plasmids couple transfer competence, costs, loss, proximity, and biofilm context. | Contact HGT fields exist; visible implementation is still closer to genome-trait transfer. | Add plasmid cost/loss/transfer kinetics and metrics. See `#102`. |
| Monod-like nutrient uptake | Monod kinetics remain a useful minimal substrate-limited growth abstraction. | Tests cover saturation and diminishing returns; live wiring remains a model backlog item. | Add benchmark sweeps for half-saturation, uptake limits, and growth coupling. See `#101`, `#109`. |
| Validation and benchmarks | ABM/IBM practice favors ODD documentation, pattern-oriented validation, explicit experiment descriptions, and distributional tests for stochastic models. | `SCIENCE_BENCHMARKS.md` and `STATISTICAL_REGRESSION.md` provide a start, but executable benchmark and CTest wiring are incomplete. | Add ODD docs, validation matrix, benchmark provenance, replicate tiers, and no-op-test protection. See `#109`, `#147`, `#173`. |

### Frontier Growth And Sectoring

The strongest current biology fit is frontier-limited expansion. Hallatschek et
al. showed that expanding microbial fronts can create large-scale genetic
segregation from random pioneer effects, even when the starting population is
well mixed. Ferox should keep measuring frontier/core divergence, lineage
retention, sector count, lineage entropy, and active-shell thickness.

Two effects should be documented separately:

- **Physical exclusion:** only frontier cells have empty neighbors and growth
  opportunity, so early occupants locally block later competitors.
- **Genetic drift and mutation:** frontier reproduction samples a small local
  subset, so mutations or inherited traits can "surf" at the edge.

The benchmark implication is that a frontier scenario should be able to run with
mutation disabled and still show physical sectoring, then run with mutation
enabled to measure added trait/genome divergence.

### Mechanical Pressure, Shoving, And Biofilm Shape

Biofilm and colony models often represent cells as individual particles or rods
that grow, overlap, and push each other until local overlaps are reduced. Kreft
et al. and Picioreanu et al. are useful references for individual-based biofilm
modeling, while Farrell et al. emphasize that mechanical interactions can change
front velocity and shape in ways reaction-diffusion fronts alone do not capture.

Ferox is not a continuous cell-mechanics engine. The practical abstraction is:

- compute local crowding/pressure from neighbor occupancy and growth demand
- slow or suppress growth in high-pressure interior regions
- prefer edge/frontier expansion when empty space exists
- optionally apply a bounded push/relax rule only near crowded frontiers

This keeps the model compatible with the current grid architecture while adding
a biologically meaningful distinction between active shell and quiescent core.

### EPS, Matrix, And Biofilm Transport

EPS should be treated as both a cost and a local environmental modifier. The
biofilm literature supports matrix roles in stress tolerance, adhesion, resource
capture, communication, HGT opportunity, and transport alteration. In Ferox
terms, EPS-like investment should affect:

- local nutrient and signal diffusion or attenuation
- toxin penetration and resistance
- persistence/stress response probability
- public-good localization and producer benefit
- HGT/contact opportunity in dense communities

The immediate documentation risk is that some docs describe transport model
APIs or parameters that are not visible in the current source tree. Keep the
research idea, but mark those pieces as planned until code and docs are
reconciled.

### Chemotaxis And Quorum Sensing

Chemotaxis and quorum sensing should remain separate concepts in Ferox docs.
Chemotaxis is directional movement or spread bias from gradients; quorum sensing
is density or signal-threshold state change.

Recommended model split:

- **Nutrient taxis:** bias spread toward better substrate or away from depleted
  regions.
- **Self-attractant taxis:** let colonies create attractant fields that reinforce
  aggregation or patterning.
- **Quorum state:** use signal concentration, local density, memory, and
  hysteresis to gate trait expression.

This avoids routing every collective behavior through one generic signal value.
It also makes benchmarks clearer: chemotaxis scenarios measure front shape and
gradient response; quorum scenarios measure activation latency, wave speed, and
state hysteresis.

### Competition, Public Goods, And Contact Warfare

Toxin and public-good models need locality. Kerr et al. showed that local
interaction can preserve non-transitive diversity, while larger-scale mixing can
collapse coexistence. Griffin et al., West et al., and Nadell et al. support
explicit costs, local relatedness, and cheater dynamics for social traits.

Useful Ferox archetypes:

- producer: pays a cost to secrete toxin or public good
- resistant: pays a cost for immunity/resilience
- sensitive/cheater: avoids producer cost but depends on local context

Useful metrics:

- producer fraction over time
- resistant/sensitive takeover probability
- boundary switch rate
- local relatedness or lineage entropy
- public-good benefit radius
- extinction/coexistence probability across seeds

For contact-dependent mechanisms such as CDI or T6SS-like warfare, keep the
first model adjacency-based: attacker, target, immunity, cooldown, and local
fitness effect. Avoid overfitting to molecular detail unless the project later
needs species-specific realism.

### Persistence And Bet Hedging

Persistence should be modeled as subpopulation heterogeneity rather than a
single colony-wide flag. Balaban et al. support reversible phenotype switching;
Fisher et al. summarize persisters as often slow-growing or growth-arrested
states that can resume after stress.

Recommended abstraction:

- each colony tracks active, dormant, and persister-like fractions
- switching rates depend on stress, nutrients, toxins, and genotype
- persisters trade lower growth for higher survival
- recovery has a lag distribution, not an instant state flip

The stale `simulation_common.c` references to absent persister genome fields are
a documentation/code consistency blocker and should be resolved before deeper
persistence work.

### HGT And Plasmids

HGT should be represented as transfer of mobile elements with costs and loss,
not just wholesale genome blending. Norman et al. frame conjugative plasmids as
major vehicles for shared gene pools; Koraimann and Wagner emphasize transfer
competence, regulation, population heterogeneity, and biofilm coupling.

Recommended metrics:

- plasmid prevalence
- donor/recipient/transconjugant counts
- transfer events per contact opportunity
- plasmid loss rate
- host fitness cost
- trait spread speed through spatial neighborhoods

The benchmark should vary density, EPS/biofilm context, donor fraction, and
plasmid cost to verify that transfer is contact-local and not globally mixed.

### Validation, Reproducibility, And Benchmark Design

Ferox should use three layers of evidence:

1. **Verification:** unit tests and deterministic fixtures prove algorithms do
   what the code claims.
2. **Statistical regression:** fixed seed sets detect behavior drift against
   known project baselines.
3. **Scientific validation:** benchmark outputs match literature-backed
   qualitative or quantitative patterns.

ODD is the right format for model documentation. A Ferox `docs/ODD.md` should
cover purpose, entities/state variables, scales, process schedule, initialization,
inputs, stochasticity, observation metrics, submodels, rationale, and evaluation.

MIASE-style fields should be added to benchmark scenarios:

- `phenomenon`
- `hypothesis`
- `mechanics_exercised`
- `metric_formula`
- `sampling_window`
- `baseline_provenance`
- `runtime_tier`
- `artifact_schema`
- `literature_refs`

Pass bands should be calibrated statistical contracts. Each band should record
baseline `n`, seed set, mean, standard deviation, quantiles, confidence interval,
acceptable effect size, calibration date, commit, platform, and generator
version.

Replicate tiers should be explicit:

- PR smoke: 8-24 fixed seeds for no-obvious-drift checks
- nightly: 100+ seeds for stable distribution summaries
- release/rebaseline: 500-1000+ seeds where runtime permits
- deep calibration: larger runs only when a scenario is being promoted to a
  scientific reference

Exact replay and statistical reproducibility should not be conflated. Exact
replay belongs to small deterministic fixtures with hashed grid/colony summaries.
Stochastic model validation should compare distributions across seed sets.

### Numerical Guardrails

Ferox's explicit field update guard `4 * diffusion + decay <= 1.0` is consistent
with Forward Euler stability intuition for a 2D four-neighbor diffusion stencil
with an added decay term. The next tests should verify:

- pulse spreading remains nonnegative
- total mass is conserved when decay/source terms are disabled
- decay monotonically reduces mass when enabled
- EPS attenuation changes diffusion locally without creating negative fields
- boundary behavior is documented and covered

For future stiff reaction-diffusion work, consider operator splitting or
semi-implicit field updates. For the current grid model, tests and clear
parameter validation are more important than adding a heavier solver.

### Sensitivity And Uncertainty Analysis

Ferox has many coupled knobs, so one-parameter sweeps are not enough. Start with
cheap screening, then deepen only for stable benchmark scenarios:

- Morris screening for broad parameter influence and interactions
- Latin hypercube sampling with PRCC for monotonic relationships
- Sobol/eFAST only for smaller, high-value parameter sets
- replicate means per parameter sample so stochastic variance is visible

This should not gate normal PRs. It should produce CSV/JSON artifacts and inform
which metrics are stable enough to enforce.

## Priority Notes

Near-term documentation and validation:

1. Fix no-op CTest and CI target drift first (`#173`), otherwise benchmark
   pass/fail status is not trustworthy.
2. Add ODD-style model documentation under `#147` or a child issue.
3. Upgrade science benchmark scenario metadata and pass-band provenance under
   `#109` / `#169`.
4. Add a sensitivity/uncertainty lane and calibrated pass-band provenance
   (`#175`).

Near-term model work:

1. Frontier pressure and shoving abstraction (`#164`)
2. Adaptive chemotaxis and quorum gating (`#165`, `#105`)
3. Public goods, cheating, and non-transitive competition (`#167`, `#166`)
4. Persistence subpopulation switching (`#168`, `#174`)
5. Plasmid/HGT kinetics and metrics (`#102`)

## Sources

- Balaban NQ, Merrin J, Chait R, Kowalik L, Leibler S. Bacterial persistence as a phenotypic switch. *Science* 305(5690):1622-1625 (2004). https://doi.org/10.1126/science.1099390
- Borgonovo E, Pangallo M, Rivkin J, Rizzo L, Siggelkow N. Sensitivity analysis of agent-based models: a new protocol. *Computational and Mathematical Organization Theory* 28:52-94 (2022). https://doi.org/10.1007/s10588-021-09358-5
- Budrene EO, Berg HC. Complex patterns formed by motile cells of *Escherichia coli*. *Nature* 349:630-633 (1991). https://doi.org/10.1038/349630a0
- Budrene EO, Berg HC. Dynamics of formation of symmetrical patterns by chemotactic bacteria. *Nature* 376:49-53 (1995). https://doi.org/10.1038/376049a0
- Evans TW, Gillespie CS, Wilkinson DJ. The SBML discrete stochastic models test suite. *Bioinformatics* 24(2):285-286 (2008). https://doi.org/10.1093/bioinformatics/btm566
- Farrell FDC, Hallatschek O, Marenduzzo D, Waclaw B. Mechanically driven growth of quasi-two-dimensional microbial colonies. *Physical Review Letters* 111:168101 (2013). https://doi.org/10.1103/PhysRevLett.111.168101
- Fisher RA, Gollan B, Helaine S. Persistent bacterial infections and persister cells. *Nature Reviews Microbiology* 15:453-464 (2017). https://doi.org/10.1038/nrmicro.2017.42
- Flemming HC, Wingender J. The biofilm matrix. *Nature Reviews Microbiology* 8:623-633 (2010). https://doi.org/10.1038/nrmicro2415
- Flemming HC, Wingender J, Szewzyk U, Steinberg P, Rice SA, Kjelleberg S. Biofilms: an emergent form of bacterial life. *Nature Reviews Microbiology* 14:563-575 (2016). https://doi.org/10.1038/nrmicro.2016.94
- Griffin AS, West SA, Buckling A. Cooperation and competition in pathogenic bacteria. *Nature* 430:1024-1027 (2004). https://doi.org/10.1038/nature02744
- Grimm V et al. Pattern-oriented modeling of agent-based complex systems: lessons from ecology. *Science* 310(5750):987-991 (2005). https://doi.org/10.1126/science.1116681
- Grimm V et al. The ODD protocol for describing agent-based and other simulation models: a second update. *JASSS* 23(2) (2020). https://doi.org/10.18564/jasss.4259
- Hallatschek O, Hersen P, Ramanathan S, Nelson DR. Genetic drift at expanding frontiers promotes gene segregation. *PNAS* 104(50):19926-19930 (2007). https://doi.org/10.1073/pnas.0710150104
- Hallatschek O, Nelson DR. Gene surfing in expanding populations. *Theoretical Population Biology* 73(1):158-170 (2008). https://doi.org/10.1016/j.tpb.2007.08.008
- Hibbing ME, Fuqua C, Parsek MR, Peterson SB. Bacterial competition: surviving and thriving in the microbial jungle. *Nature Reviews Microbiology* 8:15-25 (2010). https://doi.org/10.1038/nrmicro2259
- Kerr B, Riley MA, Feldman MW, Bohannan BJM. Local dispersal promotes biodiversity in a real-life game of rock-paper-scissors. *Nature* 418:171-174 (2002). https://doi.org/10.1038/nature00823
- Koraimann G, Wagner MA. Social behavior and decision making in bacterial conjugation. *Frontiers in Cellular and Infection Microbiology* 4:54 (2014). https://doi.org/10.3389/fcimb.2014.00054
- Kreft JU, Picioreanu C, Wimpenny JWT, van Loosdrecht MCM. Individual-based modelling of biofilms. *Microbiology* 147:2897-2912 (2001). https://doi.org/10.1099/00221287-147-11-2897
- Marino S, Hogue IB, Ray CJ, Kirschner DE. A methodology for performing global uncertainty and sensitivity analysis in systems biology. *Journal of Theoretical Biology* 254(1):178-196 (2008). https://doi.org/10.1016/j.jtbi.2008.04.011
- Miller MB, Bassler BL. Quorum sensing in bacteria. *Annual Review of Microbiology* 55:165-199 (2001). https://doi.org/10.1146/annurev.micro.55.1.165
- Monod J. The growth of bacterial cultures. *Annual Review of Microbiology* 3:371-394 (1949). https://doi.org/10.1146/annurev.mi.03.100149.002103
- Morris MD. Factorial sampling plans for preliminary computational experiments. *Technometrics* 33(2):161-174 (1991). https://doi.org/10.1080/00401706.1991.10484804
- Nadell CD, Drescher K, Foster KR. Spatial structure, cooperation and competition in biofilms. *Nature Reviews Microbiology* 14:589-600 (2016). https://doi.org/10.1038/nrmicro.2016.84
- Norman A, Hansen LH, Sorensen SJ. Conjugative plasmids: vessels of the communal gene pool. *Philosophical Transactions of the Royal Society B* 364:2275-2289 (2009). https://doi.org/10.1098/rstb.2009.0037
- Picioreanu C, Kreft JU, van Loosdrecht MCM. Particle-based multidimensional multispecies biofilm model. *Applied and Environmental Microbiology* 70(5):3024-3040 (2004). https://doi.org/10.1128/AEM.70.5.3024-3040.2004
- Sourjik V, Wingreen NS. Responding to chemical gradients: bacterial chemotaxis. *Current Opinion in Cell Biology* 24(2):262-268 (2012). https://doi.org/10.1016/j.ceb.2011.11.008
- Waltemath D et al. Minimum Information About a Simulation Experiment (MIASE). *PLoS Computational Biology* 7(4):e1001122 (2011). https://doi.org/10.1371/journal.pcbi.1001122
- West SA, Griffin AS, Gardner A, Diggle SP. Social evolution theory for microorganisms. *Nature Reviews Microbiology* 4:597-607 (2006). https://doi.org/10.1038/nrmicro1461
