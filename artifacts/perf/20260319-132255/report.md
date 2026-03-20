# Performance Scenario Report

## Bottlenecks
Slowest median metrics:
- release_scale2 :: simulation_tick (serial) = 441.52 ms
- release_scale2 :: simulation_tick baseline = 324.80 ms
- release_scale2 :: frontier telemetry compute = 258.05 ms
- release_scale2 :: atomic_tick (4 threads) = 150.31 ms
- release_scale2 :: atomic phase: total = 144.42 ms
Threadpool tiny-task overhead is high (tiny/batched ratio 21.74x).
Perf-test hints observed:
- serialize/deserialize costs are balanced
- task submission overhead is high; consider coarser work units

## Scenario Summary
- release_scale2 | simulation_tick (serial) | median_ms=441.52 | median_ops=158.54
- release_scale2 | simulation_tick baseline | median_ms=324.80 | median_ops=184.73
- release_scale2 | frontier telemetry compute | median_ms=258.05 | median_ops=232.51
- release_scale2 | atomic_tick (4 threads) | median_ms=150.31 | median_ops=345.71
- release_scale2 | atomic phase: total | median_ms=144.42 | median_ops=346.21
- release_scale2 | atomic_tick (1 thread) | median_ms=122.70 | median_ops=326.00
- release_scale2 | atomic_tick (2 threads) | median_ms=107.82 | median_ops=370.98
- release_scale2 | atomic phase: age | median_ms=77.98 | median_ops=641.21
- release_scale2 | simulation_update_scents | median_ms=51.24 | median_ops=3122.74
- release_scale2 | atomic phase: serial core | median_ms=41.29 | median_ops=1211.09
- release_scale2 | sync_from_world 800x400 | median_ms=37.70 | median_ops=2121.73
- release_scale2 | atomic phase: spread | median_ms=20.10 | median_ops=2487.31
- release_scale2 | name+color generation | median_ms=14.56 | median_ops=6870019.35
- release_scale2 | threadpool submit+execute | median_ms=14.52 | median_ops=6886578.10
- release_scale2 | threadpool tiny tasks | median_ms=14.49 | median_ops=6901787.41
- release_scale2 | simulation_resolve_combat | median_ms=13.64 | median_ops=2932.12
- release_scale2 | simulation_spread | median_ms=12.23 | median_ops=6539.69
- release_scale2 | world create/init/destroy | median_ms=11.49 | median_ops=31334.32
- release_scale2 | sync_to_world 800x400 | median_ms=10.88 | median_ops=7350.91
- release_scale2 | protocol serialize+deserialize | median_ms=9.65 | median_ops=24867.89
- release_scale2 | sync_from_world 400x200 | median_ms=9.61 | median_ops=8322.93
- release_scale2 | threadpool chunked submit | median_ms=8.91 | median_ops=11228385.79
- release_scale2 | broadcast end-to-end (0 clients) | median_ms=8.74 | median_ops=18308.73
- release_scale2 | broadcast build+serialize | median_ms=8.67 | median_ops=18452.31
- release_scale2 | server snapshot build | median_ms=8.32 | median_ops=38480.04
- release_scale2 | simulation_update_nutrients | median_ms=6.92 | median_ops=23114.71
- release_scale2 | protocol deserialize only | median_ms=6.07 | median_ops=39545.23
- release_scale2 | protocol serialize only | median_ms=5.85 | median_ops=41004.62
- release_scale2 | frontier_telemetry_compute | median_ms=5.21 | median_ops=15343.31
- release_scale2 | simulation_resolve_combat_sparse_border | median_ms=4.97 | median_ops=8045.05
- release_scale2 | broadcast build snapshot | median_ms=3.98 | median_ops=40160.64
- release_scale2 | atomic phase: sync_from_world | median_ms=3.23 | median_ops=15494.27
- release_scale2 | sync_from_world 200x100 | median_ms=2.57 | median_ops=31140.52
- release_scale2 | sync_to_world 400x200 | median_ms=2.57 | median_ops=31116.30
- release_scale2 | atomic_age phase | median_ms=2.21 | median_ops=54347.83
- release_scale2 | atomic phase: sync_to_world | median_ms=1.78 | median_ops=28137.33
- release_scale2 | atomic_spread phase | median_ms=1.69 | median_ops=71005.91
- release_scale2 | threadpool batched tasks | median_ms=0.66 | median_ops=152438992.48
- release_scale2 | sync_to_world 200x100 | median_ms=0.64 | median_ops=124610.55
