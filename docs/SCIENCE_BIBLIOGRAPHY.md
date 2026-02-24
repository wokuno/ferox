# Science Bibliography for Colony Modeling

This bibliography links peer-reviewed colony-modeling literature to Ferox mechanics.

DOI resolution was validated on 2026-02-23 using Crossref and/or DOI resolver endpoints.

## Source-to-Subsystem Mapping

| Ferox subsystem/mechanic | Why it is modeled this way | Sources |
|---|---|---|
| Spatial expansion fronts (`atomic_spread`, age-0 cascade prevention) | Frontier-limited growth drives sectoring and non-mixed expansion outcomes. | Hallatschek et al. 2007; Hallatschek and Nelson 2008 |
| Chemotaxis and directional spread (`social_factor`, gradient-biased movement) | Bacterial collectives can self-organize into stable geometric patterns from local sensing and movement. | Budrene and Berg 1991; Budrene and Berg 1995 |
| Colony competition (`aggression`, `resilience`, territory capture) | Microbes coexist under direct antagonism and local resource conflict, not only neutral growth. | Hibbing et al. 2009; Nadell et al. 2016 |
| Quorum-like signaling (`signal_emission`, `signal_sensitivity`) | Population-density signaling changes collective decisions and state transitions. | Miller and Bassler 2001; Keller and Surette 2006 |
| Biofilm-like defense and persistence (`biofilm_investment`, dormancy/stress) | Matrix-protected, state-shifting communities alter growth/defense tradeoffs. | Flemming et al. 2016; Nadell et al. 2016 |
| Mutation-driven adaptation (`genome_mutate`, evolving traits) | Ongoing mutation + selection produce long-horizon adaptation dynamics. | Good et al. 2017; Hallatschek et al. 2007 |

## Annotated References

1. **Budrene EO, Berg HC (1991)**. Complex patterns formed by motile cells of *Escherichia coli*. *Nature* 349(6310):630-633. DOI: [10.1038/349630a0](https://doi.org/10.1038/349630a0)
   - Ferox mapping: motivates local-rule pattern emergence and direction-biased spread behavior.

2. **Budrene EO, Berg HC (1995)**. Dynamics of formation of symmetrical patterns by chemotactic bacteria. *Nature* 376(6535):49-53. DOI: [10.1038/376049a0](https://doi.org/10.1038/376049a0)
   - Ferox mapping: supports chemotaxis-like neighbor/gradient responses instead of purely isotropic expansion.

3. **Hallatschek O, Hersen P, Ramanathan S, Nelson DR (2007)**. Genetic drift at expanding frontiers promotes gene segregation. *PNAS* 104(50):19926-19930. DOI: [10.1073/pnas.0710150104](https://doi.org/10.1073/pnas.0710150104)
   - Ferox mapping: supports frontier-driven lineage divergence and sector-like colony outcomes during expansion.

4. **Hallatschek O, Nelson DR (2008)**. Gene surfing in expanding populations. *Theoretical Population Biology* 73(1):158-170. DOI: [10.1016/j.tpb.2007.08.008](https://doi.org/10.1016/j.tpb.2007.08.008)
   - Ferox mapping: motivates expansion-front stochasticity and why mutation effects are amplified at colony edges.

5. **Miller MB, Bassler BL (2001)**. Quorum sensing in bacteria. *Annual Review of Microbiology* 55:165-199. DOI: [10.1146/annurev.micro.55.1.165](https://doi.org/10.1146/annurev.micro.55.1.165)
   - Ferox mapping: basis for signal-mediated state changes and density-dependent behavior tuning.

6. **Keller L, Surette MG (2006)**. Communication in bacteria: an ecological and evolutionary perspective. *Nature Reviews Microbiology* 4(4):249-258. DOI: [10.1038/nrmicro1383](https://doi.org/10.1038/nrmicro1383)
   - Ferox mapping: supports ecological framing of communication fields and multi-colony signaling effects.

7. **Hibbing ME, Fuqua C, Parsek MR, Peterson SB (2009)**. Bacterial competition: surviving and thriving in the microbial jungle. *Nature Reviews Microbiology* 8(1):15-25. DOI: [10.1038/nrmicro2259](https://doi.org/10.1038/nrmicro2259)
   - Ferox mapping: supports explicit antagonism and territorial conflict mechanics.

8. **Nadell CD, Drescher K, Foster KR (2016)**. Spatial structure, cooperation and competition in biofilms. *Nature Reviews Microbiology* 14(9):589-600. DOI: [10.1038/nrmicro.2016.84](https://doi.org/10.1038/nrmicro.2016.84)
   - Ferox mapping: informs tradeoffs among clustering, cooperation, and competitive edge dynamics.

9. **Flemming HC, Wingender J, Szewzyk U, Steinberg P, Rice SA, Kjelleberg S (2016)**. Biofilms: an emergent form of bacterial life. *Nature Reviews Microbiology* 14(9):563-575. DOI: [10.1038/nrmicro.2016.94](https://doi.org/10.1038/nrmicro.2016.94)
   - Ferox mapping: supports biofilm-like persistence, resilience, and altered stress response states.

10. **Good BH, McDonald MJ, Barrick JE, Lenski RE, Desai MM (2017)**. The dynamics of molecular evolution over 60,000 generations. *Nature* 551(7678):45-50. DOI: [10.1038/nature24287](https://doi.org/10.1038/nature24287)
    - Ferox mapping: motivates long-run mutation/selection dynamics and non-static trait landscapes.

## Notes and Limits

- Ferox is an abstract simulation and does not model wet-lab kinetics exactly.
- Sources were selected for mechanism relevance (spatial growth, communication, competition, adaptation), not clinical specificity.
