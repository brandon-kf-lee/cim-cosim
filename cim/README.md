# cim (Working Title)


## Contents
- binaries
    - Binaries necessary to run neural network
    - Includes sample image inputs, networks (full and quantized), training datasets.
- examples
    - Testing code files that use either the CPU or CIM device for neural network inference.
    - Includes code for instruction counting and cache analysis.
- harness
    - Helper library that help with benchmarking.
- include
    - Include files for the CIM userspace library.
- results
    - Collection of graphs and tables of data generated after tests are complete.
    - Include Python scripts that analyze raw logs and generate graphs.
- src
    - Source files for the CIM userspace library
- train_network
    - Independent code that generates neural networks and quantizes them