# mem_ctrl (Working Title, cim_device)
The SystemC CIM device (including a memory controller, CIM memory, and bridge).

## Contents
- include
    - Include files for SystemC CIM device.
    - Includes register headers, timing parameters, the neural network datatype.

- linux
    -  Source and make file for Linux kernel module to register CIM device in Linux.
- src
    - Source files for SystemC CIM device.
    - Includes memory controller, memory, and bridge code.
- test (deploy.sh)
    - Utility script to deploy files, crosscompile test code into QEMU/Linux.

## To Run
- The Makefile builds the SystemC CIM device and initializes the bridge based on the user's chosen parameters. To see all available options, run
```bash
make help
```