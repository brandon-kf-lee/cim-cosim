// mem_controller.cpp

#include "include/mem_controller.h"

using namespace sc_core;
using namespace tlm;

/**
 * Memory Controller constructor
 *
 * Creates the TLM target socket for CPU communication, initiator socket for SRAM access,
 * allocates DMA buffer, and spawns three persistent SystemC threads for operation.
 *
 * @param name SystemC module name for this instance
 *
 * Note:
 *   - Spawns 3 SystemC threads: dma_engine, dma_irq_manager, irq_manager
 */
Mem_Controller::Mem_Controller(sc_module_name name): 
    sc_module(name), 
    t_cpu_socket("t_cpu_socket"), 
    i_sram_socket("i_sram_socket"), 
    irq("irq"),
    dma_irq("dma_irq") {
    
    t_cpu_socket.register_b_transport(this, &Mem_Controller::b_transport);

    // Initialize interrupts
    irq.initialize(false);
    dma_irq.initialize(false);

    // Allocate DMA buffer
    dma_buffer = new uint8_t[DMA_BUFFER_SIZE];
    
    // Start DMA engine thread & irq managers
    // TODO: is this the proper way to start a SystemC thread?
    SC_THREAD(dma_engine);
    SC_THREAD(dma_irq_manager);
    SC_THREAD(irq_manager);
}

/**
 * 
 * Memory Controller destructor
 * 
 * Clean up dynamically allocated memory controller resources
 */
Mem_Controller::~Mem_Controller(){
    delete[] dma_buffer;
}


/**
 * Route CPU register accesses to appropriate handler based on address space
 *
 * Validates transaction alignment and size, then routes to either control registers
 * (0x00-0x1F) or DMA registers (0x20-0x3F) based on target address.
 *
 * @param trans TLM transaction payload (modified in-place with response)
 * @param delay SystemC time delay (accumulated)
 *
 * Note:
 *   - 4-byte aligned access only
 */
void Mem_Controller::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
    uint32_t addr = static_cast<uint32_t>(trans.get_address());
    uint32_t len = trans.get_data_length();

    // Enforce register alignment rules
    // Treat address as offset (since TB binds directly to MC)
    if (addr + len > REG_SPACE || len != 4 || (addr & 0x3)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    // Route to appropriate register handler
    if (addr < 0x20) {
        ctrl_handler(trans, delay);  // 0x00-0x1F: Control registers
    } else if (addr < 0x40) {
        dma_handler(trans, delay);   // 0x20-0x3F: DMA registers  
    } else {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
}

/**
 * Handle CPU access to memory controller registers for basic SRAM operations
 * Implements memory-mapped register interface supporting read/write/compute operations.
 *
 * @param trans TLM transaction payload with command, address, data
 * @param delay SystemC time delay
 *
 * Note:
 *   - W1C (write-1-to-clear) semantics for STAT_DONE and STAT_ERR
 *   - Automatically triggers start_operation when CTRL_START bit is set
 *   - Updates IRQ state through event notification
 */
void Mem_Controller::ctrl_handler(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
    auto cmd  = trans.get_command();                        // READ or WRITE
    auto addr = static_cast<uint32_t>(trans.get_address()); // Register offset
    auto ptr  = trans.get_data_ptr();                       // Data payload buffer

    uint32_t data = 0;
    // WRITE, write to specified registers based on what the CPU requests
    if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(&data, ptr, 4); // Copy received data into data variable for assignment
        
        switch (addr) {
            case REG_CONTROL:
                reg_control = data;
                
                // Start operation if reg_control contains CTRL_START
                if (reg_control & CTRL_START) {
                    start_operation(delay);
                }
                break;

            case REG_ADDR:  reg_addr = data; break;
            case REG_LEN:   reg_len = data; break;
            case REG_WDATA: reg_wdata = data; break;

            // W1C (write-one-to-clear) for DONE and ERR bits (if CPU writes 1 to DONE, it clears DONE, If CPU writes 1 to ERR, it clears ERR.)
            case REG_STATUS:
                if (data & STAT_DONE) {
                    reg_status &= ~STAT_DONE;
                }
                if (data & STAT_ERR) {
                    reg_status &= ~STAT_ERR;
                }
                irq_update_event.notify();
                wait(SC_ZERO_TIME);
                break;

            default:
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
    
    // READ, send data from respective registers to the CPU
    } else if (cmd == tlm::TLM_READ_COMMAND) {
        switch (addr) {
            case REG_CONTROL: data = reg_control; break;
            case REG_ADDR:    data = reg_addr;    break;
            case REG_LEN:     data = reg_len;     break;
            case REG_WDATA:   data = reg_wdata;   break;
            case REG_RDATA:   data = reg_rdata;   break;
            case REG_STATUS:  data = reg_status;  break;
            default:
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
        std::memcpy(ptr, &data, 4);
    
    // ERROR
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

/**
 * Handle CPU access to DMA registers for bulk memory transfers, implements memory-mapped DMA control interface 
 *
 * @param trans TLM transaction payload with command, address, data
 * @param delay SystemC time delay
 *
 * Address Handling:
 *   - 64-bit hosts: Split address across DMA_SRC_ADDR_HI/LO registers
 *   - 32-bit hosts: Single DMA_SRC_ADDR register
 *
 * Note:
 *   - Support for both 32-bit and 64-bit host addresses.
 *   - W1C semantics for DMA_DONE and DMA_ERR status bits
 *   - DMA engine triggered when DMA_START bit is written
 */
void Mem_Controller::dma_handler(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
    auto cmd = trans.get_command();
    auto addr = static_cast<uint32_t>(trans.get_address());
    auto ptr = trans.get_data_ptr();

    // Validate SRAM address bounds
    if (dma_dst_addr + dma_len > SRAM_SIZE) {
        printf("   DMA: ERROR - SRAM overflow error.");
        dma_status |= DMA_ERR;
        return;
    }

    uint32_t data = 0;
    // WRITE, write to specified registers based on what the CPU requests
    if (cmd == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(&data, ptr, 4);
        
        switch (addr) {
            case DMA_CONTROL:
                dma_control = data;
                if (data & DMA_START) {

                    // Only start if not currently busy
                    if (dma_status & DMA_BUSY) {
                        printf("   DMA: ERROR: Cannot start transfer while busy\n");
                        dma_status |= DMA_ERR;
                        break;
                    }
                    // TODO: Is this necessary? dma_engine will set these bits
                    // dma_status = 0;  // Clears DONE, ERR, and any other status bits
                    // dma_irq_update_event.notify(); // Update IRQ immediately to go LOW
                    // wait(SC_ZERO_TIME);

#if HOST_64BIT
                    uint64_t full_src_addr = (static_cast<uint64_t>(dma_src_addr_hi) << 32) | dma_src_addr_lo;
                    printf("CPU: Starting DMA transfer (%d bytes: 0x%016lX -> 0x%08X)\n", 
                           dma_len, full_src_addr, dma_dst_addr);
#else
                    printf("CPU: Starting DMA transfer (%d bytes: 0x%08X -> 0x%08X)\n", 
                           dma_len, dma_src_addr, dma_dst_addr);
#endif
                    dma_start_event.notify();
                }
                break;
            
#if HOST_64BIT
            case DMA_SRC_ADDR_LO: dma_src_addr_lo = data; break;
            case DMA_SRC_ADDR_HI: dma_src_addr_hi = data; break;
#else
            case DMA_SRC_ADDR: dma_src_addr = data; break;
#endif
            case DMA_DST_ADDR: dma_dst_addr = data; break;
            case DMA_LENGTH:   dma_len = data; break;

            // W1C (write-one-to-clear) for DONE and ERR bits
            case DMA_STATUS:
                if (data & DMA_DONE) {
                    dma_status &= ~DMA_DONE;
                }
                if (data & DMA_ERR) {
                    dma_status &= ~DMA_ERR;
                }
                dma_irq_update_event.notify();
                wait(SC_ZERO_TIME);
                break;

        }
    // READ, send data from respective registers to the CPU 
    } else if (cmd == tlm::TLM_READ_COMMAND) {
        switch (addr) {
#if HOST_64BIT
            case DMA_SRC_ADDR_LO: data = dma_src_addr_lo; break;
            case DMA_SRC_ADDR_HI: data = dma_src_addr_hi; break;
#else
            case DMA_SRC_ADDR: data = dma_src_addr; break;
#endif
            case DMA_CONTROL:  data = dma_control; break;
            case DMA_DST_ADDR: data = dma_dst_addr; break;
            case DMA_LENGTH:   data = dma_len; break;
            case DMA_STATUS:   data = dma_status; break;
            default:
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
        std::memcpy(ptr, &data, 4);
    
    //ERROR
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }
    
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

/**
 * Execute single memory operation using values from controller registers
 *
 * Performs read, write, or compute-in-memory operations on SRAM based on control
 * register settings. Updates status register and IRQ state on completion.
 *
 * @param delay SystemC time delay (accumulated from SRAM transaction)
 *
 * Operation Types:
 *   - CTRL_COMPUTE: Sends special SRAM_COMPUTE_CMD address to trigger CIM operation
 *   - CTRL_WRITE: Transfers reg_wdata to SRAM at reg_addr
 *   - Default: Read from SRAM into reg_rdata
 *
 * Note:
 *   - 4-byte transfer length only
 *   - Sets BUSY during operation, DONE on completion
 *   - Special compute command uses address 0xFFFFFFFF
 */
void Mem_Controller::start_operation(sc_core::sc_time& delay) {
    // Basic check, enforces 32-bit accesses
    if (reg_len != 4) {
        printf("CTRL: ERROR - Invalid length %d, expecting 4\n", reg_len);
        reg_status |= STAT_ERR;
        irq_update_event.notify();
        wait(SC_ZERO_TIME);
        return;
    }

    // Set busy status before transaction: set BUSY=1, DONE=0.
    reg_status |= STAT_BUSY;
    reg_status &= ~STAT_DONE;
    irq_update_event.notify();
    wait(SC_ZERO_TIME);

    // Prepare TLM transaction to SRAM
    // TODO: replace with prepare_sram_transaction
    tlm::tlm_generic_payload trans;
    uint8_t buf[4]; // Temporary data buffer
    trans.set_address(static_cast<sc_dt::uint64>(reg_addr)); // Address from reg_addr
    trans.set_data_length(4);                                // 4 bytes data length
    trans.set_streaming_width(4);                            // Also 4 (no wraparound)
    trans.set_byte_enable_ptr(nullptr);                      // None, allows all bytes
    trans.set_dmi_allowed(false);                            // Disable DMI
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE); // Transaction not complete yet, wait for read/write

    // COMPUTE, send special compute command through address
    if(reg_control & CTRL_COMPUTE) { 
    
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(SRAM_COMPUTE_CMD); // Tell SRAM to start compute, not read/write from memory
        trans.set_data_length(0);
        trans.set_data_ptr(nullptr);
        i_sram_socket->b_transport(trans, delay);

    // WRITE, copy reg_wdata to buffer, send and write to sram through b_transport
    } else if (reg_control & CTRL_WRITE) {
        std::memcpy(buf, &reg_wdata, 4);
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_data_ptr(buf);
        i_sram_socket->b_transport(trans, delay);    
    
    // READ, read from buffer into reg_rdata
    } else {
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_data_ptr(buf);
        i_sram_socket->b_transport(trans, delay);
        if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
            std::memcpy(&reg_rdata, buf, 4);
        }
    }

    // Error checking to ensure good SRAM transaction
    if (trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
        printf("CTRL: ERROR - Bad TLM response from SRAM: %s\n", trans.get_response_string().c_str());
        reg_status |= STAT_ERR;
    }

    // Completed transaction: set BUSY=0, DONE=1.
    reg_status &= ~STAT_BUSY;
    reg_status |= STAT_DONE;
    irq_update_event.notify();
    wait(SC_ZERO_TIME);
}

/**
 * Process DMA transfer requests in continuous chunked segments
 *
 * SystemC thread: Waits for DMA start event beofre transfering data from host memory
 *   to SRAM in chunks sized by DMA_BUFFER_SIZE.
 *
 * Transfer Process:
 *   - Reconstructs host pointer from register(s) (on 64-bit systems), otherwise, reads pointer directly
 *   - Copies data in chunks through intermediate buffer
 *   - Issues TLM write transactions to SRAM for each chunk
 *   - Increment through src and dest pointers until completion
 *
 * Note:
 *   - Handles both 32-bit and 64-bit host addressing.
 *   - Host memory access simulated with memcpy (would be bus transaction in reality)
 *   - Updates DMA status and signals IRQ on completion/error
 */
void Mem_Controller::dma_engine() {
    while (true) {
        // Wait for CPU to start DMA operation
        wait(dma_start_event);
        
        printf("   DMA: Starting transfer of %d bytes\n", dma_len);
        
        // Set busy status
        dma_status = DMA_BUSY;
        dma_status &= ~DMA_DONE;
        dma_irq_update_event.notify();
        wait(SC_ZERO_TIME);
        
        // Validate transfer parameters
        if (dma_len == 0) {
            printf("   DMA: ERROR - dma_len (%d) is 0\n", dma_len);
            dma_status |= DMA_ERR;
            dma_status &= ~DMA_BUSY;
            dma_irq_update_event.notify();
            wait(SC_ZERO_TIME);
            continue;
        }
        
        // Reconstruct source pointer based on host architecture
#if HOST_64BIT
        // 64-bit host: Reconstruct pointer from high/low registers
        // NOTE: No validation performed, caller responsible for valid addresses
        uint64_t src_addr = (static_cast<uint64_t>(dma_src_addr_hi) << 32) | dma_src_addr_lo;
        const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(src_addr);
        //printf("   DMA: Source reconstructed from HI:0x%08X LO:0x%08X -> %p\n", dma_src_addr_hi, dma_src_addr_lo, src_ptr);
#else
        // 32-bit host: Direct pointer conversion
        const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(dma_src_addr));
        //printf("   DMA: Source address: 0x%08X -> %p\n", dma_src_addr, src_ptr);
#endif

        // Perform chunked transfer
        uint32_t remaining = dma_len;
        uint32_t dst_ptr = dma_dst_addr; // Holds address to increment on
        uint32_t total_chunks = 0;
        
        while (remaining > 0) {
            sc_core::sc_time sram_delay = sc_core::SC_ZERO_TIME;
            
            // Read the maximum amount the buffer allows, or whatever's left
            uint32_t chunk_size = std::min(remaining, DMA_BUFFER_SIZE);
            
            // SIMULATION NOTE: In reality, this would be another bus transaction to a seperate system memory module
            std::memcpy(dma_buffer, src_ptr, chunk_size);
            
            // Write chunk to destination address
            tlm::tlm_generic_payload write_trans;
            prepare_sram_transaction(write_trans, tlm::TLM_WRITE_COMMAND, dst_ptr, dma_buffer, chunk_size);
            i_sram_socket->b_transport(write_trans, sram_delay);
            
            if (write_trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
                printf("DMA: ERROR - Bad TLM response from SRAM: %s\n", write_trans.get_response_string().c_str());
                dma_status |= DMA_ERR;
                break;
            }
            
            // Update pointers and remaining count
            src_ptr += chunk_size;
            dst_ptr += chunk_size;
            remaining -= chunk_size;
            total_chunks++;
            
            // Model realistic DMA timing (2ns per byte is typical for modern DMA)
            wait(SC_ZERO_TIME);
            //wait(sram_delay + sc_core::sc_time(chunk_size * 2, sc_core::SC_NS));
        }

        // Completed transfer: set BUSY=0, DONE=1.
        dma_status &= ~DMA_BUSY;
        dma_status |= DMA_DONE;

        if (dma_status & DMA_ERR) {
            printf("   DMA: ERROR - Transfer failed with error\n");
        } else {
            dma_status |= DMA_DONE;
            printf("   DMA: Transfer complete (%d chunks)\n", total_chunks);
        }

        dma_irq_update_event.notify();
        wait(SC_ZERO_TIME);
    }
}


/**
 * Configure TLM transaction payload for SRAM communication
 * Utility function to standardize TLM transaction setup
 *
 * @param trans TLM payload to configure (modified in-place)
 * @param cmd TLM_READ_COMMAND or TLM_WRITE_COMMAND
 * @param addr Target SRAM address
 * @param data Pointer to data buffer
 * @param len Transfer length in bytes
 *
 * Note:
 *   - Sets streaming width equal to data length (no wraparound)
 *   - Disables DMI and byte enable mask
 */
void Mem_Controller::prepare_sram_transaction(tlm::tlm_generic_payload& trans, tlm::tlm_command cmd, uint32_t addr, uint8_t* data, uint32_t len) {
    trans.set_command(cmd);
    trans.set_address(static_cast<sc_dt::uint64>(addr));
    trans.set_data_ptr(data);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
}

/**
 * Manage controller interrupt based on its status and if enabled
 *
 * SystemC thread: Waits for IRQ change request before updating.
 * IRQ is asserted if both interrupt enable and operation done bits are set. If manager is called when 
 *   done bit is 0, IRQ is deasserted. 
 *
 * Note:
 *   - Waits on irq_update_event for state changes
 *   - IRQ = CTRL_IRQEN && STAT_DONE
 */
// TODO: rename update_ctrl_irq_manager
void Mem_Controller::irq_manager() {
    while (true) {
        wait(irq_update_event);  // Wait for interrupt state change
        bool enable = (reg_control & CTRL_IRQEN) != 0; // Are IRQs enabled?
        bool done   = (reg_status & STAT_DONE) != 0;   // Is the operation done?
        irq.write(enable && done);                     // Raise IRQ if both is true

        // bool enable = (reg_control & CTRL_IRQEN) != 0; // Are IRQs enabled?
        // bool done   = (reg_status & STAT_DONE) != 0;   // Is the operation done?
        // bool new_irq_state = enable && done;
        
        // printf("CTRL IRQ: enable=%d, done=%d, status=0x%08X -> IRQ=%d\n", 
        //        enable, done, reg_status, new_irq_state);

        // irq.write(new_irq_state);                     // Raise IRQ if both is true
    }
}

/**
 * Manage DMA interrupt output based on its status and if enabled
 *
 * SystemC thread: Waits for IRQ change request before updating.
 * IRQ is asserted if both interrupt enable and operation done bits are set. If manager is called when 
 *   done bit is 0, IRQ is deasserted. 
 *
 * Note:
 *   - Waits on dma_irq_update_event for state changes
 *   - DMA IRQ = DMA_IRQEN && DMA_DONE
 */
void Mem_Controller::dma_irq_manager() {
    while (true) {
        wait(dma_irq_update_event);  // Wait for interrupt state change
        bool enable = (dma_control & DMA_IRQEN) != 0; // Are IRQs enabled?
        bool done   = (dma_status & DMA_DONE) != 0;   // Is the operation done?
        dma_irq.write(enable && done);                // Raise IRQ if both is true

        // bool enable = (dma_control & DMA_IRQEN) != 0;
        // bool done   = (dma_status & DMA_DONE) != 0;
        // bool new_irq_state = enable && done;
        
        // printf("    dma_irq updating:\n    DMA IRQ: enable=%d, done=%d, status=0x%08X -> IRQ=%d\n", 
        //        enable, done, dma_status, new_irq_state);

        // dma_irq.write(new_irq_state);
    }
}