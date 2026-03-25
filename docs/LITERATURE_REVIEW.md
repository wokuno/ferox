# Literature Review for Colony Simulation

This note summarizes the literature and open-source simulation survey used to
refine Ferox's modeling goals.

It complements `docs/SCIENCE_BIBLIOGRAPHY.md` by turning references into design
implications, benchmark targets, and concrete roadmap questions.

## What the Literature Says Colonies Are Optimizing

Published colony and biofilm work suggests that bacterial collectives are not
optimizing a single scalar such as raw growth rate. Spatial colonies instead
trade off several objectives at once:

- `frontier_gain`: reach open perimeter space where nutrients are fresh and
  mutations can surf the front
- `resource_security`: keep access to substrate while avoiding toxin- or
  waste-heavy pockets
- `kin_locality`: invest in cooperation/public goods when benefits stay local
  enough to reward kin clustering
- `threat_suppression`: deploy toxins, alarms, or defensive states when a
  contestable boundary justifies the metabolic cost
- `insurance`: keep some fraction of the lineage slow-growing, dormant, or
  persister-like so shocks do not erase the colony
- `opportunistic_innovation`: exploit HGT/plasmids when local conditions make a
  transferable trait worth the carriage cost

These objectives recur across the ecology, biofilm, persistence, and evolution
literature rather than appearing as Ferox-specific abstractions.

## Modeling Lessons for Ferox

- Frontier cells should matter more than interior cells. Many papers treat the
  expanding edge as the biologically active shell and the colony bulk as
  nutrient-limited, jammed, or quiescent.
- Transport fields should stay explicit. Nutrient, toxin, and signal gradients
  are central to morphology, quorum activation, and competition outcomes.
- EPS should be treated as a transport and structure modifier, not only a flat
  defensive bonus.
- Chemotaxis and quorum should have memory, thresholds, or hysteresis; purely
  instantaneous local rules overstate directional precision and understate
  switching costs.
- Mechanical crowding matters independently of chemistry. Recent work shows that
  shoving, friction, substrate mechanics, and active-shell pressure can shape
  morphology even without elaborate signaling.
- Morphology validation should be pattern-oriented. A simulator should match
  roughness, branching, sectoring, coexistence, and shell-thickness trends, not
  just produce a plausible screenshot.

## Benchmark Phenomena Worth Reproducing

- compact -> rough -> branched/dendritic morphology transitions under nutrient,
  diffusion, and motility sweeps
- active-shell thickness and quiescent-core fraction in mature colonies
- frontier gene surfing and sector-width distributions during range expansion
- coexistence versus exclusion boundary motion in two-strain competition
- quorum/EPS perturbation effects on biomass, channels, and transport
- chemotactic ring, band, or spot formation where signaling/gradient sensing is
  strong enough to matter

## Core References by Theme

### Pattern Formation and Morphology

| Source | Why it matters for Ferox |
|---|---|
| Budrene EO, Berg HC (1991), *Complex patterns formed by motile cells of Escherichia coli*, https://doi.org/10.1038/349630a0 | Canonical chemotactic rings/spots; useful for validating local-rule pattern emergence. |
| Budrene EO, Berg HC (1995), *Dynamics of formation of symmetrical patterns by chemotactic bacteria*, https://doi.org/10.1038/376049a0 | Adds time-resolved symmetry breaking and pattern spacing targets. |
| Ben-Jacob E, Cohen I, Levine H (2000), *Cooperative self-organization of microorganisms*, https://doi.org/10.1080/000187300405228 | Broad review of branching, lubrication, signaling, and nonlinear diffusion in colonies. |
| Matsushita M et al. (2004), *Colony formation in bacteria: experiments and modeling*, https://www.cambridge.org/core/journals/biofilms/article/colony-formation-in-bacteria-experiments-and-modeling/A76C4DACA0C444B1B3BD4283C4B6C7B2 | Good bridge between experiments, morphology taxonomies, and modeling choices. |
| Porter R et al. (2025), *On the growth and form of bacterial colonies*, https://www.nature.com/articles/s42254-025-00849-x | Recent review framing modern colony morphology and surface-growth questions. |

### Biofilms, Transport, and Mechanics

| Source | Why it matters for Ferox |
|---|---|
| Stewart PS (2003), *Diffusion in biofilms*, https://doi.org/10.1128/JB.185.5.1485-1491.2003 | Strong reference for effective diffusivity, penetration depth, and EPS-limited transport. |
| Klapper I, Dockery J (2010), *Mathematical description of microbial biofilms*, https://doi.org/10.1137/080739720 | Clear reaction-diffusion plus biomass modeling survey. |
| Farrell FDC et al. (2013), *Mechanically driven growth of quasi-two-dimensional microbial colonies*, https://doi.org/10.1103/PhysRevLett.111.168101 | Strong motivation for active-shell pressure and shoving/crowding mechanics. |
| Flemming HC et al. (2016), *Biofilms: an emergent form of bacterial life*, https://doi.org/10.1038/nrmicro.2016.94 | Best high-level source for EPS as structure, protection, and transport modifier. |
| Rahbar S et al. (2024), *Mechanical interactions govern self-organized ordering in bacterial colonies on surfaces*, https://arxiv.org/abs/2410.00898 | Recent mechanics-first modeling of ordering and stress transmission. |
| Perthame B, Salvarani F, Yasuda S (2026), *Multiscale analysis of a kinetic equation for mechanotaxis*, https://arxiv.org/abs/2601.05532 | Suggests mechanotaxis and substrate coupling are increasingly relevant in current theory. |

### Ecology, Strategy, and Evolutionary Tradeoffs

| Source | Why it matters for Ferox |
|---|---|
| Hibbing ME et al. (2009), *Bacterial competition: surviving and thriving in the microbial jungle*, https://doi.org/10.1038/nrmicro2259 | Core review for antagonism, local competition, and non-neutral interactions. |
| Xavier JB, Foster KR (2007), *Cooperation and conflict in microbial biofilms*, https://doi.org/10.1073/pnas.0607651104 | Frames when local public goods are stable versus exploitable. |
| Nadell CD et al. (2016), *Spatial structure, cooperation and competition in biofilms*, https://doi.org/10.1038/nrmicro.2016.84 | One of the most Ferox-relevant papers for kin assortment, cooperation, and territorial structure. |
| Lewis K (2007), *Persister cells, dormancy and infectious disease*, https://doi.org/10.1038/nrmicro1557 | Strong conceptual basis for persistence as an insurance strategy. |
| Veening JW et al. (2008), *Bistability, epigenetics, and bet-hedging in bacteria*, https://doi.org/10.1146/annurev.micro.62.081307.163002 | Useful for stochastic phenotype switching and memory. |
| Stalder T, Top E (2016), *Plasmid transfer in biofilms*, https://doi.org/10.1038/npjbiofilms.2016.22 | Best direct bridge between spatial biofilms and plasmid/HGT dynamics. |
| Granato ET et al. (2019), *The evolution and ecology of bacterial warfare*, https://doi.org/10.1016/j.cub.2019.04.024 | Good toxin-war modeling reference with explicit cost/escalation thinking. |
| Mukherjee S, Bassler BL (2019), *Bacterial quorum sensing in complex and dynamically changing environments*, https://doi.org/10.1038/s41579-019-0186-5 | Supports quorum as thresholded, context-sensitive, and non-instantaneous. |
| Keegstra JM et al. (2022), *The ecological roles of bacterial chemotaxis*, https://doi.org/10.1038/s41579-022-00709-w | Modern ecological framing for chemotaxis beyond simple attraction. |
| Pfeiffer T et al. (2001), *Cooperation and competition in the evolution of ATP-producing pathways*, https://doi.org/10.1126/science.1058079 | Useful rate-vs-yield tradeoff anchor for growth versus efficiency. |

### Validation and Reporting

| Source | Why it matters for Ferox |
|---|---|
| Grimm V et al. (2005), *Pattern-oriented modeling of agent-based complex systems*, https://doi.org/10.1126/science.1116681 | Strong benchmark philosophy: validate multiple patterns, not one scalar. |
| Grimm V et al. (2010), *The ODD protocol: a review and first update*, https://doi.org/10.1016/j.ecolmodel.2010.08.019 | Helpful template for reporting assumptions, scenarios, and observables. |
| Madigan MT et al. (2020), *Brock Biology of Microorganisms*, 16th ed. | Useful textbook anchor for metabolism, dormancy, HGT, and biofilm fundamentals. |

## Open-Source Simulation Systems Worth Studying

| Project | URL | What Ferox can borrow |
|---|---|---|
| NUFEB | https://github.com/nufeb/NUFEB | Large-scale microbial IBMs, reference cases, and clear separation between engine and domain modules. |
| iDynoMiCS | https://github.com/kreft/iDynoMiCS | Biofilm-specific benchmark scenarios, analysis scripts, and protocol-style model descriptions. |
| CellModeller | https://github.com/cellmodeller/CellModeller | Scriptable model definitions and mechanics-aware colony examples. |
| PhysiCell | https://physicell.org/ | Reproducibility habits, sample projects, and rules-driven configuration. |
| Morpheus | https://morpheus.gitlab.io/ | Declarative model configs, parameter sweeps, and model-zoo-style documentation. |
| BacArena | https://github.com/euba/BacArena | Spatial microbial ecology with metabolism-aware agent behavior. |
| gro | https://github.com/klavinslab/gro | Expressive scenario language for microbial behaviors and signaling programs. |

## Immediate Ferox Research Priorities Suggested by the Literature

- add active-shell and shoving/crowding mechanics so frontier growth differs
  from interior biomass
- upgrade chemotaxis from a static gradient bonus to a memory- and
  threshold-aware controller with self-generated gradients
- model spatial public goods, kin locality, and cheating pressure explicitly
- treat persistence as a subpopulation strategy rather than only a colony-wide
  mode
- add morphology and ecology calibration harnesses that track roughness,
  sectoring, shell thickness, coexistence duration, and EPS-on/off transport
  attenuation
- add reference scenarios plus run manifests so literature-backed results are
  easier to reproduce and compare across branches
