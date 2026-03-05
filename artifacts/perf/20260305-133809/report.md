# Performance Scenario Report

## Bottlenecks
Slowest median metrics:
- debug_scale2 :: atomic_tick (1 thread) = 2715.78 ms
- debug_scale2 :: atomic_tick (2 threads) = 1658.02 ms
- debug_scale2 :: atomic_tick (4 threads) = 1379.82 ms
- debug_scale2 :: simulation_tick (serial) = 1232.58 ms
- debug_scale2 :: simulation_tick baseline = 1149.39 ms
Atomic path is slower than serial on average (atomic/serial ratio 1.64x).
Threadpool tiny-task overhead is high (tiny/batched ratio 4.96x).
Perf-test hints observed:
- serialize/deserialize costs are balanced
- task submission overhead is high; consider coarser work units

## Scenario Summary
- debug_scale1 | atomic_tick (1 thread) | median_ms=873.61 | median_ops=22.92
- debug_scale1 | atomic_tick (2 threads) | median_ms=501.50 | median_ops=40.22
- debug_scale1 | atomic_tick (4 threads) | median_ms=499.95 | median_ops=51.56
- debug_scale1 | simulation_tick (serial) | median_ms=418.95 | median_ops=83.55
- debug_scale1 | simulation_tick baseline | median_ms=397.64 | median_ops=75.44
- debug_scale1 | protocol serialize+deserialize | median_ms=34.23 | median_ops=3506.96
- debug_scale1 | world create/init/destroy | median_ms=30.59 | median_ops=5907.93
- debug_scale1 | protocol serialize only | median_ms=24.11 | median_ops=4981.52
- debug_scale1 | protocol deserialize only | median_ms=18.55 | median_ops=6472.69
- debug_scale1 | name+color generation | median_ms=13.31 | median_ops=3759012.47
- debug_scale1 | threadpool submit+execute | median_ms=9.72 | median_ops=5203215.19
- debug_scale1 | threadpool tiny tasks | median_ms=9.46 | median_ops=5292348.92
- debug_scale1 | threadpool batched tasks | median_ms=2.15 | median_ops=23468700.04
- debug_scale2 | atomic_tick (1 thread) | median_ms=2715.78 | median_ops=14.74
- debug_scale2 | atomic_tick (2 threads) | median_ms=1658.02 | median_ops=24.13
- debug_scale2 | atomic_tick (4 threads) | median_ms=1379.82 | median_ops=36.92
- debug_scale2 | simulation_tick (serial) | median_ms=1232.58 | median_ops=56.80
- debug_scale2 | simulation_tick baseline | median_ms=1149.39 | median_ops=52.21
- debug_scale2 | protocol serialize+deserialize | median_ms=65.14 | median_ops=3684.58
- debug_scale2 | world create/init/destroy | median_ms=55.43 | median_ops=6496.45
- debug_scale2 | protocol serialize only | median_ms=45.31 | median_ops=5296.66
- debug_scale2 | protocol deserialize only | median_ms=36.02 | median_ops=6664.52
- debug_scale2 | name+color generation | median_ms=25.41 | median_ops=3934771.15
- debug_scale2 | threadpool tiny tasks | median_ms=17.39 | median_ops=5750237.99
- debug_scale2 | threadpool submit+execute | median_ms=16.70 | median_ops=5989684.42
- debug_scale2 | threadpool batched tasks | median_ms=3.17 | median_ops=31612386.32
