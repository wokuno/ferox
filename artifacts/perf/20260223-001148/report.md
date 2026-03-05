# Performance Scenario Report

## Bottlenecks
Slowest median metrics:
- debug_scale2 :: atomic_tick (1 thread) = 2827.67 ms
- debug_scale2 :: atomic_tick (2 threads) = 1615.28 ms
- debug_scale2 :: atomic_tick (4 threads) = 1352.77 ms
- debug_scale2 :: simulation_tick (serial) = 1219.97 ms
- debug_scale2 :: simulation_tick baseline = 1125.76 ms
Atomic path is slower than serial on average (atomic/serial ratio 1.61x).
Threadpool tiny-task overhead is high (tiny/batched ratio 5.25x).
Perf-test hints observed:
- serialize/deserialize costs are balanced
- task submission overhead is high; consider coarser work units

## Scenario Summary
- debug_scale1 | atomic_tick (1 thread) | median_ms=814.92 | median_ops=24.75
- debug_scale1 | atomic_tick (2 threads) | median_ms=581.20 | median_ops=34.54
- debug_scale1 | atomic_tick (4 threads) | median_ms=534.46 | median_ops=47.20
- debug_scale1 | simulation_tick (serial) | median_ms=407.88 | median_ops=85.81
- debug_scale1 | simulation_tick baseline | median_ms=385.76 | median_ops=77.77
- debug_scale1 | protocol serialize+deserialize | median_ms=32.67 | median_ops=3672.62
- debug_scale1 | world create/init/destroy | median_ms=27.95 | median_ops=6441.07
- debug_scale1 | protocol serialize only | median_ms=23.04 | median_ops=5212.07
- debug_scale1 | protocol deserialize only | median_ms=18.27 | median_ops=6567.19
- debug_scale1 | name+color generation | median_ms=13.52 | median_ops=3719703.48
- debug_scale1 | threadpool tiny tasks | median_ms=10.37 | median_ops=4878644.71
- debug_scale1 | threadpool submit+execute | median_ms=8.76 | median_ops=5732165.68
- debug_scale1 | threadpool batched tasks | median_ms=1.81 | median_ops=27714639.14
- debug_scale2 | atomic_tick (1 thread) | median_ms=2827.67 | median_ops=14.17
- debug_scale2 | atomic_tick (2 threads) | median_ms=1615.28 | median_ops=24.77
- debug_scale2 | atomic_tick (4 threads) | median_ms=1352.77 | median_ops=37.73
- debug_scale2 | simulation_tick (serial) | median_ms=1219.97 | median_ops=57.38
- debug_scale2 | simulation_tick baseline | median_ms=1125.76 | median_ops=53.30
- debug_scale2 | protocol serialize+deserialize | median_ms=66.02 | median_ops=3635.45
- debug_scale2 | world create/init/destroy | median_ms=56.00 | median_ops=6429.44
- debug_scale2 | protocol serialize only | median_ms=45.38 | median_ops=5288.64
- debug_scale2 | protocol deserialize only | median_ms=35.88 | median_ops=6688.16
- debug_scale2 | name+color generation | median_ms=25.89 | median_ops=3862617.29
- debug_scale2 | threadpool tiny tasks | median_ms=18.02 | median_ops=5556324.47
- debug_scale2 | threadpool submit+execute | median_ms=17.92 | median_ops=5586536.70
- debug_scale2 | threadpool batched tasks | median_ms=3.25 | median_ops=32532391.77
- release_scale1 | atomic_tick (1 thread) | median_ms=192.26 | median_ops=104.70
- release_scale1 | atomic_tick (2 threads) | median_ms=137.97 | median_ops=144.97
- release_scale1 | atomic_tick (4 threads) | median_ms=124.12 | median_ops=210.59
- release_scale1 | simulation_tick (serial) | median_ms=113.25 | median_ops=309.13
- release_scale1 | simulation_tick baseline | median_ms=108.15 | median_ops=277.39
- release_scale1 | name+color generation | median_ms=11.72 | median_ops=4497811.13
- release_scale1 | world create/init/destroy | median_ms=8.73 | median_ops=21230.31
- release_scale1 | threadpool tiny tasks | median_ms=8.41 | median_ops=5942804.54
- release_scale1 | threadpool submit+execute | median_ms=8.24 | median_ops=6076592.04
- release_scale1 | protocol serialize+deserialize | median_ms=8.09 | median_ops=16029.33
- release_scale1 | protocol deserialize only | median_ms=4.75 | median_ops=26083.88
- release_scale1 | protocol serialize only | median_ms=3.93 | median_ops=31031.76
- release_scale1 | threadpool batched tasks | median_ms=1.75 | median_ops=28813074.80
- release_scale2 | atomic_tick (1 thread) | median_ms=630.00 | median_ops=63.56
- release_scale2 | atomic_tick (2 threads) | median_ms=382.59 | median_ops=104.55
- release_scale2 | atomic_tick (4 threads) | median_ms=348.15 | median_ops=148.80
- release_scale2 | simulation_tick (serial) | median_ms=307.37 | median_ops=227.88
- release_scale2 | simulation_tick baseline | median_ms=286.36 | median_ops=209.53
- release_scale2 | name+color generation | median_ms=17.73 | median_ops=5642111.01
- release_scale2 | threadpool submit+execute | median_ms=17.05 | median_ops=5865321.25
- release_scale2 | threadpool tiny tasks | median_ms=16.31 | median_ops=6132792.38
- release_scale2 | world create/init/destroy | median_ms=14.19 | median_ops=25413.97
- release_scale2 | protocol serialize+deserialize | median_ms=12.54 | median_ops=19145.53
- release_scale2 | protocol deserialize only | median_ms=8.31 | median_ops=28866.39
- release_scale2 | protocol serialize only | median_ms=7.11 | median_ops=33767.46
- release_scale2 | threadpool batched tasks | median_ms=3.75 | median_ops=27237702.96
