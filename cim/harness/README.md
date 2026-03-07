# harness
High-level benchmarking helpers shared by mnist_CIM and mnist_CPU.
- mnist_bench: CLI parsing and common utilities (file load, normalize, softmax/argmax)
- perf_gate:   perf_event_open wrapper to gate/measure instructions+cycles for a region

## Contents
- mnist_bench  
    - include/mnist_bench.h, src/mnist_bench.c
- perf_gate    
    - include/perf_gate.h,   src/perf_gate.c