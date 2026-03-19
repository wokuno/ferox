# Performance Scenario Report

## Bottlenecks
Slowest median metrics:
- release_scale2 :: simulation_tick (serial) = 457.65 ms
- release_scale2 :: simulation_tick baseline = 324.86 ms
- release_scale2 :: frontier telemetry compute = 259.85 ms
- release_scale2 :: atomic_tick (4 threads) = 151.92 ms
- release_scale2 :: atomic phase: total = 144.87 ms
Threadpool tiny-task overhead is high (tiny/batched ratio 41.28x).
Perf-test hints observed:
- serialize/deserialize costs are balanced
- task submission overhead is high; consider coarser work units

## Scenario Summary
- release_scale2 | simulation_tick (serial) | median_ms=457.65 | median_ops=152.95
- release_scale2 | simulation_tick baseline | median_ms=324.86 | median_ops=184.69
- release_scale2 | frontier telemetry compute | median_ms=259.85 | median_ops=230.90
- release_scale2 | atomic_tick (4 threads) | median_ms=151.92 | median_ops=337.02
- release_scale2 | atomic phase: total | median_ms=144.87 | median_ops=345.13
- release_scale2 | atomic_tick (1 thread) | median_ms=122.50 | median_ops=326.54
- release_scale2 | atomic_tick (2 threads) | median_ms=107.12 | median_ops=373.42
- release_scale2 | atomic phase: age | median_ms=78.46 | median_ops=637.29
- release_scale2 | simulation_update_scents | median_ms=47.28 | median_ops=3384.09
- release_scale2 | atomic phase: serial core | median_ms=41.06 | median_ops=1217.79
- release_scale2 | sync_from_world 800x400 | median_ms=41.03 | median_ops=1949.60
- release_scale2 | threadpool submit+execute | median_ms=25.63 | median_ops=3901982.17
- release_scale2 | threadpool tiny tasks | median_ms=23.74 | median_ops=4212654.83
- release_scale2 | atomic phase: spread | median_ms=20.30 | median_ops=2462.69
- release_scale2 | name+color generation | median_ms=15.01 | median_ops=6660006.72
- release_scale2 | threadpool chunked submit | median_ms=13.68 | median_ops=7307270.76
- release_scale2 | sync_to_world 800x400 | median_ms=11.66 | median_ops=6858.71
- release_scale2 | simulation_resolve_combat | median_ms=11.44 | median_ops=3496.81
- release_scale2 | simulation_spread | median_ms=11.16 | median_ops=7167.82
- release_scale2 | world create/init/destroy | median_ms=10.92 | median_ops=32954.96
- release_scale2 | sync_from_world 400x200 | median_ms=10.31 | median_ops=7762.47
- release_scale2 | protocol serialize+deserialize | median_ms=9.47 | median_ops=25348.54
- release_scale2 | broadcast build+serialize | median_ms=8.51 | median_ops=18790.37
- release_scale2 | broadcast end-to-end (0 clients) | median_ms=8.50 | median_ops=18823.53
- release_scale2 | server snapshot build | median_ms=8.15 | median_ops=39273.44
- release_scale2 | simulation_update_nutrients | median_ms=7.78 | median_ops=20578.78
- release_scale2 | simulation_resolve_combat_sparse_border | median_ms=6.65 | median_ops=6017.75
- release_scale2 | protocol deserialize only | median_ms=6.04 | median_ops=39735.10
- release_scale2 | protocol serialize only | median_ms=5.88 | median_ops=40844.11
- release_scale2 | frontier_telemetry_compute | median_ms=5.30 | median_ops=15080.11
- release_scale2 | broadcast build snapshot | median_ms=3.99 | median_ops=40050.06
- release_scale2 | atomic phase: sync_from_world | median_ms=3.26 | median_ops=15318.63
- release_scale2 | sync_from_world 200x100 | median_ms=2.84 | median_ops=28178.94
- release_scale2 | sync_to_world 400x200 | median_ms=2.82 | median_ops=28348.69
- release_scale2 | atomic_age phase | median_ms=2.29 | median_ops=52310.37
- release_scale2 | atomic phase: sync_to_world | median_ms=1.78 | median_ops=28026.92
- release_scale2 | atomic_spread phase | median_ms=1.74 | median_ops=68846.81
- release_scale2 | threadpool worker follow-on | median_ms=1.08 | median_ops=92850498.51
- release_scale2 | threadpool batched tasks | median_ms=0.72 | median_ops=139081992.74
- release_scale2 | sync_to_world 200x100 | median_ms=0.69 | median_ops=116448.28
